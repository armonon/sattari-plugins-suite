#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariVoxAnalyzer.h>
#include <sattari_audio_core/SattariVoxSynthEngine.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <atomic>

// =============================================================================
//  Sattari VoxSynth — voice to instrument.
// -----------------------------------------------------------------------------
//  Sing / hum / beatbox in, get an instrument out. Two modes:
//    - Synth  : a continuous-pitch synth voice plays your pitch, with the VCA
//               following your amplitude and the filter following your vowel.
//    - MIDI   : emit MPE (note + per-note bend + pressure + CC74) so any
//               instrument follows your voice, plus a beatbox percussion map.
//  The analysis front-end (SattariVoxAnalyzer) separates onset from pitch so
//  "duh duh" beatboxing and sung "aaah" both track reliably. Quantize snaps to
//  the session-bus key. Allocation-free after prepareToPlay.
// =============================================================================
class SattariVoxSynthAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariVoxSynthAudioProcessor();
    ~SattariVoxSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari VoxSynth"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
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

    struct LiveReading
    {
        bool  voiced = false;
        int   midiNote = -1;
        float cents = 0.0f;
        float amplitude = 0.0f;
    };
    LiveReading getLiveReading() const noexcept { return live_.load(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullBusKey();

    juce::AudioProcessorValueTreeState apvts;

    sattari::dsp::VoxAnalyzer   analyzer_;
    sattari::dsp::VoxSynthEngine engine_;
    std::vector<sattari::dsp::VoxAnalyzer::Frame> frames_;
    std::vector<float> mono_;

    // MPE / MIDI output state (voice is monophonic -> one active note).
    static constexpr int kMpeChannel = 2;   // 1-based
    int   activeNote_ = -1;
    float bendRangeSemis_ = 48.0f;
    int   percHold_ = 0, percNote_ = -1;

    double sr_ = 48000.0;
    std::array<bool, 12> busScale_ { };
    int   busTonic_ = 0;
    std::atomic<LiveReading> live_ { LiveReading{} };
    juce::int64 lastBusPollMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxSynthAudioProcessor)
};
