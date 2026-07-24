#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <array>

// =============================================================================
//  Sattari De-Ess — dynamic sibilance control
// -----------------------------------------------------------------------------
//  Splits a high sibilance band out of the signal (a complementary high-pass, so
//  low + band reconstructs the input exactly) and ducks it whenever its level
//  crosses the threshold. Split mode reduces only the band (transparent); Wideband
//  mode ducks the whole signal like a broadband de-esser. Stereo-linked detection
//  keeps the image stable. Listen solos the sibilance band for easy tuning. Pure
//  reuse of the suite's EnvelopeFollower; allocation-free after prepareToPlay.
// =============================================================================
class SattariDeEssAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariDeEssAudioProcessor();
    ~SattariDeEssAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari De-Ess"; }
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

    // Live gain-reduction (dB, positive number) for the editor meter.
    float getReductionDb() const noexcept { return reductionDb.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    // RBJ biquad. `hp` extracts the sibilance band for DETECTION; `shelf` is a
    // high-shelf used for PROCESSING. Split mode crossfades dry -> shelf by how
    // much sibilance is present, so with no sibilance the output is the exact dry
    // signal (truly transparent) and a shelf only cuts the highs when it fires.
    struct Biquad
    {
        float b0=1, b1=0, b2=0, a1=0, a2=0, x1=0, x2=0, y1=0, y2=0;
        void setHP (double freq, double sr)
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (100.0, sr * 0.45, freq) / sr;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s / (2.0 * 0.707);
            const double a0 = 1.0 + alpha;
            b0 = (float) (((1.0 + c) * 0.5) / a0);
            b1 = (float) ((-(1.0 + c)) / a0);
            b2 = (float) (((1.0 + c) * 0.5) / a0);
            a1 = (float) ((-2.0 * c) / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }
        void setHighShelf (double freq, double gainDb, double sr)
        {
            const double A  = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (100.0, sr * 0.45, freq) / sr;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / 1.0 - 1.0) + 2.0);
            const double tsa = 2.0 * std::sqrt (A) * alpha;
            const double a0 =        (A + 1.0) - (A - 1.0) * c + tsa;
            b0 = (float) ( A * ((A + 1.0) + (A - 1.0) * c + tsa) / a0);
            b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0);
            b2 = (float) ( A * ((A + 1.0) + (A - 1.0) * c - tsa) / a0);
            a1 = (float) ( 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0);
            a2 = (float) (       ((A + 1.0) - (A - 1.0) * c - tsa) / a0);
        }
        void reset() { x1 = x2 = y1 = y2 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "deess" };   // after apvts
    int currentProgram = 0;
    double sampleRate = 44100.0;
    std::array<Biquad, 2> hp, shelf;
    sattari::dsp::EnvelopeFollower env;   // stereo-linked band envelope
    std::atomic<float> reductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariDeEssAudioProcessor)
};
