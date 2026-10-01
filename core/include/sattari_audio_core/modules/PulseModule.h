#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <array>
#include <cmath>

// =============================================================================
//  PulseModule — Sattari Pulse's DSP, extracted so the standalone plugin and a Mix
//  rack slot run the SAME tempo pump.
//
//  A rhythmic ducker that needs NO sidechain: the curve is generated from the
//  session tempo, phase-anchored to host PPQ while the transport plays so the pump
//  stays on the grid across loops and relocation, and free-running otherwise.
//
//  Guaranteed attenuating: |out| <= |dry| always. That is why the filtered path is
//  clamped back to the un-filtered ducked bound — a one-pole LP has unity DC gain
//  but its phase lag lets the output momentarily exceed |dry*g| by carrying memory
//  of larger recent samples across a zero crossing, which would turn a ducker into
//  something that can boost.
// =============================================================================

namespace sattari::dsp
{

class PulseModule final : public SattariModule
{
public:
    ~PulseModule() override { tempoReader.stop(); }

    static juce::StringArray rateChoices()  { return { rateLabels.data(), (int) rateLabels.size() }; }
    static juce::StringArray shapeChoices() { return { shapeLabels.data(), (int) shapeLabels.size() }; }

    const char* moduleId()    const noexcept override { return "pulse"; }
    const char* displayName() const noexcept override { return "Pulse"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "shape", 1), "Shape", shapeChoices(), 0));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "rate", 1), "Rate", rateChoices(), 2));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "depth", 1),  "Depth",  R (0.0f, 1.0f, 0.001f), 0.8f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "smooth", 1), "Smooth", R (0.0f, 1.0f, 0.001f), 0.25f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "filter", 1), "Filter", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "mix", 1),    "Mix",    R (0.0f, 1.0f, 0.001f), 1.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        tempoReader.start();
        reset();
    }

    void reset() override
    {
        phase = 0.0;
        smoothedGain = 1.0f;
        smoothCoeff = 0.0f;
        lpStateL = lpStateR = 0.0f;
        lastBpm.store (120.0, std::memory_order_relaxed);
        lastGain.store (1.0f, std::memory_order_relaxed);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const double bpm = resolveBpm();
        lastBpm.store (bpm, std::memory_order_relaxed);

        const int   shape  = juce::jlimit (0, (int) shapeLabels.size() - 1, (int) std::round (p ("shape")));
        const int   rate   = juce::jlimit (0, (int) rateLabels.size() - 1, (int) std::round (p ("rate")));
        const float depth  = juce::jlimit (0.0f, 1.0f, p ("depth"));
        const float smooth = juce::jlimit (0.0f, 1.0f, p ("smooth"));
        const float filter = juce::jlimit (0.0f, 1.0f, p ("filter"));
        const float mix    = juce::jlimit (0.0f, 1.0f, p ("mix"));

        const double cycleQuarters   = quartersFor (rate);
        const double cyclesPerSample = (bpm / 60.0) / sr / cycleQuarters;

        // Re-anchor to the host timeline while playing so the pump stays on the grid
        // across loops / relocation. Free-runs otherwise.
        if (transport.isPlaying && transport.hasPpq)
            phase = std::fmod (transport.ppqPosition / cycleQuarters, 1.0);
        if (phase < 0.0) phase += 1.0;

        // Smooth (0..1) -> time constant ~0..60 ms. At 0 the coefficient is 0 so the
        // gain follows the curve exactly, which keeps depth=0 an exact pass-through.
        const double smoothMs = (double) smooth * 60.0;
        smoothCoeff = smoothMs > 0.0
            ? (float) std::exp (-1.0 / (smoothMs * 0.001 * sr))
            : 0.0f;

        const bool  filterOn = filter > 1.0e-4f;
        const float nyquist  = (float) (sr * 0.5);

        float lastGainThisBlock = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float raw = curveGain (shape, (float) phase);
            const float targetGain = 1.0f - depth * (1.0f - raw);   // in [1-depth, 1]

            smoothedGain = smoothCoeff * smoothedGain + (1.0f - smoothCoeff) * targetGain;
            const float g = juce::jlimit (0.0f, 1.0f, smoothedGain);
            lastGainThisBlock = g;

            // Filter movement driven by the SAME curve: dark on the beat, open off it.
            float lpCoeff = 0.0f;
            if (filterOn)
            {
                const float openHz  = juce::jmin (18000.0f, nyquist * 0.95f);
                const float floorHz = 400.0f;
                const float movedHz = openHz - filter * (openHz - floorHz) * (1.0f - g);
                const float cutoffHz = juce::jlimit (20.0f, nyquist * 0.99f, movedHz);
                const float x = std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sr);
                lpCoeff = juce::jlimit (0.0f, 0.9999f, x);
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                const float dry = d[i];

                float shaped = dry * g;   // ducked signal, bounded by |dry|

                if (filterOn)
                {
                    float& st = (ch == 0) ? lpStateL : lpStateR;
                    st = lpCoeff * st + (1.0f - lpCoeff) * shaped;
                    shaped = st;

                    // Pulse may only ever ATTENUATE. The LP's phase lag can push its
                    // output past |dry*g|, so clamp back to that bound.
                    const float bound = std::fabs (dry * g);
                    shaped = juce::jlimit (-bound, bound, shaped);
                }

                // Both dry and shaped are bounded by |dry| and mix is in [0,1], so the
                // result always satisfies |out| <= |dry|.
                d[i] = dry + mix * (shaped - dry);
            }

            phase += cyclesPerSample;
            if (phase >= 1.0) phase -= 1.0;
        }

        lastGain.store (lastGainThisBlock, std::memory_order_relaxed);
    }

    double currentBpm()  const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    float  currentGain() const noexcept { return lastGain.load (std::memory_order_relaxed); }

private:
    static constexpr std::array<const char*, 7> rateLabels { "1/1", "1/2", "1/4", "1/4 dot", "1/8", "1/8 trip", "1/16" };
    static constexpr std::array<const char*, 5> shapeLabels { "Sine", "Saw Down", "Saw Up", "Square", "Spike" };
    // Ducking curve: phase 0 = the beat (deepest duck), returning toward unity.
    static float curveGain (int shape, float phase) noexcept
    {
        phase = phase - std::floor (phase);   // wrap to [0,1)
        switch (shape)
        {
            case 0: // Sine: smooth dip at the beat, unity mid-cycle.
                return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
            case 1: return phase;                       // Saw Down
            case 2: return 1.0f - phase;                // Saw Up
            case 3: return phase < 0.5f ? 0.0f : 1.0f;  // Square / gate
            case 4:                                     // Spike: narrow notch on the beat
            {
                constexpr float w = 0.25f;
                if (phase >= w) return 1.0f;
                return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (phase / w));
            }
            default: return 1.0f;
        }
    }

    static double quartersFor (int rate) noexcept
    {
        switch (rate)
        {
            case 0: return 4.0;         // 1/1
            case 1: return 2.0;         // 1/2
            case 2: return 1.0;         // 1/4
            case 3: return 1.5;         // 1/4 dotted
            case 4: return 0.5;         // 1/8
            case 5: return 1.0 / 3.0;   // 1/8 triplet
            case 6: return 0.25;        // 1/16
            default: return 1.0;
        }
    }

    double resolveBpm() noexcept
    {
        if (transport.hasHostBpm && transport.bpm > 1.0) return transport.bpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;
        return 120.0;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    double phase = 0.0;
    float  smoothedGain = 1.0f;
    float  smoothCoeff = 0.0f;
    float  lpStateL = 0.0f, lpStateR = 0.0f;

    sattari::host::TransportSnapshot transport;
    sattari::xplugin::TempoReader tempoReader;

    std::atomic<double> lastBpm  { 120.0 };
    std::atomic<float>  lastGain { 1.0f };
};

} // namespace sattari::dsp
