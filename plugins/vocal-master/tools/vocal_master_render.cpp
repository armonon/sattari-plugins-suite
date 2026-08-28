// Sattari Vocal Master — offline render + "clean the vocal out of the mix" proof.
//
// Builds a stereo signal where the VOCAL is centred (220 Hz, equal L/R) and the
// MUSIC is hard in the sides (660 Hz, anti-phase), runs it through Isolate=1,
// then MEASURES the mid (vocal) and side (music) energy of the output: the music
// must collapse while the vocal survives. Proves the isolate stage actually
// separates the vocal, not just compiles.
//
//   vocal_master_render          # render + measure + assert (local proof)
//   vocal_master_render --smoke  # render + finite/non-silent only (CI-safe)
#include "VocalMasterProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    else std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}
// RMS of the mid and side components over [from, to).
void midSideRms (const juce::AudioBuffer<float>& b, int from, int to, double& mid, double& side)
{
    double m = 0.0, s = 0.0; int cnt = 0;
    for (int i = from; i < to; ++i)
    {
        const float l = b.getSample (0, i), r = b.getSample (1, i);
        const double md = 0.5 * (l + r), sd = 0.5 * (l - r);
        m += md * md; s += sd * sd; ++cnt;
    }
    mid  = cnt ? std::sqrt (m / cnt) : 0.0;
    side = cnt ? std::sqrt (s / cnt) : 0.0;
}
}

int main (int argc, char** argv)
{
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);
    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 2.0;
    const int    n = (int) std::round (seconds * sr);

    juce::AudioBuffer<float> input;
    input.setSize (2, n, false, false, true);
    double pv = 0.0, pm = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float voc = 0.3f * (float) std::sin (pv);   // centred vocal, 220 Hz
        const float mus = 0.3f * (float) std::sin (pm);   // side music, 660 Hz
        input.setSample (0, i, voc + mus);
        input.setSample (1, i, voc - mus);
        pv += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
        pm += 2.0 * juce::MathConstants<double>::pi * 660.0 / sr;
    }

    SattariVocalMasterAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "input", 0.0f);
    setParam (apvts, "isolate", 1.0f);   // full music-down
    setParam (apvts, "denoise", 0.0f);   // isolate-only measurement
    setParam (apvts, "deess", 0.0f);
    setParam (apvts, "tone", 0.0f);
    setParam (apvts, "air", 0.0f);
    setParam (apvts, "level", 0.0f);
    setParam (apvts, "mix", 1.0f);
    setParam (apvts, "output", 0.0f);

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
    if (smoke)          { std::printf ("Vocal Master render smoke ok (peak %.3f)\n", peak); return 0; }

    const int from = (int) (0.5 * sr), to = n;     // steady region
    double inMid, inSide, outMid, outSide;
    midSideRms (input, from, to, inMid, inSide);
    midSideRms (out,   from, to, outMid, outSide);

    const double musicDropDb = 20.0 * std::log10 ((outSide + 1e-12) / (inSide + 1e-12));
    const double vocalKeepDb = 20.0 * std::log10 ((outMid  + 1e-12) / (inMid  + 1e-12));
    std::printf ("Vocal Master isolate: music (side) %.1f dB, vocal (mid) %.1f dB\n", musicDropDb, vocalKeepDb);

    if (musicDropDb > -16.0)
    { std::fprintf (stderr, "FAIL: music not removed (only %.1f dB down)\n", musicDropDb); return 1; }
    if (vocalKeepDb < -3.0)
    { std::fprintf (stderr, "FAIL: vocal not preserved (%.1f dB)\n", vocalKeepDb); return 1; }

    std::printf ("Vocal Master isolate proof ok\n");
    return 0;
}
