#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariVoxAnalyzer.h>
#include <sattari_audio_core/SattariVoxSynthEngine.h>
#include <sattari_audio_core/SattariVocoderBank.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPresetManager.h>
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

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
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
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
    int currentProgram = 0;

    sattari::dsp::VoxAnalyzer   analyzer_;
    sattari::dsp::VoxSynthEngine engine_;
    sattari::dsp::VocoderBank    vocoder_;   // talkbox mode: voice shapes the synth
    std::vector<sattari::dsp::VoxAnalyzer::Frame> frames_;
    std::vector<float> mono_;
    juce::AudioBuffer<float> synthScratch_;

    // MPE / MIDI output state (voice is monophonic -> one active note).
    //
    //  kMpeChannel is the first MEMBER channel of an MPE lower zone; channel 1 is
    //  the zone's master. Bend range is a PARAMETER rather than a constant because
    //  it has to match whatever is receiving: MPE defaults to +/-48 semitones, but
    //  a plain synth defaults to +/-2, and sending 48-scaled bends to a 2-semitone
    //  synth makes every vibrato 24x too wide. We announce the range over RPN so
    //  instruments that listen configure themselves; the parameter is there for
    //  the ones that don't.
    static constexpr int kMpeChannel  = 2;   // 1-based: lower-zone member channel
    static constexpr int kMpeMaster   = 1;   // 1-based: lower-zone master channel
    void emitMpeConfiguration (juce::MidiBuffer& midi, int at, int bendSemis) const;

    int   activeNote_ = -1;
    float bendRangeSemis_ = 48.0f;
    int   lastBendRange_ = -1;      // last announced range; -1 = never announced
    bool  lastMidiMode_ = false;    // re-announce when the user switches into MIDI mode
    int   percHold_ = 0, percNote_ = -1;

    double sr_ = 48000.0;
    std::array<bool, 12> busScale_ { };
    int   busTonic_ = 0;
    std::atomic<LiveReading> live_ { LiveReading{} };
    juce::int64 lastBusPollMs_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVoxSynthAudioProcessor)
};
