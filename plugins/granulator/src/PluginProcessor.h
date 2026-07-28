#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/SattariGranularEngine.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId, snapMidiToScale
#include <sattari_audio_core/SattariRealtime.h>          // AtomicSnapshot
#include <sattari_audio_core/SattariPresetManager.h>

#include <sattari_audio_core/SattariGranSourceSample.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// =============================================================================
//  Sattari Granulator — TWO-LAYER polyphonic granular-synthesis instrument
// -----------------------------------------------------------------------------
//  IS_SYNTH + NEEDS_MIDI_INPUT (+ an OPTIONAL discrete audio input bus for Live
//  Capture). Turns any loaded sample into a playable instrument on top of
//  sattari::dsp::SattariGranularEngine.
//
//  TWO LAYERS (A / B):
//    * TWO SattariGranularEngine instances and TWO independently-swapped source
//      slots. MIDI note-on/off is routed to BOTH engines; each block renders
//      both engines into scratch buffers and BLENDS them by `layerMorph`
//      (0 = A only, 1 = B only, 0.5 = equal blend) with per-layer trims.
//    * PER-LAYER params are duplicated with an "A_" / "B_" id prefix: source
//      region/edit state, File Position (base/scan/env/lfo amounts), grain cloud
//      (size/spray/density/mode/window/pan), pitch (coarse/fine/spread), unison,
//      and a per-layer Level + Pan.
//    * GLOBAL (shared) params keep their original single ids and are pushed
//      identically to both engines: pitch env/LFO, glide, key-snap/root/scale/
//      followKey, filter + filter env, amp/filter ADSR, LFO1/2, mod AD, sub,
//      width/tone/volume, voices/voiceMode.
//    * `layerSel` (A/B) is a UI-ONLY selector: it selects which layer's params
//      the editor edits and which layer Live Capture targets by default. It has
//      no effect on the rendered audio (both layers always render + blend).
//
//  LIVE CAPTURE:
//    * A preallocated lock-free capture ring records the plugin's audio INPUT
//      (main input bus) on the audio thread (ring write only — NO decode/alloc).
//    * A message-thread timer drains the ring, builds a SourceSample, and
//      atomic-swaps it into the capture-target layer.
//    * Params: captureArm (momentary bool), captureSecs (0.5..8, default 2.0),
//      captureTarget (A/B).
//
//  STATE / BACKWARD COMPAT: a `granulatorStateVersion` tag is written. Old
//  single-layer presets (no A_/B_ ids, one srcFilePath) load into Layer A; Layer
//  B starts empty. Two file paths + hashes are persisted (A + B).
// =============================================================================
class SattariGranulatorAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariGranulatorAudioProcessor();
    ~SattariGranulatorAudioProcessor() override;

    // Layer identity used across the processor + editor interface.
    enum class Layer { A = 0, B = 1 };
    static constexpr int kNumLayers = 2;

    // --- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Granulator"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }   // long amp release

    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets()) { currentProgram = i; presetBank.loadFactoryPreset (i); }
    }
    const juce::String getProgramName (int i) override { return presetBank.factoryPresetNames()[i]; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // =========================================================================
    //  PUBLIC INTERFACE FOR THE EDITOR
    // =========================================================================

    // APVTS: the editor attaches sliders/combos/buttons to this.
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }

    // Shared format manager (the editor's WaveformView AudioThumbnail needs one).
    juce::AudioFormatManager& formatManager() noexcept { return formats; }

    // --- Layer selection (UI-only) -------------------------------------------
    // Which layer the editor is currently editing (mirrors the `layerSel` param).
    Layer selectedLayer() const noexcept
    {
        return ((int) std::lround (param ("layerSel")) == 1) ? Layer::B : Layer::A;
    }

    // Prefix ("A_"/"B_") for a per-layer APVTS id on a given layer. The editor
    // uses this to (re)bind its per-layer widgets when the selected layer flips.
    static const char* layerPrefix (Layer layer) noexcept { return layer == Layer::B ? "B_" : "A_"; }

    // Build a full per-layer APVTS id: layerParamId(Layer::B, "grainSize") -> "B_grainSize".
    // Returns a juce::String so callers can hand it straight to an attachment.
    static juce::String layerParamId (Layer layer, const char* baseId)
    {
        return juce::String (layerPrefix (layer)) + baseId;
    }

    // The (base, un-prefixed) ids of every PER-LAYER parameter, in editor order.
    // The editor iterates this to build/rebind one layer's control rack.
    static const std::vector<juce::String>& perLayerParamIds();

    // --- Source load / access (message thread) --------------------------------
    // Decode `file` into a fresh SourceSample and atomic-swap it into `layer`.
    // Returns false if the file could not be read (that layer is left untouched).
    // Applies the layer's current srcMono downmix at build time.
    bool loadFile (Layer layer, const juce::File& file);

    // The most-recently-published source for a layer (null before anything loads).
    // The editor uses it to (re)build its per-layer thumbnail + read length.
    std::shared_ptr<const sattari::granulator::SourceSample> currentSource (Layer layer) const;

    // Human-readable name of the loaded file for a layer (empty when none /
    // captured buffer).
    juce::String currentSourceName (Layer layer) const;

    // --- Destructive edit ops (message thread; each a pure rebuild + swap) -----
    // All operate on the given layer's source.
    void editReverse (Layer layer);          // reverse the whole buffer
    void editNormalize (Layer layer);        // peak-normalise to ~ -0.5 dBFS
    void editTrimToSelection (Layer layer);  // crop to [srcStart, srcEnd]; reset offsets
    void editMonoFold (Layer layer);         // sum to a single-channel buffer
    void editUndo (Layer layer);             // pop the last pre-edit source back in
    bool canUndo (Layer layer) const noexcept;

    // --- Live Capture (editor triggers, message-thread drain) -----------------
    // Arm/disarm capture into a layer. Arming clears the ring and starts writing
    // the input bus on the audio thread; the message-thread timer drains it into
    // a fresh SourceSample once `captureSecs` of audio have accumulated (or on
    // disarm). Momentary: `armCapture` mirrors the captureArm param.
    void armCapture (bool arm) noexcept;
    bool isCapturing() const noexcept { return capture.armed.load (std::memory_order_relaxed); }
    // 0..1 progress of the in-flight capture (for a UI meter); 0 when idle.
    float captureProgress() const noexcept;
    // Drain any completed capture on the message thread (call from the editor
    // timer). Returns true if a new source was published this call.
    bool serviceCapture();

    // Publish-to-key-bus (message thread; call from the editor timer). When
    // broadcastKey is on, a note is sounding, and we are NOT following, this
    // publishes the currently-played (key-snapped) root + scale to the cross-
    // plugin key/session bus so playing the granulator DRIVES the suite's key.
    // Throttled + change-gated so it never thrashes the bus file. Returns true if
    // it published this call. Safe to call every timer tick.
    bool serviceKeyBroadcast();

    // --- Live grain visualisation --------------------------------------------
    // Lock-free snapshot published by the audio thread every block. `A`/`B` give
    // each layer's frame; the editor draws the selected layer's sparks.
    sattari::dsp::SattariGranularEngine::GrainVizFrame vizFrame (Layer layer) const noexcept
    {
        return viz[(size_t) layer].load();
    }

    // --- KEY story (drives the shared header KeyButton) -----------------------
    bool isFollowingBusKey() const noexcept { return followingBus.load (std::memory_order_relaxed); }
    int  effectiveKeyRoot() const noexcept  { return effKeyRoot.load (std::memory_order_relaxed); }
    int  effectiveKeyScale() const noexcept { return effKeyScale.load (std::memory_order_relaxed); }

    // Menu choice lists (shared by the layout + the KeyButton config).
    static juce::StringArray keyRootChoices();
    static juce::StringArray keyScaleChoices();   // maps to ScaleId via scaleIdFromChoice
    static juce::StringArray divisionChoices();    // File-Position / LFO tempo grid

    // Map a UI scale-choice index -> ScaleId (Major/Minor/Penta.../HarmMinor).
    static sattari::dsp::ScaleId scaleIdFromChoice (int choice) noexcept;
    // Map the cross-plugin bus scale code (0=major,1=minor,2=chromatic) -> ScaleId.
    static sattari::dsp::ScaleId scaleIdFromBus (int busScale) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    // Raw value of a per-layer param on a specific layer (A_/B_ prefixed).
    float layerParam (Layer layer, const char* baseId) const noexcept;

    // Assemble a layer's EngineParams from its per-layer + the shared globals.
    sattari::dsp::SattariGranularEngine::EngineParams gatherParams (Layer layer) const noexcept;

    // Build the immutable dsp-facing source the engine consumes from an
    // editor-facing source (identical fields, different namespace).
    static std::shared_ptr<const sattari::dsp::SourceSample>
        toDspSource (const std::shared_ptr<const sattari::granulator::SourceSample>& s);

    // Publish a freshly-built editor-facing source into a layer: pushes the old
    // one onto that layer's undo stack, atomic-stores the new pair, and hands the
    // dsp view to that layer's engine. Message thread only.
    void publishSource (Layer layer,
                        std::shared_ptr<const sattari::granulator::SourceSample> next,
                        bool pushUndo);

    // --- Source edit primitives (pure SourceSample -> SourceSample) -----------
    static std::shared_ptr<const sattari::granulator::SourceSample>
        makeReversed (const sattari::granulator::SourceSample& src);
    static std::shared_ptr<const sattari::granulator::SourceSample>
        makeNormalized (const sattari::granulator::SourceSample& src);
    static std::shared_ptr<const sattari::granulator::SourceSample>
        makeTrimmed (const sattari::granulator::SourceSample& src, float start01, float end01);
    static std::shared_ptr<const sattari::granulator::SourceSample>
        makeMonoFold (const sattari::granulator::SourceSample& src);
    // Downmix/pick channels per the srcMono choice (0 Stereo,1 Mono,2 Left,3 Right).
    static std::shared_ptr<const sattari::granulator::SourceSample>
        applyChannelMode (const std::shared_ptr<const sattari::granulator::SourceSample>& src, int mode);

    // Build a SourceSample from a raw interleaved-by-channel capture (msg thread).
    std::shared_ptr<const sattari::granulator::SourceSample>
        buildCaptureSource (const std::vector<float>& mono, int lengthSamples, double captureSr) const;

    // tanh soft-ceiling limiter applied to the blended two-layer mix.
    static float softCeiling (float x) noexcept;

    // =========================================================================
    //  Per-layer state (source slots, engines, undo, viz) — indexed by Layer.
    // =========================================================================
    struct LayerState
    {
        sattari::dsp::SattariGranularEngine engine;

        // Double-buffered immutable source. `msg` is the editor-facing copy;
        // `dsp` is the field-for-field dsp mirror handed to the engine. Both
        // swapped together via std::atomic_store (C++17 free functions).
        std::shared_ptr<const sattari::granulator::SourceSample> sourceMsg;
        std::shared_ptr<const sattari::dsp::SourceSample>        sourceDsp;

        // Bounded undo stack of prior editor-facing sources (message thread only).
        std::vector<std::shared_ptr<const sattari::granulator::SourceSample>> undoStack;

        // Loaded-file bookkeeping for preset recall (path + content hash).
        juce::String sourcePath;
        juce::String sourceHash;
    };
    static constexpr int kMaxUndo = 8;

    LayerState& L (Layer layer) noexcept { return layers[(size_t) layer]; }
    const LayerState& L (Layer layer) const noexcept { return layers[(size_t) layer]; }

    // =========================================================================
    //  Live-capture ring (audio-thread writer / message-thread drainer).
    // =========================================================================
    struct CaptureRing
    {
        // Preallocated in prepareToPlay to the max capture length (8 s * sr).
        std::vector<float> data;                 // mono ring storage
        int   capacity = 0;                      // == data.size()
        std::atomic<int>  writePos { 0 };        // audio-thread write cursor
        std::atomic<int>  filled   { 0 };        // samples written since arm (<= target)
        std::atomic<int>  target   { 0 };        // samples to capture before "done"
        std::atomic<bool> armed    { false };    // audio thread writes while true
        std::atomic<bool> done     { false };    // set by audio thread at target
        std::atomic<int>  targetLayer { 0 };     // Layer to publish into (latched at arm)
        double captureSr = 48000.0;              // sr the ring is recorded at
    } capture;

    // =========================================================================
    //  State
    // =========================================================================
    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "granulator" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    std::array<LayerState, kNumLayers> layers;

    // Preallocated per-layer render scratch (sized in prepareToPlay so the audio
    // thread never allocates). Each layer renders here, then blends into `buffer`.
    juce::AudioBuffer<float> layerScratch;   // 2 x maxBlockSize

    // --- Built-in reverb on the BLENDED two-layer output ---------------------
    // A global Freeverb-based reverb applied AFTER the morph blend and BEFORE the
    // master volume. juce::Reverb scales dry *2 and wet *3 internally, so its
    // dry/wet is not a clean crossfade — we take a PURE wet from it (dryLevel 0)
    // and do our own dry/wet against a saved dry copy, mirroring plugins/space.
    // reverbMix == 0 is then an EXACT pass-through. `reverbDry` holds the pre-verb
    // signal; both are sized in prepareToPlay so the audio thread never allocates.
    juce::Reverb             reverb;
    juce::AudioBuffer<float> reverbDry;      // 2 x maxBlockSize (pre-reverb copy)

    juce::AudioFormatManager formats;

    // Cross-plugin buses + host transport.
    sattari::xplugin::SessionReader sessionReader;   // KEY bus (Follow Song Key)
    sattari::xplugin::TempoReader   tempoReader;     // TEMPO bus (bpm fallback)

    // Lock-free audio -> UI publish of each layer's granulation viz frame.
    std::array<sattari::rt::AtomicSnapshot<sattari::dsp::SattariGranularEngine::GrainVizFrame>,
               kNumLayers> viz;

    // KEY status mirrored for the editor (updated each block).
    std::atomic<bool> followingBus { false };
    std::atomic<int>  effKeyRoot { 0 };
    std::atomic<int>  effKeyScale { 0 };

    // captureArm edge detection (message thread reads the param; audio thread
    // never allocates). The editor pulses captureArm; serviceCapture() reacts.
    bool lastArmParam = false;

    // --- Per-note expression (MPE) drive state (audio thread only) -----------
    // Latest pitch-bend wheel value per MIDI channel, stored as the WHEEL fraction
    // (-1..+1) so the semitone amount is (fraction * bendRange) resolved per block
    // from the `bendRange` param. MPE spreads notes across channels 2..16, so a
    // per-channel bend maps to per-note bend; classic MIDI uses channel 1 only.
    // `mpeMode` (param) selects the default bend range convention: classic 2 st
    // vs. the MPE ±48 st spec — but the actual range comes from the `bendRange`
    // param (default 2 st) so a plain host with the wheel behaves musically.
    // Channel pressure / poly aftertouch map to per-note pressure via
    // setVoicePressure / setPressureAllHeld.
    static constexpr float kMpeBendRangeSemis = 48.0f;   // MPE full-spec range
    std::array<float, 16> channelBendFrac {};            // per-channel wheel -1..+1

    // --- Publish-to-key-bus (broadcastKey) — audio -> message hand-off -------
    // When broadcastKey is on (and NOT following), playing the granulator DRIVES
    // the suite's key: the AUDIO thread resolves the current (key-snapped) root +
    // scale that a note-on would produce and stashes it in these lock-free atomics
    // (NO file I/O on the audio thread). The MESSAGE thread (editor timer, via
    // serviceKeyBroadcast()) reads them, throttles, and calls publishSession so
    // other Sattari plugins follow. broadcastWant* carry the intended pair;
    // broadcastActive gates publishing on "broadcastKey on AND a note is sounding
    // AND not following" (feedback guard). lastBroadcast* / broadcastId live on the
    // message thread and only re-publish when the pair actually changes.
    std::atomic<int>  broadcastWantRoot  { -1 };
    std::atomic<int>  broadcastWantScale { -1 };   // 0=major,1=minor bus code
    std::atomic<bool> broadcastActive    { false };
    int         lastBroadcastRoot  = -1;   // message thread
    int         lastBroadcastScale = -1;   // message thread
    juce::int64 lastBroadcastMs    = 0;    // message thread throttle
    juce::int64 broadcastId = 0;           // monotonically-increasing session id

    // --- Transient bus edge detection (audio thread) -------------------------
    // We poll tempoReader.current().transientId each block; when it advances AND
    // the `transientTrigger` param is on, an external onset fired (Rhythm Genie /
    // Side Chain published) — re-fire the grain cloud on both engines via
    // triggerAccent (strength from `transientSens`) so grains lock to the pulse.
    juce::int64 lastTransientId = 0;

    // Version tag written into saved state so future format changes stay safe.
    static constexpr int kStateVersion = 2;   // 1 == legacy single-layer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGranulatorAudioProcessor)
};
