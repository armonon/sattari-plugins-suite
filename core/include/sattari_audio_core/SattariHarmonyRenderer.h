// =============================================================================
//  Sattari Harmony Renderer
// -----------------------------------------------------------------------------
//  A continuous, allocation-free four-voice vocal harmony renderer. Musical
//  note selection lives in SattariHarmonyVoices; this class turns that plan into
//  audio while preserving state across arbitrary host block boundaries.
//
//  Each voice owns a waveform-locked epoch shifter, a click-free pitch glide,
//  an independent delay/pan/level path, and a deterministic humanisation LFO.
//  The ensemble stage is a feed-forward pair of modulated delay taps. It can
//  thicken a voice into a choir without feedback, runaway gain, or another
//  pitch detector. No allocation or locking occurs in process().
// =============================================================================
#pragma once

#include <sattari_audio_core/SattariLivePitchEngine.h>
#include <sattari_audio_core/SattariFormantShifter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sattari::dsp
{

class HarmonyRenderer
{
public:
    static constexpr int maxVoices = 4;

    struct Voice
    {
        bool active = false;
        int targetMidi = -1;
        float level = 0.35f;          // linear
        float pan = 0.0f;             // -1..+1
        float delayMs = 0.0f;         // explicit arrangement delay
        float detuneCents = 0.0f;
        float vibratoCents = 0.0f;
        float formantSemitones = 0.0f; // Studio-only envelope warp; never changes f0
    };

    struct Settings
    {
        float glideMs = 28.0f;
        float humanPitchCents = 0.0f;
        float humanTimingMs = 0.0f;
        float vibratoRateHz = 5.2f;
        float ensemble = 0.0f;        // 0..1
        float ensembleDepthMs = 7.0f;
        float ensembleRateHz = 0.35f;
        float ensembleSpread = 0.75f;
        float lowCutHz = 90.0f;
        float highCutHz = 16000.0f;
        int alignmentDelaySamples = 0;
        bool formantsEnabled = false; // caller must account for the 1024-sample stage
    };

    void prepare (double sampleRate, int maximumBlockSize,
                  double minimumHz, double maximumHz,
                  int maximumAlignmentSamples = 8192,
                  bool useExtremeShifter = false)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxBlock = std::max (1, maximumBlockSize);
        extremeMode = useExtremeShifter;
        monoInput.assign ((size_t) maxBlock, 0.0f);
        wetLeft.assign ((size_t) maxBlock, 0.0f);
        wetRight.assign ((size_t) maxBlock, 0.0f);

        const int requestedDelay = std::max (maximumAlignmentSamples,
            (int) std::ceil (sr * 0.35) + maxBlock + 64);
        int ringSize = 1;
        while (ringSize < requestedDelay) ringSize <<= 1;
        delayMask = ringSize - 1;

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& state = states[(size_t) v];
            state.shifter.prepare (sr, minimumHz, maximumHz, maxBlock);
            state.extremeShifter.prepare (sr, maxBlock);
            state.formant.prepare (sr);
            state.shifted.assign ((size_t) maxBlock, 0.0f);
            state.delayRing.assign ((size_t) ringSize, 0.0f);
            state.residualRing.assign ((size_t) ringSize, 0.0f);
            state.ensemblePhase = 0.25 * (double) v;
            state.vibratoPhase = 0.17 * (double) v;
            state.randomState = 0x9e3779b9u
                              ^ (0x85ebca6bu * (std::uint32_t) (v + 1));
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill (monoInput.begin(), monoInput.end(), 0.0f);
        std::fill (wetLeft.begin(), wetLeft.end(), 0.0f);
        std::fill (wetRight.begin(), wetRight.end(), 0.0f);
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& state = states[(size_t) v];
            state.shifter.reset();
            state.extremeShifter.reset();
            state.formant.reset();
            std::fill (state.shifted.begin(), state.shifted.end(), 0.0f);
            std::fill (state.delayRing.begin(), state.delayRing.end(), 0.0f);
            std::fill (state.residualRing.begin(), state.residualRing.end(), 0.0f);
            state.write = 0;
            state.smoothedSemitones = 0.0;
            state.humanPitch = state.humanTarget = 0.0;
            state.humanTiming = state.humanTimingTarget = 0.25 + 0.25 * (double) v;
            state.samplesToHumanTarget = 0;
            state.controlRemaining = 0;
            state.ratio = 1.0;
            state.randomState = 0x9e3779b9u ^ (0x85ebca6bu * (std::uint32_t) (v + 1));
            state.ensemblePhase = 0.25 * (double) v;
            state.vibratoPhase = 0.17 * (double) v;
            state.lowPass = state.highPassMemory = 0.0f;
        }
    }

    int getLatencySamples (bool withFormants = false) const noexcept
    {
        return (extremeMode ? states.front().extremeShifter.getLatencySamples()
                            : states.front().shifter.getLatencySamples())
             + (withFormants ? states.front().formant.latencySamples() : 0);
    }

    // Input is summed to mono deliberately: a generated singer is a mono object,
    // then pan and ensemble placement build the stereo arrangement. This halves
    // the shifter count versus independently re-detecting duplicated stereo input
    // and prevents left/right phase disagreement from smearing the image.
    void process (const float* const* input, int inputChannels,
                  float* outputLeft, float* outputRight, int numSamples,
                  double sourceMidi, double sourcePeriodSamples, bool voiced,
                  const std::array<Voice, maxVoices>& voices,
                  const Settings& settings) noexcept
    {
        if (input == nullptr || inputChannels <= 0 || outputLeft == nullptr
            || outputRight == nullptr || numSamples <= 0 || numSamples > maxBlock)
            return;

        const float inputScale = 1.0f / (float) inputChannels;
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < inputChannels; ++ch) sum += input[ch][i];
            monoInput[(size_t) i] = sum * inputScale;
            wetLeft[(size_t) i] = wetRight[(size_t) i] = 0.0f;
        }

        constexpr double twoPi = 6.283185307179586476925286766559;
        const double glideSeconds = std::max (0.0015, 0.001 * (double) settings.glideMs);
        const double glide = 1.0 - std::exp (-1.0 / (sr * glideSeconds));
        const double humanPole = 1.0 - std::exp (-1.0 / (sr * 0.085));
        const double timingPole = 1.0 - std::exp (-1.0 / (sr * 0.35));
        const float lowCut = std::clamp (settings.lowCutHz, 20.0f, 1000.0f);
        const float highCut = std::clamp (settings.highCutHz, lowCut + 100.0f,
                                          (float) (0.47 * sr));
        const float hpA = (float) (1.0 - std::exp (-twoPi * (double) lowCut / sr));
        const float lpA = (float) (1.0 - std::exp (-twoPi * (double) highCut / sr));

        for (int v = 0; v < maxVoices; ++v)
        {
            auto& state = states[(size_t) v];
            const auto& voice = voices[(size_t) v];
            const double targetSemitones = voice.active && voiced && voice.targetMidi >= 0
                ? std::clamp ((double) voice.targetMidi - sourceMidi, -12.0, 12.0)
                : 0.0;

            const float pan = std::clamp (voice.pan, -1.0f, 1.0f);
            const float angle = (pan + 1.0f) * 0.7853981633974483f;
            const float panL = std::cos (angle), panR = std::sin (angle);
            const int baseDelay = std::max (0, settings.alignmentDelaySamples)
                + std::max (0, (int) std::round ((double) voice.delayMs * 0.001 * sr));
            const double ensemblePhaseStep = (double) settings.ensembleRateHz / sr;

            // Fixed sample-clock control intervals, carried across callbacks.
            // Humanisation and vibrato must not slow down with larger DAW blocks.
            for (int offset = 0; offset < numSamples;)
            {
                if (state.controlRemaining == 0)
                {
                    const double cents = 100.0 * state.smoothedSemitones
                        + voice.detuneCents + state.humanPitch
                        + voice.vibratoCents * std::sin (twoPi * state.vibratoPhase);
                    // Avoid the exact-octave epoch degeneracy, as before.
                    state.ratio = std::pow (2.0, std::clamp (cents, -1199.5, 1199.5) / 1200.0);
                    state.controlRemaining = 16;
                }
                // The epoch shifter can inspect all input supplied in a call.
                // Feed one sample at a time so even odd-sized host callbacks
                // expose identical history, without adding a buffering delay.
                constexpr int count = 1;
                if (extremeMode)
                    state.extremeShifter.process (monoInput.data() + offset, state.shifted.data() + offset,
                        count, state.ratio, voiced && voice.active, sourcePeriodSamples);
                else
                    state.shifter.process (monoInput.data() + offset, state.shifted.data() + offset,
                        count, state.ratio, sourcePeriodSamples, voiced && voice.active);
                if (settings.formantsEnabled)
                {
                    FormantShifter::Params p;
                    p.shiftSemitones = std::clamp (voice.formantSemitones, -12.0f, 12.0f);
                    state.formant.process (state.shifted.data() + offset, state.shifted.data() + offset, count, p);
                }
                for (int i = offset; i < offset + count; ++i)
                {
                    if (state.samplesToHumanTarget-- <= 0)
                    {
                        state.humanTarget = bipolarRandom (state) * (double) settings.humanPitchCents;
                        state.humanTimingTarget = 0.5 + 0.5 * bipolarRandom (state);
                        state.samplesToHumanTarget = std::max (1, (int) std::round (0.18 * sr)) - 1;
                    }
                    state.humanPitch += humanPole * (state.humanTarget - state.humanPitch);
                    state.humanTiming += timingPole * (state.humanTimingTarget - state.humanTiming);
                    state.smoothedSemitones += glide * (targetSemitones - state.smoothedSemitones);
                    const float shifted = state.shifted[(size_t) i];
                    state.residualRing[(size_t) state.write] = shifted;
                    float residualRejected = shifted;
                    if (std::abs (targetSemitones) > 11.5 && voiced && voice.active)
                    {
                        // At the octave edge, PSOLA can leave a quiet copy of the
                        // source fundamental under the intended voice. A feed-forward
                        // comb at half the detected source period puts that fundamental
                        // 180 degrees apart while the octave target adds coherently.
                        // It is active only for octave roles and adds no feedback.
                        const float halfPeriod = delayedFrom (
                            state.residualRing, state.write, 0.5 * sourcePeriodSamples);
                        residualRejected = 0.5f * (shifted + halfPeriod);
                    }
                    state.lowPass += lpA * (residualRejected - state.lowPass);
                    state.highPassMemory += hpA * (state.lowPass - state.highPassMemory);
                    const float filtered = state.lowPass - state.highPassMemory;
                    state.delayRing[(size_t) state.write] = filtered;

                    const double humanDelay = std::max (0.0, (double) settings.humanTimingMs) * 0.001 * sr * state.humanTiming;
                    const double delay = baseDelay + humanDelay;
                    const float main = delayed (state, delay);
                    const double mod = std::sin (twoPi * state.ensemblePhase);
                    const double depth = std::max (0.0, (double) settings.ensembleDepthMs) * 0.001 * sr;
                    const float tapA = delayed (state, delay + 0.007 * sr
                                                       + depth * (0.5 + 0.5 * mod));
                    const float tapB = delayed (state, delay + 0.013 * sr
                                                       + depth * (0.5 - 0.5 * mod));
                    const float ensembleGain = 0.32f * std::clamp (settings.ensemble, 0.0f, 1.0f);
                    const float spread = std::clamp (settings.ensembleSpread, 0.0f, 1.0f);
                    const float centre = 0.5f * (tapA + tapB);
                    const float side = 0.5f * (tapA - tapB) * spread;
                    const float level = voice.active ? std::max (0.0f, voice.level) : 0.0f;
                    wetLeft[(size_t) i] += level * (panL * main + ensembleGain * (centre + side));
                    wetRight[(size_t) i] += level * (panR * main + ensembleGain * (centre - side));

                    state.write = (state.write + 1) & delayMask;
                    state.ensemblePhase += ensemblePhaseStep + 0.0144 * (double) (v + 1) / sr;
                    if (state.ensemblePhase >= 1.0) state.ensemblePhase -= 1.0;
                    state.vibratoPhase += std::clamp ((double) settings.vibratoRateHz, 0.1, 12.0) / sr;
                    if (state.vibratoPhase >= 1.0) state.vibratoPhase -= 1.0;
                }
                state.controlRemaining -= count;
                offset += count;
            }
        }

        std::copy_n (wetLeft.data(), numSamples, outputLeft);
        std::copy_n (wetRight.data(), numSamples, outputRight);
    }

private:
    struct VoiceState
    {
        LiveEpochShifter shifter;
        ExtremePitchShifter extremeShifter;
        FormantShifter formant;
        std::vector<float> shifted, delayRing, residualRing;
        int write = 0;
        double smoothedSemitones = 0.0;
        double humanPitch = 0.0, humanTarget = 0.0;
        double humanTiming = 0.0, humanTimingTarget = 0.0;
        int samplesToHumanTarget = 0;
        int controlRemaining = 0;
        double ratio = 1.0;
        double ensemblePhase = 0.0, vibratoPhase = 0.0;
        std::uint32_t randomState = 1;
        float lowPass = 0.0f, highPassMemory = 0.0f;
    };

    static double bipolarRandom (VoiceState& state) noexcept
    {
        state.randomState = state.randomState * 1664525u + 1013904223u;
        return 2.0 * (double) (state.randomState >> 8) / 16777215.0 - 1.0;
    }

    float delayed (const VoiceState& state, double delaySamples) const noexcept
    {
        return delayedFrom (state.delayRing, state.write, delaySamples);
    }

    float delayedFrom (const std::vector<float>& ring, int write,
                       double delaySamples) const noexcept
    {
        const double bounded = std::clamp (delaySamples, 0.0, (double) delayMask - 2.0);
        double read = (double) write - bounded;
        while (read < 0.0) read += (double) (delayMask + 1);
        const int i0 = (int) std::floor (read) & delayMask;
        const int i1 = (i0 + 1) & delayMask;
        const float frac = (float) (read - std::floor (read));
        return ring[(size_t) i0] + frac * (ring[(size_t) i1] - ring[(size_t) i0]);
    }

    double sr = 48000.0;
    int maxBlock = 0, delayMask = 0;
    bool extremeMode = false;
    std::array<VoiceState, maxVoices> states;
    std::vector<float> monoInput, wetLeft, wetRight;
};

} // namespace sattari::dsp
