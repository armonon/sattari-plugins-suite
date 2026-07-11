#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPsolaShifter.h>
#include <sattari_audio_core/SattariCrepePitch.h>
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

class SattariAutoPitchAudioProcessor final : public juce::AudioProcessor,
                                             private juce::AudioProcessorValueTreeState::Listener,
                                             private juce::AsyncUpdater
{
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
    // Whether a crepe.onnx model was found + loaded at prepare time — lets the UI
    // label the Neural Pitch toggle honestly instead of silently doing nothing.
    bool neuralModelPresent() const noexcept { return crepeModelPresent; }

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
        int    scale = 1;   // 0=major, 1=minor, 2=chromatic (effective)
    };
    KeyOrigin currentKeyOrigin() const;

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
    // Voiced pitch range (Hz) for the selected Sound Source. Shared by the YIN
    // gate (analyseCurrentPitchFrame) and the CREPE override so both reject the
    // same out-of-range estimates.
    void voicedRangeHzForSource (double& minHz, double& maxHz) const;
    PitchFrame analyseCurrentPitchFrame() const;
    PitchFrame buildCorrectionFrame (PitchFrame detected) const;
    int currentCorrectionRootIndex() const;
    int effectiveScaleIndex() const;     // bus scale when Follow Song Key is on, else the param
    bool keyMapDriving() const;
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
    void processWithRubberBand (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void processFallbackFormantGuide (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);
    void applyModeCharacter (juce::AudioBuffer<float>& buffer) const;

    // Reported latency of whichever engine is active right now: the streaming
    // PSOLA path (Low Latency / Live Engine) has a small fixed delay; the Rubber
    // Band print path adds its pipeline delay plus the FIFO cushion. Called on
    // prepare and, when the engine toggles, from handleAsyncUpdate() on the
    // message thread (setLatencySamples must never run on the audio thread).
    int  activeEngineLatencySamples() const;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

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
    double lastFedPitchScale  = 1.0; // pitch ratio actually sent to the shifter last block
    std::array<double, 12> autoKeyHistogram {};
    int detectedAutoRoot = 9;
    int detectedAutoScale = 1;   // AutoKey auto-detected mode: 0=major, 1=minor

    // --- Note-transition retune modeling (Antares-class "feel") --------------
    // When the correction target jumps to a new note, transitionBoost spikes to
    // 1.0 and decays exponentially. The applied retune coefficient is blended
    // between the slow "sustain" speed and a fast "onset" speed by this boost,
    // so the engine catches new notes quickly but lets vibrato through while a
    // note is held. lastTargetMidiForTransition tracks the previous target.
    int    lastTargetMidiForTransition = -1000;
    double transitionBoost = 0.0;        // 1.0 right after a note onset, decays to 0
    double flexEngageState = 0.0;        // smoothed Flex-Tune engagement [0,1]

    // Reported processing latency (Rubber Band start delay). Recomputed in
    // prepareToPlay so DAW delay-comp keeps things tight; Low Latency trims it.
    int reportedLatencySamples = 0;

    // Which engine ran last block (liveEngine || lowLatency). processBlock compares
    // against this to detect a runtime engine swap and flush/prime both paths so
    // the switch has no ~28 ms dropout, stale-FIFO replay, or phase-click.
    bool lastUseLiveEngine = false;

    // Cross-plugin session bus: host tempo + a transient pulse counter, cached
    // on the audio thread and published off it (editor timer). Lets other
    // Sattari plugins follow tempo / react to onsets, not just the key.
    std::atomic<double>      hostBpmForBus { 0.0 };
    std::atomic<juce::int64> sessionTransientId { 0 };
    juce::int64              lastBusPublishMs = 0;   // per-instance bus publish throttle

    // Follow Song Key: read the key Sattari Key (or any publisher) puts on the bus.
    sattari::xplugin::SessionReader sessionReader;

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

    // Preallocated scratch for polyAnalyseAndComputeScale() so the ~50 ms HPS hop
    // never allocates on the audio thread. Sized in prepareToPlay; the analysis
    // clears/refills (no construction) and stays within reserved capacity.
    struct PolyPeak     { float val = 0.0f; int bin = 0; };
    struct PolyDetected { double freq = 0.0; int midi = 0; float weight = 0.0f; };
    std::vector<float>        polyHps;
    std::vector<PolyPeak>     polyPeaks;
    std::vector<PolyDetected> polyNotes;

    // Track Key Map state. The vector is built on a background thread, then
    // swapped under `keyMapLock`. The audio thread reads with a try-lock and
    // skips lookup if the writer holds it (the writer's hold is microseconds).
    juce::CriticalSection keyMapLock;
    std::vector<KeyMapEntry> keyMap;
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
    void applyKeyMapAtSeconds (double seconds);

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
    void pushRbFifo (const juce::AudioBuffer<float>& src, int chSrcBase, int n);
    void resetRbFifo();
#endif

    // --- Live-monitor engine: streaming TD-PSOLA -----------------------------
    // Near-zero-latency, formant-preserving pitch shift for tracking/monitoring.
    // Rubber Band stays the default "print" engine (higher fidelity, more
    // latency); the "Live Engine" toggle swaps to PSOLA per channel.
    std::vector<sattari::dsp::PsolaShifter> psola;
    void processWithPsola (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain);

    // --- Optional CREPE-class neural pitch (ONNX, model-optional) ------------
    // When a crepe.onnx model is present and "Neural Pitch" is on, its estimate
    // overrides YIN (killing octave errors on breathy vocals). Absent a model,
    // isAvailable() is false and YIN is used — zero behavioural change.
    sattari::dsp::CrepePitchTracker crepe;
    bool  crepeModelPresent = false;
    float lastCrepeHz = 0.0f;
    float lastCrepeConfidence = 0.0f;
    // Inference runs on a background worker, never the audio thread. The audio
    // thread only pushes samples (lock-free ring) and reads these result atomics.
    std::thread        crepeThread;
    std::atomic<bool>  crepeThreadRun { false };
    std::atomic<float> crepeResultHz { 0.0f };
    std::atomic<float> crepeResultConf { 0.0f };
    std::atomic<bool>  crepeResultVoiced { false };
    void crepeWorkerLoop();

    // --- Vibrato preservation ------------------------------------------------
    // Low-passed "note centre" of the detected pitch. Correction snaps the centre
    // to the scale while a fraction of the singer's fast pitch oscillation
    // (vibrato) is re-injected, so hard tuning doesn't flatten expressive vibrato.
    // Reset on note onset so new notes still snap.
    double vibratoCenterMidi = -1000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessor)
};
