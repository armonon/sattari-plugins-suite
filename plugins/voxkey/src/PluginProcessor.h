#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariCrepePitch.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PitchDetector.h"
#include "VoiceMidiMapper.h"
#include <atomic>
#include <array>
#include <thread>

class SattariVoxKeyAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariVoxKeyAudioProcessor();
    ~SattariVoxKeyAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override
    {
        sessionReader.stop();
        crepeThreadRun.store (false);
        if (crepeThread.joinable()) crepeThread.join();
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    VoxKeyVoiceMidiMapper::State getDisplayState() const;
    void panic();

    // Editor feedback: is the cross-plugin bus currently driving the key? Writes
    // the effective root (followed root when true, manual "key" param otherwise)
    // so the editor can grey the Key menu and show the followed root — the bus
    // override is never silent.
    bool followingBusKey (int& rootOut) const noexcept
    {
        if (apvts.getRawParameterValue ("followKey")->load() > 0.5f)
            if (auto s = sessionReader.current(); s.valid)
            {
                rootOut = juce::jlimit (0, 11, s.root);
                return true;
            }
        rootOut = juce::jlimit (0, 11, (int) *apvts.getRawParameterValue ("key"));
        return false;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    VoxKeyVoiceMidiMapper::Settings readSettings() const;
    void publishDisplayState (const VoxKeyVoiceMidiMapper::State& state);
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "voxkey" };   // after apvts
    int currentProgram = 0;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    sattari::xplugin::SessionReader sessionReader;   // cross-plugin "Follow Song Key"
    VoxKeyPitchDetector pitchDetector;
    VoxKeyVoiceMidiMapper midiMapper;

    // --- Optional CREPE neural pitch (ONNX, model-optional) ------------------
    // Voice->MIDI lives on pitch accuracy; when a crepe.onnx model is present and
    // "Neural Pitch" is on, its estimate replaces YIN's (no octave jumps on
    // breathy vocals). Inference runs on a worker thread; the audio thread only
    // writes the lock-free ring and reads result atomics. Absent a model this is
    // inert and YIN stands — zero new behaviour.
    sattari::dsp::CrepePitchTracker crepe;
    bool  crepeModelPresent = false;
    std::thread        crepeThread;
    std::atomic<bool>  crepeThreadRun { false };
    std::atomic<float> crepeResultHz { 0.0f };
    std::atomic<float> crepeResultConf { 0.0f };
    std::atomic<bool>  crepeResultVoiced { false };
    void crepeWorkerLoop();
    juce::AudioBuffer<float> monoScratch;
    std::atomic<bool> panicRequested { false };

    // --- Paraphonic (HPS) detector (alpha) -----------------------------------
    static constexpr int paraFftOrder = 12;            // 4096
    static constexpr int paraFftSize  = 1 << paraFftOrder;
    std::unique_ptr<juce::dsp::FFT> paraFft;
    std::vector<float> paraRing, paraFftBuf, paraHann;
    int paraWritePos = 0, paraSamplesSinceFft = 0;
    std::array<int, 3>  paraCurrentNotes { -1, -1, -1 };
    std::array<int, 3>  paraDetectFrame (double sr);  // returns up to 3 MIDI notes (-1 = none)
    void               paraProcessBlock (const float* mono, int n, double sr, juce::MidiBuffer& midi);
    int                paraFrameCounter = 0;
    std::array<int, 3>  paraActiveNotes { -1, -1, -1 };  // currently-sounding outputs

    std::atomic<float> displayRms { 0.0f };
    std::atomic<float> displayConfidence { 0.0f };
    std::atomic<int> displayDetectedNote { -1 };
    std::atomic<int> displayOutputNote { -1 };
    std::atomic<bool> displayVoiced { false };
    std::atomic<bool> displayMidiActive { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxKeyAudioProcessor)
};
