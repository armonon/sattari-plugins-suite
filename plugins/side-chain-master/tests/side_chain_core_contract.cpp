// Side Chain Master — ducking contract.
//
// Enables the external sidechain bus, feeds a steady tone on the main input and
// a gated loud/quiet signal on the sidechain, and proves the duck engages: the
// main output is measurably attenuated while the sidechain is loud (above
// threshold) versus while it is quiet. No ears required.
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

// Run `blocks` blocks with a steady main tone and a sidechain at `scGain`.
// Returns the main-output RMS of the final (settled) block.
double runDuck (SideChainMasterAudioProcessor& proc, double sr, int block, int blocks,
                int totalInputChannels, double& phase, float scGain)
{
    juce::AudioBuffer<float> last (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (totalInputChannels, block);
        buffer.clear();
        for (int i = 0; i < block; ++i)
        {
            const float main = 0.30f * (float) std::sin (phase);
            const float sc   = scGain * (float) std::sin (phase * 1.5);
            phase += juce::MathConstants<double>::twoPi * 220.0 / sr;
            buffer.setSample (0, i, main);
            buffer.setSample (1, i, main);
            if (totalInputChannels >= 4)
            {
                buffer.setSample (2, i, sc);
                buffer.setSample (3, i, sc);
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        if (b == blocks - 1)
            last.makeCopyOf (buffer, true);
    }

    double s = 0.0;
    for (int i = 0; i < block; ++i)
    {
        const double l = last.getSample (0, i);
        s += l * l;
    }
    return std::sqrt (s / block);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const double sr = 48000.0;
    const int block = 512;

    SideChainMasterAudioProcessor proc;

    // Enable the (default-disabled) external sidechain input bus.
    auto layout = proc.getBusesLayout();
    if (layout.inputBuses.size() > 1)
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    const bool layoutOk = proc.setBusesLayout (layout);

    proc.prepareToPlay (sr, block);
    const int totalIn = proc.getTotalNumInputChannels();
    std::printf ("sidechain bus enabled=%d, totalInputChannels=%d\n", (int) layoutOk, totalIn);
    if (totalIn < 4)
    {
        std::fprintf (stderr, "FAIL: could not enable external sidechain bus (totalIn=%d)\n", totalIn);
        return 1;
    }

    auto& apvts = proc.parameters();
    setParam (apvts, "external", 1.0f);    // use external sidechain
    setParam (apvts, "mode", 0.0f);        // standard duck
    setParam (apvts, "threshold", -30.0f); // well below the SC level so it engages
    setParam (apvts, "ratio", 8.0f);
    setParam (apvts, "attack", 2.0f);
    setParam (apvts, "release", 120.0f);
    setParam (apvts, "depth", 18.0f);
    setParam (apvts, "listen", 0.0f);

    double phase = 0.0;
    // Warm up, then measure quiet-SC, then loud-SC (settled each time).
    runDuck (proc, sr, block, 10, totalIn, phase, 0.0f);
    const double quietOut = runDuck (proc, sr, block, 30, totalIn, phase, 0.0f);
    const double loudOut  = runDuck (proc, sr, block, 30, totalIn, phase, 0.9f);

    const double ratioDb = 20.0 * std::log10 ((loudOut + 1e-12) / (quietOut + 1e-12));
    std::printf ("main out RMS: quiet-SC=%.4f  loud-SC=%.4f  duck=%.1f dB\n",
                 quietOut, loudOut, ratioDb);

    // Expect at least ~4 dB of ducking when the sidechain goes loud.
    if (loudOut >= quietOut * 0.63)
    {
        std::fprintf (stderr, "FAIL: sidechain did not duck the main output (duck=%.1f dB)\n", ratioDb);
        return 1;
    }

    std::printf ("Side Chain Master ducking contract ok\n");
    return 0;
}
