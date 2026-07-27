#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariPitchDetector.h>      // YIN detector, ScaleId, diatonic helpers
#include <sattari_audio_core/SattariPsolaShifter.h>       // low-latency formant-preserving detune
#include <sattari_audio_core/SattariEnvelopeFollower.h>   // atmosphere ducking keyed off the lead
#include <sattari_audio_core/SattariCrossPluginBus.h>     // SessionReader / TempoReader (consumer only)
#include <sattari_audio_core/SattariPresetManager.h>      // shared factory-preset bank

#include "../../harmony/src/HarmonyVoicing.h"             // diatonicShift + scaleIdFromChoice/Bus
#include <sattari_audio_core/SattariRbFormant.h>          // hi-fi Rubber Band detune+formant (Studio)

#include <array>
#include <atomic>
#include <vector>

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
    static juce::StringArray modeChoices();          // Studio / Choir / Glass
    static juce::StringArray voiceCountChoices();     // 2 / 4 / 8
    static juce::StringArray engineChoices();         // Live (PSOLA) / Studio (RB)
    static juce::StringArray rootChoices();           // C..B
    static juce::StringArray scaleChoices();          // Major..MinPent (maps to ScaleId)
    static juce::StringArray intervalChoices();       // Oct-down..Oct-up
    static int  intervalDegrees (int choiceIndex) noexcept;   // choice -> diatonic degree

    // --- KEY story (drives the shared header KeyButton) -----------------------
    bool isFollowingBusKey() const noexcept { return followingBus.load (std::memory_order_relaxed); }
    int  effectiveRoot()     const noexcept { return effKeyRoot.load (std::memory_order_relaxed); }
    int  effectiveScale()    const noexcept { return effKeyScale.load (std::memory_order_relaxed); }

    // --- Live viz atomics (audio -> UI) --------------------------------------
    float leadLevelDb()  const noexcept { return leadDb.load (std::memory_order_relaxed); }
    float choirLevelDb() const noexcept { return choirDbAtomic.load (std::memory_order_relaxed); }
    float atmosLevelDb() const noexcept { return atmosDb.load (std::memory_order_relaxed); }
    float duckGainNow()  const noexcept { return duckGr.load (std::memory_order_relaxed); }
    float characterNow() const noexcept { return charAtomic.load (std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    // --- CHARACTER macro ------------------------------------------------------
    // The per-block anchor evaluation (HUMAN 0 -> MASSIVE .5 -> DESTROYED 1). Every
    // "eff*" field is the effective, macro-driven value the DSP actually uses; the
    // user knobs act as ceilings/offsets on top (see resolveCharacter()).
    struct CharacterState
    {
        int   voiceCount   = 4;
        float detuneSpread = 12.0f;   // cents
        float timing       = 16.0f;   // ms
        float humanize     = 0.35f;
        float formantSpread = 2.0f;   // semitones
        float harmonyMix   = 0.5f;
        float alignAmount  = 0.7f;
        float pitchTighten = 0.6f;
        float destroyMix   = 0.0f;
        float drive        = 0.35f;
        float crush        = 0.2f;
        float ringAmt      = 0.15f;
        float trailsMix    = 0.0f;
        float trailReverse = 0.5f;
        float reverbMix    = 0.3f;
        float duckAmount   = 0.6f;
        float width        = 1.2f;
    };
    CharacterState resolveCharacter() const noexcept;

    // Resolve the effective (root, scale) from the followKey param + song-key bus.
    void resolveKey (int& root, sattari::dsp::ScaleId& scale,
                     int& scaleChoiceOut, bool& fromBus) const;

    // tanh soft-ceiling limiter on the bounded final output.
    static float softCeiling (float x) noexcept;

    // =========================================================================
    //  State
    // =========================================================================
    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank { apvts, "glass-choir" };   // after apvts
    int    currentProgram = 0;
    double sampleRate = 44100.0;
    int    preparedBlockSize = 512;

    // --- Lead pitch tracking (YIN) — drives PSOLA grain spacing + harmony MIDI --
    sattari::dsp::PitchDetector detector;
    int   analysisHop = 0, analysisCounter = 0;
    int   leadMidiLive = -1;
    bool  voicedLive = false;
    float leadHzSmoothed = 0.0f;
    double periodSamples = 300.0;   // detected fundamental period (fallback ~150 Hz)

    // --- Per-voice ensemble double engines -----------------------------------
    // Each double has BOTH a PSOLA shifter (Live/Glass, low-latency) and an
    // RbFormant (Studio, hi-fi). Only one is used per block by engine quality; both
    // are prepared up front. Timing-drift random-walk + a delay ring per voice
    // (the Sattari Double pattern).
    struct DoubleVoice
    {
        sattari::dsp::PsolaShifter psola;
        sattari::dsp::RbFormant rb;
        std::vector<float> delay;      // per-voice timing delay ring
        int    writePos = 0;
        double driftPos = 0.0, driftTarget = 0.0;
        int    driftCountdown = 0;
        juce::Random rng;
    };
    std::array<DoubleVoice, kMaxDoubles> doubles;
    int delayLen = 0;

    // --- Harmony voices (diatonic, RbFormant when RB available else PSOLA) -----
    struct HarmonyVoice
    {
        sattari::dsp::PsolaShifter psola;
        sattari::dsp::RbFormant rb;
        double smoothedRatio = 1.0;
        double humanizeCents = 0.0, humanizeTarget = 0.0;
    };
    std::array<HarmonyVoice, kNumHarmony> harmony;
    juce::Random humanizeRng;

    // --- Reverb (Freeverb pure-wet) on the choir -> atmosphere ---------------
    juce::Reverb reverb;

    // --- Ducking follower keyed off |lead| -----------------------------------
    sattari::dsp::EnvelopeFollower duckFollower;

    // --- Imager-style width filters (M/S low-mono on choir + atmosphere) ------
    // 2nd-order Butterworth HP for the elliptical low-mono side strip. One per
    // widened bus (choir, atmosphere).
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0; }
        void setHP2 (double f, double sr) noexcept;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    std::array<Biquad, 2> lowMonoHP;   // [0]=choir, [1]=atmosphere

    // --- Warp-style DESTROY chain state (parallel tap) ------------------------
    // Vowel bandpass resonators (F1/F2), ring-mod phase, bit/SR crush hold.
    struct DestroyBiquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0; }
        void setBandpass (double sr, double freq, double q) noexcept;
        inline float process (float x) noexcept
        {
            const double y = b0 * (double) x + z1;
            z1 = b1 * (double) x - a1 * y + z2;
            z2 = b2 * (double) x - a2 * y;
            return (float) y;
        }
    };
    std::array<DestroyBiquad, 2> destroyF1, destroyF2;   // per channel (L/R)
    std::array<double, 2> destroyRingPhase { { 0.0, 0.0 } };
    std::array<float, 2>  crushHold { { 0.0f, 0.0f } };
    std::array<int, 2>    crushCount { { 0, 0 } };

    // --- Reverse-delay TRAIL (v1 simple; full granular trail = v2) ------------
    // A stereo ring recorded from the choir; a read head walks it in reverse over
    // trailSizeMs, blended by trailsMix.
    std::array<std::vector<float>, 2> trailRing;
    int    trailCap = 0, trailWrite = 0;
    double trailReadPos = 0.0;

    // --- Preallocated per-block scratch (sized in prepareToPlay) --------------
    std::vector<float> monoLead;      // mono-summed lead
    std::vector<float> shiftScratch;  // one voice's shifted signal
    juce::AudioBuffer<float> choirBuf;   // 2 x block — ensemble + harmony sum
    juce::AudioBuffer<float> atmosBuf;   // 2 x block — reverb wet
    juce::AudioBuffer<float> destroyBuf; // 2 x block — parallel destroy tap
    juce::AudioBuffer<float> reverbDry;  // 2 x block — pre-reverb copy for pure-wet mix

    // --- Lead dry-delay ring (aligns lead to max engine latency) --------------
    std::array<std::vector<float>, 2> leadDelay;
    int leadDelayWrite = 0;
    int reportedLatency = 0;

    // --- Cross-plugin buses (consumer only) ----------------------------------
    sattari::xplugin::SessionReader sessionReader;   // song-key bus (Follow Song Key)
    sattari::xplugin::TempoReader   tempoReader;     // tempo bus (synced trail fallback)

    // --- KEY status mirrored for the editor (updated each block) -------------
    std::atomic<bool> followingBus { false };
    std::atomic<int>  effKeyRoot { 0 };
    std::atomic<int>  effKeyScale { 0 };

    // --- Live viz atomics -----------------------------------------------------
    std::atomic<float> leadDb { -100.0f }, choirDbAtomic { -100.0f }, atmosDb { -100.0f };
    std::atomic<float> duckGr { 1.0f }, charAtomic { 0.35f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGlassChoirAudioProcessor)
};
