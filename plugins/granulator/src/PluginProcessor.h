#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/modules/GranulatorModule.h>
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
    using Layer = sattari::dsp::GranulatorModule::Layer;
    static constexpr int kNumLayers = sattari::dsp::GranulatorModule::kNumLayers;

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
    static const char* layerPrefix (Layer layer) noexcept { return sattari::dsp::GranulatorModule::layerPrefix (layer); }

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
    bool isCapturing() const noexcept { return module.captureArmed(); }
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
        return module.vizFor (layer).load();
    }

    // --- KEY story (drives the shared header KeyButton) -----------------------
    bool isFollowingBusKey() const noexcept { return module.isFollowingBusKey(); }
    int  effectiveKeyRoot() const noexcept  { return module.effectiveRoot(); }
    int  effectiveKeyScale() const noexcept { return module.effectiveScale(); }

    // Menu choice lists (shared by the layout + the KeyButton config).
    static juce::StringArray keyRootChoices()  { return sattari::dsp::GranulatorModule::keyRootChoices(); }
    static juce::StringArray keyScaleChoices() { return sattari::dsp::GranulatorModule::keyScaleChoices(); }   // maps to ScaleId via scaleIdFromChoice
    static juce::StringArray divisionChoices() { return sattari::dsp::GranulatorModule::divisionChoices(); }    // File-Position / LFO tempo grid

    // Map a UI scale-choice index -> ScaleId (Major/Minor/Penta.../HarmMinor).
    static sattari::dsp::ScaleId scaleIdFromChoice (int c) noexcept { return sattari::dsp::GranulatorModule::scaleIdFromChoice (c); }
    // Map the cross-plugin bus scale code (0=major,1=minor,2=chromatic) -> ScaleId.
    static sattari::dsp::ScaleId scaleIdFromBus (int b) noexcept { return sattari::dsp::GranulatorModule::scaleIdFromBus (b); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    float layerParam (Layer layer, const char* baseId) const noexcept;

    // Build the immutable dsp-facing source the engine consumes from an
    // editor-facing source (identical fields, different namespace).
    static std::shared_ptr<const sattari::dsp::SourceSample>
        toDspSource (const std::shared_ptr<const sattari::granulator::SourceSample>& s);

    // Publish a freshly-built editor-facing source into a layer: pushes the old
    // one onto that layer's undo stack, keeps the editor copy, and hands the dsp
    // view to the module. Message thread only.
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
    static std::shared_ptr<const sattari::granulator::SourceSample>
        applyChannelMode (const std::shared_ptr<const sattari::granulator::SourceSample>& src, int mode);

    std::shared_ptr<const sattari::granulator::SourceSample>
        buildCaptureSource (const std::vector<float>& mono, int lengthSamples, double captureSr) const;

    // =========================================================================
    //  MESSAGE-THREAD per-layer state. The engine and the dsp-facing source live
    //  in the module; what stays here is everything the audio thread never
    //  touches — the editor's copy, undo history, and file bookkeeping.
    // =========================================================================
    struct LayerMsg
    {
        std::shared_ptr<const sattari::granulator::SourceSample> sourceMsg;
        std::vector<std::shared_ptr<const sattari::granulator::SourceSample>> undoStack;
        juce::String sourcePath;
        juce::String sourceHash;
    };
    static constexpr int kMaxUndo = 8;

    LayerMsg& L (Layer layer) noexcept { return layerMsg[(size_t) layer]; }
    const LayerMsg& L (Layer layer) const noexcept { return layerMsg[(size_t) layer]; }

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "granulator" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and a Stack layer run the SAME engine.
    // Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::GranulatorModule module;

    std::array<LayerMsg, kNumLayers> layerMsg;

    juce::AudioFormatManager formats;

    // captureArm edge detection (message thread reads the param).
    bool lastArmParam = false;

    // Key-broadcast throttle — message thread only, so it stays out of the module.
    int         lastBroadcastRoot  = -1;
    int         lastBroadcastScale = -1;
    juce::int64 lastBroadcastMs    = 0;
    juce::int64 broadcastId        = 0;   // monotonically-increasing session id

    static constexpr int kStateVersion = 2;   // 1 == legacy single-layer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGranulatorAudioProcessor)
};
