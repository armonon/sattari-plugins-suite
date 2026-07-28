#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/modules/GlassChoirModule.h>
#include <sattari_audio_core/SattariPresetManager.h>

// =============================================================================
//  Sattari Glass Choir — vocal-ensemble EFFECT (audio in, NOT a synth)
// -----------------------------------------------------------------------------
//  Turns one mono/stereo vocal into a lush ensemble by ~90% ASSEMBLY of existing
//  Sattari DSP blocks. Nothing here re-rolls a shifter, a reverb, a follower or a
//  widener — it wires the suite's proven building blocks together:
//
//    * per-voice DOUBLES  : detune + formant + timing-drift + equal-power pan
//                           (the Sattari Double pattern), rendered with RbFormant
//                           (Studio) or the low-latency PsolaShifter (Live/Glass).
//    * key-aware HARMONY  : up to 4 diatonic voices via HarmonyVoicing::diatonicShift,
//                           optionally FOLLOWing the cross-plugin song key.
//    * parallel DESTROY   : a tap through the Warp drive/crush/vowel/ring chain.
//    * ATMOSPHERE         : juce::Reverb pure-wet on the choir, ducked by an
//                           EnvelopeFollower keyed off |lead| (side-chain-master).
//    * mono-safe WIDTH    : Imager-style M/S width on choir + atmosphere (the lead
//                           stays mono-centred), with low-mono + mono-audition.
//    * a simple reverse-delay TRAIL (full granular trails are v2).
//
//  A single CHARACTER macro (HUMAN -> MASSIVE -> DESTROYED) is evaluated per block
//  and scales the underlying params; the user knobs act as ceilings/offsets.
//
//  3-OUTPUT-BUS layout (Lead main + Choir aux + Atmosphere aux). Aux buses are
//  disabled by default; when a host leaves one inactive its signal FOLDS into the
//  Lead so a single-out host loses nothing.
//
//  RT-SAFE: every ring/scratch buffer and every engine (RbFormant / PsolaShifter /
//  Reverb / EnvelopeFollower) is preallocated + prepared in prepareToPlay(); the
//  audio thread only reads params + lock-free atomics. Output is soft-ceiling
//  bounded. Latency is reported honestly (max engine latency; the dry lead is
//  delayed to match).
// =============================================================================
class SattariGlassChoirAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariGlassChoirAudioProcessor();
    ~SattariGlassChoirAudioProcessor() override = default;

    // Engine sizing.
    static constexpr int kMaxDoubles  = 8;   // ensemble voice cap (2/4/8)
    static constexpr int kNumHarmony  = 4;   // diatonic harmony voices
    static constexpr int kMaxRbVoices = 8;   // RB voices only in Studio; else 4

    // --- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Glass Choir"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }   // reverb + trail tail

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

    // =========================================================================
    //  PUBLIC INTERFACE FOR THE EDITOR + CONTRACT
    // =========================================================================

    // APVTS: the editor attaches sliders/combos/buttons to this.
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }

    // Menu choice lists (shared by the layout + the KeyButton config).
    static juce::StringArray modeChoices()       { return sattari::dsp::GlassChoirModule::modeChoices(); }
    static juce::StringArray voiceCountChoices() { return sattari::dsp::GlassChoirModule::voiceCountChoices(); }
    static juce::StringArray engineChoices()     { return sattari::dsp::GlassChoirModule::engineChoices(); }
    static juce::StringArray rootChoices()       { return sattari::dsp::GlassChoirModule::rootChoices(); }
    static juce::StringArray scaleChoices()      { return sattari::dsp::GlassChoirModule::scaleChoices(); }
    static juce::StringArray intervalChoices()   { return sattari::dsp::GlassChoirModule::intervalChoices(); }
    static int  intervalDegrees (int i) noexcept { return sattari::dsp::GlassChoirModule::intervalDegrees (i); }

    // --- KEY story (drives the shared header KeyButton) -----------------------
    bool isFollowingBusKey() const noexcept { return module.isFollowingBusKey(); }
    int  effectiveRoot()     const noexcept { return module.effectiveRoot(); }
    int  effectiveScale()    const noexcept { return module.effectiveScale(); }

    // --- Live viz atomics (audio -> UI) --------------------------------------
    float leadLevelDb()  const noexcept { return module.leadLevelDb(); }
    float choirLevelDb() const noexcept { return module.choirLevelDb(); }
    float atmosLevelDb() const noexcept { return module.atmosLevelDb(); }
    float duckGainNow()  const noexcept { return module.duckGainNow(); }
    float characterNow() const noexcept { return module.characterNow(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "glass-choir" };   // after apvts
    int currentProgram = 0;

    // Thin wrapper: the standalone plugin and the Vocal rack slot run the SAME
    // module. Parameter IDs are unchanged (empty prefix), so sessions survive.
    sattari::dsp::GlassChoirModule module;

    // The Choir / Atmosphere stems. The module writes them only because THIS
    // shell hands them over; a rack slot does not, and gets the folded mix.
    juce::AudioBuffer<float> choirOut, atmosOut;
    int reportedLatencyPublished = -1;   // last value published to the host

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGlassChoirAudioProcessor)
};
