#pragma once

#include <JuceHeader.h>
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
    float getCorrelation() const noexcept { return corr.load (std::memory_order_relaxed); }

private:
    // -- Transposed-direct-form-II biquad (double state). Mirrors plugins/eq. ---
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        double process (double x) noexcept
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void setLP2 (double f, double sr) noexcept;   // 2nd-order Butterworth LP (Q=1/sqrt2)
        void setHP2 (double f, double sr) noexcept;   // 2nd-order Butterworth HP
    };

    // A Linkwitz-Riley 4th-order crossover = two cascaded 2nd-order Butterworths.
    struct LR4
    {
        Biquad a, b;
        void reset() noexcept { a.reset(); b.reset(); }
        double process (double x) noexcept { return b.process (a.process (x)); }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    void registerFactoryPresets();

    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;
public:
    sattari::midi::SattariMidiLearn midiLearn { apvts };
private:
    int currentProgram = 0;
    double sampleRate = 44100.0;

    static constexpr int kBands = 3;   // low / mid / high side bands

    // Side 3-band split. lowLP -> low band; a band-limit of (lowLP..highHP);
    // highHP -> high band. Realised as two LR4 crossovers at loMidHz / midHiHz:
    //   low  = LR4_LP(loMid)
    //   high = LR4_HP(midHi)
    //   mid  = side - low - high   (exact complementary reconstruction)
    LR4 sideLowLP;    // low  = LP at loMid
    LR4 sideHighHP;   // high = HP at midHi

    // Elliptical low-mono: a HP on the SIDE only, so everything below the cutoff
    // stays mono (its side is removed). LR4 for a steep, phase-coherent skirt.
    LR4 lowMonoHP;

    // Haas/ensemble short delay applied to the widened side (adds spread).
    std::vector<float> haasRing;
    int haasCap = 1, haasWrite = 0;

    // Correlation accumulators (mirrors plugins/meter).
    double corrLL = 0.0, corrRR = 0.0, corrLR = 0.0;
    int corrCount = 0;
    std::atomic<float> corr { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariImageAudioProcessor)
};
