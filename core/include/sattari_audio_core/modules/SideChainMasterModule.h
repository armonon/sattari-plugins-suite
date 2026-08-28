#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <cmath>

// =============================================================================
//  SideChainMasterModule — Sattari Side Chain Master's DSP, extracted so the
//  standalone plugin and a Mix rack slot run the SAME ducker.
//
//  The classic "duck this under that": a detector reads the EXTERNAL key (kick,
//  vocal) through a high-pass, drives a soft ducking gain, and the main signal —
//  delayed by an optional lookahead so the duck can start just *before* the
//  transient lands — is attenuated by it.
//
//  This is the first genuinely two-signal module, and the reason the rack grew a
//  Sidechain bus at all: without setSidechain() the module could only key off
//  itself, which is not what a side-chain plugin is for. The `external` parameter
//  is KEPT (unlike Comp/Gate, where a routed key is unambiguous intent) because
//  here it selects a real second behaviour — internal-key ducking — and it ships
//  in saved sessions.
//
//  Two ducking sources, mutually exclusive:
//    * detector-driven — threshold / ratio / depth over the keyed envelope;
//    * TEMPO-SYNC duck — a host-locked curve that needs no key at all, phase
//      anchored to PPQ while playing so the pump stays on the grid, free-running
//      on the cross-plugin tempo bus (then 120 BPM) otherwise. The EDM pump.
//
//  Five voicings (Clean / Pump / Tight / Smooth / Creative) are parameter
//  offsets applied after the user values are read, not separate code paths.
// =============================================================================

namespace sattari::dsp
{

class SideChainMasterModule final : public SattariModule
{
public:
    ~SideChainMasterModule() override { tempoReader.stop(); }

    static juce::StringArray modeChoices()     { return { "Clean", "Pump", "Tight", "Smooth", "Creative" }; }
    static juce::StringArray syncRateChoices() { return { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }; }

    const char* moduleId()    const noexcept override { return "sidechain"; }
    const char* displayName() const noexcept override { return "Side Chain"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        auto f = [&out, &prefix] (const juce::String& id, const juce::String& name,
                                  float min, float max, float def, const juce::String& suffix = {})
        {
            juce::NormalisableRange<float> range (min, max, 0.001f);
            out.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID (prefix + id, 1), name, range, def,
                juce::AudioParameterFloatAttributes().withLabel (suffix)));
        };

        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "mode", 1), "Mode", modeChoices(), 1));
        f ("threshold",    "Threshold",     -60.0f,  0.0f,   -24.0f, "dB");
        f ("ratio",        "Ratio",           1.0f, 20.0f,     6.0f, ":1");
        f ("attack",       "Attack",          0.1f, 80.0f,     3.0f, "ms");
        f ("release",      "Release",        20.0f, 1000.0f, 180.0f, "ms");
        f ("depth",        "Duck Depth",      0.0f, 36.0f,    12.0f, "dB");
        f ("makeup",       "Makeup",        -12.0f, 12.0f,     0.0f, "dB");
        f ("mix",          "Mix",             0.0f, 100.0f,  100.0f, "%");
        f ("sidechainHpf", "Sidechain HPF",  20.0f, 250.0f,   65.0f, "Hz");
        f ("lookahead",    "Lookahead",       0.0f, 10.0f,     0.0f, "ms");
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "external", 1), "Use External Sidechain", true));
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "listen", 1), "Solo Detector Listen", false));

        // Tempo-synced "volume shaper" ducking: a host-locked curve ducks the signal
        // on the beat with no sidechain needed — the classic EDM pump.
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "syncDuck", 1), "Tempo Sync Duck", false));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "syncRate", 1), "Sync Rate", syncRateChoices(), 2));
        f ("syncCurve", "Sync Curve", 0.2f, 4.0f, 1.0f);
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // The detector's own follower runs at fixed-ish times (~1.5 ms attack, 45 ms
        // release) shared by every mode, so prepare picks the times once. The GR
        // smoother uses the user attack/release, set per-block.
        detectorFollower.prepare (sr, 1.5f, 45.0f);
        grSmoother.prepare (sr);
        tempoReader.start();   // sync-duck tempo fallback when the host reports none

        // Lookahead delay line sized for the 10 ms maximum — allocation-free after this.
        maxLookaheadSamples = (int) std::ceil (0.010 * sr);
        lookaheadRingLen    = maxLookaheadSamples + 1;
        lookaheadRing.setSize (2, lookaheadRingLen);

        reset();
    }

    void reset() override
    {
        smoothedGainReductionDb = 0.0f;
        detectorFollower.reset (0.0f);
        grSmoother.reset (0.0f);
        hpX1 = hpY1 = 0.0f;
        syncPhase = 0.0;
        lookaheadRing.clear();
        lookaheadRingPos = 0;
        lastGainReductionDb.store (0.0f, std::memory_order_relaxed);
        lastExternalAvailable.store (false, std::memory_order_relaxed);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }

    // The external key. Unlike Comp/Gate a parameter still guards its use here,
    // because "no external key" is a real mode for this plugin, not an accident.
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
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const bool useExternal = p ("external") > 0.5f
                                 && sidechain != nullptr
                                 && sidechain->getNumChannels() > 0
                                 && sidechain->getNumSamples() >= n;
        lastExternalAvailable.store (useExternal, std::memory_order_relaxed);

        const int   mode        = (int) std::round (p ("mode"));
        float       thresholdDb = p ("threshold");
        float       ratio       = juce::jmax (1.0f, p ("ratio"));
        float       attackMs    = p ("attack");
        float       releaseMs   = p ("release");
        float       depthDb     = p ("depth");
        const float makeupDb    = p ("makeup");
        float       mix         = p ("mix") * 0.01f;
        const float hpfHz       = p ("sidechainHpf");
        const bool  listen      = p ("listen") > 0.5f;
        const bool  syncDuck    = p ("syncDuck") > 0.5f;
        const int   syncRate    = (int) std::round (p ("syncRate"));
        const float syncCurve   = juce::jmax (0.05f, p ("syncCurve"));

        const int lookaheadSamples = juce::jlimit (0, maxLookaheadSamples,
            (int) std::lround (p ("lookahead") * 0.001 * sr));

        // Tempo-synced duck phase: lock to host PPQ while playing, else free-run on BPM.
        // When the host reports no usable tempo (transport-less or standalone), fall
        // back to the cross-plugin tempo bus so the sync-duck still runs in time.
        double effectiveBpm = transport.hasHostBpm ? transport.bpm : 0.0;
        if (! (effectiveBpm > 1.0))
            if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)
                effectiveBpm = s.bpm;
        if (! (effectiveBpm > 1.0))
            effectiveBpm = 120.0;

        static constexpr double rateQuarters[] = { 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25 };
        const double cycleQuarters   = rateQuarters[juce::jlimit (0, 5, syncRate)];
        const double cyclesPerSample = (effectiveBpm / 60.0) / sr / cycleQuarters;
        if (syncDuck && transport.isPlaying && transport.hasPpq)
            syncPhase = std::fmod (transport.ppqPosition / cycleQuarters, 1.0);
        if (syncPhase < 0.0) syncPhase += 1.0;

        // Voicings are offsets on the user values, applied after depth is read so the
        // sync path sees the same voiced depth the detector path does.
        if (mode == 1) { thresholdDb -= 3.0f; depthDb += 6.0f; releaseMs *= 0.75f; }        // Pump
        if (mode == 2) { attackMs *= 0.45f; releaseMs *= 0.55f; depthDb += 2.0f; }          // Tight
        if (mode == 3) { attackMs *= 1.8f; releaseMs *= 1.7f; depthDb *= 0.75f; }           // Smooth
        if (mode == 4) { ratio = juce::jmax (ratio, 10.0f); depthDb += 10.0f; mix *= 0.9f; } // Creative

        const float syncDepthGain = dbToGain (-depthDb);
        grSmoother.setTimes (attackMs, releaseMs);
        const float makeupGain = dbToGain (makeupDb);

        float grForMeter = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // Detector source: the external key if routed and enabled, else the signal
            // itself (mono sum of whichever buffer we are keying off).
            const float rawDetector = useExternal ? monoSample (*sidechain, i)
                                                  : monoSample (buffer, i);
            const float filteredDetector = applyDetectorHighPass (rawDetector, hpfHz);
            const float detectorEnvelope = detectorFollower.processRectified (filteredDetector);

            float wetGain;
            if (syncDuck)
            {
                // Tempo-locked curve: full duck on the beat (phase 0), recovering to
                // unity by the next cycle; syncCurve shapes the recovery slope.
                const float shaped = std::pow ((float) syncPhase, syncCurve);
                const float reductionGain = syncDepthGain + (1.0f - syncDepthGain) * shaped;
                wetGain = juce::jlimit (0.0f, 4.0f, reductionGain * makeupGain);
                grForMeter = -juce::Decibels::gainToDecibels (reductionGain + 1.0e-9f, -120.0f);
                syncPhase += cyclesPerSample;
                if (syncPhase >= 1.0) syncPhase -= 1.0;
            }
            else
            {
                const float detectorDb = juce::Decibels::gainToDecibels (detectorEnvelope + 1.0e-9f, -120.0f);
                const float overDb = juce::jmax (0.0f, detectorDb - thresholdDb);
                const float compressedDb = overDb * (1.0f - (1.0f / ratio));
                const float targetReductionDb = -juce::jmin (depthDb, compressedDb);
                smoothedGainReductionDb = grSmoother.processFalling (targetReductionDb);
                wetGain = juce::jlimit (0.0f, 4.0f, dbToGain (smoothedGainReductionDb) * makeupGain);
                grForMeter = -smoothedGainReductionDb;
            }

            // Lookahead: write the current sample, read the delayed one (delay 0 reads
            // back what we just wrote, i.e. unity); both dry and wet use the delayed
            // signal so the mix stays phase-aligned with itself.
            const int readPos = (lookaheadRingPos - lookaheadSamples + lookaheadRingLen) % lookaheadRingLen;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const int ringCh = juce::jmin (ch, lookaheadRing.getNumChannels() - 1);
                lookaheadRing.setSample (ringCh, lookaheadRingPos, buffer.getSample (ch, i));
                const float delayed = lookaheadRing.getSample (ringCh, readPos);
                const float wet = listen ? filteredDetector : delayed * wetGain;
                buffer.setSample (ch, i, juce::jlimit (-0.995f, 0.995f, delayed * (1.0f - mix) + wet * mix));
            }
            if (++lookaheadRingPos >= lookaheadRingLen) lookaheadRingPos = 0;
        }

        lastGainReductionDb.store (grForMeter, std::memory_order_relaxed);
    }

    // Lock-free readouts for an editor.
    float currentGainReductionDb() const noexcept { return lastGainReductionDb.load (std::memory_order_relaxed); }
    bool  externalKeyInUse()       const noexcept { return lastExternalAvailable.load (std::memory_order_relaxed); }

private:
    static float monoSample (const juce::AudioBuffer<float>& b, int i) noexcept
    {
        const int ch = b.getNumChannels();
        if (ch <= 0 || i >= b.getNumSamples()) return 0.0f;
        float mono = 0.0f;
        for (int c = 0; c < ch; ++c) mono += b.getSample (c, i);
        return mono / (float) ch;
    }

    float applyDetectorHighPass (float input, float cutoffHz) noexcept
    {
        cutoffHz = juce::jlimit (20.0f, 250.0f, cutoffHz);
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoffHz);
        const float dt = 1.0f / (float) sr;
        const float alpha = rc / (rc + dt);
        const float y = alpha * (hpY1 + input - hpX1);
        hpX1 = input;
        hpY1 = y;
        return y;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    static float dbToGain (float db) noexcept { return juce::Decibels::decibelsToGain (db, -120.0f); }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    float smoothedGainReductionDb = 0.0f;
    EnvelopeFollower detectorFollower;   // rising peak detector
    EnvelopeFollower grSmoother;         // falling GR smoother
    float hpX1 = 0.0f, hpY1 = 0.0f;

    juce::AudioBuffer<float> lookaheadRing;
    int lookaheadRingPos = 0;
    int lookaheadRingLen = 1;
    int maxLookaheadSamples = 0;

    double syncPhase = 0.0;

    sattari::host::TransportSnapshot transport;
    sattari::xplugin::TempoReader tempoReader;
    const juce::AudioBuffer<float>* sidechain = nullptr;

    std::atomic<float> lastGainReductionDb { 0.0f };
    std::atomic<bool>  lastExternalAvailable { false };
};

} // namespace sattari::dsp
