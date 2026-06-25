#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include "CleanModule.h"
#include "TuneModule.h"
#include "ControlModule.h"
#include "ToneModule.h"
#include "DeEsserModule.h"
#include "ColorModule.h"
#include "WidthModule.h"
#include "SpaceModule.h"
#include "FinishModule.h"
#include "MacroController.h"

class RoyalChainAudioProcessor final : public juce::AudioProcessor
{
public:
    RoyalChainAudioProcessor();
    ~RoyalChainAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.75; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    float getInputMeter() const { return inputMeter.load(); }
    float getOutputMeter() const { return outputMeter.load(); }
    float getTuneConfidence() const { return tuneConfidence.load(); }
    float getControlMeter() const { return controlMeter.load(); }
    float getSibilanceMeter() const { return sibilanceMeter.load(); }
    float getWidthMeter() const { return widthMeter.load(); }
    float getLimiterMeter() const { return limiterMeter.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateMeter (std::atomic<float>& meter, float target);
    float measurePeak (const juce::AudioBuffer<float>& buffer) const;

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    CleanModule cleanModule;
    TuneModule tuneModule;
    ControlModule controlModule;
    ToneModule toneModule;
    DeEsserModule deEsserModule;
    ColorModule colorModule;
    WidthModule widthModule;
    SpaceModule spaceModule;
    FinishModule finishModule;

    double currentSampleRate = 44100.0;
    std::atomic<float> inputMeter { 0.0f };
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<float> tuneConfidence { 0.0f };
    std::atomic<float> controlMeter { 0.0f };
    std::atomic<float> sibilanceMeter { 0.0f };
    std::atomic<float> widthMeter { 0.0f };
    std::atomic<float> limiterMeter { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainAudioProcessor)
};
