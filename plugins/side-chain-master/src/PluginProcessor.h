#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

class SideChainMasterAudioProcessor final : public juce::AudioProcessor
{
public:
    SideChainMasterAudioProcessor();
    ~SideChainMasterAudioProcessor() override { tempoReader.stop(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { tempoReader.stop(); }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn CC input
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
    const juce::AudioProcessorValueTreeState& parameters() const { return apvts; }

    float getLastGainReductionDb() const { return lastGainReductionDb.load(); }
    bool getLastExternalSidechainAvailable() const { return lastExternalSidechainAvailable.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    sattari::xplugin::TempoReader tempoReader;   // cross-plugin tempo bus (sync-duck fallback)

    float getFloatParam (const char* id) const;
    int getChoiceParam (const char* id) const;
    bool getBoolParam (const char* id) const;
    float nextDetectorSample (const juce::AudioBuffer<float>& detectorBuffer, const juce::AudioBuffer<float>& fallbackBuffer, int sampleIndex, bool useExternal);
    float applyDetectorHighPass (float input, float cutoffHz);

    juce::AudioProcessorValueTreeState apvts;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:

    double currentSampleRate = 44100.0;
    float smoothedGainReductionDb = 0.0f;
    sattari::dsp::EnvelopeFollower detectorFollower;   // rising peak detector
    sattari::dsp::EnvelopeFollower grSmoother;         // falling GR smoother
    float hpX1 = 0.0f;
    float hpY1 = 0.0f;
    std::atomic<float> lastGainReductionDb { 0.0f };
    std::atomic<bool> lastExternalSidechainAvailable { false };

    // Lookahead: the dry/wet signal is delayed so the detector (reading the
    // undelayed sidechain) can start ducking just before the transient lands.
    juce::AudioBuffer<float> lookaheadRing;
    int lookaheadRingPos = 0;
    int lookaheadRingLen = 1;
    int maxLookaheadSamples = 0;
    int reportedLatency = -1;

    // Tempo-synced duck phase (free-runs from host BPM when transport is stopped).
    double syncPhase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SideChainMasterAudioProcessor)
};
