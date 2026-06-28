#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include "MaqamScales.h"
#include <atomic>
#include <memory>
#include <vector>

#if SATTARI_MAQAM_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

// =============================================================================
//  Sattari Maqam — realtime microtonal (quarter-tone) vocal tuner
// -----------------------------------------------------------------------------
//  Corrects a vocal to an Arabic/Persian/Turkish maqam, including the neutral
//  (quarter-tone) degrees that 12-TET tuners can't reach. Same engine lineage as
//  Auto Pitch (shared YIN tracker + Rubber Band, formant-preserved, octave
//  guard) but the correction target is a maqam degree in cents, not a semitone.
//  Root can follow the cross-plugin session key.
// =============================================================================
class SattariMaqamAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariMaqamAudioProcessor();
    ~SattariMaqamAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Maqam"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    static juce::StringArray maqamChoices();
    static juce::StringArray rootChoices();

    float getInputMeter()  const noexcept { return inputMeter.load(); }
    float getOutputMeter() const noexcept { return outputMeter.load(); }

    // Live readout for the editor's maqam ladder (cents within the octave).
    struct Snapshot
    {
        bool   voiced = false;
        double detectedCents = 0.0;   // 0..1200, position of the sung note
        double correctedCents = 0.0;  // 0..1200, snapped maqam degree
        int    maqamIndex = 0;
        int    rootPc = 0;
        bool   followingBus = false;
    };
    Snapshot getSnapshot() const noexcept;

    sattari::midi::SattariMidiLearn midiLearn { apvts };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    int   resolveRoot (bool& fromBus) const;

    juce::AudioProcessorValueTreeState apvts;
    sattari::xplugin::SessionReader sessionReader;
    sattari::dsp::PitchDetector pitchDetector;

    double currentSampleRate = 44100.0;
    int    preparedBlockSize = 512;

    std::vector<float> monoScratch;
    int    analysisHop = 0, analysisCounter = 0;
    float  leadHzSmoothed = 0.0f;
    bool   voicedLive = false;
    double smoothedRatio = 1.0;

    juce::AudioBuffer<float>     wetBuffer;
    juce::dsp::DelayLine<float>  dryDelay { 1 << 16 };
    int    reportedLatency = 0;

    std::atomic<float> inputMeter { 0.0f }, outputMeter { 0.0f };
    std::atomic<int>   snapDetected { 0 }, snapCorrected { 0 };
    std::atomic<bool>  snapVoiced { false }, snapBus { false };
    std::atomic<int>   snapRoot { 0 };

#if SATTARI_MAQAM_HAS_RUBBERBAND
    std::vector<std::unique_ptr<RubberBand::RubberBandStretcher>> stretchers;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMaqamAudioProcessor)
};
