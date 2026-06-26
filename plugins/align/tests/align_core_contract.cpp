// Sattari Align — alignment contract.
//
// Enables the Guide sidechain bus and feeds matching click trains on Main and
// Guide. Proves the cross-correlation engine:
//   1. With Main and Guide aligned: Guide is seen active, confidence is high,
//      and the detected offset settles near zero.
//   2. With Guide delayed by a known amount: the detected offset moves toward
//      that delay (within a generous tolerance).
// No ears required.
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

constexpr int   kClickPeriod = 2400;   // 50 ms @ 48 kHz
constexpr int   kClickWidth  = 8;

float clickAt (long long globalSample, int delaySamples)
{
    const long long s = globalSample - delaySamples;
    if (s < 0) return 0.0f;
    const long long phase = s % kClickPeriod;
    return (phase < kClickWidth) ? 0.8f : 0.0f;
}

// Run `seconds` of audio; Guide delayed by `guideDelay` samples relative to Main.
// Returns the final live snapshot.
SattariAlignAudioProcessor::LiveSnapshot
run (SattariAlignAudioProcessor& proc, double sr, int block, double seconds,
     int totalIn, long long& globalSample, int guideDelay)
{
    const int blocks = (int) std::ceil (seconds * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (totalIn, block);
        buffer.clear();
        for (int i = 0; i < block; ++i)
        {
            const float main  = clickAt (globalSample, 0);
            const float guide = clickAt (globalSample, guideDelay);
            buffer.setSample (0, i, main);
            buffer.setSample (1, i, main);
            if (totalIn >= 4)
            {
                buffer.setSample (2, i, guide);
                buffer.setSample (3, i, guide);
            }
            ++globalSample;
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
    }
    return proc.getLiveSnapshot();
}
} // namespace

int main()
{
    const double sr = 48000.0;
    const int block = 512;

    SattariAlignAudioProcessor proc;

    // Enable the (default-disabled) Guide input bus.
    auto layout = proc.getBusesLayout();
    if (layout.inputBuses.size() > 1)
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    proc.setBusesLayout (layout);
    proc.prepareToPlay (sr, block);

    const int totalIn = proc.getTotalNumInputChannels();
    std::printf ("guide bus: totalInputChannels=%d\n", totalIn);
    if (totalIn < 4)
    {
        std::fprintf (stderr, "FAIL: could not enable Guide bus (totalIn=%d)\n", totalIn);
        return 1;
    }

    auto& apvts = proc.parameters();
    setParam (apvts, "autoAlign", 1.0f);
    setParam (apvts, "lock", 0.0f);
    setParam (apvts, "bypass", 0.0f);
    setParam (apvts, "algorithm", 0.0f);     // Bulk Shift (v1)
    setParam (apvts, "searchRangeMs", 40.0f);
    setParam (apvts, "smoothing", 0.2f);

    long long globalSample = 0;

    // --- Phase 1: aligned ----------------------------------------------------
    const auto aligned = run (proc, sr, block, 5.0, totalIn, globalSample, 0);
    std::printf ("aligned:  guideActive=%d confidence=%.3f offset=%.2f ms\n",
                 (int) aligned.guideActive, aligned.confidence, aligned.detectedOffsetMs);
    int fails = 0;
    if (! aligned.guideActive)        { std::fprintf (stderr, "FAIL: Guide not seen active\n"); ++fails; }
    if (aligned.confidence < 0.3f)    { std::fprintf (stderr, "FAIL: low confidence on aligned guide\n"); ++fails; }
    if (std::fabs (aligned.detectedOffsetMs) > 3.0f)
        { std::fprintf (stderr, "FAIL: aligned offset not near zero (%.2f ms)\n", aligned.detectedOffsetMs); ++fails; }

    // --- Phase 2: Guide delayed by 5 ms (240 samples) ------------------------
    const int delaySamples = 240;
    const float trueDelayMs = (float) (delaySamples / sr * 1000.0);
    const auto delayed = run (proc, sr, block, 6.0, totalIn, globalSample, delaySamples);
    std::printf ("delayed:  offset=%.2f ms (true delay %.2f ms)\n",
                 delayed.detectedOffsetMs, trueDelayMs);
    // The engine should respond: offset moves meaningfully and lands near the
    // true delay (generous tolerance — this is a settling estimate).
    if (std::fabs (delayed.detectedOffsetMs) < std::fabs (aligned.detectedOffsetMs) + 1.0f)
        { std::fprintf (stderr, "FAIL: offset did not respond to introduced delay\n"); ++fails; }
    if (std::fabs (std::fabs (delayed.detectedOffsetMs) - trueDelayMs) > 4.0f)
        { std::fprintf (stderr, "FAIL: detected offset not near true delay\n"); ++fails; }

    if (fails != 0) { std::fprintf (stderr, "Align contract FAILED (%d)\n", fails); return 1; }
    std::printf ("Align alignment contract ok\n");
    return 0;
}
