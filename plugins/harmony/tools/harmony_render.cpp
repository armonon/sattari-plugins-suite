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
    const bool formanttest = (argc > 1 && std::strcmp (argv[1], "--formanttest") == 0);
    const bool bench       = (argc > 1 && std::strcmp (argv[1], "--bench") == 0);
    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 3.0;
    const int    n = (int) std::round (seconds * sr);

    const double leadHz    = 261.63;   // C4
    const double harmonyHz = 392.00;   // G4 — a diatonic 5th above in C major

    // --bench : reported latency and realtime cost, by voice count.
    // Harmony runs one Rubber Band instance PER ENABLED VOICE, so its cost scales
    // with the choir size in a way a single-voice proof cannot show. Printed as a
    // realtime factor (audio seconds rendered per wall second); anything at or
    // below ~1x will not survive live use with that many voices.
    if (bench)
    {
        juce::AudioBuffer<float> tone;
        tone.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.63 * (double) i / sr);
            tone.setSample (0, i, v); tone.setSample (1, i, v);
        }
        std::printf ("Harmony cost by voice count (%.1f s of audio, %.0f Hz, block %d)\n",
                     seconds, sr, block);
        std::printf ("  mode        voices   latency(smp)   wall(s)   realtime\n");
        for (int pass = 0; pass < 2; ++pass)
        for (int voices = 1; voices <= SattariHarmonyAudioProcessor::kNumVoices; ++voices)
        {
            if (pass == 1 && voices != 1 && voices != 3 && voices != 8) continue;
            SattariHarmonyAudioProcessor p2;
            auto& a2 = p2.parameters();
            setParam (a2, "lowLatency", pass == 1 ? 1.0f : 0.0f);
            setParam (a2, "source", 0.0f);  setParam (a2, "followKey", 0.0f);
            setParam (a2, "root", 0.0f);    setParam (a2, "scale", 0.0f);
            setParam (a2, "tune", 1.0f);    setParam (a2, "mix", 1.0f);
            setParam (a2, "choir", 0.0f);   setParam (a2, "humanize", 0.0f);
            for (int v = 1; v <= SattariHarmonyAudioProcessor::kNumVoices; ++v)
                setParam (a2, "v" + juce::String (v) + "on", v <= voices ? 1.0f : 0.0f);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto r  = sattari::harness::renderThrough (p2, tone, sr, block);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            const double wall = (t1 - t0) / 1000.0;
            juce::ignoreUnused (r);
            std::printf ("  %-10s  %5d    %10d   %7.3f   %6.1fx\n",
                         pass == 1 ? "lowLatency" : "standard",
                         voices, p2.getLatencySamples(), wall,
                         wall > 1.0e-6 ? seconds / wall : 0.0);
        }
        std::printf ("Harmony bench ok\n");
        return 0;
    }

    // --formanttest : do the per-voice Formant controls DO anything?
    //
    // This gate exists because they did not. setFormantScale() forwards only to
    // Rubber Band's R3 engine (`if (m_r3)` in RubberBandStretcher.cpp), so while
    // Harmony ran the default R2 engine all eight controls were exposed,
    // automatable, shipped with non-zero defaults, and silently inert - and
    // nothing in the suite could see it, because the other proofs use a SINE,
    // which has no spectral envelope to shift.
    //
    // The test is a difference measurement, which makes it immune to that: render
    // twice, changing ONLY v1formant. A dead parameter produces bit-identical
    // output. A working one cannot. Needs a harmonically rich source, so the
    // input here is a band-limited sawtooth rather than the sine used elsewhere.
    if (formanttest)
    {
        const double leadF = 261.63;   // C4
        juce::AudioBuffer<float> rich;
        rich.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;
            for (int h = 1; h <= 24; ++h)               // band-limited saw: real envelope
            {
                const double f = leadF * (double) h;
                if (f > 0.45 * sr) break;
                v += std::sin (2.0 * juce::MathConstants<double>::pi * f * t) / (double) h;
            }
            const float sMp = (float) (0.25 * v);
            rich.setSample (0, i, sMp);
            rich.setSample (1, i, sMp);
        }

        auto renderWithFormant = [&] (float semis)
        {
            SattariHarmonyAudioProcessor p2;
            auto& a2 = p2.parameters();
            setParam (a2, "source", 0.0f);    setParam (a2, "followKey", 0.0f);
            setParam (a2, "root", 0.0f);      setParam (a2, "scale", 0.0f);
            setParam (a2, "tune", 1.0f);      setParam (a2, "mix", 1.0f);
            setParam (a2, "choir", 0.0f);     setParam (a2, "humanize", 0.0f);
            setParam (a2, "spread", 0.0f);
            setParam (a2, "v1on", 1.0f);      setParam (a2, "v1int", 8.0f);   // a 5th up
            setParam (a2, "v1level", 0.0f);   setParam (a2, "v1pan", 0.0f);
            setParam (a2, "v1formant", semis);
            for (int v = 2; v <= SattariHarmonyAudioProcessor::kNumVoices; ++v)
                setParam (a2, "v" + juce::String (v) + "on", 0.0f);
            return sattari::harness::renderThrough (p2, rich, sr, block);
        };

        const auto flat    = renderWithFormant (0.0f);
        const auto shifted = renderWithFormant (6.0f);

        const int from = (int) (1.0 * sr), len = n - from;
        double maxDiff = 0.0, refPeak = 0.0;
        for (int ch = 0; ch < juce::jmin (flat.getNumChannels(), shifted.getNumChannels()); ++ch)
            for (int i = from; i < from + len; ++i)
            {
                const double a = flat.getSample (ch, i), b = shifted.getSample (ch, i);
                maxDiff = std::max (maxDiff, std::abs (a - b));
                refPeak = std::max (refPeak, std::abs (a));
            }
        const double rel = refPeak > 1.0e-9 ? maxDiff / refPeak : 0.0;

        // Spectral centroid of the steady tail: +6 st of formant must move energy UP.
        auto centroid = [&] (const juce::AudioBuffer<float>& b)
        {
            double num = 0.0, den = 0.0;
            for (int h = 1; h <= 40; ++h)
            {
                const double f = 392.00 * (double) h;      // the harmony note, G4
                if (f > 0.45 * sr) break;
                std::vector<float> mono ((size_t) len);
                for (int i = 0; i < len; ++i)
                    mono[(size_t) i] = 0.5f * (b.getSample (0, from + i) + b.getSample (1, from + i));
                const double amp = goertzelAmp (mono.data(), len, f, sr);
                num += f * amp; den += amp;
            }
            return den > 1.0e-12 ? num / den : 0.0;
        };
        const double cFlat = centroid (flat), cShift = centroid (shifted);

        std::printf ("formant: v1 formant 0 st vs +6 st\n");
        std::printf ("  waveform difference : %.5f (%.2f%% of peak)\n", maxDiff, rel * 100.0);
        std::printf ("  harmony centroid    : %.1f Hz -> %.1f Hz  (%+.1f Hz)\n",
                     cFlat, cShift, cShift - cFlat);

        if (rel < 0.01)
        {
            std::fprintf (stderr,
                "FAIL: changing Formant changed nothing (%.4f%% of peak). The control is\n"
                "      INERT. Most likely the stretcher is not on R3 - setFormantScale()\n"
                "      forwards only to R3 (see RubberBandStretcher.cpp).\n", rel * 100.0);
            return 1;
        }
        if (cShift <= cFlat)
        {
            std::fprintf (stderr,
                "FAIL: +6 st of formant did not move the spectral envelope UP\n"
                "      (%.1f Hz -> %.1f Hz). Check the sign and the pitch compensation.\n",
                cFlat, cShift);
            return 1;
        }
        std::printf ("Harmony formant-control proof ok\n");
        return 0;
    }

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
