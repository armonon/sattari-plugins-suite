#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/ChopModule.h>
#include <sattari_audio_core/SattariOnsetCurve.h>
#include <sattari_audio_core/SattariSliceTable.h>

// =============================================================================
//  Sattari Chop — slicing sampler.
// -----------------------------------------------------------------------------
//  Load a file, detect onsets, slice, play the slices from MIDI. The expensive
//  analysis runs once on load (worker/message thread); the Sensitivity control only
//  re-thresholds the stored onset curve, so re-slicing is cheap enough to be live.
//
//  Realtime contract (spec §5): the audio thread never allocates, locks or does file
//  I/O. The loaded source + its slice list are published as an IMMUTABLE snapshot; the
//  audio thread takes a reference under a try-lock and reads it. Every snapshot is
//  retained for the session, so a buffer is never freed on the audio thread.
//
//  This shell keeps everything on the MESSAGE thread — loading, analysis, slicing,
//  the §3 editing gestures. The playback voices live in sattari::dsp::ChopModule,
//  shared with a Sattari Stack layer, and the two halves meet at setSource().
// =============================================================================
class SattariChopAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariChopAudioProcessor();
    ~SattariChopAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Chop"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // --- Source + slicing (message thread) ------------------------------------
    bool loadFile (const juce::File& file);       // read, analyse, slice, publish
    void reslice();                               // re-threshold the stored curve only
    bool hasSource() const noexcept;
    juce::String sourceName() const;
    int  numSlices() const noexcept;
    double sourceLengthSeconds() const noexcept;

    // Lowest MIDI note; slice i is triggered by kBaseNote + i.
    static constexpr int kBaseNote = sattari::dsp::ChopModule::kBaseNote;   // C1

    // Snapshot for the editor's waveform + markers (message thread reads). The type
    // is the module's, so the hand-off needs no conversion.
    using Source = sattari::dsp::ChopModule::Source;
    Source::Ptr snapshot() const;                 // may be nullptr

    // --- §3 editing gestures (message thread). snap=true applies onset magnetism then a
    // zero-crossing resolve (§3.2); Alt-drag in the editor passes snap=false for free
    // placement (but a boundary always still resolves to a zero crossing to avoid clicks).
    void addSliceAtSample  (std::int64_t sample, bool snap = true);
    void moveSliceNear     (std::int64_t fromSample, std::int64_t toSample, bool snap = true);
    void deleteSliceNear   (std::int64_t sample);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void publish (Source::Ptr s);
    void republishFromTable();                    // rebuild slices from the (edited) table, keep audio
    void rebuildSlicesFromTable (Source& s) const;
    std::int64_t snapToZeroCrossing (const juce::AudioBuffer<float>& b, std::int64_t pos) const;
    std::int64_t onsetSnap (std::int64_t sample, std::int64_t windowSamples) const;  // nearest detected onset

    juce::AudioProcessorValueTreeState apvts;
    double currentSampleRate = 44100.0;

    // Analysis state — message thread only.
    sattari::dsp::OnsetCurve onset;
    sattari::dsp::SliceTable table;
    std::vector<float> monoScratch;
    juce::String loadedPath;

    // The message-thread copy of what was last published. The module holds the
    // audio-thread reference and the session-long retain.
    Source::Ptr current;

    juce::AudioFormatManager formatManager;

    // Thin wrapper: the standalone plugin and a Stack layer run the SAME voices.
    // Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::ChopModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariChopAudioProcessor)
};
