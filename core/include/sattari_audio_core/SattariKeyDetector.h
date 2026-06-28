#pragma once

// =============================================================================
//  Sattari Audio Suite — shared musical-key detector (libKeyFinder-class)
// -----------------------------------------------------------------------------
//  Krumhansl-Schmuckler key-profile correlation of a 12-bin chroma against all
//  24 keys (12 major + 12 minor) — the same decision logic libKeyFinder/Mixxx
//  use, minus the FFTW dependency (callers supply the chroma; Auto Pitch already
//  builds one). Pure std so it unit-tests on its own. This sharpens the AutoKey
//  that gets published on the cross-plugin bus, which Harmony and Maqam follow.
// =============================================================================

#include <cmath>

namespace sattari::dsp
{

struct KeyResult
{
    int    root = 0;          // 0..11 (C..B)
    bool   minor = false;     // mode
    double confidence = 0.0;  // margin of the winner over the runner-up (>=0)
    bool   valid = false;
};

namespace detail
{
    inline constexpr double kKrumhanslMajor[12] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
    };
    inline constexpr double kKrumhanslMinor[12] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
    };

    // Pearson correlation of `chroma` (rotated so `root` aligns to the profile
    // tonic) against a 12-bin key profile.
    inline double pearson (const double* chroma, const double* profile, int root) noexcept
    {
        double mH = 0.0, mP = 0.0;
        for (int i = 0; i < 12; ++i) { mH += chroma[i]; mP += profile[i]; }
        mH /= 12.0; mP /= 12.0;
        double num = 0.0, dH = 0.0, dP = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double h = chroma[(i + root) % 12] - mH;
            const double p = profile[i] - mP;
            num += h * p; dH += h * h; dP += p * p;
        }
        const double den = std::sqrt (dH * dP);
        return den > 1.0e-12 ? num / den : -1.0;
    }
}

// Best root for a fixed mode (this reproduces Auto Pitch's existing AutoKey,
// now shared + tested). Returns the root with the highest profile correlation.
inline int detectKeyRootForMode (const double chroma[12], bool minor) noexcept
{
    const double* profile = minor ? detail::kKrumhanslMinor : detail::kKrumhanslMajor;
    double best = -2.0; int bestRoot = 0;
    for (int root = 0; root < 12; ++root)
    {
        const double s = detail::pearson (chroma, profile, root);
        if (s > best) { best = s; bestRoot = root; }
    }
    return bestRoot;
}

// Full libKeyFinder-class detection: the strongest of all 24 keys. `confidence`
// is the margin over the runner-up (bigger = less ambiguous).
inline KeyResult detectKey (const double chroma[12]) noexcept
{
    double tot = 0.0;
    for (int i = 0; i < 12; ++i) tot += chroma[i];
    KeyResult r;
    if (tot <= 1.0e-9) return r;   // silence / no energy -> invalid

    double best = -2.0, second = -2.0;
    int  bestRoot = 0; bool bestMinor = false;
    auto consider = [&] (double s, int root, bool minor)
    {
        if (s > best)        { second = best; best = s; bestRoot = root; bestMinor = minor; }
        else if (s > second) { second = s; }
    };
    for (int root = 0; root < 12; ++root)
    {
        consider (detail::pearson (chroma, detail::kKrumhanslMajor, root), root, false);
        consider (detail::pearson (chroma, detail::kKrumhanslMinor, root), root, true);
    }

    r.valid = true;
    r.root = bestRoot;
    r.minor = bestMinor;
    r.confidence = best - second;
    return r;
}

// Bus scale code: 0 = major, 1 = minor (matches the cross-plugin convention).
inline int keyScaleCode (const KeyResult& k) noexcept { return k.minor ? 1 : 0; }

} // namespace sattari::dsp
