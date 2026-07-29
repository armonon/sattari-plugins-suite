#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <cmath>

// =============================================================================
//  GateModule — Sattari Gate's DSP, extracted so the standalone plugin and a Mix
//  rack slot run the SAME gate.
//
//  Stereo-linked detector through a sidechain HP+LP, peak or RMS level estimate,
//  a hysteresis state machine with a hold counter (open above threshold, close
//  only below threshold-hysteresis AND after hold expires — that pair is what
//  stops chatter), then an attack/release ramp toward the target gain. Optional
//  downward expander instead of a hard gate, and a tempo-locked trance gate.
//
//  Two things a module has to do differently from a plugin:
//    * LATENCY. Lookahead delays the main path so the detector can pre-open. The
//      plugin called setLatencySamples(); a module cannot, so it reports through
//      latencySamples() and the rack sums its active slots and republishes.
//    * TRANSPORT. The trance gate locks its phase to host PPQ. A module has no
//      playhead, so the shell pushes the snapshot in via setTransport().
// =============================================================================

namespace sattari::dsp
{

class GateModule final : public SattariModule
{
public:
    ~GateModule() override { tempoReader.stop(); }

    const char* moduleId()    const noexcept override { return "gate"; }
    const char* displayName() const noexcept override { return "Gate"; }

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

        f ("threshold",  "Threshold",  -80.0f,   0.0f, -40.0f, 0.1f,  "dB");
        f ("range",      "Range",        0.0f,  90.0f,  60.0f, 0.1f,  "dB");
        f ("ratio",      "Ratio",        1.0f,  20.0f,   2.0f, 0.01f, ":1");
        f ("attack",     "Attack",      0.05f, 100.0f,   1.0f, 0.05f, "ms");
        f ("hold",       "Hold",         0.0f, 500.0f,  40.0f, 1.0f,  "ms");
        f ("release",    "Release",      5.0f, 2000.0f, 120.0f, 1.0f, "ms");
        f ("lookahead",  "Lookahead",    0.0f,  10.0f,   2.0f, 0.1f,  "ms");
        f ("hysteresis", "Hysteresis",   0.0f,  24.0f,   4.0f, 0.1f,  "dB");
        f ("scHpf",      "SC HPF",      20.0f, 2000.0f, 20.0f, 1.0f,  "Hz");
        f ("scLpf",      "SC LPF",     500.0f, 20000.0f, 20000.0f, 1.0f, "Hz");
        b ("scListen",   "SC Listen",  false);
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "detMode", 1), "Detector",
            juce::StringArray { "Peak", "RMS" }, 0));
        b ("expander",   "Expander",   false);
        b ("tempoGate",  "Tempo Gate", false);
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "tempoDiv", 1), "Rate",
            juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }, 3));
        f ("tempoDuty",  "Duty",         5.0f,  95.0f,  50.0f, 1.0f,  "%");
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Fast rectified peak follower; the gate's own attack/hold/release shapes
        // the audible edge.
        detectorFollower.prepare (sr, 0.5f, 5.0f);

        // Lookahead line sized for the 10 ms maximum, so process() never allocates.
        maxLookaheadSamples = (int) std::ceil (0.010 * sr);
        lookaheadRingLen = maxLookaheadSamples + 1;
        lookaheadRing.setSize (2, lookaheadRingLen);

        reset();
        tempoReader.start();
    }

    void reset() override
    {
        detectorFollower.reset (0.0f);
        rmsSquare = 0.0f;
        currentGateGain = 0.0f;
        holdCounter = 0;
        gateOpen = false;
        hpX1 = hpY1 = 0.0f;
        lpY1 = 0.0f;
        syncPhase = 0.0;
        lookaheadRing.clear();
        lookaheadRingPos = 0;
        gainReductionDb.store (0.0f);
        gateOpenPublished.store (false);
        tempoGateActive.store (false);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override
    {
        transport = t;
    }

    // External key: gate a snare to a trigger, open a pad from a kick. Same rule as
    // CompModule — a routed sidechain IS the intent, so no extra toggle guards it, and
    // nullptr reproduces the previous main-signal behaviour exactly.
    void setSidechain (const juce::AudioBuffer<float>* sc) noexcept override { sidechain = sc; }

    // Lookahead delay, in samples, for the rack to sum into its reported latency.
    int latencySamples() const noexcept override
    {
        if (state == nullptr) return 0;
        const float ms = cachedParameterValue ("lookahead");
        return juce::jlimit (0, maxLookaheadSamples, (int) std::lround (ms * 0.001 * sr));
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh == 0 || n == 0) return;

        const float thresholdDb  = p ("threshold");
        const float rangeDb      = juce::jmax (0.0f, p ("range"));
        const float ratio        = juce::jmax (1.0f, p ("ratio"));
        const float attackMs     = p ("attack");
        const float holdMs       = juce::jmax (0.0f, p ("hold"));
        const float releaseMs    = p ("release");
        const float lookaheadMs  = p ("lookahead");
        const float hysteresisDb = juce::jmax (0.0f, p ("hysteresis"));
        const float scHpfHz      = p ("scHpf");
        const float scLpfHz      = p ("scLpf");
        const bool  scListen     = p ("scListen") > 0.5f;
        const int   detMode      = (int) p ("detMode");     // 0 = Peak, 1 = RMS
        const bool  expander     = p ("expander") > 0.5f;
        const bool  tempoGate    = p ("tempoGate") > 0.5f;
        const int   tempoDiv     = (int) p ("tempoDiv");
        const float tempoDuty    = juce::jlimit (0.05f, 0.95f, p ("tempoDuty") * 0.01f);

        tempoGateActive.store (tempoGate);

        // One-pole ramp coefficients: coeff = exp(-1 / (sr * timeSec)).
        auto coeffFor = [this] (float ms) -> float
        {
            const float t = ms * 0.001f;
            return t > 0.0f ? std::exp (-1.0f / (float) (sr * (double) t)) : 0.0f;
        };
        const float attackCoeff  = coeffFor (attackMs);
        const float releaseCoeff = coeffFor (releaseMs);
        const int   holdSamples  = (int) std::lround (holdMs * 0.001 * sr);

        const float floorGain = dbToGain (-rangeDb);      // range 0 => open (bypass)
        const float openThreshDb  = thresholdDb;
        const float closeThreshDb = thresholdDb - hysteresisDb;
        const float rmsCoeff = coeffFor (5.0f);

        const int lookaheadSamples = juce::jlimit (0, maxLookaheadSamples,
                                                   (int) std::lround (lookaheadMs * 0.001 * sr));

        // --- Tempo-gate phase ---------------------------------------------------
        double cyclesPerSample = 0.0;
        if (tempoGate)
        {
            const double bpm = resolveBpm();
            const double cycleBeats = cycleBeatsFor (tempoDiv);
            cyclesPerSample = (bpm / 60.0) / sr / cycleBeats;

            // Lock phase to host PPQ while playing so the gate lands on the beat.
            if (transport.isPlaying && transport.hasPpq)
                syncPhase = std::fmod ((transport.ppqPosition / cycleBeats), 1.0);
            if (syncPhase < 0.0) syncPhase += 1.0;
        }

        float grForMeter = 0.0f;
        bool  anyOpen = false;

        // Only key off the sidechain when it actually covers this block.
        const bool scActive = sidechain != nullptr
                              && sidechain->getNumChannels() > 0
                              && sidechain->getNumSamples() >= n;
        const int scChans = scActive ? juce::jmin (2, sidechain->getNumChannels()) : 0;

        for (int i = 0; i < n; ++i)
        {
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

            const float filtered = sidechainFilter (det, scHpfHz, scLpfHz);
            const float rectified = std::fabs (filtered);

            float level;
            if (detMode == 1)
            {
                rmsSquare = rectified * rectified + rmsCoeff * (rmsSquare - rectified * rectified);
                level = std::sqrt (juce::jmax (0.0f, rmsSquare));
            }
            else
            {
                level = detectorFollower.processRectified (filtered);
            }

            const float levelDb = juce::Decibels::gainToDecibels (level + 1.0e-9f, -120.0f);

            float targetGain;
            if (tempoGate)
            {
                const bool inOpenWindow = (float) syncPhase < tempoDuty;
                gateOpen = inOpenWindow;
                targetGain = inOpenWindow ? 1.0f : floorGain;

                syncPhase += cyclesPerSample;
                if (syncPhase >= 1.0) syncPhase -= 1.0;
            }
            else
            {
                // Hysteresis + hold: the signal must fall below closeThreshDb AND the
                // hold must expire before the gate closes. That pair is what keeps it
                // from chattering around a single threshold.
                if (! gateOpen)
                {
                    if (levelDb >= openThreshDb)
                    {
                        gateOpen = true;
                        holdCounter = holdSamples;
                    }
                }
                else
                {
                    if (levelDb >= closeThreshDb)
                        holdCounter = holdSamples;    // still above close: refresh hold
                    else if (holdCounter > 0)
                        --holdCounter;
                    else
                        gateOpen = false;
                }

                if (gateOpen)
                {
                    targetGain = 1.0f;
                }
                else if (expander)
                {
                    // Downward expander: attenuation scales with distance below the
                    // threshold, softened by ratio, clamped to the range floor.
                    const float belowDb = juce::jmax (0.0f, openThreshDb - levelDb);
                    const float reductionDb = juce::jmin (rangeDb, belowDb * (ratio - 1.0f));
                    targetGain = dbToGain (-reductionDb);
                }
                else
                {
                    targetGain = floorGain;   // hard gate
                }
            }

            const float c = (targetGain > currentGateGain) ? attackCoeff : releaseCoeff;
            currentGateGain = targetGain + c * (currentGateGain - targetGain);
            currentGateGain = juce::jlimit (0.0f, 1.0f, currentGateGain);

            anyOpen = anyOpen || gateOpen;
            const float grDb = -juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, currentGateGain), -120.0f);
            grForMeter = std::max (grForMeter, grDb);

            // Lookahead: write current, read the delayed one back (0 => unity delay).
            const int readPos = (lookaheadRingPos - lookaheadSamples + lookaheadRingLen) % lookaheadRingLen;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const int ringCh = juce::jmin (ch, lookaheadRing.getNumChannels() - 1);
                const float cur = buffer.getReadPointer (ch)[i];
                lookaheadRing.setSample (ringCh, lookaheadRingPos, cur);
                const float delayed = lookaheadRing.getSample (ringCh, readPos);

                const float outv = scListen ? filtered : delayed * currentGateGain;
                buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, outv);
            }
            if (++lookaheadRingPos >= lookaheadRingLen) lookaheadRingPos = 0;
        }

        gainReductionDb.store (grForMeter, std::memory_order_relaxed);
        gateOpenPublished.store (anyOpen, std::memory_order_relaxed);
    }

    // --- UI readouts ---------------------------------------------------------
    float currentGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }
    bool  isGateOpen()             const noexcept { return gateOpenPublished.load (std::memory_order_relaxed); }
    bool  isTempoGateActive()      const noexcept { return tempoGateActive.load (std::memory_order_relaxed); }

private:
    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    static float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db, -120.0f); }

    static double cycleBeatsFor (int div) noexcept
    {
        switch (div)
        {
            case 0: return 4.0;         // 1/1
            case 1: return 2.0;         // 1/2
            case 2: return 1.0;         // 1/4
            case 3: return 0.5;         // 1/8
            case 4: return 1.0 / 3.0;   // 1/8T
            case 5: return 0.25;        // 1/16
            default: return 0.5;
        }
    }

    double resolveBpm() noexcept
    {
        if (transport.hasHostBpm && transport.bpm > 1.0) return transport.bpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;
        return 120.0;
    }

    float sidechainFilter (float input, float hpHz, float lpHz) noexcept
    {
        float x = input;

        if (hpHz > 20.5f)
        {
            hpHz = juce::jlimit (20.0f, 2000.0f, hpHz);
            const float rc = 1.0f / (juce::MathConstants<float>::twoPi * hpHz);
            const float dt = 1.0f / (float) sr;
            const float alpha = rc / (rc + dt);
            const float y = alpha * (hpY1 + x - hpX1);
            hpX1 = x;
            hpY1 = y;
            x = y;
        }
        else
        {
            hpX1 = x;
            hpY1 = x;
        }

        const float nyquist = 0.5f * (float) sr;
        if (lpHz < nyquist * 0.95f)
        {
            lpHz = juce::jlimit (100.0f, nyquist * 0.98f, lpHz);
            const float dt = 1.0f / (float) sr;
            const float rc = 1.0f / (juce::MathConstants<float>::twoPi * lpHz);
            const float alpha = dt / (rc + dt);
            lpY1 += alpha * (x - lpY1);
            x = lpY1;
        }
        else
        {
            lpY1 = x;
        }

        return x;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    EnvelopeFollower detectorFollower;
    float rmsSquare = 0.0f;

    float currentGateGain = 0.0f;
    int   holdCounter = 0;
    bool  gateOpen = false;

    float hpX1 = 0.0f, hpY1 = 0.0f;
    float lpY1 = 0.0f;

    juce::AudioBuffer<float> lookaheadRing;
    int lookaheadRingPos = 0;
    int lookaheadRingLen = 1;
    int maxLookaheadSamples = 0;

    double syncPhase = 0.0;
    const juce::AudioBuffer<float>* sidechain = nullptr;   // valid only during process()
    sattari::host::TransportSnapshot transport;
    sattari::xplugin::TempoReader tempoReader;

    std::atomic<float> gainReductionDb   { 0.0f };
    std::atomic<bool>  gateOpenPublished { false };
    std::atomic<bool>  tempoGateActive   { false };
};

} // namespace sattari::dsp
