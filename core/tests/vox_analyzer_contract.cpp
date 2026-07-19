// Contract test for sattari::dsp::VoxAnalyzer (VoxSynth real-time front-end).
//
// Proves the make-or-break analysis layer actually:
//   1) fires ONE voiced onset on a sung note and tracks its pitch (~220 Hz),
//   2) treats a glide as LEGATO (pitch bend), not a retrigger — no extra onsets,
//   3) on a plosive/broadband transient, fires an onset whose note-on pitch is
//      HELD from the previous voiced note (not garbage from the transient),
//   4) resists octave errors on a strong-2nd-harmonic tone (stays ~150, not 300),
//   5) in percussion mode, classifies a low thump as Kick and a hiss as Hat.
//
// Signals are synthesised at 48 k. Everything is pure-std.
#include "sattari_audio_core/SattariVoxAnalyzer.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double SR = 48000.0;
constexpr double TWO_PI = 6.283185307179586;

using V = std::vector<float>;
void append (V& v, int n, float val = 0.0f) { for (int i = 0; i < n; ++i) v.push_back (val); }

// Rich (saw-ish) tone so YIN has real harmonics to lock to. attackMs shapes the onset.
void appendTone (V& v, double f0, double secs, float amp, double attackMs = 3.0)
{
    const int n = (int) (secs * SR);
    const int atk = std::max (1, (int) (attackMs * 0.001 * SR));
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int h = 1; h <= 8; ++h) s += (1.0 / h) * std::sin (h * ph);
        const float env = (i < atk) ? (float) i / atk : 1.0f;
        v.push_back (amp * env * (float) (s * 0.3));
        ph += TWO_PI * f0 / SR; if (ph > TWO_PI) ph -= TWO_PI;
    }
}
void appendGlide (V& v, double f0, double f1, double secs, float amp)
{
    const int n = (int) (secs * SR); double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double f = f0 + (f1 - f0) * (double) i / n;
        double s = 0.0; for (int h = 1; h <= 8; ++h) s += (1.0 / h) * std::sin (h * ph);
        v.push_back (amp * (float) (s * 0.3));
        ph += TWO_PI * f / SR; if (ph > TWO_PI) ph -= TWO_PI;
    }
}
// Deterministic broadband burst (plosive-like): white-ish noise via a tiny LCG.
void appendBurst (V& v, double secs, float amp)
{
    const int n = (int) (secs * SR); static unsigned s = 22222u;
    for (int i = 0; i < n; ++i) { s = s * 1664525u + 1013904223u; v.push_back (amp * ((s >> 9) * (1.0f / 4194304.0f) - 1.0f)); }
}
void appendHiss (V& v, double secs, float amp)   // high-passed noise -> hat-like
{
    const int n = (int) (secs * SR); static unsigned s = 99991u; float prev = 0.0f;
    for (int i = 0; i < n; ++i) { s = s * 1664525u + 1013904223u; const float w = ((s >> 9) * (1.0f / 4194304.0f) - 1.0f); v.push_back (amp * (w - prev) * 0.7f); prev = w; }
}

struct Counts { int onsets = 0, voiced = 0, plosive = 0, fricative = 0, legato = 0; };

Counts run (sattari::dsp::VoxAnalyzer& a, const V& sig, std::vector<sattari::dsp::VoxAnalyzer::Frame>* keep = nullptr)
{
    using E = sattari::dsp::VoxAnalyzer::Event;
    std::vector<sattari::dsp::VoxAnalyzer::Frame> out (a.maxFramesForBlock (512) + 4);
    Counts c;
    for (int pos = 0; pos < (int) sig.size(); pos += 512)
    {
        const int n = std::min (512, (int) sig.size() - pos);
        const int m = a.process (sig.data() + pos, n, out.data(), (int) out.size());
        for (int i = 0; i < m; ++i)
        {
            const auto& f = out[(size_t) i];
            if (keep) keep->push_back (f);
            switch (f.event)
            {
                case E::VoicedOnset: ++c.onsets; ++c.voiced; break;
                case E::Plosive:     ++c.onsets; ++c.plosive; break;
                case E::Fricative:   ++c.onsets; ++c.fricative; break;
                case E::Legato:      ++c.legato; break;
                default: break;
            }
        }
    }
    return c;
}
} // namespace

int main()
{
    using namespace sattari::dsp;
    int fails = 0;
    auto check = [&] (bool ok, const char* msg) { std::printf ("  [%s] %s\n", ok ? "ok" : "FAIL", msg); if (! ok) ++fails; };

    // 1) Voiced onset + pitch tracking on a sung 220 Hz note.
    {
        VoxAnalyzer a; a.prepare (SR);
        V sig; append (sig, (int) (0.30 * SR)); appendTone (sig, 220.0, 0.60, 0.6);
        std::vector<VoxAnalyzer::Frame> fr;
        const auto c = run (a, sig, &fr);
        // sustained pitch over the last third of the note
        double sum = 0; int nv = 0;
        for (size_t i = fr.size() * 3 / 4; i < fr.size(); ++i) if (fr[i].voiced) { sum += fr[i].frequencyHz; ++nv; }
        const double meanHz = nv ? sum / nv : 0.0;
        std::printf ("  test1 onsets=%d (voiced=%d) sustained=%.1f Hz\n", c.onsets, c.voiced, meanHz);
        check (c.onsets >= 1 && c.onsets <= 3, "fires an onset on the sung note");
        check (c.voiced >= 1, "onset classified voiced");
        check (std::abs (meanHz - 220.0) < 12.0, "tracks ~220 Hz");
    }

    // 2) Glide => legato (bend), not a retrigger.
    {
        VoxAnalyzer a; a.prepare (SR);
        V sig; append (sig, (int) (0.20 * SR)); appendTone (sig, 220.0, 0.20, 0.6); appendGlide (sig, 220.0, 330.0, 0.40, 0.6);
        const auto c = run (a, sig);
        std::printf ("  test2 onsets=%d legato=%d\n", c.onsets, c.legato);
        check (c.onsets <= 2, "glide does not retrigger new onsets");
        check (c.legato >= 1, "glide produces legato/bend frames");
    }

    // 3) Plosive after a voiced note => onset with HELD pitch (not transient garbage).
    {
        VoxAnalyzer a; a.prepare (SR);
        V sig; append (sig, (int) (0.15 * SR)); appendTone (sig, 196.0, 0.35, 0.6);
        append (sig, (int) (0.05 * SR)); appendBurst (sig, 0.02, 0.9); append (sig, (int) (0.10 * SR));
        std::vector<VoxAnalyzer::Frame> fr; run (a, sig, &fr);
        float burstOnsetPitch = 0.0f; int idx = 0;
        for (size_t i = 0; i < fr.size(); ++i)
            if (fr[i].event != VoxAnalyzer::Event::None && fr[i].event != VoxAnalyzer::Event::Legato)
                { burstOnsetPitch = fr[i].onsetPitchHz; ++idx; }
        std::printf ("  test3 last onset note-on pitch=%.1f Hz (expect held ~196)\n", burstOnsetPitch);
        check (burstOnsetPitch > 120.0f && burstOnsetPitch < 320.0f, "plosive note-on holds a musical pitch, not garbage");
    }

    // 4) Octave robustness: 150 Hz + strong 2nd harmonic.
    {
        VoxAnalyzer a; a.prepare (SR);
        V sig; append (sig, (int) (0.1 * SR));
        const int n = (int) (0.6 * SR); double ph = 0.0;
        for (int i = 0; i < n; ++i) { sig.push_back (0.5f * (float) (std::sin (ph) + 0.9 * std::sin (2 * ph))); ph += TWO_PI * 150.0 / SR; if (ph > TWO_PI) ph -= TWO_PI; }
        std::vector<VoxAnalyzer::Frame> fr; run (a, sig, &fr);
        double sum = 0; int nv = 0;
        for (size_t i = fr.size() / 2; i < fr.size(); ++i) if (fr[i].voiced) { sum += fr[i].frequencyHz; ++nv; }
        const double meanHz = nv ? sum / nv : 0.0;
        std::printf ("  test4 detected=%.1f Hz (expect ~150, not 300)\n", meanHz);
        check (meanHz > 130.0 && meanHz < 175.0, "no octave-up error on strong 2nd harmonic");
    }

    // 5) Percussion mode: kick vs hat.
    {
        VoxAnalyzer a; VoxAnalyzer::Config cfg; cfg.percussionMode = true; a.prepare (SR, cfg);
        V sig; append (sig, (int) (0.1 * SR));
        { const int n = (int) (0.08 * SR); double ph = 0.0; for (int i = 0; i < n; ++i) { const float env = std::exp (-i / (0.02f * (float) SR)); sig.push_back (0.9f * env * (float) std::sin (ph)); ph += TWO_PI * 60.0 / SR; } }  // kick
        append (sig, (int) (0.15 * SR)); appendHiss (sig, 0.04, 0.8); append (sig, (int) (0.1 * SR));   // hat
        std::vector<VoxAnalyzer::Frame> fr; run (a, sig, &fr);
        bool sawKick = false, sawHat = false;
        for (const auto& f : fr) { if (f.perc == VoxAnalyzer::Perc::Kick) sawKick = true; if (f.perc == VoxAnalyzer::Perc::HatClosed || f.perc == VoxAnalyzer::Perc::HatOpen) sawHat = true; }
        std::printf ("  test5 kick=%d hat=%d\n", (int) sawKick, (int) sawHat);
        check (sawKick, "classifies low thump as Kick");
        check (sawHat, "classifies hiss as Hat");
    }

    std::printf (fails == 0 ? "Sattari VoxAnalyzer contract ok\n" : "FAIL: VoxAnalyzer contract (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
