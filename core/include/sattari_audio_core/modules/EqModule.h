#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <array>
#include <cmath>

// =============================================================================
//  EQ — HPF / LPF + three dynamic peaking bands.
// -----------------------------------------------------------------------------
//  Each bell has a Dyn amount. At Dyn 0 the band is a static peaking filter; as
//  Dyn rises, the band's gain is scaled back by how much energy is actually
//  present in that band (a bandpass detector + envelope follower per band), so it
//  only works when there is something to work on.
//
//  Lifted verbatim from the standalone plugin's DSP, which now wraps this.
// =============================================================================

namespace sattari::dsp
{

class EqModule final : public SattariModule
{
public:
    static constexpr int kBands = 3;

    const char* moduleId()    const noexcept override { return "eq"; }
    const char* displayName() const noexcept override { return "EQ"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        auto hz = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi, 1.0f, 0.35f); };

        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "hpf", 1 }, "HPF",
            hz (20.0f, 500.0f), 20.0f));                                            // 20 = off
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "lpf", 1 }, "LPF",
            hz (2000.0f, 20000.0f), 20000.0f));                                     // 20k = off

        const float defF[kBands] = { 120.0f, 1000.0f, 6000.0f };
        for (int i = 0; i < kBands; ++i)
        {
            const juce::String b (i + 1);
            out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "f" + b, 1 },
                "Band " + b + " Freq", hz (30.0f, 18000.0f), defF[i]));
            out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "g" + b, 1 },
                "Band " + b + " Gain", juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "q" + b, 1 },
                "Band " + b + " Q", juce::NormalisableRange<float> (0.3f, 8.0f, 0.01f, 0.5f), 0.9f));
            out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "d" + b, 1 },
                "Band " + b + " Dyn", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
        }
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "output", 1 }, "Output",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        pHpf = apvts.getRawParameterValue (prefix + "hpf");
        pLpf = apvts.getRawParameterValue (prefix + "lpf");
        for (int i = 0; i < kBands; ++i)
        {
            const juce::String b (i + 1);
            pF[i] = apvts.getRawParameterValue (prefix + "f" + b);
            pG[i] = apvts.getRawParameterValue (prefix + "g" + b);
            pQ[i] = apvts.getRawParameterValue (prefix + "q" + b);
            pD[i] = apvts.getRawParameterValue (prefix + "d" + b);
        }
        pOut = apvts.getRawParameterValue (prefix + "output");
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr_ = sampleRate;
        for (auto& e : detEnv) e.prepare (sampleRate, 5.0f, 80.0f);
        reset();
    }

    void reset() override
    {
        for (auto& band : bell) for (auto& b : band) b.reset();
        for (auto& b : detBP) b.reset();
        for (auto& e : detEnv) e.reset (0.0f);
        for (auto& b : hp) b.reset();
        for (auto& b : lp) b.reset();
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const float hpf = val (pHpf, 20.0f), lpf = val (pLpf, 20000.0f);
        const bool hpOn = hpf > 21.0f, lpOn = lpf < 19900.0f;
        if (hpOn) for (auto& b : hp) b.setHP (hpf, sr_);
        if (lpOn) for (auto& b : lp) b.setLP (lpf, sr_);

        float f[kBands], g[kBands], q[kBands], dyn[kBands];
        for (int i = 0; i < kBands; ++i)
        {
            f[i] = val (pF[i], 1000.0f); g[i] = val (pG[i], 0.0f);
            q[i] = val (pQ[i], 0.9f);    dyn[i] = val (pD[i], 0.0f);
            for (auto& ch : bell[(size_t) i]) ch.setPeaking (f[i], g[i], q[i], sr_);
            detBP[(size_t) i].setBandpass (f[i], 2.0, sr_);
        }
        const float outGain = juce::Decibels::decibelsToGain (val (pOut, 0.0f));

        for (int s = 0; s < n; ++s)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) mono += buffer.getReadPointer (ch)[s];
            mono /= (float) juce::jmax (1, numCh);

            float k[kBands];
            for (int i = 0; i < kBands; ++i)
            {
                const float det = (float) detBP[(size_t) i].process (mono);
                const float e = detEnv[(size_t) i].processRectified (std::fabs (det));
                const float eDb = juce::Decibels::gainToDecibels (e + 1.0e-6f);
                const float activity = juce::jlimit (0.0f, 1.0f, (eDb + 45.0f) / 30.0f);  // -45..-15 dB
                k[i] = 1.0f - dyn[i] * (1.0f - activity);   // dyn 0 -> static (k=1)
            }

            for (int ch = 0; ch < numCh; ++ch)
            {
                double x = buffer.getReadPointer (ch)[s];
                if (hpOn) x = hp[(size_t) ch].process (x);
                if (lpOn) x = lp[(size_t) ch].process (x);
                for (int i = 0; i < kBands; ++i)
                {
                    const double peaked = bell[(size_t) i][(size_t) ch].process (x);
                    x = x + (double) k[i] * (peaked - x);
                }
                buffer.getWritePointer (ch)[s] = (float) x * outGain;
            }
        }
    }

private:
    static float val (std::atomic<float>* p, float fallback) noexcept
    { return p != nullptr ? p->load() : fallback; }

    static double clampF (double f, double sr) { return juce::jlimit (10.0, sr * 0.45, f); }

    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        void reset() { x1=x2=y1=y2=0; }
        double process (double x) noexcept
        { const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }

        void setPeaking (double f, double gainDb, double Q, double sr)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
            const double a0 = 1.0 + alpha / A;
            b0 = (1.0 + alpha * A) / a0; b1 = (-2.0 * c) / a0; b2 = (1.0 - alpha * A) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha / A) / a0;
        }
        void setBandpass (double f, double Q, double sr)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
            const double a0 = 1.0 + alpha;
            b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0; a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
        void setHP (double f, double sr)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
        void setLP (double f, double sr)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 - c) * 0.5) / a0; b1 = (1.0 - c) / a0; b2 = ((1.0 - c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
    };

    std::atomic<float>* pHpf = nullptr; std::atomic<float>* pLpf = nullptr;
    std::atomic<float>* pF[kBands] {}; std::atomic<float>* pG[kBands] {};
    std::atomic<float>* pQ[kBands] {}; std::atomic<float>* pD[kBands] {};
    std::atomic<float>* pOut = nullptr;

    double sr_ = 44100.0;
    std::array<std::array<Biquad, 2>, kBands> bell;    // [band][channel] peaking
    std::array<Biquad, kBands>                detBP;   // [band] bandpass detector (mono/linked)
    std::array<sattari::dsp::EnvelopeFollower, kBands> detEnv;
    std::array<Biquad, 2> hp, lp;
};

} // namespace sattari::dsp
