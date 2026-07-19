#pragma once

// =============================================================================
//  Sattari Audio Suite — shared musical-key detector (libKeyFinder-class)
// -----------------------------------------------------------------------------
//  Key-profile correlation of a 12-bin chroma against all 24 keys (12 major + 12
//  minor) — the same decision logic libKeyFinder/Mixxx use, minus the FFTW
//  dependency (callers supply the chroma; Auto Pitch already builds one). Pure
//  std so it unit-tests on its own. This sharpens the AutoKey that gets published
//  on the cross-plugin bus, which Harmony and Maqam follow.
//
//  PROFILE CHOICE — chosen by evidence, not fashion. Three published profile sets
//  are implemented (Krumhansl-Schmuckler 1990, Temperley/Kostka-Payne 2007,
//  Aarden-Essen 2003) and benchmarked head-to-head in core/tools/key_detector_-
//  bench.cpp on 24 keys x 6 realistic stress cases (diatonic progressions,
//  borrowed notes, broadband floor, relative-major/minor confusion, dominant-
//  emphasis, plagal/no-leading-tone). Measured result:
//
//        Profile set                Accuracy   MeanMargin
//        Krumhansl-Schmuckler         91.7 %      0.132     <-- default
//        Temperley/Kostka-Payne       83.3 %      0.257
//        Aarden-Essen                 83.3 %      0.215
//
//  Krumhansl-Schmuckler is the accuracy winner on this benchmark (its only miss
//  is the dominant-emphasis case in major keys; the modern profiles additionally
//  miss faint-tonic minor keys, confusing them with the bVI major, and plagal
//  major contexts). The modern profiles were NOT superior here, so K-S stays the
//  default. They carry a larger confidence margin (~2x, less run-to-run flicker)
//  and remain selectable via SATTARI_KEY_PROFILE for callers that prefer that
//  trade. detectKey()/detectKeyRootForMode() signatures are unchanged.
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
    // --- Profile sets ---------------------------------------------------------
    // Three published key-profile pairs are kept here. Pearson correlation is
    // scale-invariant, so the differing magnitudes across sets are irrelevant —
    // only the *shape* matters. The default used by detectKey()/detectKeyRootFor-
    // Mode() is selected by SATTARI_KEY_PROFILE below, chosen by the benchmark in
    // core/tools/key_detector_bench.cpp (see the header note at the top of file).

    // 1) Krumhansl-Schmuckler (1990) — the classic probe-tone profiles. Baseline.
    inline constexpr double kKrumhanslMajor[12] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
    };
    inline constexpr double kKrumhanslMinor[12] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
    };

    // 2) Temperley / Kostka-Payne — profiles fit to a corpus of common-practice
    //    scores (Temperley, "Music and Probability", 2007). Modern, MIR-favoured.
    inline constexpr double kTemperleyMajor[12] = {
        0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400
    };
    inline constexpr double kTemperleyMinor[12] = {
        0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330
    };

    // 3) Aarden-Essen — profiles derived from note-duration statistics of the
    //    Essen folksong collection (Aarden, 2003). Large magnitudes; shape only.
    inline constexpr double kAardenMajor[12] = {
        17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587, 0.291248, 22.062, 0.145624, 8.15494, 0.232998, 4.95122
    };
    inline constexpr double kAardenMinor[12] = {
        18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362, 0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623
    };

    // 4) Sha'ath (2011) — profiles derived specifically for AUDIO key detection
    //    ("Estimating Musical Key and Mode", the KeyFinder profiles). Strong on real
    //    recordings where the literature score-corpus profiles underperform.
    inline constexpr double kShaathMajor[12] = {
        6.6, 2.0, 3.5, 2.3, 4.6, 4.0, 2.5, 5.2, 2.4, 3.7, 2.3, 3.4
    };
    inline constexpr double kShaathMinor[12] = {
        6.5, 2.7, 3.5, 5.4, 2.6, 3.5, 2.5, 5.2, 4.0, 2.7, 4.3, 3.2
    };

    // 5) Bellman-Budge (1970) — note-frequency counts from a corpus; another
    //    audio-favoured alternative that resists the perfect-fifth confusion.
    inline constexpr double kBellmanMajor[12] = {
        16.80, 0.86, 12.95, 1.41, 13.49, 11.93, 1.25, 20.28, 1.80, 8.04, 0.62, 10.57
    };
    inline constexpr double kBellmanMinor[12] = {
        18.16, 0.69, 12.99, 13.34, 1.07, 11.15, 1.38, 21.07, 7.49, 1.53, 0.92, 10.21
    };

    // 6) Empirical — profiles FIT to our own HPCP chroma on the GiantSteps dataset
    //    (mean tonic-aligned chroma over labelled tracks; see core/tools/fit_profiles).
    //    In-domain, so it tracks the real chroma pipeline rather than a foreign corpus.
    inline constexpr double kEmpiricalMajor[12] = {
        0.1018, 0.0726, 0.0862, 0.0759, 0.0882, 0.0864, 0.0745, 0.0980, 0.0741, 0.0822, 0.0781, 0.0819
    };
    inline constexpr double kEmpiricalMinor[12] = {
        0.0943, 0.0783, 0.0842, 0.0865, 0.0787, 0.0835, 0.0777, 0.0940, 0.0812, 0.0785, 0.0848, 0.0784
    };

    // Default profile set: 0=Krumhansl 1=Temperley 2=Aarden 3=Sha'ath 4=Bellman
    // 6=Empirical. Chosen by REAL-DATASET evidence (80 GiantSteps tracks, HPCP chroma,
    // SattariKeyDatasetEval): the old default (Krumhansl) was the WORST for exact key
    // (36 %). For correction, exact key is what matters (a perfect-fifth error tunes to
    // the wrong scale), so Aarden-Essen is the default — best-exact literature profile
    // at 45 % (vs 41 % Sha'ath, 40 % Temperley). Sha'ath has the top MIREX (54, more
    // partial credit for fifths) and an empirical fit reached 47.5 % held-out but is
    // EDM-biased, so both stay selectable rather than default. The old synthetic-bench
    // note (which crowned Krumhansl) was measured on 12 clean tones — not real audio.
    #ifndef SATTARI_KEY_PROFILE
    #define SATTARI_KEY_PROFILE 2
    #endif

    #if   SATTARI_KEY_PROFILE == 0
        inline constexpr const double* kDefaultMajor = kKrumhanslMajor;
        inline constexpr const double* kDefaultMinor = kKrumhanslMinor;
    #elif SATTARI_KEY_PROFILE == 1
        inline constexpr const double* kDefaultMajor = kTemperleyMajor;
        inline constexpr const double* kDefaultMinor = kTemperleyMinor;
    #elif SATTARI_KEY_PROFILE == 3
        inline constexpr const double* kDefaultMajor = kShaathMajor;
        inline constexpr const double* kDefaultMinor = kShaathMinor;
    #elif SATTARI_KEY_PROFILE == 4
        inline constexpr const double* kDefaultMajor = kBellmanMajor;
        inline constexpr const double* kDefaultMinor = kBellmanMinor;
    #elif SATTARI_KEY_PROFILE == 6
        inline constexpr const double* kDefaultMajor = kEmpiricalMajor;
        inline constexpr const double* kDefaultMinor = kEmpiricalMinor;
    #else
        inline constexpr const double* kDefaultMajor = kAardenMajor;
        inline constexpr const double* kDefaultMinor = kAardenMinor;
    #endif

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

    // Parametrised detection core. Both public entry points and the benchmark
    // route through this so every profile set runs the *identical* decision logic
    // (correlation + relative-key tiebreak). `major`/`minor` are 12-bin profiles.
    inline KeyResult detectKeyWithProfiles (const double chroma[12],
                                            const double* major,
                                            const double* minor) noexcept
    {
        double tot = 0.0;
        for (int i = 0; i < 12; ++i) tot += chroma[i];
        KeyResult r;
        if (tot <= 1.0e-9) return r;   // silence / no energy -> invalid

        double best = -2.0, second = -2.0;
        int  bestRoot = 0;   bool bestMinor = false;
        int  secondRoot = 0; bool secondMinor = false;
        auto consider = [&] (double s, int root, bool isMinor)
        {
            if (s > best)        { second = best; secondRoot = bestRoot; secondMinor = bestMinor;
                                   best = s; bestRoot = root; bestMinor = isMinor; }
            else if (s > second) { second = s; secondRoot = root; secondMinor = isMinor; }
        };
        for (int root = 0; root < 12; ++root)
        {
            consider (pearson (chroma, major, root), root, false);
            consider (pearson (chroma, minor, root), root, true);
        }

        // Relative-key tiebreak. A major key and its relative minor (major tonic a
        // minor third above the minor tonic, i.e. major root == (minor root + 3) % 12)
        // share all seven pitch classes, so the profiles score them almost equally
        // and the winner is often the wrong one of the pair. When the top two ARE a
        // relative pair and nearly tied, prefer the mode whose tonic pitch class
        // carries more energy — in real music the tonic is the most-emphasised scale
        // degree, so it disambiguates the pair far better than the raw margin.
        const bool topsAreRelative =
            (bestMinor != secondMinor)
            && ((! bestMinor && bestRoot   == (secondRoot + 3) % 12)   // best major, second its relative minor
                || ( bestMinor && secondRoot == (bestRoot  + 3) % 12)); // best minor, second its relative major
        if (topsAreRelative && (best - second) < 0.06
            && chroma[secondRoot] > chroma[bestRoot] * 1.05)
        {
            const double tb = best; best = second; second = tb;
            const int    tr = bestRoot; bestRoot = secondRoot; secondRoot = tr;
            const bool   tm = bestMinor; bestMinor = secondMinor; secondMinor = tm;
        }

        r.valid = true;
        r.root = bestRoot;
        r.minor = bestMinor;
        r.confidence = best - second;
        return r;
    }
}

// Best root for a fixed mode (this reproduces Auto Pitch's existing AutoKey,
// now shared + tested). Returns the root with the highest profile correlation.
inline int detectKeyRootForMode (const double chroma[12], bool minor) noexcept
{
    const double* profile = minor ? detail::kDefaultMinor : detail::kDefaultMajor;
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
    return detail::detectKeyWithProfiles (chroma, detail::kDefaultMajor, detail::kDefaultMinor);
}

// Bus scale code: 0 = major, 1 = minor (matches the cross-plugin convention).
inline int keyScaleCode (const KeyResult& k) noexcept { return k.minor ? 1 : 0; }

} // namespace sattari::dsp
