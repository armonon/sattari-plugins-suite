#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariPsolaShifter.h>

#include <array>
#include <cmath>
#include <vector>

// =============================================================================
//  DoubleModule — Sattari Double's DSP, extracted so the standalone plugin and a
//  Vocal rack slot run the SAME doubler.
//
//  Two synthetic "second takes" of the same vocal, panned apart and added around
//  the dry. What makes it read as a real double rather than a chorus is that the
//  two voices are DECORRELATED at every level:
//    * opposite detune (+/- cents), formant-preserving so the timbre is unchanged;
//    * different base delays (the second is 1.28x the first, not a round multiple,
//      so the two never line up rhythmically);
//    * an independent slow random-walk drift per voice, each with its own RNG
//      seed and its own re-target interval — so they breathe apart the way two
//      human takes do.
//
//  The detune is PSOLA-driven at the detected pitch period, so grains land on
//  real periods; unvoiced or silent input falls back to a nominal 150 Hz period
//  that just keeps the grain spacing sane.
// =============================================================================

namespace sattari::dsp
{

class DoubleModule final : public SattariModule
{
public:
    DoubleModule()
    {
        // Distinct seeds are load-bearing: identical drift on both voices would
        // collapse the effect into a single detuned copy.
        voices[0].rng.setSeed (0x1234abcd);
        voices[1].rng.setSeed (0x7f3e2a11);
    }

    const char* moduleId()    const noexcept override { return "double"; }
    const char* displayName() const noexcept override { return "Double"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "detune", 1), "Detune",
            R (0.0f, 30.0f, 0.1f), 12.0f));   // cents
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "timing", 1), "Timing",
            R (3.0f, 45.0f, 0.1f), 18.0f));   // ms
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "drift", 1), "Drift",
            R (0.0f, 1.0f, 0.01f), 0.4f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "width", 1), "Width",
            R (0.0f, 1.0f, 0.01f), 0.85f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "level", 1), "Doubles",
            R (0.0f, 1.0f, 0.01f), 0.7f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "output", 1), "Output",
            R (-24.0f, 12.0f, 0.1f), 0.0f));  // dB
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        detector.prepare (sr, 60.0f, 1000.0f, 0.045f);
        const int nb = juce::jmax (1, maxBlockSize);
        monoScratch.assign  ((size_t) nb, 0.0f);
        outLScratch.assign  ((size_t) nb, 0.0f);
        outRScratch.assign  ((size_t) nb, 0.0f);
        shiftScratch.assign ((size_t) nb, 0.0f);

        delayLen = (int) std::ceil (sr * 0.09) + 64;   // covers max timing + drift + slack
        for (auto& v : voices)
        {
            v.psola.prepare (sr, 60.0, 1000.0);
            v.delay.assign ((size_t) delayLen, 0.0f);
        }
        reset();
    }

    void reset() override
    {
        for (auto& v : voices)
        {
            std::fill (v.delay.begin(), v.delay.end(), 0.0f);
            v.writePos = 0;
            v.driftPos = v.driftTarget = 0.0;
            v.driftCountdown = 0;
        }
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (state == nullptr) return;
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;
        const int capacity = (int) monoScratch.size();
        if (n > capacity)
        {
            if (capacity <= 0) return;
            for (int start = 0; start < n; start += capacity)
            {
                const int count = juce::jmin (capacity, n - start);
                juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(),
                                                numCh, start, count);
                process (chunk, midi);
            }
            return;
        }

        // Mono sum for pitch detection (drives PSOLA's grain spacing).
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) s += buffer.getReadPointer (ch)[i];
            monoScratch[(size_t) i] = s / (float) numCh;
        }
        detector.pushBlock (monoScratch.data(), n);
        const auto det = detector.analyse (0.25f);
        const double period = (det.voiced && det.frequencyHz > 0.0f)
                            ? juce::jlimit (40.0, 800.0, sr / (double) det.frequencyHz)
                            : sr / 150.0;   // nominal fallback keeps grains sane

        const float detuneC  = p ("detune");
        const float timingMs = p ("timing");
        const float drift    = p ("drift");
        const float width    = p ("width");
        const float level    = p ("level");
        const float outGain  = juce::Decibels::decibelsToGain (p ("output"));

        const double baseDelay = timingMs * 0.001 * sr;
        const float  maxDrift  = drift * (float) (0.006 * sr);   // up to ~6 ms wobble

        const int Lc = 0, Rc = juce::jmin (1, numCh - 1);

        // Equal-power pan: voice 0 to the left, voice 1 to the right, spread by width.
        auto panGain = [] (float pan, float& gl, float& gr)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
            gl = std::cos (t * juce::MathConstants<float>::halfPi);
            gr = std::sin (t * juce::MathConstants<float>::halfPi);
        };
        float g0l, g0r, g1l, g1r;
        panGain (-width, g0l, g0r);
        panGain (+width, g1l, g1r);

        // Start from the dry signal (doubles are added around it).
        for (int i = 0; i < n; ++i)
        {
            outLScratch[(size_t) i] = buffer.getReadPointer (Lc)[i];
            outRScratch[(size_t) i] = buffer.getReadPointer (Rc)[i];
        }

        for (int v = 0; v < 2; ++v)
        {
            auto& vc = voices[(size_t) v];
            const double ratio = std::pow (2.0, (v == 0 ? +detuneC : -detuneC) / 1200.0);
            const double delaySamples = baseDelay * (v == 0 ? 1.0 : 1.28);
            const float  gl = (v == 0 ? g0l : g1l);
            const float  gr = (v == 0 ? g0r : g1r);

            // Formant-preserving detune of the mono take.
            vc.psola.process (monoScratch.data(), shiftScratch.data(), n, ratio, period);

            for (int i = 0; i < n; ++i)
            {
                // Slow random-walk drift so the two takes breathe independently.
                if (--vc.driftCountdown <= 0)
                {
                    vc.driftTarget    = (vc.rng.nextDouble() * 2.0 - 1.0) * maxDrift;
                    vc.driftCountdown = (int) (sr * (0.18 + 0.12 * vc.rng.nextDouble()));
                }
                vc.driftPos += 0.0006 * (vc.driftTarget - vc.driftPos);

                vc.delay[(size_t) vc.writePos] = shiftScratch[(size_t) i];
                const double readPos = (double) vc.writePos - (delaySamples + vc.driftPos);
                const float d = readDelay (vc.delay, delayLen, readPos);
                outLScratch[(size_t) i] += level * gl * d;
                outRScratch[(size_t) i] += level * gr * d;
                if (++vc.writePos >= delayLen) vc.writePos = 0;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            buffer.getWritePointer (Lc)[i] = outLScratch[(size_t) i] * outGain;
            if (numCh > 1) buffer.getWritePointer (1)[i] = outRScratch[(size_t) i] * outGain;
        }
    }

private:
    // Fractional read from a ring buffer, `pos` in samples (may be negative).
    static float readDelay (const std::vector<float>& ring, int len, double pos) noexcept
    {
        while (pos < 0.0)  pos += len;
        while (pos >= len) pos -= len;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % len;
        const float f = (float) (pos - i0);
        return ring[(size_t) i0] * (1.0f - f) + ring[(size_t) i1] * f;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    PitchDetector detector;

    // Two double voices. Each owns a PSOLA shifter, a modulated delay line, and a
    // slow random-walk drift generator (decorrelated per voice).
    struct Voice
    {
        PsolaShifter psola;
        std::vector<float> delay;    // ring buffer
        int    writePos = 0;
        double driftPos = 0.0;       // current drift offset in samples (smoothed)
        double driftTarget = 0.0;
        int    driftCountdown = 0;
        juce::Random rng;
    };
    std::array<Voice, 2> voices;
    std::vector<float> monoScratch, outLScratch, outRScratch, shiftScratch;
    int delayLen = 0;
};

} // namespace sattari::dsp
