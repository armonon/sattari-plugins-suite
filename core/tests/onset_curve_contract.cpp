// Contract for Chop's offline onset detection (SattariOnsetCurve.h).
//
// Locks the properties §2.2 depends on: the expensive analysis runs ONCE and the
// sensitivity slider only re-thresholds the stored curve; markers land ON transients;
// sensitivity is monotonic (more sensitivity => at least as many markers); one transient
// yields one marker (refractory); and re-thresholding is deterministic.
//
// This proves the ALGORITHM on signals whose onset positions are known exactly. It does
// NOT prove real-world chopping accuracy — the spec is explicit that clean synthetic
// material proves nothing about dense mixes. That is what tools/chop_onset_probe does,
// on real full-length tracks.
#include <sattari_audio_core/SattariOnsetCurve.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}
using sattari::dsp::OnsetCurve;

constexpr double kSr = 44100.0;

// Percussive hits at the given sample positions: fast attack, exponential decay, plus a
// little noise so it is not a pure tone.
std::vector<float> makeHits (const std::vector<std::int64_t>& positions, std::int64_t total)
{
    std::vector<float> x ((std::size_t) total, 0.0f);
    std::uint32_t rng = 12345u;
    auto noise = [&rng] { rng = rng * 1664525u + 1013904223u; return (float) ((rng >> 9) & 0x7fffff) / 4194304.0f - 1.0f; };
    for (auto p : positions)
    {
        const int len = (int) (kSr * 0.12);
        for (int i = 0; i < len; ++i)
        {
            const std::int64_t idx = p + i;
            if (idx >= total) break;
            const float env = std::exp (-(float) i / (float) (kSr * 0.02));
            const float tone = std::sin (2.0f * 3.14159265f * 180.0f * (float) i / (float) kSr);
            x[(std::size_t) idx] += env * (0.7f * tone + 0.3f * noise());
        }
    }
    return x;
}

bool nearAny (std::int64_t pos, const std::vector<std::int64_t>& truth, std::int64_t tol)
{
    for (auto t : truth) if (std::llabs (pos - t) <= tol) return true;
    return false;
}
}

int main()
{
    std::printf ("SattariAudioCore onset-curve contract\n");

    const std::vector<std::int64_t> truth { 10000, 30000, 50000, 70000, 90000 };
    const std::int64_t total = 120000;
    const auto audio = makeHits (truth, total);

    OnsetCurve oc;
    oc.analyze (audio.data(), total, kSr);

    // 1) analyze() produces a curve; hop positions advance.
    {
        check (oc.analysed(), "analyze produces a detection curve");
        check (oc.curve().size() > 100, "curve has one value per hop (" + std::to_string (oc.curve().size()) + ")");
        check (oc.hopPosition (1) > oc.hopPosition (0), "hop positions increase");
    }

    // 2) Markers land ON the transients (within ~15 ms).
    {
        const auto cands = oc.candidates (0.5f);
        const std::int64_t tol = (std::int64_t) (kSr * 0.015);
        int hit = 0;
        for (const auto& c : cands) if (nearAny (c.samplePos, truth, tol)) ++hit;
        check (! cands.empty(), "mid sensitivity finds onsets (" + std::to_string (cands.size()) + ")");
        // Most detections should be real ones, not spurious.
        check (hit * 2 >= (int) cands.size(), "most markers land on actual transients ("
               + std::to_string (hit) + "/" + std::to_string (cands.size()) + ")");
    }

    // 3) Every real transient is found at high sensitivity.
    {
        const auto cands = oc.candidates (0.95f);
        const std::int64_t tol = (std::int64_t) (kSr * 0.020);
        int found = 0;
        for (auto t : truth)
        {
            for (const auto& c : cands)
                if (std::llabs (c.samplePos - t) <= tol) { ++found; break; }
        }
        check (found == (int) truth.size(), "high sensitivity finds all " + std::to_string (truth.size())
               + " transients (found " + std::to_string (found) + ")");
    }

    // 4) Sensitivity is monotonic — the whole point of the live slider.
    {
        const auto lo = oc.candidates (0.05f);
        const auto mid = oc.candidates (0.5f);
        const auto hi = oc.candidates (0.95f);
        check (lo.size() <= mid.size() && mid.size() <= hi.size(),
               "more sensitivity never yields fewer markers ("
               + std::to_string (lo.size()) + " <= " + std::to_string (mid.size())
               + " <= " + std::to_string (hi.size()) + ")");
    }

    // 5) One transient -> one marker (refractory), even wide open.
    {
        const auto cands = oc.candidates (1.0f);
        int clustered = 0;
        for (std::size_t i = 1; i < cands.size(); ++i)
            if (cands[i].samplePos - cands[i - 1].samplePos < (std::int64_t) (kSr * 0.030)) ++clustered;
        check (clustered == 0, "no two markers inside the refractory window (" + std::to_string (clustered) + ")");
    }

    // 6) Re-thresholding is deterministic and does NOT re-analyse.
    {
        const auto a = oc.candidates (0.6f);
        const auto b = oc.candidates (0.6f);
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].samplePos == b[i].samplePos;
        check (same, "same sensitivity -> identical markers");
    }

    // 7) Silence yields no markers at any sensitivity.
    {
        std::vector<float> quiet ((std::size_t) 60000, 0.0f);
        OnsetCurve q;
        q.analyze (quiet.data(), 60000, kSr);
        check (q.candidates (1.0f).empty(), "silence produces no onsets");
    }

    std::printf (failures == 0 ? "onset-curve contract: PASS\n" : "onset-curve contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
