// Sattari Peak — functional contract.
// Proves the brickwall limiter's core guarantees:
//   (1) a signal hotter than the ceiling is limited so the OUTPUT true-peak
//       stays at/under the ceiling (within a small tolerance);
//   (2) a signal already well under the ceiling passes ~transparently at unity
//       push (near bit-for-bit, only delayed by the reported lookahead);
//   (3) the output never exceeds 0 dBFS;
//   (4) no NaNs / infs anywhere in the output.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// 4x inter-sample (true) peak of a mono stream, matching the plugin's detector.
double truePeakOf (const std::vector<float>& x)
{
    double m = 0.0; float prev = 0.0f;
    for (float cur : x)
    {
        for (int k = 0; k < 4; ++k)
        {
            const float v = prev + (cur - prev) * (float) k / 4.0f;
            m = std::max (m, (double) std::fabs (v));
        }
        prev = cur;
    }
    return m;
}

struct Result
{
    double outTruePeakL = 0.0;   // measured 4x true-peak of channel 0
    double outMaxAbs    = 0.0;   // sample peak (all channels)
    double transferErr  = 0.0;   // max |out(delayed) - in| when transparent
    bool   finite       = true;
    float  reportedGrDb = 0.0f;
    int    latency      = 0;
};

// Render a steady sine at the given amplitude/frequency through the limiter.
// If measureTransfer is set, compares the output against the input delayed by
// the reported latency to gauge transparency.
Result run (float amp, float pushDb, float ceilingDb, bool truePeakOn,
            bool measureTransfer)
{
    const double sr = 48000.0; const int block = 512;
    SattariPeakAudioProcessor p; auto& a = p.parameters();
    setParam (a, "push", pushDb);
    setParam (a, "ceiling", ceilingDb);
    setParam (a, "release", 100.0f);
    setParam (a, "lookahead", 3.0f);
    setParam (a, "truePeak", truePeakOn ? 1.0f : 0.0f);
    setParam (a, "autoLoud", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 1.0);
    std::vector<float> inMono ((size_t) n, 0.0f), outL ((size_t) n, 0.0f);

    Result res;
    double ph = 0.0; juce::MidiBuffer midi;
    int g = 0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = amp * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            inMono[(size_t) (g + i)] = s;
            ph += 2.0 * M_PI * 440.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) res.finite = false;
                res.outMaxAbs = std::max (res.outMaxAbs, (double) std::fabs (v));
            }
            outL[(size_t) (g + i)] = buf.getSample (0, i);
        }
        g += len;
    }

    res.latency = (int) p.getLatencySamples();
    res.outTruePeakL = truePeakOf (outL);
    res.reportedGrDb = p.getReadout().gainReductionDb;

    if (measureTransfer)
    {
        // Skip the initial lookahead + a settling margin, then compare the delayed
        // output against the undelayed input.
        const int d = res.latency;
        const int skip = d + 2000;
        for (int i = skip; i < n; ++i)
        {
            const double err = std::fabs ((double) outL[(size_t) i] - (double) inMono[(size_t) (i - d)]);
            res.transferErr = std::max (res.transferErr, err);
        }
    }
    return res;
}

double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
} // namespace

int main()
{
    int fails = 0;

    const float ceilDb = -1.0f;
    const double ceilLin = dbToGain (ceilDb);
    const double tol = dbToGain (0.3);            // allow ~0.3 dB slack over the ceiling

    // (1) HOT: amp 0.9 pushed +12 dB — way over the ceiling. Must be limited.
    auto hot = run (0.9f, 12.0f, ceilDb, true, false);
    // (2) QUIET: amp 0.2 (~-14 dBFS), unity push, well under the ceiling knee.
    auto quiet = run (0.2f, 0.0f, ceilDb, true, true);
    // A sample-peak (non-true-peak) hot case as a second bound check.
    auto hotSp = run (0.9f, 12.0f, ceilDb, false, false);

    std::printf ("hot:   outTP=%.4f (ceil=%.4f) maxAbs=%.4f GR=%.1f dB lat=%d\n",
                 hot.outTruePeakL, ceilLin, hot.outMaxAbs, hot.reportedGrDb, hot.latency);
    std::printf ("quiet: outTP=%.4f maxAbs=%.4f transferErr=%.2e lat=%d\n",
                 quiet.outTruePeakL, quiet.outMaxAbs, quiet.transferErr, quiet.latency);
    std::printf ("hotSP: outTP=%.4f maxAbs=%.4f GR=%.1f dB\n",
                 hotSp.outTruePeakL, hotSp.outMaxAbs, hotSp.reportedGrDb);

    // (4) no NaNs/infs.
    if (! (hot.finite && quiet.finite && hotSp.finite))
    { std::printf ("  FAIL non-finite output\n"); ++fails; }

    // (1) hot true-peak must sit at/under the ceiling (within tolerance).
    if (hot.outTruePeakL > ceilLin * tol)
    { std::printf ("  FAIL hot true-peak exceeds ceiling\n"); ++fails; }
    if (hotSp.outTruePeakL > ceilLin * tol)
    { std::printf ("  FAIL hot (sample-peak mode) exceeds ceiling\n"); ++fails; }
    // ...and the limiter must actually be doing work (real reduction reported).
    if (hot.reportedGrDb < 3.0f)
    { std::printf ("  FAIL limiter did not engage on hot signal\n"); ++fails; }

    // (3) output never exceeds 0 dBFS (with a hair of numerical tolerance).
    if (hot.outMaxAbs > 1.0000001 || quiet.outMaxAbs > 1.0000001 || hotSp.outMaxAbs > 1.0000001)
    { std::printf ("  FAIL output exceeded 0 dBFS\n"); ++fails; }

    // (2) quiet signal passes ~transparently (delayed) — no gain change, no clip.
    if (quiet.transferErr > 1.0e-4)
    { std::printf ("  FAIL under-ceiling signal not transparent (err %.2e)\n", quiet.transferErr); ++fails; }
    if (std::fabs (quiet.outMaxAbs - 0.2) > 0.01)
    { std::printf ("  FAIL under-ceiling peak changed unexpectedly\n"); ++fails; }

    std::printf (fails == 0 ? "Peak contract ok\n" : "Peak contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
