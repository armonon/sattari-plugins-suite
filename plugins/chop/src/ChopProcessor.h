#pragma once

#include <JuceHeader.h>
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
    static constexpr int kBaseNote = 36;          // C1

    // Snapshot for the editor's waveform + markers (message thread reads).
    struct Source : juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> audio;
        double sr = 44100.0;
        juce::String name;
        std::vector<std::pair<std::int64_t, std::int64_t>> slices;   // [start,end)
        using Ptr = juce::ReferenceCountedObjectPtr<Source>;
    };
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

    // Published snapshot (audio thread reads under try-lock).
    Source::Ptr current;
    juce::ReferenceCountedArray<Source> retained;
    mutable juce::SpinLock sourceLock;

    juce::AudioFormatManager formatManager;

    // --- Voices ---------------------------------------------------------------
    static constexpr int kMaxVoices = 32;
    struct Voice
    {
        Source::Ptr src;
        int    slice = -1;
        double pos = 0.0;
        float  gain = 1.0f;
        float  env = 0.0f;       // short attack/release ramp, avoids clicks
        bool   releasing = false;
        bool   active = false;
        int    note = -1;
    };
    std::array<Voice, kMaxVoices> voices;
    int voiceCounter = 0;

    void startVoice (int sliceIndex, int note, float velocity);
    void stopVoicesForNote (int note);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariChopAudioProcessor)
};
