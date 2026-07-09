#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/SattariGranularEngine.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId, snapMidiToScale
#include <sattari_audio_core/SattariRealtime.h>          // AtomicSnapshot

#include "SourceSample.h"

#include <atomic>
#include <memory>
#include <vector>

// =============================================================================
//  Sattari Granulator — polyphonic granular-synthesis instrument
// -----------------------------------------------------------------------------
//  IS_SYNTH + NEEDS_MIDI_INPUT. Turns any loaded sample into a playable
//  instrument on top of sattari::dsp::SattariGranularEngine (the header-only DSP
//  core). This processor owns:
//
//    * the ~70-param APVTS layout (slug-prefixed lowerCamel IDs);
//    * an AudioFormatManager and the double-buffered immutable SourceSample —
//      built ONCE on the message thread (file decode, or a pure reverse /
//      normalize / trim / mono-fold rebuild) and published to the audio thread
//      via std::atomic_store on a shared_ptr<const SourceSample> (C++17 free
//      functions). The engine borrows a block-local ref so a mid-block swap never
//      frees a buffer under in-flight grains;
//    * a bounded undo stack of prior source shared_ptrs (message-thread only);
//    * the KEY-bus SessionReader (Follow Song Key) + the TEMPO-bus readTempo
//      fallback + the host AudioPlayHead for tempo-synced File Position scanning;
//    * a lock-free GrainVizFrame published for the editor's live grain sparks.
//
//  processBlock: snapshots the source, pushes an EngineParams POD, feeds bus
//  state (bus scale 0/1/2 -> ScaleId via scaleIdFromBus), then walks the
//  MidiBuffer sample-accurately (engine.noteOn/noteOff + engine.render per
//  sub-slice) and publishes the viz frame.
// =============================================================================
class SattariGranulatorAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariGranulatorAudioProcessor();
    ~SattariGranulatorAudioProcessor() override;

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
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

    // --- Source load / access (message thread) --------------------------------
    // Decode `file` into a fresh SourceSample and atomic-swap it in. Returns
    // false if the file could not be read (the current source is left untouched).
    // Applies the current srcMono downmix at build time.
    bool loadFile (const juce::File& file);

    // The most-recently-published source (may be null before anything is loaded).
    // Editor uses it to (re)build its in-memory thumbnail and to read length for
    // Start/End handle mapping.
    std::shared_ptr<const sattari::granulator::SourceSample> currentSource() const;

    // Human-readable name of the loaded file (empty when none / captured buffer).
    juce::String currentSourceName() const;

    // --- Destructive edit ops (message thread; each a pure rebuild + swap) -----
    void editReverse();          // reverse the whole buffer
    void editNormalize();        // peak-normalise to ~ -0.5 dBFS
    void editTrimToSelection();  // crop to [srcStart, srcEnd]; resets offsets to 0..1
    void editMonoFold();         // sum to a single-channel buffer
    void editUndo();             // pop the last pre-edit source back in
    bool canUndo() const noexcept;

    // --- Live grain visualisation --------------------------------------------
    // Lock-free snapshot published by the audio thread every block. The editor's
    // 30 Hz timer reads this to draw the File-Position cursor + grain sparks.
    sattari::dsp::SattariGranularEngine::GrainVizFrame vizFrame() const noexcept
    {
        return viz.load();
    }

    // --- KEY story (drives the shared header KeyButton) -----------------------
    // The bus-follow status the editor's status dot reflects: true while the
    // followKey param is on AND a fresh key is on the session bus.
    bool isFollowingBusKey() const noexcept { return followingBus.load (std::memory_order_relaxed); }

    // Root (0..11) + scale-choice index actually in effect right now (resolved
    // from the bus when following, else the manual params). scaleChoice indexes
    // keyScaleChoices().
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
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    // Assemble the EngineParams POD from the current APVTS values (audio thread).
    sattari::dsp::SattariGranularEngine::EngineParams gatherParams() const noexcept;

    // Build the immutable dsp-facing source the engine consumes from an APVTS
    // (editor-facing) source (identical fields, different namespace).
    static std::shared_ptr<const sattari::dsp::SourceSample>
        toDspSource (const std::shared_ptr<const sattari::granulator::SourceSample>& s);

    // Publish a freshly-built editor-facing source: pushes the old one onto the
    // undo stack, atomic-stores the new pair (editor + dsp), and hands the dsp
    // view to the engine. Message thread only.
    void publishSource (std::shared_ptr<const sattari::granulator::SourceSample> next,
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

    // =========================================================================
    //  State
    // =========================================================================
    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    sattari::dsp::SattariGranularEngine engine;

    juce::AudioFormatManager formats;

    // Double-buffered immutable source. `sourceMsg` is the editor-facing copy the
    // UI reads; `sourceDsp` is the field-for-field dsp mirror handed to the
    // engine. Both swapped together via std::atomic_store (C++17 free functions).
    std::shared_ptr<const sattari::granulator::SourceSample> sourceMsg;
    std::shared_ptr<const sattari::dsp::SourceSample>        sourceDsp;

    // Bounded undo stack of prior editor-facing sources (message thread only).
    std::vector<std::shared_ptr<const sattari::granulator::SourceSample>> undoStack;
    static constexpr int kMaxUndo = 8;

    // Loaded-file bookkeeping for preset recall (path + content hash).
    juce::String sourcePath;    // absolute path, empty for captured/edited-from-none
    juce::String sourceHash;    // content hash of the loaded file

    // Cross-plugin buses + host transport.
    sattari::xplugin::SessionReader sessionReader;   // KEY bus (Follow Song Key)
    sattari::xplugin::TempoReader   tempoReader;     // TEMPO bus (bpm fallback)

    // Lock-free audio -> UI publish of the granulation viz frame.
    sattari::rt::AtomicSnapshot<sattari::dsp::SattariGranularEngine::GrainVizFrame> viz;

    // KEY status mirrored for the editor (updated each block).
    std::atomic<bool> followingBus { false };
    std::atomic<int>  effKeyRoot { 0 };
    std::atomic<int>  effKeyScale { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGranulatorAudioProcessor)
};
