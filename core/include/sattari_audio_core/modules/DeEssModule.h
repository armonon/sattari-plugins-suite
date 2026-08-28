#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <array>
#include <atomic>
#include <cmath>

// =============================================================================
//  DeEssModule — Sattari De-Ess's DSP, extracted so the standalone plugin and a
//  Mix rack slot run the SAME de-esser.
//
//  Detection is on a high-passed sibilance band, stereo-linked (max of the two)
//  so ducking never pulls the image. Two reduction modes:
//    Split     — crossfade the dry signal toward a high-shelf-cut version, by how
//                far the band is over threshold. Only the sibilance is touched.
//    Wideband  — duck the whole signal, classic broadband de-essing.
//  Listen solos the detector band so the user can hear what is being keyed on.
// =============================================================================

namespace sattari::dsp
{

class DeEssModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "deess"; }
    const char* displayName() const noexcept override { return "De-Ess"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using R = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (prefix + "freq", 1), "Frequency", R (2500.0f, 11000.0f, 1.0f, 0.5f), 6500.0f));
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (prefix + "threshold", 1), "Threshold", R (-60.0f, 0.0f, 0.1f), -28.0f));
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (prefix + "range", 1), "Range", R (0.0f, 24.0f, 0.1f), 8.0f));   // max reduction dB
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "mode", 1), "Mode", juce::StringArray { "Split", "Wideband" }, 0));
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "listen", 1), "Listen", false));
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (prefix + "output", 1), "Output", R (-12.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        env.prepare (sr, 0.8f, 45.0f);   // fast attack catches the "ess", medium release
        reset();
    }

    void reset() override
    {
        for (auto& b : shelf) b.reset();
        for (auto& b : hp)    b.reset();
        env.reset (0.0f);
        reductionDb.store (0.0f);
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const double freq   = (double) p ("freq");
        const float rangeDb = p ("range");
        for (int ch = 0; ch < 2; ++ch)
        {
            hp[(size_t) ch].setHP (freq, sr);
            shelf[(size_t) ch].setHighShelf (freq, -rangeDb, sr);   // full-cut shape
        }

        const float threshLin = juce::Decibels::decibelsToGain (p ("threshold"));
        const bool  wideband  = p ("mode") > 0.5f;
        const bool  listen    = p ("listen") > 0.5f;
        const float outGain   = juce::Decibels::decibelsToGain (p ("output"));

        float maxRed = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Detect on the high (sibilance) band; stereo-linked so the image stays
            // put. hpBand is also what Listen solos.
            float hpBand[2] = { 0.0f, 0.0f };
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                hpBand[ch] = hp[(size_t) ch].process (buffer.getReadPointer (ch)[i]);
                peak = std::max (peak, std::fabs (hpBand[ch]));
            }
            const float e = env.processRectified (peak);

            // Activity: 0 below threshold, ramping to 1 by the time we've hit Range.
            float redDb = 0.0f;
            if (e > threshLin && threshLin > 0.0f && rangeDb > 0.0f)
            {
                const float overDb = juce::Decibels::gainToDecibels (e / threshLin);
                redDb = juce::jmin (rangeDb, overDb);
                maxRed = std::max (maxRed, redDb);
            }
            const float k  = rangeDb > 0.0f ? redDb / rangeDb : 0.0f;   // split crossfade
            const float gr = juce::Decibels::decibelsToGain (-redDb);   // wideband gain

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float in = buffer.getReadPointer (ch)[i];
                const float sh = shelf[(size_t) ch].process (in);   // run continuously
                float y;
                if (listen)         y = hpBand[ch];              // solo the detector band
                else if (wideband)  y = in * gr;                 // duck everything
                else                y = in + k * (sh - in);      // crossfade dry -> shelved
                buffer.getWritePointer (ch)[i] = y * outGain;
            }
        }

        reductionDb.store (maxRed, std::memory_order_relaxed);
    }

    float currentReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    struct Biquad
    {
        float b0=1, b1=0, b2=0, a1=0, a2=0, x1=0, x2=0, y1=0, y2=0;
        void setHP (double freq, double srate)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (100.0, srate * 0.45, freq) / srate;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s / (2.0 * 0.707);
            const double a0 = 1.0 + alpha;
            b0 = (float) (((1.0 + c) * 0.5) / a0);
            b1 = (float) ((-(1.0 + c)) / a0);
            b2 = (float) (((1.0 + c) * 0.5) / a0);
            a1 = (float) ((-2.0 * c) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }
        void setHighShelf (double freq, double gainDb, double srate)
        {
            const double A  = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (100.0, srate * 0.45, freq) / srate;
            const double c = std::cos (w0), s = std::sin (w0);
            // Shelf slope S = 1, i.e. (1/S - 1) == 0 — written out so the RBJ form
            // stays recognisable against the reference equations.
            const double alpha = s * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);
            const double tsa = 2.0 * std::sqrt (A) * alpha;
            const double a0 =        (A + 1.0) - (A - 1.0) * c + tsa;
            b0 = (float) ( A * ((A + 1.0) + (A - 1.0) * c + tsa) / a0);
            b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0);
            b2 = (float) ( A * ((A + 1.0) + (A - 1.0) * c - tsa) / a0);
            a1 = (float) ( 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0);
            a2 = (float) (       ((A + 1.0) - (A - 1.0) * c - tsa) / a0);
        }
        void reset() { x1 = x2 = y1 = y2 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    std::array<Biquad, 2> hp, shelf;
    EnvelopeFollower env;   // stereo-linked band envelope
    std::atomic<float> reductionDb { 0.0f };
};

} // namespace sattari::dsp
