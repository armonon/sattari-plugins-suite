// Sub Conjurer — functional contract.
// Proves the synth (1) tracks a low input tone and generates a bounded,
// non-silent SUB-band tone (not mid content), and (2) stays quiet on silence
// (the envelope gate closes — no runaway self-oscillation).
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render `hz` (0 = silence) through Sub Conjurer in sub-only mode; return ch0.
juce::AudioBuffer<float> render (double hz, double seconds, double& peakOut)
{
    const double sr = 48000.0; const int block = 512;
    SubConjurerAudioProcessor p; auto& a = p.parameters();
    setParam (a, "mode", 2.0f);        // Deep
    setParam (a, "tuning", 0.0f);      // Follow Pitch (default is Lock Note)
    setParam (a, "amount", 1.0f);
    setParam (a, "subonly", 1.0f);     // isolate the synthesized sub
    setParam (a, "glide", 0.0f);       // near-instant lock
    setParam (a, "octave", 1.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; peakOut = 0.0;
    juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            if (hz > 0.0) { s = 0.5f * (float) std::sin (ph); ph += 2.0 * M_PI * hz / sr; }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            out.setSample (0, start + i, buf.getSample (0, i));
            out.setSample (1, start + i, buf.getSample (1, i));
            peakOut = std::max (peakOut, (double) std::fabs (buf.getSample (0, i)));
        }
    }
    return out;
}

// RMS + dominant frequency (from zero crossings) over the SECOND half (settled).
void measure (const juce::AudioBuffer<float>& b, double sr, double& rms, double& domHz)
{
    const int n = b.getNumSamples(); const int from = n / 2;
    const float* d = b.getReadPointer (0);
    double sumsq = 0.0; int crossings = 0; float prev = d[from];
    for (int i = from; i < n; ++i)
    {
        sumsq += (double) d[i] * d[i];
        if ((prev < 0.0f && d[i] >= 0.0f) || (prev > 0.0f && d[i] <= 0.0f)) ++crossings;
        prev = d[i];
    }
    const int cnt = n - from;
    rms = cnt ? std::sqrt (sumsq / cnt) : 0.0;
    const double secs = (double) cnt / sr;
    domHz = secs > 0.0 ? (crossings / 2.0) / secs : 0.0;   // half-crossings per second
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && juce::String (argv[1]) == "--render-ui")
    {
        SubConjurerAudioProcessor processor;
        const juce::File destination (argv[2]);
        const bool ok = sattari::test::renderEditorToPng (processor, destination);
        std::printf ("%s %s\n", ok ? "rendered" : "FAIL render",
                     destination.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }
    const double sr = 48000.0;
    int fails = 0;

    // 1) Feed a sustained 60 Hz tone -> a synthesized, bounded, SUB-band tone.
    double peak = 0.0;
    auto sub = render (60.0, 1.2, peak);
    double rms = 0.0, domHz = 0.0; measure (sub, sr, rms, domHz);
    std::printf ("tone60: rms=%.4f domHz=%.1f peak=%.3f\n", rms, domHz, peak);

    bool finite = true;
    for (int i = 0; i < sub.getNumSamples(); ++i) if (! std::isfinite (sub.getSample (0, i))) finite = false;

    if (! finite)        { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (rms < 5.0e-3)    { std::printf ("  FAIL sub silent (envelope never opened?)\n"); ++fails; }
    // Deep mode must follow the source one octave down. This assertion is
    // intentionally narrow enough to catch a detector that silently remains on
    // the default 55 Hz lock note (which produces 27.5 Hz here).
    if (std::abs (domHz - 30.0) > 2.0)
                         { std::printf ("  FAIL pitch follow expected ~30 Hz, got %.1f\n", domHz); ++fails; }
    if (peak > 1.0)      { std::printf ("  FAIL output clips past limiter ceiling\n"); ++fails; }

    // 2) Silence in -> essentially silent out (gate closed, no self-oscillation).
    double speak = 0.0;
    auto sil = render (0.0, 0.8, speak);
    double srms = 0.0, sdom = 0.0; measure (sil, sr, srms, sdom);
    std::printf ("silence: rms=%.5f peak=%.4f\n", srms, speak);
    if (srms > 1.0e-2)   { std::printf ("  FAIL synth runs on silence\n"); ++fails; }

    std::printf (fails == 0 ? "Sub Conjurer contract ok\n" : "Sub Conjurer contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
