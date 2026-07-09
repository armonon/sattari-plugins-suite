#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Peak — true-peak-aware brickwall limiter / maximizer
// -----------------------------------------------------------------------------
//  A lookahead brickwall limiter. The dry signal is delayed through a ring
//  buffer while a peak detector (optionally 4x-oversampled to catch inter-
//  sample / "true" peaks, the same detector Sattari Meter uses) scans the
//  window ahead. Whenever an upcoming peak would exceed the ceiling, the gain
//  envelope drops INSTANTLY (over the lookahead) to the exact reduction needed,
//  then recovers with a program-dependent release. Because the reduction is in
//  place before the loud sample reaches the output, |out| <= ceiling is
//  guaranteed; a final true-peak-aware soft ceiling catches any residual
//  inter-sample overshoot so the output never crosses 0 dBFS.
//
//  A one-knob "Target LUFS" mode measures short-term BS.1770 loudness (K-
//  weighted, same as Sattari Meter) and slowly auto-drives the input push so
//  integrated loudness approaches the target while the ceiling still holds.
//
//  Editor polls live gain-reduction, current true-peak and short-term LUFS via
//  lock-free atomics.
// =============================================================================
class SattariPeakAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariPeakAudioProcessor();
    ~SattariPeakAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Peak"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
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

    struct Readout
    {
        float gainReductionDb = 0.0f;   // current attenuation (>= 0)
        float truePeakDb       = -100.0f; // output true-peak
        float shortLufs        = -100.0f; // short-term loudness (out)
        float pushDb           = 0.0f;    // effective push applied (incl. auto)
    };
    Readout getReadout() const noexcept
    {
        Readout r;
        r.gainReductionDb = grDb.load    (std::memory_order_relaxed);
        r.truePeakDb      = tpDb.load    (std::memory_order_relaxed);
        r.shortLufs       = shortLufs.load (std::memory_order_relaxed);
        r.pushDb          = pushDbOut.load (std::memory_order_relaxed);
        return r;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }
    bool  boolParam (const char* id) const noexcept { return param (id) > 0.5f; }

    // K-weighting biquad (identical form to Sattari Meter) for the LUFS engine.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() { x1 = x2 = y1 = y2 = 0; }
        double process (double x) noexcept
        { const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };
    void designKWeighting (double sr);

    // Inter-sample (true) peak of one channel step: 4x linear interpolation
    // between the previous sample and the current one. Matches Sattari Meter.
    static float interSamplePeak (float prev, float cur) noexcept
    {
        float m = std::fabs (cur);
        for (int k = 1; k < 4; ++k)
        {
            const float v = prev + (cur - prev) * (float) k / 4.0f;
            m = std::max (m, std::fabs (v));
        }
        return m;
    }

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    // --- Lookahead delay line + envelope --------------------------------------
    juce::AudioBuffer<float> lookaheadRing;   // stereo dry delay
    std::vector<float>       peakRing;         // detector peak per delayed sample slot
    int   ringLen = 1, ringPos = 0;
    int   maxLookaheadSamples = 0;
    int   reportedLatency = -1;

    float envGain = 1.0f;      // current smoothed limiter gain (<= 1)
    float relCoeff = 0.0f;     // release one-pole coefficient (per block)
    std::array<float, 2> tpHist { { 0.0f, 0.0f } };   // detector history for ISP

    // --- Auto target-LUFS drive -----------------------------------------------
    std::array<Biquad, 2> shelf, hp;   // K-weighting per channel (output metering)
    std::vector<float> zRing;          // 3 s of K-weighted mono-square (short-term)
    int    zCap = 0, zWrite = 0;
    double shortSum = 0.0;
    int    shortLen = 0;
    float  autoPushDb = 0.0f;          // slowly-adapted extra push toward target

    // --- Output metering ------------------------------------------------------
    std::array<float, 2> outTpHist { { 0.0f, 0.0f } };
    float tpMax = 0.0f;

    std::atomic<float> grDb { 0.0f }, tpDb { -100.0f }, shortLufs { -100.0f }, pushDbOut { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPeakAudioProcessor)
};
