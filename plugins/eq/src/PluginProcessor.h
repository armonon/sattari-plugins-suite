#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <array>

// =============================================================================
//  Sattari EQ — dynamic parametric EQ
// -----------------------------------------------------------------------------
//  Three fully parametric bells (freq / gain / Q) plus a high-pass and low-pass.
//  Each bell can be static OR dynamic: turn its Dyn up and the band's move only
//  engages when that band is actually loud — a downward dynamic EQ for taming
//  harshness or mud without dulling quiet passages. Dynamic engagement is a
//  per-sample crossfade toward a fixed per-block filter, so there is no zipper
//  from re-computing coefficients. Reuses the suite EnvelopeFollower.
// =============================================================================
class SattariEQAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariEQAudioProcessor();
    ~SattariEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
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

    static constexpr int kBands = 3;

    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        void reset() { x1=x2=y1=y2=0; }
        double process (double x) noexcept
        { const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
        void setPeaking (double f, double gainDb, double Q, double sr);
        void setBandpass (double f, double Q, double sr);
        void setHP (double f, double sr);
        void setLP (double f, double sr);
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "eq" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;

    // Thin wrapper: the standalone plugin and the Mix rack slot run the SAME module.
    sattari::dsp::EqModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariEQAudioProcessor)
};
