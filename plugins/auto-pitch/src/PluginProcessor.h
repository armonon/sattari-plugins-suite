#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "ContinuousMotionEstimator.h"

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

class SattariAutoPitchAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariAutoPitchAudioProcessor();
    ~SattariAutoPitchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    // Intent Engine v2 mode control (bench / DAW experimentation only)
    void setIntentMode (IntentMode m) noexcept { intentMode.store ((int) m, std::memory_order_relaxed); }
    IntentMode getIntentMode() const noexcept  { return (IntentMode) intentMode.load (std::memory_order_relaxed); }
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

    // Live snapshot for the editor (timer-polled). Trivially-copyable POD with
    // sensible defaults when nothing has been processed yet.
    struct LiveSnapshot
    {
        bool  voiced = false;
        int   detectedMidi = -1;     // nearest MIDI note of detected pitch
        float correctionCents = 0.0f;
        float confidence = 0.0f;
        int   detectedRoot = 0;      // 0..11, AutoKey output (or selected key)
    };
    // Push the current correction key to the cross-plugin bus so other Sattari
    // plugins (e.g. Royal Chain's Tune) can follow. Called from the editor
    // timer with internal throttling so we never write per audio block.
    void publishCurrentKeyToBus();

    LiveSnapshot getLiveSnapshot() const noexcept
    {
        LiveSnapshot s;
        s.voiced = lastPitchFrame.voiced;
        s.detectedMidi = lastPitchFrame.voiced
            ? juce::jlimit (0, 127, (int) std::round (lastPitchFrame.midi)) : -1;
        s.correctionCents = (float) juce::jlimit (-200.0, 200.0, lastPitchFrame.correctionCents);
        s.confidence = (float) juce::jlimit (0.0, 1.0, lastPitchFrame.confidence);
        s.detectedRoot = detectedAutoRoot;
        return s;
    }

private:
    struct PitchFrame
    {
        double frequencyHz = 0.0;
        double midi = 0.0;
        double targetMidi = 0.0;
        double correctionCents = 0.0;
        double confidence = 0.0;
        double rms = 0.0;
        bool voiced = false;
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
    PitchFrame analyseCurrentPitchFrame() const;
    PitchFrame buildCorrectionFrame (PitchFrame detected) const;
    int currentCorrectionRootIndex() const;
    void updateAutoKeyHistogram (const PitchFrame& frame);
    // Polyphonic AutoKey path: spectral chroma of the sidechain (master) audio
    // feeds the histogram instead of the monophonic vocal pitch. Both paths share
    // correlateAutoKeyHistogram() so the Krumhansl decision logic stays one place.
    void pushSidechainBlock (const float* mono, int numSamples);
    void runChromaAnalysis();
    void correlateAutoKeyHistogram();
    float styleCorrectionStrength() const;
    float styleRetuneCoefficient() const;
    juce::String currentStyleChoice() const;
    double pitchScaleForFrame (const PitchFrame& frame) const;
    void processWithElastique   (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processWithSignalsmith (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processWithRubberBand  (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processFallbackFormantGuide (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void applyModeCharacter (juce::AudioBuffer<float>& buffer) const;

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

    // Preallocated scratch so the audio thread never calls makeCopyOf():
    //  - formantScratch: dry copy for the no-RubberBand pitch-guide fallback
    //  - harmonyScratch: base copy for the "Big Harmony" style layer
    // Declared mutable because applyModeCharacter() is const.
    mutable juce::AudioBuffer<float> formantScratch;
    mutable juce::AudioBuffer<float> harmonyScratch;
    double smoothedPitchScale = 1.0;
    std::array<double, 12> autoKeyHistogram {};
    int detectedAutoRoot = 9;

    // --- Note-transition retune modeling (Antares-class "feel") --------------
    // When the correction target jumps to a new note, transitionBoost spikes to
    // 1.0 and decays exponentially. The applied retune coefficient is blended
    // between the slow "sustain" speed and a fast "onset" speed by this boost,
    // so the engine catches new notes quickly but lets vibrato through while a
    // note is held. lastTargetMidiForTransition tracks the previous target.
    int    lastTargetMidiForTransition = -1000;
    double transitionBoost = 0.0;        // 1.0 right after a note onset, decays to 0
    double flexEngageState = 0.0;        // smoothed Flex-Tune engagement [0,1]
    // Hysteresis: prevents vibrato oscillating across a scale-note boundary from
    // repeatedly spiking transitionBoost=1.0 on every crossing.
    int    hysteresisAltTarget = -1000;  // target before lastTargetMidiForTransition
    double hysteresisAge       = 999.0;  // seconds elapsed since last target change

    // Intent Engine v2 (shadow/active experiment — Strategy G DSP: FROZEN)
    std::atomic<int> intentMode { 0 };   // IntentMode::PRODUCTION
    IntentV2State    intentState;
    PitchFrame applyIntentV2 (PitchFrame frame, int rootIndex,
                               const juce::String& scaleName, double blockTimeSec);

    // Reported processing latency (Rubber Band start delay). Recomputed in
    // prepareToPlay so DAW delay-comp keeps things tight; Low Latency trims it.
    int reportedLatencySamples = 0;

    // Cross-plugin session bus: host tempo + a transient pulse counter, cached
    // on the audio thread and published off it (editor timer). Lets other
    // Sattari plugins follow tempo / react to onsets, not just the key.
    std::atomic<double>      hostBpmForBus { 0.0 };
    std::atomic<juce::int64> sessionTransientId { 0 };
    juce::int64              lastBusPublishMs = 0;   // per-instance bus publish throttle

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
    std::vector<float> chromaRing;                        // mono ring of incoming sidechain samples
    std::vector<float> chromaFftScratch;                  // size 2 * fftSize for performFrequencyOnlyForwardTransform
    std::vector<float> chromaHannWindow;                  // Hann window (precomputed)
    int chromaWritePos = 0;
    int chromaSamplesSinceFft = 0;
    bool sidechainSeenAudio = false;                      // true when SC bus delivered non-silence
    std::vector<float> scMonoScratch;                     // per-block mono sum of sidechain, no allocation in process

    // --- Polyphonic (alpha) HPS detector on the main input -------------------
    // Reuses chromaFft for FFT but its own ring + a separate accumulator so the
    // sidechain-chroma path stays independent.
    std::vector<float> polyRing;
    int polyWritePos = 0, polySamplesSinceFft = 0;
    double polyAveragePitchScale = 1.0;        // smoothed weighted-mean correction
    std::array<int, 3> polyLastNotes { -1, -1, -1 };
    void   polyPushBlock (const float* mono, int n);
    double polyAnalyseAndComputeScale();       // returns target pitch scale, or 1.0 if no correction

    // Track Key Map state. The vector is built on a background thread, then
    // swapped under `keyMapLock`. The audio thread reads with a try-lock and
    // skips lookup if the writer holds it (the writer's hold is microseconds).
    juce::CriticalSection keyMapLock;
    std::vector<KeyMapEntry> keyMap;
    std::atomic<bool> keyMapScanning { false };
    std::atomic<int>  keyMapProgress { -1 };
    std::atomic<int>  keyMapCurrentSegment { -1 };
    juce::String keyMapFileName;
    juce::String keyMapLastError;
    std::unique_ptr<std::thread> keyMapScanThread;
    void runKeyMapScan (juce::File audioFile);
    void applyKeyMapAtSeconds (double seconds);

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
    juce::AudioBuffer<float> ssOutput;
    std::vector<const float*> ssInputPtrs;
    std::vector<float*>       ssOutputPtrs;
#endif

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    std::vector<std::unique_ptr<RubberBand::RubberBandStretcher>> rubberBand;
    juce::AudioBuffer<float> rubberBandOutput;
    std::vector<float> rubberBandScratch;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessor)
};
