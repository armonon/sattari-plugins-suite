#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
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
#include "AudioTap.h"

class RoyalChainAudioProcessor final : public juce::AudioProcessor
{
public:
    RoyalChainAudioProcessor();
    ~RoyalChainAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { sessionReader.stop(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    static constexpr int stageCount = 9;
    static const char* stageName (int);
    static juce::String stageBypassId (int);

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // MIDI Learn
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.75; }

    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets()) { currentProgram = i; presetBank.loadFactoryPreset (i); }
    }
    const juce::String getProgramName (int i) override
    { return juce::isPositiveAndBelow (i, presetBank.numFactoryPresets()) ? presetBank.factoryPresetNames()[i] : juce::String(); }
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
    // Compass feed (relaxed loads; published once per block by processBlock).
    float getPitchHz() const noexcept { return pitchHz.load (std::memory_order_relaxed); }
    int getEffectiveTuneKey() const noexcept { return effectiveTuneKey.load (std::memory_order_relaxed); }
    int getEffectiveTuneScale() const noexcept { return effectiveTuneScale.load (std::memory_order_relaxed); }
    // Lock-free mono tap of the processed output for the Compass water dish.
    const rc::AudioTap& getCompassTap() const noexcept { return compassTap; }
    const rc::AudioTap& getCompassInputTap() const noexcept { return compassInTap; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();
    void updateMeter (std::atomic<float>& meter, float target);
    float measurePeak (const juce::AudioBuffer<float>& buffer) const;
    void processChunk (juce::AudioBuffer<float>&);
    void captureDry (juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, int&);
    template <typename Process> void stage (int index, juce::AudioBuffer<float>& buffer, Process&& process)
    {
        const int count = buffer.getNumSamples();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            stageDry.copyFrom (ch, 0, buffer, ch, 0, count);
        process();
        auto& mix = stageMix[(size_t) index];
        mix.setTargetValue (stageBypass[(size_t) index]->load() >= 0.5f ? 0.0f : 1.0f);
        for (int s = 0; s < count; ++s)
        {
            const float wet = mix.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, s, stageDry.getSample (ch, s) * (1.0f - wet) + buffer.getSample (ch, s) * wet);
        }
    }

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "royal-chain" };   // after apvts
    int currentProgram = 0;
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
    int blockCapacity = 1, originalPosition = 0, tunePosition = 0;
    juce::AudioBuffer<float> stageDry, originalDry, originalDelay, tuneDry, tuneDelay;
    std::array<std::atomic<float>*, stageCount> stageBypass {};
    std::array<juce::SmoothedValue<float>, stageCount> stageMix;
    juce::SmoothedValue<float> comparisonMix, matchGain;
    std::atomic<float>* auditionOriginal = nullptr;
    std::atomic<float>* levelMatch = nullptr;
    double dryPower = 0.0, wetPower = 0.0;

    // Cross-plugin "Sync Key from Auto Pitch": realtime-safe reader (background
    // poll thread + lock-free snapshot; no file I/O on the audio thread).
    sattari::xplugin::SessionReader sessionReader;

    std::atomic<float> inputMeter { 0.0f };
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<float> tuneConfidence { 0.0f };
    std::atomic<float> controlMeter { 0.0f };
    std::atomic<float> sibilanceMeter { 0.0f };
    std::atomic<float> widthMeter { 0.0f };
    std::atomic<float> limiterMeter { 0.0f };
    rc::AudioTap compassTap, compassInTap;
    std::atomic<float> pitchHz { 0.0f };
    std::atomic<int> effectiveTuneKey { 0 }, effectiveTuneScale { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainAudioProcessor)
};
