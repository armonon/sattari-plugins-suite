#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariKeyAnalysis.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPsolaShifter.h>
#include <sattari_audio_core/SattariLivePitchEngine.h>
#include <sattari_audio_core/SattariSongKeyEstimator.h>
#include <sattari_audio_core/SattariTempoEstimator.h>
#include <sattari_audio_core/SattariSongModel.h>
#include <sattari_audio_core/SattariChordAnalysis.h>
#include <sattari_audio_core/SattariHarmonyVoices.h>
#include <sattari_audio_core/SattariHarmonyRenderer.h>
#include <sattari_audio_core/SattariCrepePitch.h>
#include <sattari_audio_core/SattariPitchFusion.h>
#include <sattari_audio_core/SattariKeyNet.h>
#include <sattari_audio_core/SattariScaleInterval.h>
#include <sattari_audio_core/SattariKeyNetSpec.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariHarmonicMask.h>
#include <sattari_audio_core/SattariRealtime.h>
#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ContinuousMotionEstimator.h"

#ifndef SATTARI_AUTOPITCH_PRINT_SIGNALSMITH
 #define SATTARI_AUTOPITCH_PRINT_SIGNALSMITH 0
#endif

#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && ! SATTARI_AUTOPITCH_HAS_SIGNALSMITH
 #error "SIGNALSMITH print engine selected without signalsmith-stretch support"
#endif

// ---------------------------------------------------------------------------
// Intent Engine v2 — experimental targeting layer (shadow/active modes)
// ---------------------------------------------------------------------------
enum class IntentMode : int
{
    PRODUCTION = 0,   // default — production targeting only
    V2_SHADOW  = 1,   // run v2 in parallel, do NOT drive audio
    V2_ACTIVE  = 2,   // v2 drives targetMidi when confident; Strategy G unchanged
};

enum class IntentReasonCode : int
{
    NEAREST_STABLE           = 0,
    DIRECTIONAL_DESTINATION  = 1,
    PASSING_TONE             = 2,
    SLIDE_DESTINATION        = 3,
    STABILITY_ANCHOR         = 4,
    VIBRATO_HOLD             = 5,
    LOW_CONFIDENCE_FALLBACK  = 6,
    SLIDE_DESTINATION_V2     = 7,   // Phase 4 — continuous-motion estimator path
};

struct IntentV2State
{
    static constexpr int kHistLen       = 20;   // ~116ms @ 256/44100 — must exceed vibrato half-period (100ms @ 5 Hz)
    static constexpr int kMaxDwell      = 32;
    static constexpr double kSlideVel   = 0.6;  // MIDI/sec — sliding threshold
    static constexpr double kPassDwell  = 0.18; // max seconds near a note = passing tone
    static constexpr double kStabSec    = 0.10; // min seconds to anchor
    static constexpr double kPersistSec = 0.12; // min stable seconds before switching

    double pitchHist[kHistLen] {};
    double velHist  [kHistLen] {};  // signed velocity (MIDI/sec) per pitch hop
    int    histFill  = 0;
    int    histPos   = 0;

    int    stableTarget    = -1000;
    double stableSec       = 0.0;
    double lastVoicedPitch = -1.0;

    struct DwellEntry { int note = -1; int frames = 0; };
    DwellEntry dwellTable[kMaxDwell] {};
    int dwellCount = 0;

    // Per-block output — written audio thread, read by trace/log (bench only)
    int    prodTarget     = -1;
    int    v2Target       = -1;
    float  intentConf     = 0.0f;
    IntentReasonCode reason = IntentReasonCode::LOW_CONFIDENCE_FALLBACK;
    bool   fallback       = false;
    int    targetChanges  = 0;

    // Continuous motion estimator — drives SLIDE_DESTINATION_V2 tier.
    ContinuousMotionEstimator motion;
    ContinuousMotionEstimator::MotionFrame lastMotion;

    // Intent v3 — MERGED 2026-08-25. Full RC regression passed:
    // MotionBench 1.788 MIDI/sec, active audio 81.4% acc @ conf≥0.9, 0 octave errors,
    // 0 crashes, 0 NaN/Inf, CoreContract PASS, AU/VST3 smoke PASS.
    // Rollback: flip this flag to false and rebuild.
    static constexpr bool kSlideDestinationV2Enabled = true;

    // Live Pitch mode — when true: block=128, OptionWindowShort RubberBand,
    // SLIDE_DESTINATION_V2 suppressed (look-ahead extrapolation incompatible with ≤30ms budget).
    // Default false: preserves frozen engine behavior identically.
    static constexpr bool kLivePitchEnabled = false;

    void reset()
    {
        histFill = histPos = 0;
        stableTarget = -1000; stableSec = 0.0; lastVoicedPitch = -1.0;
        dwellCount = 0;
        for (auto& e : dwellTable) e = {};
        for (auto& v : pitchHist) v = 0.0;
        for (auto& v : velHist)   v = 0.0;
        prodTarget = v2Target = -1;
        intentConf = 0.0f;
        reason = IntentReasonCode::LOW_CONFIDENCE_FALLBACK;
        fallback = false; targetChanges = 0;
        motion.reset();
        lastMotion = {};
    }

    void pushPitch (double midi, double blockSec)
    {
        // Store signed velocity (MIDI/sec) before updating pitch history
        const double vel = (lastVoicedPitch >= 0.0 && blockSec > 0.0)
                           ? (midi - lastVoicedPitch) / blockSec
                           : 0.0;
        pitchHist[histPos] = midi;
        velHist  [histPos] = vel;
        histPos = (histPos + 1) % kHistLen;
        if (histFill < kHistLen) ++histFill;
        lastVoicedPitch = midi;
    }

    double smoothedVelocity() const
    {
        if (histFill < 2) return 0.0;
        // Average of stored per-hop velocity values
        double sum = 0.0;
        const int count = std::min (histFill, kHistLen);
        for (int i = 0; i < count; ++i)
            sum += velHist[(histPos - 1 - i + kHistLen * 2) % kHistLen];
        return sum / count;
    }

    // Check if recent velocity signs are consistent (matches Python semantics)
    // Threshold: 0.3 MIDI/sec — matches the Python is_directionally_consistent
    bool directionConsistent (int frames = 6) const
    {
        const int n = std::min (frames, histFill);
        if (n < 3) return false;
        int pos = 0, neg = 0;
        for (int i = 0; i < n; ++i)
        {
            const double v = velHist[(histPos - 1 - i + kHistLen * 2) % kHistLen];
            if (v >  0.3) ++pos;
            if (v < -0.3) ++neg;
        }
        return (pos >= (int)(n * 0.6)) || (neg >= (int)(n * 0.6));
    }

    // True if the history window contains BOTH strongly-positive AND strongly-
    // negative velocities anywhere within it.  Vibrato always has both once
    // kHistLen > half_period_frames.  A true slide has only one sign.
    bool hasSignReversal (int frames = kHistLen) const
    {
        const int n = std::min (frames, histFill);
        if (n < 4) return false;
        bool hasPos = false, hasNeg = false;
        for (int i = 0; i < n; ++i)
        {
            const double v = velHist[(histPos - 1 - i + kHistLen * 2) % kHistLen];
            if (v >  0.3) hasPos = true;
            if (v < -0.3) hasNeg = true;
        }
        return hasPos && hasNeg;
    }

    double directionSign() const
    {
        const double sv = smoothedVelocity();
        return sv > 0.0 ? 1.0 : (sv < 0.0 ? -1.0 : 0.0);
    }

    int getDwell (int note) const
    {
        for (int i = 0; i < dwellCount; ++i)
            if (dwellTable[i].note == note) return dwellTable[i].frames;
        return 0;
    }

    void incrementDwell (int note)
    {
        for (int i = 0; i < dwellCount; ++i)
            if (dwellTable[i].note == note) { ++dwellTable[i].frames; return; }
        if (dwellCount < kMaxDwell)
            dwellTable[dwellCount++] = { note, 1 };
    }

    void resetDwell() { dwellCount = 0; for (auto& e : dwellTable) e = {}; }
};

#if SATTARI_AUTOPITCH_HAS_ELASTIQUE
 #include <elastique3/IElastiqueV3.h>
#endif

#if SATTARI_AUTOPITCH_HAS_SIGNALSMITH
 #include "signalsmith-stretch/signalsmith-stretch.h"
#endif

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

class SattariAutoPitchAudioProcessor final : public juce::AudioProcessor,
#if JucePlugin_Enable_ARA
                                             public juce::AudioProcessorARAExtension,
#endif
                                             private juce::AudioProcessorValueTreeState::Listener,
                                             private juce::AsyncUpdater,
                                             private juce::Timer
{
    struct UiFrame
    {
        double midi = 0.0;
        double targetMidi = 0.0;
        double correctionCents = 0.0;
        double confidence = 0.0;
        double fedPitchScale = 1.0;
        int detectedRoot = 9;
        int detectedScale = 1;
        bool voiced = false;
        int   intentMidi = -1;         // note believed intended
        float intentConfidence = 1.0f; // scales the correction actually applied
    };

public:
    SattariAutoPitchAudioProcessor();
    ~SattariAutoPitchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    // Transport relocate / loop: flush all streaming state (RB FIFO, PSOLA rings,
    // smoothers, Rubber Band stretchers) so the engine never replays stale audio
    // or splices across a discontinuity. Allocation-free (RT-safe).
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Auto Pitch"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    bool applyTraceContractJson (const juce::String& traceJson);
    juce::String getLastTraceSummary() const { return lastTraceSummary; }
    juce::String getLiveStatusSummary() const;
    juce::String getKeyDetectionTabSummary() const;
    // Whether the embedded CREPE model loaded at prepare time — lets the UI label
    // the Neural Pitch toggle honestly in ONNX and fallback builds.
    bool neuralModelPresent() const noexcept { return crepeModelPresent; }

    // Effective tuning reference. Manual Detune is used normally; when Follow
    // Song Reference is armed and the suite bus publishes a reference, that
    // value becomes authoritative alongside the followed song key.
    double currentReferenceHz() const noexcept;

    // Intent Engine v2 mode control (bench / DAW experimentation only)
    void setIntentMode (IntentMode m) noexcept { intentMode.store ((int) m, std::memory_order_relaxed); }
    IntentMode getIntentMode() const noexcept  { return (IntentMode) intentMode.load (std::memory_order_relaxed); }
    // Target-note hysteresis on/off (bench A/B only; always on in the plugin).
    void setTargetHysteresisEnabled (bool on) noexcept { targetHysteresisEnabled = on; }
    // Key profile for the sung-note AutoKey histogram (bench A/B).
    void setAutoKeyHistogramProfile (int profile) noexcept { autoKeyHistogramProfile = (sattari::dsp::KeyProfile) profile; }
    // Sung-note AutoKey histogram memory in seconds (bench A/B only).
    void setAutoKeyMemorySeconds (double seconds) noexcept { autoKeyMemorySec = juce::jmax (0.5, seconds); }
    const IntentV2State& getIntentV2State() const noexcept { return intentState; }

    // --- Track Key Map -------------------------------------------------------
    //  Offline scan of an audio file -> timeline of (timeSec, rootIdx, scaleIdx)
    //  segments. When `useKeyMap` is enabled and a map is loaded, the live
    //  correction key follows the map based on the host playhead position, so
    //  the vocal stays in the right key as the song modulates.
    struct KeyMapEntry
    {
        double timeSec = 0.0;
        int rootIdx = 0;        // 0..11
        int scaleIdx = 0;       // 0=major, 1=minor, 2=chromatic
        float confidence = 0.0f;
    };
    struct KeyMapStatus
    {
        bool hasMap = false;
        int  numSegments = 0;
        int  currentSegment = -1;
        int  scanProgress = -1;            // -1 = idle, 0..100 = scanning
        juce::String loadedFileName;
        juce::String lastError;
    };

    bool startKeyMapScan (const juce::File& audioFile);
    void clearKeyMap();
    KeyMapStatus getKeyMapStatus() const;
    // Key Map's (root, scale) at a playhead time — const lookup, for offline verification.
    // Returns {root, scale} of the latest segment at or before `seconds`; {-1,-1} if empty.
    struct KeyMapKey { int root = -1; int scale = -1; };
    KeyMapKey keyMapKeyAtSeconds (double seconds) const;

    // --- Key Map visualisation (editor timeline) -----------------------------
    // The scan also captures a peak envelope of the file so the editor can draw the
    // song's waveform with its key SECTIONS overlaid, instead of a text summary.
    // All three are published together with the map under keyMapLock.
    std::vector<KeyMapEntry> getKeyMapSegments() const;
    std::vector<float>       getKeyMapWaveform() const;   // 0..1 peak envelope
    double                   getKeyMapDurationSec() const;
    // Host playhead (seconds), updated per block so the timeline can draw a position
    // marker even when Use Key Map is off.
    double getKeyMapPlayheadSec() const { return keyMapPlayheadSec.load (std::memory_order_relaxed); }

    // --- Pitch Edit timeline -------------------------------------------------
    // Capture detected note regions against the host timeline, then let the
    // editor retarget or bypass individual regions. This is the realtime-safe
    // foundation for Graph-style surgical correction without requiring ARA.
    struct PitchEditEntry
    {
        juce::int64 id = 0;
        double startSec = 0.0, endSec = 0.0;
        int sourceMidi = 60, targetMidi = 60;
        float confidence = 0.0f;
        bool enabled = true;
        // Per-object overrides. A negative value means "follow the global control"
        // so old sessions migrate without changing their sound.
        float amount = -1.0f, retune = -1.0f, transition = -1.0f;
        float humanize = -1.0f, vibrato = -1.0f;
        float formantSemitones = -100.0f;
        float gainDb = 0.0f;
        double timeOffsetSec = 0.0;
    };
    std::vector<PitchEditEntry> getPitchEdits() const;
    void clearPitchEdits();
    bool setPitchEditEntry (juce::int64 id, const PitchEditEntry& entry);
    bool splitPitchEdit (juce::int64 id, double splitSec);
    bool joinPitchEdit (juce::int64 id);
    bool deletePitchEdit (juce::int64 id);
    void beginPitchEditGesture();
    void endPitchEditGesture();
    bool undoPitchEdit();
    bool redoPitchEdit();
    bool canUndoPitchEdit() const;
    bool canRedoPitchEdit() const;
    int getPitchEditRevision() const noexcept { return pitchEditRevision.load (std::memory_order_relaxed); }

    // Live snapshot for the editor (timer-polled). Trivially-copyable POD with
    // sensible defaults when nothing has been processed yet.
    struct LiveSnapshot
    {
        bool  voiced = false;
        int   detectedMidi = -1;     // nearest MIDI note of detected pitch
        float correctionCents = 0.0f;
        float confidence = 0.0f;
        int   detectedRoot = 0;      // 0..11, AutoKey output (or selected key)
        int   detectedScale = 0;     // scale index of the AutoKey output (0 major, 1 minor)
        //  INPUT / INTENT / OUTPUT transparency: what was sung, what Auto Pitch
        //  believes was meant, and how sure it is. intentConfidence scales the
        //  correction, so this readout explains the amount actually applied.
        int   intentMidi = -1;       // the note Auto Pitch believes was intended
        float intentConfidence = 0.0f;
        float appliedScale = 1.0f;   // 0..1 of the requested correction actually used
    };
    // Push the current correction key to the cross-plugin bus so other Sattari
    // plugins (e.g. Royal Chain's Tune) can follow. Called from the editor
    // timer with internal throttling so we never write per audio block.
    void publishCurrentKeyToBus();

    // Where the correction key is *actually* coming from right now, plus the
    // effective root/scale. The editor uses this to reflect the real key in the
    // header (and disable the manual dropdowns) so a bus/AutoKey override is
    // never silent. FollowBus only reports when Follow is on AND a fresh session
    // is on the bus; otherwise it falls through to AutoKey or Manual.
    struct KeyOrigin
    {
        enum class Source { Manual, AutoKey, FollowBus, KeyMap };
        Source source = Source::Manual;
        int    root  = 9;   // 0..11, effective correction root
        int    scale = 1;   // index into the extended local scale/mode palette
        //  HOW MUCH TO TRUST root/scale. Krumhansl margin (winner minus
        //  runner-up) for detector sources; 1.0 for Manual, because a key the
        //  user typed is a decision rather than an estimate.
        //
        //  This field exists because it was missing. AutoKey computed the margin
        //  and dropped it here, so a 0-margin fallback reached the session bus
        //  indistinguishable from a confident detection, and every key-following
        //  plugin in the suite accepted it as fact.
        float  confidence = 1.0f;
        bool   dynamic = false;
        int    revision = 0;
        float  windowSeconds = 0.0f;
    };
    //  MINIMUM MARGIN AN AUTOKEY VERDICT NEEDS BEFORE IT MAY REACH THE SESSION
    //  BUS. Derived from measurement, not chosen: the Krumhansl margin on 48
    //  known-correct synthetic melodies (12 roots x major/minor x sine/harmonic,
    //  all verified correct) ranged 0.157 to 0.327, while three real solo vocals
    //  whose key AutoKey could not determine measured 0.067, 0.053 and -0.031.
    //  Nothing falls between 0.067 and 0.157. 0.10 is the geometric midpoint of
    //  that gap, keeping ~1.5x headroom on both sides.
    //
    //  A negative margin is possible and meaningful: the detector's relative-key
    //  swap can promote a runner-up, so below zero it has overridden its own
    //  ranking and is guessing outright.
    static constexpr float kMinBusKeyConfidence = (float) sattari::dsp::kReleaseKeyConfidence;

    KeyOrigin currentKeyOrigin() const;

    //  Full-song drop analysis (ported from Sattari Key): decode + finalised
    //  SongKeyEstimator + TempoEstimator on a background thread. The finalised
    //  whole-file verdict outranks the mid-stream one in AutoKey.
    void analyseSongFileAsync (const juce::File& file);
    bool isSupportedAudioFile (const juce::File& file) const;
    void clearSongFileResult();
    std::thread fileWorker;
    std::atomic<bool>  fileWorkerCancel { false };
    std::atomic<float> fileProgress { 0.0f };      // 0..1; <0 = error
    std::atomic<bool>  fileKeyValid { false };
    std::atomic<int>   fileKeyRoot { 0 };
    std::atomic<int>   fileKeyMinor { 0 };
    std::atomic<float> fileKeyConf { 0.0f };
    std::atomic<float> fileBpm { 0.0f };
    std::atomic<float> fileDetuneCents { 0.0f };
    //  Latest intent confidence, for the UI's INPUT/INTENT/OUTPUT readout.
    //  mutable: pitchScaleForFrame is const (it computes, it does not mutate
    //  state) but it is the one place that knows the applied intent scale.
    mutable std::atomic<float> intentConfidenceUi { 1.0f };

    //  Harmony planning. The planner is contract-tested and shared by the
    //  renderer/readouts, so the workspace reports the notes it really sings.
    //  Planned on the message thread from the UI snapshot, so no audio-thread
    //  cost at all.
    struct HarmonyReadout
    {
        bool valid = false;
        int  leadMidi = -1;
        std::array<int, 4> midi { -1, -1, -1, -1 };
        std::array<bool, 4> active { false, false, false, false };
        double confidence = 0.0;
        bool   fromChord = false;
        int    heldNotes = 0;      // MIDI mode: how many notes were actually held
    };
    HarmonyReadout planHarmonyNow() const;
    struct HarmonyEditOverlay
    {
        juce::int64 sourceId = 0;
        int voice = 0;
        double startSec = 0.0, endSec = 0.0;
        int midi = -1;
    };
    // Read-only generated harmony objects for the Edit timeline. They follow the
    // same planner/settings as the audible renderer but never mutate source notes.
    std::vector<HarmonyEditOverlay> getHarmonyEditOverlays() const;
    mutable sattari::dsp::HarmonyVoicePlanner harmonyPlanner;

    //  The shared song model (Phase 1): everything the drop analysis learned,
    //  time-aware. Written by the file worker under the mutex, copied out by the
    //  UI at poll rate. A copy at 12 Hz of a few-KB struct is cheaper than any
    //  clever lock-free scheme and impossible to tear.
    //  Manual region override. The user's key for a region outranks detection
    //  permanently, reports full confidence (their assertion IS the evidence),
    //  and is pushed straight into the Follow Song Map table.
    void setRegionKey (int regionIndex, int root, bool minor);

    //  Apply the time-aware key source at a transport position, and read the
    //  resulting correction key directly. Public so the offline harness can prove
    //  Follow Song Map actually drives correction — the defect that made it a
    //  dead control was invisible to every other test. Reading the EFFECTIVE
    //  root here rather than through currentKeyOrigin() matters: that one reports
    //  the UI frame, which only refreshes from processBlock.
    void applyKeyMapAtSeconds (double seconds);
    int  effectiveCorrectionRoot() const { return currentCorrectionRootIndex(); }
    int  effectiveCorrectionScale() const { return effectiveScaleIndex(); }
    bool effectiveCorrectionRootIsMapDriven() const { return keyMapDriving(); }

    sattari::dsp::SongAnalysis songAnalysisCopy() const
    {
        const std::lock_guard<std::mutex> lock (songAnalysisMutex);
        return songAnalysis;
    }
    mutable std::mutex songAnalysisMutex;
    sattari::dsp::SongAnalysis songAnalysis;

    LiveSnapshot getLiveSnapshot() const noexcept
    {
        const auto frame = uiFrame.load();
        LiveSnapshot s;
        s.voiced = frame.voiced;
        s.detectedMidi = frame.voiced
            ? juce::jlimit (0, 127, (int) std::round (frame.midi)) : -1;
        s.correctionCents = (float) juce::jlimit (-200.0, 200.0, frame.correctionCents);
        s.confidence = (float) juce::jlimit (0.0, 1.0, frame.confidence);
        s.detectedRoot = frame.detectedRoot;
        s.detectedScale = frame.detectedScale;
        s.intentMidi = frame.intentMidi;
        s.intentConfidence = frame.intentConfidence;
        //  How much of the requested correction actually survived the intent
        //  gate — the number the UI needs to explain what it did.
        s.appliedScale = juce::jlimit (0.2f, 1.0f, frame.intentConfidence);
        return s;
    }

    // Diagnostic snapshot of the correction internals — the raw detected pitch, the
    // target scale tone, the correction the plugin WANTS, and the shift it actually fed
    // the engine this block. Used offline to separate detection error (detected != true
    // pitch) from retune undershoot (applied << wanted). Not for the audio thread.
    struct DebugFrame { double detectedMidi = 0, targetMidi = 0, wantedCents = 0, appliedCents = 0, confidence = 0; bool voiced = false; };
    DebugFrame getDebugFrame() const noexcept
    {
        const auto frame = uiFrame.load();
        DebugFrame d;
        d.voiced       = frame.voiced;
        d.detectedMidi = frame.midi;
        d.targetMidi   = frame.targetMidi;
        d.wantedCents  = frame.correctionCents;
        d.appliedCents = 1200.0 * std::log2 (juce::jmax (1.0e-6, frame.fedPitchScale));
        d.confidence   = frame.confidence;
        return d;
    }

private:
    //  Intent estimation. Returns 0..1: how strongly Auto Pitch believes the
    //  detected pitch is heading for `targetMidi` rather than being an
    //  in-between, a slide, or a detector wobble. Correction is SCALED by this,
    //  because a false correction is more damaging than a missed one.
    struct PitchFrame;
    double estimateIntentConfidence (const PitchFrame& frame) const noexcept;

    struct PitchFrame
    {
        double frequencyHz = 0.0;
        double midi = 0.0;
        double targetMidi = 0.0;
        double correctionCents = 0.0;
        double confidence = 0.0;
        double rms = 0.0;
        float objectAmount = -1.0f, objectRetune = -1.0f, objectTransition = -1.0f;
        float objectHumanize = -1.0f, objectVibrato = -1.0f;
        float objectFormantSemitones = -100.0f, objectGainDb = 0.0f;
        bool objectActive = false;
        bool objectEnabled = true;
        bool voiced = false;
        // Packed key root / scale / Edit-Notes mask the target was chosen under
        // (set by buildCorrectionFrame; -1 when no target was chosen).
        int  targetContext = -1;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void setParameterValue (const juce::String& parameterID, float value);
    static int rootToChoiceIndex (const juce::String& root);
    static int scaleToChoiceIndex (const juce::String& scale);
    static int styleToChoiceIndex (const juce::String& style);
    static juce::String midiNoteName (double midi);
    sattari::audio_core::KeySignature currentKeySignature() const;
    juce::String currentSattariWheelCode() const;

    void resetPitchState (int maxBlockSize);
    void pushPitchSample (float sample);
    // Voiced pitch range (Hz) for the selected Sound Source. Shared by the YIN
    // gate (analyseCurrentPitchFrame) and the CREPE override so both reject the
    // same out-of-range estimates.
    void voicedRangeHzForSource (double& minHz, double& maxHz) const;
    PitchFrame analyseCurrentPitchFrame() const;
    PitchFrame buildCorrectionFrame (PitchFrame detected) const;
    void publishUiFrame() noexcept;
    int currentCorrectionRootIndex() const;
    int effectiveScaleIndex() const;     // bus scale when Follow Song Key is on, else the param
    bool keyMapDriving() const;
    void updateAutoKeyHistogram (const PitchFrame& frame);
    // Polyphonic AutoKey path: spectral chroma of the sidechain (master) audio
    // feeds its own chroma histogram instead of the monophonic vocal pitch. Both
    // paths share correlateAutoKeyHistogram(), which picks the live source's
    // histogram and profile (Krumhansl for sung notes, Aarden for audio chroma).
    void pushSidechainBlock (const float* mono, int numSamples);
    void runChromaAnalysis();
    void correlateAutoKeyHistogram();
    float styleCorrectionStrength() const;
    float styleRetuneCoefficient() const;
    float styleCorrectionStrengthFor (float amount) const;
    float styleRetuneCoefficientFor (float speed) const;
    juce::String currentStyleChoice() const;
    double pitchScaleForFrame (const PitchFrame& frame) const;
    void processWithRubberBand (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain,
                                bool straightTranspose = false);
    void processWithElastique   (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processWithSignalsmith (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processFallbackFormantGuide (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void applyModeCharacter (juce::AudioBuffer<float>& buffer) const;
    void renderHarmonyInto (juce::AudioBuffer<float>& lead,
                            const juce::AudioBuffer<float>& untouchedInput,
                            double hostTimeSeconds);
    void updateHarmonyChordAtSeconds (double seconds) noexcept;
    void applyGraphTimeCorrection (juce::AudioBuffer<float>& buffer, double hostTimeSeconds);
    void renderDelayedDryReference (const juce::AudioBuffer<float>& input);
    void copyDelayedDryReferenceTo (juce::AudioBuffer<float>& output) const;

    // Reported latency of whichever engine is active right now: the streaming
    // PSOLA path (Low Latency / Live Engine) has a small fixed delay; the Rubber
    // Band print path adds its pipeline delay plus the FIFO cushion. Called on
    // prepare and, when the engine toggles, from handleAsyncUpdate() on the
    // message thread (setLatencySamples must never run on the audio thread).
    int  activeEngineLatencySamples() const;
    int  monoEngineLatencySamples() const noexcept;
    //  Delay of whichever engine the print chain actually dispatches to
    //  (signalsmith when compiled in, otherwise Rubber Band).
    int  printEngineLatencySamples() const noexcept;
    //  Live Mode's delay is the epoch shifter's, not Rubber Band's. Reported
    //  through activeEngineLatencySamples() and used for the dry reference, so
    //  the bypass stays aligned with whichever engine is actually running.
    int  liveEngineLatencySamples() const noexcept;
    bool liveModeActive() const noexcept;
    bool ultraLiveProfileActive() const noexcept;
    bool extremeLiveProfileActive() const noexcept;
    std::vector<sattari::dsp::LivePitchEngine>& activeLiveEngines() noexcept;
    const std::vector<sattari::dsp::LivePitchEngine>& activeLiveEngines() const noexcept;
    // PSOLA prep helpers. Low-latency vocal mode raises the min-Hz floor so PSOLA's
    // fixed ~2-period latency shrinks (33 ms -> ~15 ms) at the cost of not tracking
    // fundamentals below the floor. preparePsolaInto builds a fresh shifter set (may
    // allocate — message thread only); psolaLatencyFor computes the resulting latency
    // analytically so the host can be told the new value before the audio-thread swap.
    void preparePsolaInto (std::vector<sattari::dsp::PsolaShifter>& dst, bool lowLatency);
    int  psolaLatencyFor (bool lowLatency) const;
    double psolaFloorHz  (bool lowLatency) const;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    // Message-thread heartbeat (~2 Hz) that republishes the key to the cross-plugin bus
    // whether or not the editor is open and whether or not a neural model is installed —
    // the worker thread only exists when a model is present, so publishing can't live there.
    void timerCallback() override;

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    double currentSampleRate = 44100.0;
    int preparedBlockSize = 512;
    juce::String lastTraceSummary { "No trace loaded" };

    std::vector<float> pitchFrameBuffer;
    int pitchWriteIndex = 0;
    int pitchSamplesUntilAnalysis = 0;
    PitchFrame lastPitchFrame;
    sattari::rt::AtomicSnapshot<UiFrame> uiFrame;

    // Preallocated scratch so the audio thread never calls makeCopyOf():
    //  - formantScratch: dry copy for the no-RubberBand pitch-guide fallback
    //  - harmonyScratch: untouched vocal source for the continuous voice renderer
    // Declared mutable because applyModeCharacter() is const.
    mutable juce::AudioBuffer<float> formantScratch;
    mutable juce::AudioBuffer<float> harmonyScratch;
    juce::AudioBuffer<float> harmonyWetScratch;
    // Three prepared banks keep generated voices aligned with Extreme, Ultra,
    // and Range/Studio. Switching profiles is allocation-free.
    sattari::dsp::HarmonyRenderer harmonyExtremeRenderer, harmonyUltraRenderer,
                                  harmonyRangeRenderer;
    sattari::dsp::HarmonyVoicePlanner harmonyRenderPlanner;
    std::array<std::atomic<int>, 4> harmonyChordPcs { -1, -1, -1, -1 };
    std::atomic<int> harmonyChordCount { 0 };
    std::atomic<float> harmonyChordConfidence { 0.0f };
    std::array<std::atomic<int>, 4> harmonyHeldMidi { -1, -1, -1, -1 };
    bool harmonyWasActive = false;
    int harmonyRendererBank = -1; // 0=Extreme, 1=Ultra, 2=Range/Studio
    double smoothedPitchScale = 1.0;
    double lastFedPitchScale  = 1.0; // pitch ratio actually sent to the shifter last block
    double smoothedXposeScale = 1.0; // Transpose-to-Key: smoothed whole-mix shift ratio (glide on change)
    bool   lastXposeActive    = false; // Transpose mode ran last block (flush engines on toggle)
    // Sung-note (f0) AutoKey histogram: one pitch class per detected fundamental.
    std::array<double, 12> autoKeyHistogram {};
    // Audio-chroma AutoKey histogram (sidechain / polyphonic main input, HPCP).
    // Kept apart from the f0 histogram because the two need different key
    // profiles (see autoKeyHistogramProfile / kAutoKeyChromaProfile) and live on
    // very different scales (f0 weights sum to thousands, chroma frames to ~1),
    // so a shared array was dominated by whichever source had fed it last.
    std::array<double, 12> chromaKeyHistogram {};
    // Live Mode may be called with 64..2048-sample host blocks. Decimate its
    // AutoKey observations onto the same pitch-hop clock as Studio so histogram
    // decay and key confidence do not change with a DAW's buffer size.
    int liveAutoKeySamplesUntilAnalysis = 0;
    // Root, mode and confidence form one verdict. There are two publishers (the
    // audio-thread detector and message-thread dropped-file path), so a seqlock is
    // not sufficient: two writers can overlap and expose an apparently stable,
    // torn value. Pack the complete verdict into one lock-free atomic instead.
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "Auto Key verdict must remain lock-free on the audio thread");
    std::atomic<std::uint64_t> detectedAutoKeyVerdict { 0x19u }; // A minor, confidence 0
    void publishDetectedAutoKey (int root, int scale, float confidence) noexcept;
    void readDetectedAutoKey (int& root, int& scale, float& confidence) const noexcept;
    //  True when the last publish attempt was withheld for low confidence, so the
    //  UI can say the bus is deliberately empty instead of appearing broken.
    std::atomic<bool>  busKeySuppressed { false };
    // Scale -> scaleMode mirror requested from parameterChanged (which may run on
    // the audio thread); -1 = nothing pending. Applied on the message thread.
    std::atomic<int> pendingScaleMirror { -1 };

    // --- Note-transition retune modeling (Antares-class "feel") --------------
    // When the correction target jumps to a new note, transitionBoost spikes to
    // 1.0 and decays exponentially. The applied retune coefficient is blended
    // between the slow "sustain" speed and a fast "onset" speed by this boost,
    // so the engine catches new notes quickly but lets vibrato through while a
    // note is held. lastTargetMidiForTransition tracks the previous target.
    int    lastTargetMidiForTransition = -1000;
    double transitionBoost = 0.0;        // 1.0 right after a note onset, decays to 0
    double flexEngageState = 0.0;        // smoothed Flex-Tune engagement [0,1]
    double classicControlPrevCents = 0.0;
    //  -20 ms, recalibrated 2026-08-20 on the six-clip corpus; prepareToPlay
    //  re-reads it from SATTARI_AUTOPITCH_CLASSIC_CONTROL_MS. The initialiser is
    //  kept in step with that default so the two can never disagree.
    double classicControlOffsetSeconds = -0.020;
    bool   classicControlValid = false;
    //  Velocity is measured across the interval in which the pitch frame ACTUALLY
    //  moved, not across one host block: targetPitchScale only refreshes once per
    //  pitchHopSize, so dividing by the block time inflated the figure by
    //  hop/blockSize at small buffers. The lead is held between frames so it stays
    //  a continuous lead instead of a clamp-saturated spike once per hop.
    double classicControlElapsedSec = 0.0;
    double classicControlLeadCents  = 0.0;

    // Hysteresis: prevents vibrato oscillating across a scale-note boundary from
    // repeatedly spiking transitionBoost=1.0 on every crossing.
    int    hysteresisAltTarget = -1000;  // target before lastTargetMidiForTransition
    double hysteresisAge       = 999.0;  // seconds elapsed since last target change
    // Target-note hysteresis: a note sung 30-45 c off with vibrato crosses the
    // half-semitone line every cycle, and a per-frame nearest-note target then
    // flips between two notes so the corrections cancel. Hold the current target
    // until the pitch is clearly nearer another note or stays there long enough.
    // Dry signal delayed by the reported latency so host bypass stays in time.
    // Sized in prepare to cover the largest latency this instance can report.
    // Fed with the dry input on EVERY block (bypassed or not), so engaging bypass
    // plays the audio of one latency ago rather than the tail of the last bypass.
    juce::AudioBuffer<float> bypassDelay;
    int bypassDelayWrite = 0;
    void writeBypassDelay (juce::AudioBuffer<float>& main, bool replaceWithDelayed);
    bool   targetHysteresisEnabled = true;
    int    heldTargetMidi      = -1;
    // Key / scale / Edit-Notes mask the held target was chosen under. A held note
    // is only valid inside the context that produced it.
    int    heldTargetContext   = -1;
    double heldTargetAwaySec   = 0.0;
    double heldTargetSilentSec = 0.0;
    // hopSec is the time elapsed since the previous call. Call it exactly once per
    // analysis update, on the FINAL frame of that update.
    void applyTargetHysteresis (PitchFrame& frame, double hopSec);

    // Intent Engine v2 (shadow/active experiment — Strategy G DSP: FROZEN)
    std::atomic<int> intentMode { 0 };   // IntentMode::PRODUCTION
    IntentV2State    intentState;
    PitchFrame applyIntentV2 (PitchFrame frame, int rootIndex,
                               const juce::String& scaleName, double blockTimeSec);

    // Reported processing latency (Rubber Band start delay). Recomputed in
    // prepareToPlay so DAW delay-comp keeps things tight; Low Latency trims it.
    int reportedLatencySamples = 0;

    // Sample-identical, latency-aligned reference for Correction Off and true
    // zero-amount operation. The ring runs continuously, so selecting the dry
    // path does not have to reconstruct unity-ratio audio through Rubber Band.
    juce::AudioBuffer<float> dryDelayRing, dryDelayOutput;
    int dryDelayRingSize = 0, dryDelayWrite = 0, dryDelaySamples = 0;

    // Which engine ran last block (liveEngine || lowLatency). processBlock compares
    // against this to detect a runtime engine swap and flush/prime both paths so
    // the switch has no ~28 ms dropout, stale-FIFO replay, or phase-click.
    bool lastUseLiveEngine = false;

    // Cross-plugin session bus: host tempo + a transient pulse counter, cached
    // on the audio thread and published off it (editor timer). Lets other
    // Sattari plugins follow tempo / react to onsets, not just the key.
    std::atomic<double>      hostBpmForBus { 0.0 };
    std::atomic<juce::int64> sessionTransientId { 0 };
    // Per-instance bus publish throttle. Atomic + CAS because publishing now happens
    // from BOTH the editor timer (immediate while open) and the CREPE worker thread
    // (so the key keeps broadcasting after the GUI is closed) — the CAS guarantees at
    // most one writer per second, so the two threads never write the bus file at once.
    std::atomic<juce::int64> lastBusPublishMs { 0 };

    // Follow Song Key: read the key Sattari Key (or any publisher) puts on the bus.
    sattari::xplugin::SessionReader sessionReader;

    // Optional MIDI-note targeting, separate from MIDI Learn. The most recently
    // held note becomes the exact correction target, enabling playable tuning
    // and DAW MIDI-note automation without disturbing the scale workflow.
    std::array<bool, 128> midiNotesHeld {};
    std::atomic<int> midiTargetNote { -1 };
    void updateMidiTargets (const juce::MidiBuffer& midi) noexcept;

    // Humanize: a slow, low-passed random walk that nudges the corrected pitch
    // by up to a few tens of cents. Re-evaluated once per block. State is just
    // two filtered scalars and a juce::Random; deterministic across plugin loads
    // only insofar as Random is seeded the same way.
    juce::Random humanizeRng;
    double humanizeTargetCents = 0.0;
    double humanizeSmoothedCents = 0.0;
    int    humanizeBlocksUntilNewTarget = 0;

    // Chroma FFT for the polyphonic-AutoKey / sidechain path.
    static constexpr int chromaFftOrder = 12;             // 4096-point FFT
    static constexpr int chromaFftSize  = 1 << chromaFftOrder;
    std::unique_ptr<juce::dsp::FFT> chromaFft;
    // HPCP (tuning-corrected + harmonic-summed) chroma for AutoKey — far more
    // reliable than a naive per-bin power map on full-mix material (validated on
    // GiantSteps: linear ~20 -> HPCP ~52 MIREX). Reset + accumulated per analysis
    // window; the rolling histogram blend below smooths it and follows modulations.
    sattari::dsp::HpcpChromagram autoKeyHpcp;
    // One suite key decision implementation. The release path deliberately runs the held-out
    // winning Aarden chroma configuration; the optional neural key model stays
    // quarantined until it beats that path on representative held-out genres.
    sattari::dsp::KeyAnalysisEngine keyAnalysis;
    // The AutoKey histogram counts detected FUNDAMENTALS (one pitch class per sung
    // note, no harmonics). Aarden weights the 5th above the tonic to offset the
    // harmonic leakage in audio chroma; on a harmonic-free note histogram that bias
    // turns a tonic-heavy melody into the key a fourth up. Krumhansl (tonic-led):
    // generated-voice key corpus 52% -> 89% exact, a cappella 48/48 (2026-09-23).
    sattari::dsp::KeyProfile autoKeyHistogramProfile = sattari::dsp::KeyProfile::Krumhansl;
    // Time constant of the sung-note histogram (see updateAutoKeyHistogram).
    double autoKeyMemorySec = 15.0;
    // Audio chroma (sidechain / polyphonic path) keeps the profile it was
    // validated with: Aarden's raised 5th compensates the harmonic leakage that
    // real chroma has and a fundamentals-only histogram does not.
    static constexpr sattari::dsp::KeyProfile kAutoKeyChromaProfile = sattari::dsp::KeyProfile::Aarden;
    std::vector<float> chromaRing;                        // mono ring of incoming sidechain samples
    std::vector<float> chromaFftScratch;                  // size 2 * fftSize for performFrequencyOnlyForwardTransform
    std::vector<float> chromaHannWindow;                  // Hann window (precomputed)
    int chromaWritePos = 0;
    int chromaSamplesSinceFft = 0;
    bool sidechainSeenAudio = false;                      // recent poly/sidechain analysis audio is active
    int  sidechainHoldSamples = 0;                        // expires so muted/disconnected SC cannot freeze AutoKey
    std::vector<float> scMonoScratch;                     // per-block mono sum of sidechain, no allocation in process

    // --- Real polyphonic retune: per-peak phase-locked vocoder ---------------
    // When `polyphonic` is ON (default OFF), correction goes through a genuine
    // phase vocoder (Laroche-Dolson peak shifting): every spectral peak is
    // retuned INDEPENDENTLY to its nearest enabled in-scale tone, with identity
    // phase-locking so each partial stays phase-coherent. This corrects the
    // strongest simultaneous notes independently on clear material; it is NOT
    // source separation (overlapping partials of different notes are not split).
    // All buffers are preallocated in prepareToPlay -> preparePolyVocoder(), so
    // the audio-thread frame processing never allocates or locks (RT-safe). The
    // STFT introduces (pvFrameSize - pvHop) samples of latency, reported to the
    // host when poly is on (see activeEngineLatencySamples()).
    // 4096-point frame: 11.7 Hz/bin @ 48k resolves close partials (e.g. an octave's
    // C5 fundamental vs C4 2nd harmonic, ~22 Hz apart) that a 2048 frame merged into
    // one bin — required for the harmonic-comb grouping to separate colliding notes.
    // Costs latency (pvFrameSize - pvHop = 3072 smp ≈ 64 ms) but only in poly mode,
    // which targets sustained chords/pads where that is inaudible.
    static constexpr int pvFftOrder  = 12;                    // 4096-point frame
    static constexpr int pvFrameSize = 1 << pvFftOrder;       // 4096
    static constexpr int pvOverlap   = 4;                     // 4x overlap
    static constexpr int pvHop       = pvFrameSize / pvOverlap; // 1024
    std::unique_ptr<juce::dsp::FFT> pvFft;
    std::vector<float> pvWindow;                              // Hann analysis+synthesis window
    float pvOlaNorm = 1.5f;                                   // overlap-add gain (sum w^2 at hop offsets)
    int   pvInFifoLatency = 0;                                // pvFrameSize - pvHop
    int   pvRover = 0;                                        // shared write cursor (channels lockstep)
    bool  lastPolyActive = false;                            // poly path ran last block (flush on toggle)
    // Per-channel streaming state (smb-style overlap-add FIFOs + phase memory).
    std::vector<std::vector<float>> pvInFifo;                 // [ch][pvFrameSize]
    std::vector<std::vector<float>> pvOutFifo;                // [ch][pvFrameSize]
    std::vector<std::vector<float>> pvOutAccum;               // [ch][pvFrameSize]
    std::vector<std::vector<float>> pvLastPhase;              // [ch][pvFrameSize/2 + 1]
    std::vector<std::vector<float>> pvSumPhase;               // [ch][pvFrameSize/2 + 1]
    // Shared per-frame scratch (reused across channels within a single frame).
    std::vector<std::complex<float>> pvFftIn, pvFftOut;      // size pvFrameSize
    std::vector<float> pvMag, pvPhase, pvFreq;                // size half+1 (analysis)
    std::vector<float> pvSynMag, pvSynPhase;                  // size half+1 (synthesis)
    std::vector<int>   pvPeakBins;                            // detected spectral peaks (bin indices)
    std::vector<int>   pvBinPeak;                             // size half+1: owning-peak index, -1 = identity
    std::vector<float> pvPeakRatio;                           // per-peak retune ratio
    std::vector<int>   pvPeakFund;                            // per-peak: index of its harmonic-comb fundamental peak
    // Poly Harmony (opt-in): a dedicated per-channel phase accumulator + per-peak harmony
    // ratio, so the harmony voice is phase-coherent and fully isolated from the corrected voice.
    std::vector<std::vector<float>> pvSumPhaseHarm;           // [ch][half+1]
    std::vector<float> pvPeakHarmRatio;                       // per-peak harmony (diatonic-3rd) ratio
    // Tonal Focus (opt-in): a causal HPSS mask per channel classifies each analysis bin as
    // harmonic (tonal partial) vs percussive (broadband/transient). Predominantly-percussive
    // bins are routed to the identity (unshifted) path so hats/air stay natural while tonal
    // partials are still tuned. pvTonal is per-frame scratch (size half+1).
    std::vector<sattari::dsp::HarmonicMask> pvHarmMask;       // [ch]
    std::vector<float> pvTonal;                               // size half+1: per-bin tonalness [0,1]
    void preparePolyVocoder (int numChannels);
    void resetPolyVocoder();
    void polyVocoderFrame (int channel);
    void processPolyphonicRetune (juce::AudioBuffer<float>& buffer, float linearGain);

    // Track Key Map state. The vector is built on a background thread, then
    // swapped under `keyMapLock`. The audio thread reads with a try-lock and
    // skips lookup if the writer holds it (the writer's hold is microseconds).
    juce::CriticalSection keyMapLock;
    std::vector<KeyMapEntry> keyMap;
    //  Follow Song Map's table: the analysed scale regions, in the SAME shape the
    //  validated Key Map follower consumes. Guarded by keyMapLock like keyMap.
    std::vector<KeyMapEntry> songMapEntries;
    // Peak envelope + duration of the scanned file, published with the map so the
    // editor can draw the waveform under the key sections.
    std::vector<float> keyMapWave;
    double keyMapDurationSec = 0.0;
    std::atomic<double> keyMapPlayheadSec { 0.0 };
    std::atomic<bool> keyMapScanning { false };
    // Set by setStateInformation() (and teardown) to ask an in-progress background
    // scan to stop before it publishes, so a restored/cleared map isn't clobbered
    // by a scan that started earlier. The scan loop polls it every window, making
    // the subsequent join return almost immediately (setStateInformation stays
    // effectively non-blocking). Reset to false in startKeyMapScan().
    std::atomic<bool> keyMapScanCancel { false };
    std::atomic<int>  keyMapProgress { -1 };
    std::atomic<int>  keyMapCurrentSegment { -1 };
    juce::String keyMapFileName;
    juce::String keyMapLastError;
    std::unique_ptr<std::thread> keyMapScanThread;
    void runKeyMapScan (juce::File audioFile);


    // Captured/editable note regions. Audio reads/writes only through try-locks;
    // the vector is reserved on prepare so capture never allocates in processBlock.
    mutable juce::CriticalSection pitchEditLock;
    std::vector<PitchEditEntry> pitchEdits;
    std::vector<std::vector<PitchEditEntry>> pitchEditUndo, pitchEditRedo;
    std::atomic<int> pitchEditRevision { 0 };
    juce::int64 nextPitchEditId = 1;
    bool pitchEditGestureActive = false;
    bool pitchEditGestureChanged = false;
    int lastCapturedPitchEdit = -1;
    double lastCapturedEndSec = -1.0;
    // Audio-thread-only fallback for the brief interval in which the editor owns
    // pitchEditLock. It prevents an edit gesture from dropping correction for a
    // host block merely because the GUI is updating the note vector.
    mutable PitchEditEntry cachedAppliedPitchEdit;
    mutable bool cachedAppliedPitchEditValid = false;
    void pushPitchEditUndoLocked();
    void markPitchEditChangedLocked();
    void capturePitchEditAtSeconds (double seconds, double blockDuration, const PitchFrame& frame);
    void applyPitchEditAtSeconds (double seconds, PitchFrame& frame) const;

    // Graph time correction is an opt-in, latency-compensated fractional delay.
    // The fixed 500 ms look-ahead allows note objects to move earlier as well as
    // later without reading future host audio. Storage is allocated in prepare.
    juce::AudioBuffer<float> graphTimeRing;
    int graphTimeRingSize = 0, graphTimeWrite = 0;
    double graphTimeDelaySamples = 0.0;
    bool graphTimeWasActive = false;

#if SATTARI_AUTOPITCH_HAS_ELASTIQUE
    // zplane elastique v3 — primary low-latency pitch engine.
    // TODO: replace void* with CElastiqueV3If* once SDK header is confirmed.
    void* elastique = nullptr;
    juce::AudioBuffer<float> elastiqueOutput;
    std::vector<std::vector<float>> elastiqueScratchIn;
    std::vector<std::vector<float>> elastiqueScratchOut;
#endif

#if SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    // signalsmith-stretch — MIT, secondary low-latency engine (active when
    // elastique SDK is absent). Single instance manages all channels.
    signalsmith::stretch::SignalsmithStretch<float> ssStretch;
    // reset() on an unconfigured stretch dereferences null, and hosts may call
    // releaseResources() without a prior prepareToPlay() (scan, remove, teardown).
    bool ssConfigured = false;
    juce::AudioBuffer<float> ssOutput;
    std::vector<const float*> ssInputPtrs;
    std::vector<float*>       ssOutputPtrs;
    int signalsmithLatencySamples = 0;
#endif

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    std::vector<std::unique_ptr<RubberBand::RubberBandStretcher>> rubberBand;
    juce::AudioBuffer<float> rubberBandOutput;
    std::vector<float> rubberBandScratch;

    // --- Output FIFO for the Rubber Band engine ------------------------------
    // Rubber Band in real-time mode is bursty: after process(N) it may have
    // fewer than N samples ready (and far fewer during its start-up latency).
    // We drain everything it produces into this ring and hand the host exactly
    // N samples per block from the ring. This removes the old behaviour of
    // back-filling the block tail with DRY input, which spliced un-shifted
    // signal onto shifted signal every block and caused audible crackle.
    juce::AudioBuffer<float> rbFifo;   // numCh x rbFifoCap, allocation-free in RT
    int  rbFifoCap   = 0;              // ring capacity in samples
    int  rbFifoRead  = 0;              // read cursor (shared across channels)
    int  rbFifoCount = 0;             // samples currently queued (shared)
    int  rbCushion   = 0;             // samples buffered before popping begins
    bool rbPrimed    = false;         // has the cushion filled yet?
    int  rbLatency   = 0;             // Rubber Band start delay (samples)
    double rubberBandEnginePitchScale = 1.0; // last ratio delivered on the fixed control grid
    int rubberBandAutomationHoldSamples = 0; // hysteresis: finish a moving-ratio glide on-grid
    void pushRbFifo (const juce::AudioBuffer<float>& src, int chSrcBase, int n);
    void resetRbFifo();
#endif

    // --- Live-monitor engine: streaming TD-PSOLA -----------------------------
    // Near-zero-latency, formant-preserving pitch shift for tracking/monitoring.
    // Rubber Band stays the default "print" engine (higher fidelity, more
    // latency); the "Live Engine" toggle swaps to PSOLA per channel.
    // Live Mode has three fully-prepared paths so changes never allocate on the
    // audio thread. Extreme targets 2 ms, waveform-locked Ultra covers C3+ at
    // <12 ms, and Extended preserves the 70 Hz floor at ~22 ms. Only channel 0
    // plans; the others follow its pitch scale so stereo cannot drift apart.
    std::vector<sattari::dsp::LivePitchEngine> liveEngines;
    std::vector<sattari::dsp::LivePitchEngine> liveExtendedEngines;
    std::vector<sattari::dsp::ExtremeLivePitchEngine> extremeLiveEngines;
    bool lastLiveModeActive = false;
    int lastLiveProfile = 0;

    std::vector<sattari::dsp::PsolaShifter> psola;
    std::vector<sattari::dsp::PsolaShifter> pendingPsola;     // message-thread-built, audio-thread-swapped (low-latency toggle)
    std::atomic<bool> psolaSwapReady { false };              // pendingPsola is ready to swap in
    std::atomic<bool> psolaLiveLowLat { false };            // low-latency state of the LIVE shifter (set by the audio thread on swap)
    bool pendingLowLat = false;                             // low-latency target the pendingPsola set was built for
    void processWithPsola (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processWithLiveEngine (juce::AudioBuffer<float>& buffer, float linearGain,
                                double pitchOffsetCents);

    // --- Optional CREPE-class neural pitch (ONNX, model-optional) ------------
    // When "Neural Pitch" is on, CREPE is conservatively fused with YIN: agreeing
    // estimates refine one another, high-confidence CREPE can rescue an unvoiced
    // YIN frame, and conflicting neural/octave estimates cannot seize correction.
    sattari::dsp::CrepePitchTracker crepe;
    sattari::dsp::MonophonicPitchFusion pitchFusion;
    bool  crepeModelPresent = false;
    sattari::dsp::KeyNet keyNet;              // neural key model (90-dim HPCP feature -> 24 keys)
    bool  keyNetPresent = false;
    // Spectrogram key model (72.1 MIREX vs the chroma model's 61.5) — offline Key Map
    // scan only: it needs a 30 s window and a pseudo-CQT, too heavy for the live path.
    sattari::dsp::LogSpec     logSpec;
    sattari::dsp::KeyNetSpec  keyNetSpec;
    bool  keyNetSpecPresent = false;
    float lastCrepeHz = 0.0f;
    float lastCrepeConfidence = 0.0f;
    // Inference runs on a background worker, never the audio thread. The audio
    // thread only pushes samples (lock-free ring) and reads these result atomics.
    std::thread        crepeThread;
    std::atomic<bool>  crepeThreadRun { false };
    std::atomic<float> crepeResultHz { 0.0f };
    std::atomic<float> crepeResultConf { 0.0f };
    std::atomic<bool>  crepeResultVoiced { false };
    // Offline (host bounce / non-realtime): the 12 ms worker can't keep pace with
    // instant block processing, so neural pitch silently degrades to YIN. When
    // offlineMode is set, the audio thread runs the ONNX inference SYNCHRONOUSLY under
    // crepeMutex, and the worker skips (so the two never call analyse() concurrently).
    std::atomic<bool>  offlineMode { false };
    std::mutex         crepeMutex;
    void crepeWorkerLoop();

    // --- Live neural key (AutoKey over the full-mix sidechain) ---------------
    // The audio thread accumulates a DECAYED mean + covariance of the per-window
    // HPCP chroma (the exact 90-dim feature the key-net was trained on) — cheap and
    // RT-safe — and stashes the latest feature under a try_lock. The CREPE worker
    // consumes it and runs keyNet.detect() off the audio thread; correlateAutoKey-
    // Histogram prefers this neural key over the fixed profile when it's valid. Only
    // the polyphonic sidechain path feeds it, so a monophonic vocal (no sidechain)
    // still falls back to the histogram detector — which is where the profile belongs.
    double             knMean[12]      = {};
    double             knOuter[12][12] = {};
    double             knWeight        = 0.0;
    std::mutex         keyNetFeatMutex;
    std::array<float, 90> keyNetFeat {};
    std::atomic<bool>  keyNetFeatReady { false };
    std::atomic<bool>  keyNetLiveValid { false };
    std::atomic<int>   keyNetLiveRoot  { 0 };
    std::atomic<int>   keyNetLiveMinor { 0 };

    // Live AutoKey via the SPECTROGRAM model — the same keynet_spec.onnx the Key Map
    // scan uses and that Sattari Key now publishes from. It measures 72.1 MIREX vs
    // 61.5 for the HPCP keyNet above, so with both available the spec model wins and
    // AutoKey agrees with what Sattari Key broadcasts instead of quietly disagreeing.
    // It wants the ~30 s excerpt it was trained on, hence a long rolling ring rather
    // than the short chroma FFT; inference is throttled by AUDIO CONSUMED (not by call
    // count) and its verdict is LATCHED, because it speaks far less often than the
    // chroma path and must not be overwritten in between.
    std::unique_ptr<std::atomic<float>[]> akSpecRing;
    int  akSpecRingSize = 0;
    std::atomic<int>  akSpecWritePos { 0 };
    std::atomic<long> akSpecFilled   { 0 };
    std::vector<float> akSpecMono, akSpecFeat;      // worker scratch, sized in prepare
    long akSpecLastRun = -1;                        // akSpecFilled when the net last ran
    std::atomic<bool> akSpecValid { false };
    std::atomic<int>  akSpecRoot  { 0 };
    std::atomic<int>  akSpecMinor { 0 };

    //  --- Sattari Key, absorbed -------------------------------------------------
    //  The SongKeyEstimator measured 57.6% exact / 65.12 MIREX on 1337 labelled
    //  tracks against 39-45% for every chroma profile and 37.5% held-out for the
    //  spectrogram model above, so once it is warm it outranks both.
    //
    //  It drains the SAME akSpecRing the spectrogram model reads — akSpecFilled is
    //  a monotonic sample counter, so a private read cursor turns the rolling ring
    //  into a continuous stream. No new audio-thread work, and by construction the
    //  estimator hears exactly the audio the other full-mix detectors hear.
    //  Worker-thread-only, like the ring itself.
    sattari::dsp::SongKeyEstimator songKeyLive;    // crepe-worker-thread-only
    long songKeyReadCursor = 0;                    // worker-only, vs akSpecFilled
    std::vector<float> songKeyDrain;               // worker scratch
    std::atomic<bool>  skValid { false };
    std::atomic<int>   skRoot  { 0 };
    std::atomic<int>   skMinor { 0 };
    std::atomic<float> skConf  { 0.0f };
    std::atomic<int>   skFrames { 0 };
    std::atomic<bool>  songKeyRescan { false };    // any thread -> worker

    static constexpr double kAkSpecSeconds     = 30.0;
    static constexpr double kAkSpecIntervalSec = 5.0;

    // --- Vibrato preservation ------------------------------------------------
    // Low-passed "note centre" of the detected pitch. Correction snaps the centre
    // to the scale while a fraction of the singer's fast pitch oscillation
    // (vibrato) is re-injected, so hard tuning doesn't flatten expressive vibrato.
    // Reset on note onset so new notes still snap.
    double vibratoCenterMidi = -1000.0;

    // --- Synthetic vibrato generator (addVibrato) ----------------------------
    // Distinct from Preserve Vibrato (which keeps the SINGER's own): this ADDS a
    // sinusoidal pitch LFO like a vocalist's natural vibrato. Depth (cents), rate
    // (Hz) and onset (ms) are params; the onset ramp blooms 0->1 over vibratoOnset
    // ms from each note ONSET (reset on a detected note change). Depth 0 is a
    // strict no-op (golden render untouched). Both reset in resetPitchState.
    double addVibratoPhase = 0.0;         // LFO phase, radians
    double addVibratoRampSamples = 0.0;   // samples elapsed since the current note's onset

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessor)
};
