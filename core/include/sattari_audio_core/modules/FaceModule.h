#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariRbFormant.h>

#include <array>
#include <cmath>
#include <vector>

// =============================================================================
//  FaceModule — Sattari Face's DSP, extracted so the standalone plugin and a
//  Vocal rack slot run the SAME shifter.
//
//  Independently shifts PITCH and FORMANTS (the vocal-tract resonances that make
//  a voice sound big/small, male/female). Move formants alone for a gender/size
//  morph at the same notes; move pitch alone for a formant-corrected transpose;
//  move both for full transformation.
//
//  The dry path is delayed by exactly the Rubber Band latency, so a partial Mix
//  is a phase-aligned blend rather than a comb filter against a time-offset copy.
//  latencySamples() reports that same figure for the host to compensate.
// =============================================================================

namespace sattari::dsp
{

class FaceModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "face"; }
    const char* displayName() const noexcept override { return "Face"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "pitch", 1), "Pitch",
            R (-12.0f, 12.0f, 0.1f), 0.0f));    // semitones
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "formant", 1), "Formant",
            R (-12.0f, 12.0f, 0.1f), 0.0f));    // semitones (gender/size)
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "mix", 1), "Mix",
            R (0.0f, 1.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<P> (juce::ParameterID (prefix + "output", 1), "Output",
            R (-24.0f, 12.0f, 0.1f), 0.0f));    // dB
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int nb = juce::jmax (1, maxBlockSize);
        for (auto& r : rb) r.prepare (sr, nb);
        wetScratch.assign ((size_t) nb, 0.0f);

        rbLatency = rb[0].latencySamples();
        const int dl = rbLatency + nb + 8;
        for (auto& d : dryDelay) d.assign ((size_t) dl, 0.0f);

        reset();
    }

    void reset() override
    {
        for (auto& d : dryDelay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (wetScratch.begin(), wetScratch.end(), 0.0f);
        dryWrite = 0;
    }

    int latencySamples() const noexcept override { return rbLatency; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;
        if ((int) wetScratch.size() < n) return;   // prepare() sized this; never allocate here
        if (dryDelay[0].empty()) return;

        const double pitchScale   = std::pow (2.0, (double) p ("pitch")   / 12.0);
        const double formantScale = std::pow (2.0, (double) p ("formant") / 12.0);
        const float  mix     = p ("mix");
        const float  outGain = juce::Decibels::decibelsToGain (p ("output"));
        const int    dl      = (int) dryDelay[0].size();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* in = buffer.getReadPointer (ch);
            rb[(size_t) ch].process (in, wetScratch.data(), n, pitchScale, formantScale);

            auto& dd = dryDelay[(size_t) ch];
            float* out = buffer.getWritePointer (ch);
            int w = dryWrite;
            for (int i = 0; i < n; ++i)
            {
                dd[(size_t) w] = in[i];
                int r = w - rbLatency; while (r < 0) r += dl;
                const float dry = dd[(size_t) r];
                out[i] = (dry * (1.0f - mix) + wetScratch[(size_t) i] * mix) * outGain;
                if (++w >= dl) w = 0;
            }
            if (ch == numCh - 1) dryWrite = w;
        }
    }

private:
    float p (const char* id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    std::array<RbFormant, 2> rb;
    std::vector<float> wetScratch;

    // Dry delay so a dry/wet blend stays phase-aligned with the RB latency.
    std::array<std::vector<float>, 2> dryDelay;
    int dryWrite = 0, rbLatency = 0;
};

} // namespace sattari::dsp
