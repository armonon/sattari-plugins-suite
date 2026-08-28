#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <cmath>

// =============================================================================
//  CompModule — Sattari Comp's DSP, extracted so the standalone plugin and a Mix
//  rack slot run the SAME compressor.
//
//  Log-domain gain computer with a soft knee, stereo-linked peak detection through
//  a switchable sidechain high-pass, and attack/release smoothing on the gain
//  reduction envelope (fast toward more reduction, slow on recovery).
//
//  It is the first module to need host tempo: Tempo Release resolves its release
//  time from a musical division at the session BPM. The host pushes that in via
//  setTransport() before each process() call, because a module has no playhead of
//  its own. The cross-plugin TEMPO bus is the fallback when the host reports none.
// =============================================================================

namespace sattari::dsp
{

class CompModule final : public SattariModule
{
public:
    // The module owns the TEMPO-bus reader, so it owns stopping it too. There is no
    // release hook on SattariModule and adding one for a single module is not worth
    // the interface churn — the destructor is the natural place.
    ~CompModule() override { tempoReader.stop(); }

    const char* moduleId()    const noexcept override { return "comp"; }
    const char* displayName() const noexcept override { return "Comp"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        auto f = [&out, &prefix] (const char* id, const char* name, float lo, float hi,
                                  float def, float step, const char* suffix)
        {
            out.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (prefix + id, 1), name,
                juce::NormalisableRange<float> (lo, hi, step), def,
                juce::AudioParameterFloatAttributes().withLabel (suffix)));
        };
        auto b = [&out, &prefix] (const char* id, const char* name, bool def)
        {
            out.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID (prefix + id, 1), name, def));
        };

        f ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, 0.1f, "dB");
        f ("ratio",     "Ratio",       1.0f, 20.0f,  4.0f, 0.01f, ":1");
        f ("knee",      "Knee",        0.0f, 24.0f,  6.0f, 0.1f, "dB");
        f ("attack",    "Attack",      0.1f, 120.0f, 8.0f, 0.1f, "ms");
        f ("release",   "Release",    20.0f, 1500.0f, 140.0f, 1.0f, "ms");
        b ("autoRelease", "Auto Release", false);
        f ("makeup",    "Makeup",    -12.0f, 24.0f,  0.0f, 0.1f, "dB");
        b ("autoGain",  "Auto Gain",  false);
        f ("mix",       "Mix",         0.0f, 100.0f, 100.0f, 0.1f, "%");
        f ("scHpf",     "SC HPF",     20.0f, 400.0f, 20.0f, 1.0f, "Hz");
        b ("scListen",  "SC Listen",  false);
        b ("tempoRelease", "Tempo Release", false);
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "releaseDiv", 1), "Release Div",
            juce::StringArray { "1/16", "1/8T", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2" }, 2));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        grSmoother.prepare (sr, 8.0f, 140.0f);
        reset();
        tempoReader.start();
    }

    void reset() override
    {
        grSmoother.reset (0.0f);
        smoothedGrDb = 0.0f;
        hpX1 = hpY1 = 0.0f;
        gainReductionDb.store (0.0f);
        effectiveReleaseMs.store (140.0);
        tempoReleaseActive.store (false);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override
    {
        hostBpm    = t.bpm;
        hostHasBpm = t.hasHostBpm;
    }

    // When the shell routes an external key, the detector reads THAT instead of the
    // main signal — compress a bass to a kick, duck a pad under a vocal. No parameter
    // guards this: routing a sidechain is itself the user's intent, and an extra
    // "external" toggle would let a routed key silently do nothing. Unrouted (nullptr)
    // is the previous behaviour exactly, so nothing changes for existing sessions.
    void setSidechain (const juce::AudioBuffer<float>* sc) noexcept override { sidechain = sc; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh == 0 || n == 0) return;

        const float thresholdDb = p ("threshold");
        const float ratio       = juce::jmax (1.0f, p ("ratio"));
        const float kneeDb      = juce::jmax (0.0f, p ("knee"));
        const float attackMs    = p ("attack");
        const float userRelMs   = p ("release");
        const bool  autoRelease = p ("autoRelease") > 0.5f;
        const float makeupDb    = p ("makeup");
        const bool  autoGain    = p ("autoGain") > 0.5f;
        const float mix         = juce::jlimit (0.0f, 1.0f, p ("mix") * 0.01f);
        const float scHpfHz     = p ("scHpf");
        const bool  scListen    = p ("scListen") > 0.5f;

        double releaseMs = resolveReleaseMs ((double) userRelMs);
        if (autoRelease)
            releaseMs = juce::jlimit (20.0, 3000.0, releaseMs * 1.8);
        effectiveReleaseMs.store (releaseMs);

        grSmoother.setTimes (attackMs, (float) releaseMs);

        // Auto make-up: add back roughly the static gain lost at the threshold, at a
        // gentle 0.5 factor so it stays musical rather than a full restore.
        const float slope = 1.0f - 1.0f / ratio;
        const float autoMakeupDb = autoGain ? 0.5f * slope * (-thresholdDb) : 0.0f;
        const float makeupGain = dbToGain (makeupDb + autoMakeupDb);

        const float invRatio = 1.0f / ratio;
        const float halfKnee = 0.5f * kneeDb;

        // Only key off the sidechain when it actually covers this block.
        const bool scActive = sidechain != nullptr
                              && sidechain->getNumChannels() > 0
                              && sidechain->getNumSamples() >= n;
        const int scChans = scActive ? juce::jmin (2, sidechain->getNumChannels()) : 0;

        float grForMeter = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // Stereo-linked detector: max of |L|,|R| keeps the image stable. Keys off
            // the external sidechain when one is routed, else the main signal.
            float det = 0.0f;
            if (scActive)
            {
                for (int ch = 0; ch < scChans; ++ch)
                    det = std::max (det, std::fabs (sidechain->getReadPointer (ch)[i]));
            }
            else
            {
                for (int ch = 0; ch < numCh; ++ch)
                    det = std::max (det, std::fabs (buffer.getReadPointer (ch)[i]));
            }

            const float filtered = detectorHighPass (det, scHpfHz);
            const float detMag = std::fabs (filtered);
            const float detDb = juce::Decibels::gainToDecibels (detMag + 1.0e-9f, -120.0f);

            // Log-domain gain computer (soft knee). targetGrDb <= 0.
            const float over = detDb - thresholdDb;
            float targetGrDb;
            if (kneeDb > 0.0001f && over > -halfKnee && over < halfKnee)
            {
                const float x = over + halfKnee;                    // 0 .. kneeDb
                targetGrDb = -((1.0f - invRatio) * (x * x) / (2.0f * kneeDb));
            }
            else if (over <= -halfKnee)
            {
                targetGrDb = 0.0f;                                  // below the knee
            }
            else
            {
                targetGrDb = -(over) * (1.0f - invRatio);           // hard-knee region
            }

            // Fast toward MORE reduction, slow on recovery.
            smoothedGrDb = grSmoother.processFalling (targetGrDb);
            const float compGain = dbToGain (smoothedGrDb);

            grForMeter = std::max (grForMeter, -smoothedGrDb);

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float dry = buffer.getReadPointer (ch)[i];
                if (scListen)
                {
                    buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, filtered);
                    continue;
                }
                const float wet  = dry * compGain * makeupGain;
                const float outv = dry * (1.0f - mix) + wet * mix;   // parallel blend
                buffer.getWritePointer (ch)[i] = juce::jlimit (-4.0f, 4.0f, outv);
            }
        }

        gainReductionDb.store (grForMeter, std::memory_order_relaxed);
    }

    // --- UI readouts (the standalone's meters read these off the module) -------
    float  currentGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }
    double currentEffectiveReleaseMs() const noexcept { return effectiveReleaseMs.load (std::memory_order_relaxed); }
    bool   isTempoReleaseActive() const noexcept { return tempoReleaseActive.load (std::memory_order_relaxed); }

private:
    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    static float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db, -120.0f); }

    static double releaseBeatsFor (int div) noexcept
    {
        switch (div)
        {
            case 0: return 0.25;        // 1/16
            case 1: return 1.0 / 3.0;   // 1/8 triplet
            case 2: return 0.5;         // 1/8
            case 3: return 0.75;        // 1/8 dotted
            case 4: return 1.0;         // 1/4
            case 5: return 1.5;         // 1/4 dotted
            case 6: return 2.0;         // 1/2
            default: return 0.5;
        }
    }

    // Host tempo first (pushed in via setTransport), then the cross-plugin TEMPO bus,
    // then 120. The module cannot call getPlayHead() — it has no AudioProcessor.
    double resolveBpm() noexcept
    {
        if (hostHasBpm && hostBpm > 1.0) return hostBpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;
        return 120.0;
    }

    double resolveReleaseMs (double userReleaseMs) noexcept
    {
        if (p ("tempoRelease") <= 0.5f)
        {
            tempoReleaseActive.store (false);
            return userReleaseMs;
        }
        tempoReleaseActive.store (true);
        const double beats = releaseBeatsFor ((int) p ("releaseDiv"));
        return juce::jlimit (20.0, 3000.0, beats * (60000.0 / resolveBpm()));
    }

    float detectorHighPass (float input, float cutoffHz) noexcept
    {
        // Below ~20 Hz the HP is effectively bypassed (nothing musical to remove).
        if (cutoffHz <= 20.5f)
        {
            hpX1 = input;
            hpY1 = input;
            return input;
        }
        cutoffHz = juce::jlimit (20.0f, 400.0f, cutoffHz);
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoffHz);
        const float dt = 1.0f / (float) sr;
        const float alpha = rc / (rc + dt);
        const float y = alpha * (hpY1 + input - hpX1);
        hpX1 = input;
        hpY1 = y;
        return y;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    EnvelopeFollower grSmoother;
    float smoothedGrDb = 0.0f;
    float hpX1 = 0.0f, hpY1 = 0.0f;

    const juce::AudioBuffer<float>* sidechain = nullptr;   // valid only during process()
    double hostBpm = 120.0;
    bool   hostHasBpm = false;
    sattari::xplugin::TempoReader tempoReader;

    std::atomic<float>  gainReductionDb { 0.0f };
    std::atomic<double> effectiveReleaseMs { 140.0 };
    std::atomic<bool>   tempoReleaseActive { false };
};

} // namespace sattari::dsp
