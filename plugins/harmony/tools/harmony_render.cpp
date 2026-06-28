// Sattari Harmony — offline render + "the harmony is really there" proof.
//
// Feeds a steady C4 (261.63 Hz) lead, enables a single voice a diatonic 5th
// above (-> G4, 392 Hz), renders through the real path, then MEASURES (Goertzel)
// the energy at 392 Hz: the output must carry strong G4 energy the input never
// had, while the C4 lead survives. Proves a harmony voice is actually generated
// at the correct pitch, not just that it compiles.
//
//   harmony_render          # render + measure + assert (local proof)
//   harmony_render --smoke  # render + finite/non-silent only (CI-safe)
#include "HarmonyProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    else std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}
// Goertzel single-bin amplitude estimate.
double goertzelAmp (const float* x, int n, double freq, double sr)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) { const double s0 = (double) x[i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * std::cos (w);
    const double im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}
}

int main (int argc, char** argv)
{
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);
    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 3.0;
    const int    n = (int) std::round (seconds * sr);

    const double leadHz    = 261.63;   // C4
    const double harmonyHz = 392.00;   // G4 — a diatonic 5th above in C major

    juce::AudioBuffer<float> input;
    input.setSize (2, n, false, false, true);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float s = 0.35f * (float) std::sin (ph);
        input.setSample (0, i, s);
        input.setSample (1, i, s);
        ph += 2.0 * juce::MathConstants<double>::pi * leadHz / sr;
    }

    SattariHarmonyAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "source", 0.0f);      // Scale
    setParam (apvts, "followKey", 0.0f);   // manual key
    setParam (apvts, "root", 0.0f);        // C
    setParam (apvts, "scale", 0.0f);       // major
    setParam (apvts, "tune", 1.0f);
    setParam (apvts, "mix", 1.0f);
    setParam (apvts, "choir", 0.0f);
    setParam (apvts, "humanize", 0.0f);
    setParam (apvts, "spread", 0.0f);
    setParam (apvts, "v1on", 1.0f); setParam (apvts, "v1int", 8.0f); setParam (apvts, "v1level", 0.0f); setParam (apvts, "v1pan", 0.0f);
    setParam (apvts, "v2on", 0.0f);
    setParam (apvts, "v3on", 0.0f);
    setParam (apvts, "v4on", 0.0f);

    const auto out = sattari::harness::renderThrough (proc, input, sr, block);

    bool finite = true; float peak = 0.0f;
    for (int ch = 0; ch < out.getNumChannels() && finite; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const float v = out.getSample (ch, i);
            if (! std::isfinite (v)) { finite = false; break; }
            peak = std::max (peak, std::abs (v));
        }
    if (! finite)       { std::fprintf (stderr, "FAIL: non-finite output\n"); return 1; }
    if (peak < 1.0e-3f) { std::fprintf (stderr, "FAIL: silent output\n"); return 1; }
    if (smoke)          { std::printf ("Harmony render smoke ok (peak %.3f)\n", peak); return 0; }

    // Mono sum of the steady tail.
    const int from = (int) (1.0 * sr), len = n - from;
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i) mono[(size_t) i] = 0.5f * (out.getSample (0, from + i) + out.getSample (1, from + i));

    const double inHarm  = goertzelAmp (input.getReadPointer (0) + from, len, harmonyHz, sr);
    const double outHarm = goertzelAmp (mono.data(), len, harmonyHz, sr);
    const double outLead = goertzelAmp (mono.data(), len, leadHz,    sr);
    std::printf ("Harmony render: G4 in=%.4f out=%.4f, C4 lead out=%.4f\n", inHarm, outHarm, outLead);

    if (outHarm < 0.03)
    { std::fprintf (stderr, "FAIL: no harmony energy at the 5th (%.4f)\n", outHarm); return 1; }
    if (outHarm < 8.0 * (inHarm + 1.0e-6))
    { std::fprintf (stderr, "FAIL: 5th not added by the voice (out %.4f vs in %.4f)\n", outHarm, inHarm); return 1; }
    if (outLead < 0.05)
    { std::fprintf (stderr, "FAIL: lead not preserved (%.4f)\n", outLead); return 1; }

    std::printf ("Harmony 5th-above generation proof ok\n");
    return 0;
}
