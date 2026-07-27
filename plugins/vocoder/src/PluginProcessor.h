#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/modules/VocoderModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Vocoder — classic carrier-synthesis vocoder
// -----------------------------------------------------------------------------
//  The main input (the vocal) is the MODULATOR. The CARRIER is an internal
//  band-limited poly synth (saw / pulse) whose notes come from EITHER incoming
//  MIDI or the Sattari KEY bus (SessionReader) — so with zero MIDI routing the
//  carrier auto-voices an in-key chord and the vocal "sings" it. A shared
//  SattariKeyButton drives manual / follow key selection.
//
//  DSP: both the modulator and the synthesised carrier are split into N matched
//  log-spaced bandpass bands (biquad bank mirroring Sattari EQ). Each modulator
//  band's amplitude is tracked with the shared EnvelopeFollower; the matching
//  carrier band is multiplied by that envelope and the bands are summed. A
//  high-passed "unvoiced" path from the modulator is mixed back in so consonants
//  (s / t / f) stay intelligible. Everything is preallocated in prepareToPlay;
//  processBlock is allocation- and lock-free (the bus is read from atomics).
// =============================================================================
class SattariVocoderAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariVocoderAudioProcessor();
    ~SattariVocoderAudioProcessor() override = default;   // module stops its own bus reader

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Vocoder"; }
    bool acceptsMidi() const override { return true; }
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

    static juce::StringArray rootChoices()    { return sattari::dsp::VocoderModule::rootChoices(); }
    static juce::StringArray scaleChoices()   { return sattari::dsp::VocoderModule::scaleChoices(); }
    static juce::StringArray waveChoices()    { return sattari::dsp::VocoderModule::waveChoices(); }
    static juce::StringArray voicingChoices() { return sattari::dsp::VocoderModule::voicingChoices(); }

    // Effective key state for the editor's KeyButton.
    using KeyState = sattari::dsp::VocoderModule::KeyState;
    KeyState currentKeyState() const { return module.currentKeyState(); }

    static constexpr int kMaxBands   = 32;
    static constexpr int kMaxCarrier = 6;   // carrier voices (chord) + unisons

    // --- Direct-form-II-transpose biquad (mirrors Sattari EQ) -----------------
    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        void reset() noexcept { x1=x2=y1=y2=0; }
        double process (double x) noexcept
        { const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
        void setBandpass (double f, double Q, double sr) noexcept;
        void setHP (double f, double sr) noexcept;
    };

    // --- Band-limited (polyBLEP) saw / pulse oscillator -----------------------
    struct BlepOsc
    {
        double phase = 0.0, inc = 0.0;
        void setFreq (double hz, double sr) noexcept { inc = (sr > 0.0) ? hz / sr : 0.0; }
        void reset() noexcept { phase = 0.0; }
        // wave: 0 = saw, 1 = pulse (50%). Returns roughly [-1, 1].
        float next (int wave) noexcept;
    };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "vocoder" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Create rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::VocoderModule module;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocoderAudioProcessor)
};
