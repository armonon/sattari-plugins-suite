#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  SpaceModule — Sattari Space's DSP, extracted so the standalone plugin and a
//  Create rack slot run the SAME reverb.
//
//  A Freeverb-based reverb (size / damping / width / mix) fed by a tempo-synced
//  stereo delay (note-division time, damped feedback, mix). The delay locks to
//  the host tempo; with no host tempo it falls back to the cross-plugin tempo
//  bus, then to 120 BPM. Allocation-free after prepare().
//
//  juce::Reverb scales dry by 2x and wet by 3x internally, so its own dry/wet is
//  not a clean crossfade. We take a PURE wet from it (dryLevel 0) and mix it
//  ourselves against the saved dry(+delay) signal — which is what makes
//  revmix = 0 an exact pass-through rather than an approximate one.
// =============================================================================

namespace sattari::dsp
{

class SpaceModule final : public SattariModule
{
public:
    ~SpaceModule() override { tempoReader.stop(); }

    static juce::StringArray divisionChoices()
    { return { "Off", "1/4", "1/4 dot", "1/8", "1/8 dot", "1/8 trip", "1/16" }; }

    const char* moduleId()    const noexcept override { return "space"; }
    const char* displayName() const noexcept override { return "Space"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "size", 1),     "Size",       R (0.0f, 1.0f, 0.01f), 0.6f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "damp", 1),     "Damping",    R (0.0f, 1.0f, 0.01f), 0.5f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "revmix", 1),   "Reverb Mix", R (0.0f, 1.0f, 0.01f), 0.3f));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "div", 1), "Delay Time", divisionChoices(), 3));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "feedback", 1), "Feedback",   R (0.0f, 0.95f, 0.01f), 0.35f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "delaymix", 1), "Delay Mix",  R (0.0f, 1.0f, 0.01f), 0.25f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "width", 1),    "Width",      R (0.0f, 1.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "output", 1),   "Output",     R (-24.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reverb.setSampleRate (sr);
        dryScratch.setSize (2, juce::jmax (1, maxBlockSize));
        tempoReader.start();
        delayLen = (int) std::ceil (sr * 3.0) + 8;    // up to a slow whole-note delay
        for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);
        reset();
    }

    // Freeverb holds a long tail; without this a transport jump replays the
    // previous position's reverb over the new one.
    void reset() override
    {
        reverb.reset();
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        delayWrite = 0;
        fbLpState = { { 0.0f, 0.0f } };
        dryScratch.clear();
        lastBpm.store (120.0, std::memory_order_relaxed);
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

        const int div = (int) std::round (p ("div"));
        const double beats = beatsFor (div);
        const bool delayOn = beats > 0.0;
        double delaySamples = beats * (60.0 / bpm) * sr;
        delaySamples = juce::jlimit (1.0, (double) delayLen - 4.0, delaySamples);

        const float fb       = p ("feedback");
        const float delayMix = p ("delaymix");
        const float width    = p ("width");
        const float outGain  = juce::Decibels::decibelsToGain (p ("output"));

        // --- Tempo-synced stereo delay (into the reverb) ---------------------
        if (delayOn)
        {
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < numCh; ++ch)
                {
                    auto& d = delayBuf[(size_t) ch];
                    const double rp = (double) delayWrite - delaySamples;
                    int r0 = (int) std::floor (rp); double fr = rp - r0;
                    while (r0 < 0) r0 += delayLen;
                    const int r1 = (r0 + 1) % delayLen;
                    const float delayed = d[(size_t) r0] * (1.0f - (float) fr) + d[(size_t) r1] * (float) fr;

                    const float in = buffer.getReadPointer (ch)[i];
                    // Damped feedback (one-pole LP in the loop).
                    fbLpState[(size_t) ch] += 0.35f * (delayed - fbLpState[(size_t) ch]);
                    d[(size_t) delayWrite] = in + fbLpState[(size_t) ch] * fb;
                    buffer.getWritePointer (ch)[i] = in + delayMix * delayed;
                }
                if (++delayWrite >= delayLen) delayWrite = 0;
            }
        }

        // --- Reverb (Freeverb) -----------------------------------------------
        // Pure wet out of juce::Reverb, our own crossfade against the saved dry.
        const float revmix = p ("revmix");
        const int scratchN = juce::jmin (n, dryScratch.getNumSamples());
        for (int ch = 0; ch < numCh; ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, scratchN);

        juce::Reverb::Parameters rp;
        rp.roomSize = p ("size");
        rp.damping  = p ("damp");
        rp.wetLevel = 1.0f / 3.0f;   // wet is *3 internally -> ~unity
        rp.dryLevel = 0.0f;
        rp.width    = width;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        if (numCh >= 2)
            reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
        else
            reverb.processMono (buffer.getWritePointer (0), n);

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            const float* dry = dryScratch.getReadPointer (ch);
            for (int i = 0; i < scratchN; ++i)
                out[i] = (dry[i] * (1.0f - revmix) + out[i] * revmix) * outGain;
        }
    }

    double currentBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }

private:
    // Beats (quarter-note = 1.0) for each division.
    static double beatsFor (int div) noexcept
    {
        switch (div)
        {
            case 1: return 1.0;         // 1/4
            case 2: return 1.5;         // 1/4 dotted
            case 3: return 0.5;         // 1/8
            case 4: return 0.75;        // 1/8 dotted
            case 5: return 1.0 / 3.0;   // 1/8 triplet
            case 6: return 0.25;        // 1/16
            default: return 0.0;        // Off
        }
    }

    double resolveBpm() noexcept
    {
        if (transport.hasHostBpm && transport.bpm > 1.0) return transport.bpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;   // tempo bus fallback
        return 120.0;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    juce::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;   // dry(+delay) held for our own reverb mix
    sattari::xplugin::TempoReader tempoReader;
    sattari::host::TransportSnapshot transport;

    // Tempo-synced stereo delay with damped feedback.
    std::array<std::vector<float>, 2> delayBuf;
    int delayLen = 0, delayWrite = 0;
    std::array<float, 2> fbLpState { { 0.0f, 0.0f } };
    std::atomic<double> lastBpm { 120.0 };
};

} // namespace sattari::dsp
