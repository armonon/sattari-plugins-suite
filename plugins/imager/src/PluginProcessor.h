#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/ImageModule.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Image — mono-safety-first stereo imager
// -----------------------------------------------------------------------------
//  Encodes L/R to Mid/Side, then shapes ONLY the Side signal so the Mid (mono
//  sum) is never touched — that keeps the image mono-compatible by construction.
//  The Side is split into three bands by Linkwitz-Riley crossovers (mirroring
//  plugins/eq biquads, cascaded to LR4) and each band gets its own width:
//  lows are usually pulled toward mono, highs widened. A global width scales the
//  whole Side on top. An elliptical low-mono filter forces everything below a
//  chosen frequency to mono (bass-safety). Optional Haas/ensemble adds a short
//  inter-channel delay to the Side for extra spread. A mono-audition button sums
//  to mono for checking, and a live L/R correlation readout (mirroring
//  plugins/meter) is exposed for the editor.
//
//  Neutral (all widths = 1, low-mono off, Haas off, no mono-audition) is a
//  bit-exact pass-through: M/S encode then immediate decode is an involution,
//  and each LR4 band-split reconstructs the Side exactly when the band gains are
//  all 1 (complementary LR crossovers sum flat).
// =============================================================================
class SattariImageAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariImageAudioProcessor();
    ~SattariImageAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Image"; }
    bool acceptsMidi() const override { return true; }   // for MIDI Learn CC input
    bool producesMidi() const override { return false; }
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

    // Live L/R correlation (-1..+1), updated ~3x/second for the editor.
    float getCorrelation() const noexcept { return module.currentCorrelation(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::ImageModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariImageAudioProcessor)
};
