#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <vector>

// =============================================================================
//  Sattari Space — reverb + tempo-synced delay
// -----------------------------------------------------------------------------
//  Fills the suite's time-FX gap. A Freeverb-based reverb (size / damping /
//  width / mix) fed by a tempo-synced stereo delay (note-division time, damped
//  feedback, mix). The delay locks to the host tempo; when the host reports no
//  tempo (standalone, or a transport-less host) it falls back to the Sattari
//  session bus tempo — the consumer side of the cross-plugin tempo bus — then to
//  120 BPM. Allocation-free after prepareToPlay.
// =============================================================================
class SattariSpaceAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariSpaceAudioProcessor();
    ~SattariSpaceAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Space"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

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

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    static juce::StringArray divisionChoices();
    double currentBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    double resolveBpm();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "space" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    juce::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;   // dry(+delay) held for our own reverb mix
    sattari::xplugin::TempoReader tempoReader;   // consumes the cross-plugin tempo bus

    // Tempo-synced stereo delay with damped feedback.
    std::array<std::vector<float>, 2> delayBuf;
    int delayLen = 0, delayWrite = 0;
    std::array<float, 2> fbLpState { { 0.0f, 0.0f } };
    std::atomic<double> lastBpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariSpaceAudioProcessor)
};
