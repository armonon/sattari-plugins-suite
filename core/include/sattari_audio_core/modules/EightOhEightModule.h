#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  EightOhEightModule — Sattari 808's DSP, extracted so the standalone plugin
//  and a Create rack slot run the SAME sub designer.
//
//  NOT a synth, despite the name. It is an audio-FOLLOWER: it tracks the pitch
//  of whatever you feed it and lays a synthesised 808 body underneath, gated and
//  retriggered by the input's own envelope. That is why it belongs in the Create
//  effect rack and not in an instrument host — it is `aumf`, not `aumu`, and it
//  needs an input signal to have anything to do.
//
//  MIDI notes, when present, override the tracked pitch: a played note is an
//  explicit statement of intent and should beat the detector. "Tune to Key" then
//  snaps the result to the session key from the cross-plugin bus.
// =============================================================================

namespace sattari::dsp
{

class EightOhEightModule final : public SattariModule
{
public:
    ~EightOhEightModule() override { sessionReader.stop(); }

    const char* moduleId()    const noexcept override { return "eightoheight"; }
    const char* displayName() const noexcept override { return "808"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
    
        // Body waveform: a pure sine (deep, clean) or a triangle (a touch more edge
        // that survives small speakers).
        out.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (prefix + "wave", 1), "Body Wave",
            juce::StringArray { "Sine", "Triangle" }, 0));

        // Tune-to-key snap: quantise the tracked fundamental to the KEY-bus scale so
        // the re-synthesised 808 always sits in the current song key. Default OFF so
        // out of the box the body follows the raw tracked pitch; the editor greys the
        // menu the same way the rest of the suite does. When on with no fresh key on
        // the bus, the body still follows raw pitch (nothing to snap to).
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "snap", 1), "Tune to Key", false));

        // Glide / portamento toward each new tracked pitch (0 ~ instant, 1 ~ 300 ms).
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "glide", 1), "Glide",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.15f));

        // Amp decay: how long the 808 body tail rings after each retrigger.
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "decay", 1), "Amp Decay",
            juce::NormalisableRange<float> (0.05f, 2.50f, 0.001f, 0.5f), 0.60f));

        // Drive / click: the attack transient "click" plus body saturation.
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "drive", 1), "Drive / Click",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));

        // Sub-fold / harmonics: mixes in a one-octave-down fold plus a little 2nd/3rd
        // harmonic weight for the classic distorted-sub growl.
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "subfold", 1), "Sub Fold",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));

        // Blend: dry/wet between the untouched input (0) and the 808 body (1).
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "blend", 1), "Blend",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f));

        // Output trim.
        out.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (prefix + "output", 1), "Output",
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int samplesPerBlock, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate;
        oscillatorPhase = 0.0f;
        smoothedHz = 55.0f;
        targetHz = 55.0f;
        ampEnv = 0.0f;
        clickEnv = 0.0f;
        inputEnvFast = 0.0f;
        noteActive = false;
        limiterState = 1.0f;

        monoScratch.assign ((size_t) juce::jmax (samplesPerBlock, 32), 0.0f);
        sessionReader.start();   // poll the cross-plugin session bus off the audio thread

        // Long low-frequency analysis window (>= one period at the 25 Hz floor).
        pitchDetector.prepare (sampleRate, 25.0f, 300.0f, 0.090f);
        analysisAccumulator = 0;
        analysisHop = juce::jmax (512, samplesPerBlock);

        // Sample-rate-correct limiter recovery (~20 ms).
        limiterRecoveryCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.02));

        displayHz.store (smoothedHz, std::memory_order_relaxed);
    }

    // The amp/click envelopes and the oscillator phase all carry across a
    // transport jump, so a relocate would otherwise replay the previous note's
    // tail underneath the new position.
    void reset() override
    {
        oscillatorPhase = 0.0f;
        smoothedHz = targetHz = 55.0f;
        ampEnv = 0.0f;
        clickEnv = 0.0f;
        inputEnvFast = 0.0f;
        noteActive = false;
        limiterState = 1.0f;
        std::fill (monoScratch.begin(), monoScratch.end(), 0.0f);
        displayHz.store (55.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if ((int) monoScratch.size() < numSamples)
            monoScratch.resize ((size_t) numSamples);

        // Mono sum of the input for pitch tracking + gating.
        std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* input = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                monoScratch[(size_t) i] += input[i] / (float) numChannels;
        }

        const int   wave    = (int) p ("wave");
        const bool  snap    = p ("snap") > 0.5f;
        const float glide   = p ("glide");
        const float decay   = p ("decay");
        const float drive   = p ("drive");
        const float subfold = p ("subfold");
        const float blend   = p ("blend");
        const float output  = p ("output");

        // Feed this block's mono sum into the shared pitch detector.
        pitchDetector.pushBlock (monoScratch.data(), numSamples);

        // Re-estimate the fundamental on a decimated schedule (analysis is heavy).
        analysisAccumulator += numSamples;
        float wantedHz = targetHz;
        bool  haveFresh = false;
        if (analysisAccumulator >= analysisHop)
        {
            analysisAccumulator = 0;
            const auto detected = pitchDetector.analyse (0.22f);
            if (detected.voiced && detected.frequencyHz > 0.0f)
            {
                // Octave-continuity guard: fold a fresh estimate that leaps more than
                // ~a fifth back toward the running pitch so the body never octave-jumps
                // on a mis-detection.
                wantedHz = sattari::dsp::foldToOctaveNear (detected.frequencyHz, smoothedHz);
                haveFresh = true;
            }
        }

        // A live MIDI note-on overrides the tracked pitch for that block (play the 808
        // from a keyboard on top of the follow).
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn())
            {
                wantedHz = sattari::dsp::midiToHz ((float) metadata.getMessage().getNoteNumber());
                haveFresh = true;
            }

        // Tune-to-key: snap the tracked note to the nearest scale tone of the current
        // KEY-bus session (root + scale), then convert back to Hz. No fresh bus key ->
        // pass the tracked pitch through unchanged.
        if (snap && wantedHz > 0.0f)
        {
            if (const auto s = sessionReader.current(); s.valid)
            {
                const int rawMidi = sattari::dsp::nearestMidiNote (wantedHz);
                if (rawMidi >= 0)
                {
                    const int snapped = sattari::dsp::snapMidiToScale (rawMidi, s.root, busScaleToId (s.scale));
                    wantedHz = sattari::dsp::midiToHz ((float) snapped);
                }
            }
        }

        targetHz = clampBodyHz (wantedHz);

        // Time-based portamento coefficient (glide=0 ~ instant, 1 ~ 300 ms).
        const float glideTime = 0.0005f + glide * 0.30f;
        const float glideCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * glideTime));

        // Amp-decay coefficient: exponential per-sample decay toward zero. The decay
        // parameter is the ~-60 dB time in seconds.
        const float decayCoeff = std::exp (-1.0f / (float) (currentSampleRate * juce::jmax (0.01, (double) decay / 6.9)));
        // Click transient decays fast (~6 ms) regardless of body decay.
        const float clickCoeff = std::exp (-1.0f / (float) (currentSampleRate * 0.006));
        // Fast input follower coefficient for gating/retrigger (~5 ms).
        const float envCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * 0.005));

        // Retrigger thresholds (hysteresis so a sustained note doesn't chatter).
        constexpr float onThresh  = 0.02f;
        constexpr float offThresh = 0.008f;

        const float driveK = 1.0f + drive * 6.0f;
        const float driveNorm = std::tanh (driveK);
        const float harm2 = subfold * 0.30f;   // 2nd harmonic weight
        const float subMix = subfold * 0.6f;   // one-octave-down fold weight

        for (int i = 0; i < numSamples; ++i)
        {
            const float in = monoScratch[(size_t) i];
            const float rect = std::abs (in);

            // Fast input follower for gating.
            inputEnvFast += envCoeff * (rect - inputEnvFast);

            // Retrigger the 808 body on a fresh onset (input rising through the on
            // threshold from below the off threshold). Reset the amp env to full and
            // fire the attack click; re-align phase so each hit starts clean.
            if (! noteActive && inputEnvFast > onThresh)
            {
                noteActive = true;
                ampEnv = 1.0f;
                clickEnv = 1.0f;
                oscillatorPhase = 0.0f;
            }
            else if (noteActive && inputEnvFast < offThresh)
            {
                noteActive = false;   // let the tail ring out; next onset retriggers
            }

            // Decay the envelopes.
            ampEnv *= decayCoeff;
            clickEnv *= clickCoeff;

            // Per-sample portamento toward the target pitch.
            smoothedHz += glideCoeff * (targetHz - smoothedHz);

            const float phaseInc = twoPi * smoothedHz / (float) currentSampleRate;
            oscillatorPhase += phaseInc;
            if (oscillatorPhase >= twoPi)
                oscillatorPhase -= twoPi;

            // 808 body: sine or triangle fundamental.
            float body;
            if (wave == 1)
            {
                // Triangle from phase [0, 2pi): rises to +1 at pi, back to -1.
                const float t = oscillatorPhase / twoPi;         // [0, 1)
                body = 4.0f * std::abs (t - 0.5f) - 1.0f;         // [-1, 1], triangle
            }
            else
            {
                body = std::sin (oscillatorPhase);
            }

            // Sub-harmonic fold one octave down (half-frequency sine) + a little 2nd
            // harmonic for growl.
            body += subMix * std::sin (0.5f * oscillatorPhase);
            body += harm2 * std::sin (2.0f * oscillatorPhase);

            // Body saturation / drive (normalised so drive adds harmonics without a
            // level jump).
            body = std::tanh (body * driveK) / driveNorm;

            // Attack click: a short high-passed transient burst on retrigger, scaled
            // by drive so "Drive / Click" governs both.
            const float click = clickEnv * clickEnv * (drive * 0.9f) * std::sin (oscillatorPhase * 6.0f);

            float wet = (body * ampEnv + click) * 0.9f;

            // Per-sample limiter so the wet body never runs past the ceiling.
            const float peak = std::abs (wet);
            constexpr float ceiling = 0.95f;
            if (peak > ceiling)
                limiterState = juce::jmin (limiterState, ceiling / (peak + 1.0e-9f));
            else
                limiterState += limiterRecoveryCoeff * (1.0f - limiterState);
            wet *= limiterState;

            // Apply the output trim + soft-clip ceiling to the WET body only, so the
            // dry signal stays untouched. Blending in the *processed* wet keeps the
            // wet path's behaviour identical to before (at blend=1 this is exactly
            // softClip(wet * output)), while blend=0 collapses to an exact dry
            // pass-through: mixed = dry, sample-for-sample.
            const float wetOut = juce::jlimit (-1.5f, 1.5f, softClip (wet * output));

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                const float dry = out[i];
                out[i] = (1.0f - blend) * dry + blend * wetOut;
            }
        }

        displayHz.store (smoothedHz, std::memory_order_relaxed);
        juce::ignoreUnused (haveFresh);
    }

    // Lock-free readout for an editor: the current 808 body frequency in Hz.
    float currentBodyHz() const noexcept { return displayHz.load (std::memory_order_relaxed); }

private:
    static constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // The 808 body lives in the sub range; clamp so a mistracked octave never sends
    // the oscillator above the low-bass region or into the infrasonic mud.
    static float clampBodyHz (float hz) noexcept
    {
        return juce::jlimit (20.0f, 160.0f, hz);
    }

    static float softClip (float x) noexcept
    {
        return std::tanh (x);
    }

    // Bus scale convention (matches Auto Pitch / Sattari Key): 0 major, 1 minor,
    // 2 chromatic. Map onto the shared scale-snap ids.
    static sattari::dsp::ScaleId busScaleToId (int busScale) noexcept
    {
        switch (busScale)
        {
            case 0:  return sattari::dsp::ScaleId::Major;
            case 1:  return sattari::dsp::ScaleId::Minor;
            default: return sattari::dsp::ScaleId::ChromaticOff;   // 2 = chromatic / unknown
        }
    }

    float p (const char* id) const noexcept
    {
        if (state == nullptr) return 0.0f;
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;

    sattari::xplugin::SessionReader sessionReader;   // cross-plugin "Tune to Key"

    double currentSampleRate = 44100.0;

    // Shared low-frequency pitch tracker, decimated on a hop counter.
    sattari::dsp::PitchDetector pitchDetector;
    int analysisHop = 512;
    int analysisAccumulator = 0;

    // 808 body oscillator + amp-decay envelope state.
    float oscillatorPhase = 0.0f;
    float smoothedHz = 55.0f;    // glided oscillator frequency (per-sample)
    float targetHz = 55.0f;      // block target the oscillator glides toward
    float ampEnv = 0.0f;         // retriggering amp-decay envelope [0, 1]
    float clickEnv = 0.0f;       // short attack click transient [0, 1]

    // Gate/retrigger tracking on the input level.
    float inputEnvFast = 0.0f;   // fast input follower for gating
    bool  noteActive = false;    // true while the input holds a note

    float limiterState = 1.0f;
    float limiterRecoveryCoeff = 0.001f;

    std::vector<float> monoScratch;

    std::atomic<float> displayHz { 55.0f };
};

} // namespace sattari::dsp
