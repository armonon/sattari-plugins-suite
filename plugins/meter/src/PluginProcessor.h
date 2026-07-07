#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
//  Sattari Meter — loudness, true-peak & correlation
// -----------------------------------------------------------------------------
//  A pass-through analyser (audio is never altered) that reports ITU-R BS.1770
//  loudness — momentary (400 ms), short-term (3 s) and an integrated average —
//  plus 4x-oversampled true-peak and L/R correlation. Uses proper K-weighting
//  (a ~+4 dB high-shelf at 1.68 kHz feeding a 38 Hz high-pass). Editor polls the
//  results on a timer.
// =============================================================================
class SattariMeterAudioProcessor final : public juce::AudioProcessor
{
public:
    SattariMeterAudioProcessor();
    ~SattariMeterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Meter"; }
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
        float momentaryLufs = -100.0f, shortLufs = -100.0f, integratedLufs = -100.0f;
        float truePeakDb = -100.0f, correlation = 0.0f;
    };
    Readout getReadout() const noexcept
    {
        Readout r;
        r.momentaryLufs  = momLufs.load (std::memory_order_relaxed);
        r.shortLufs      = shortLufs_.load (std::memory_order_relaxed);
        r.integratedLufs = intLufs.load (std::memory_order_relaxed);
        r.truePeakDb     = tpDb.load (std::memory_order_relaxed);
        r.correlation    = corr.load (std::memory_order_relaxed);
        return r;
    }
    void resetIntegrated() { intResetFlag.store (true, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        void reset() { x1=x2=y1=y2=0; }
        double process (double x) noexcept
        { const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }
    };
    void designKWeighting (double sr);

    juce::AudioProcessorValueTreeState apvts;
    double sampleRate = 44100.0;

    std::array<Biquad, 2> shelf, hp;   // K-weighting, per channel

    // Sliding-window sums of the K-weighted mono-summed square (z^2).
    std::vector<float> zRing;          // 3 s of z^2
    int   zCap = 0, zWrite = 0;
    double momSum = 0.0, shortSum = 0.0;
    int   momLen = 0, shortLen = 0;    // window lengths in samples

    // Integrated (absolute-gated running mean of z^2).
    double intSum = 0.0; long long intCount = 0;
    std::atomic<bool> intResetFlag { false };

    // True-peak (4x oversample) with slow decay + correlation accumulators.
    std::array<float, 2> tpHist { { 0.0f, 0.0f } };
    float tpMax = 0.0f;
    double corrLL = 0.0, corrRR = 0.0, corrLR = 0.0; int corrCount = 0;

    std::atomic<float> momLufs { -100.0f }, shortLufs_ { -100.0f }, intLufs { -100.0f },
                       tpDb { -100.0f }, corr { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMeterAudioProcessor)
};
