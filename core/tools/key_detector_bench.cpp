// =============================================================================
//  Key-profile benchmark — evidence for the default profile in SattariKeyDetector
// -----------------------------------------------------------------------------
//  Builds REALISTIC synthetic chromas (weighted pitch-class histograms — exactly
//  what the plugins' chromagram produces) for all 24 keys and several stress
//  cases, then scores each published profile set (Krumhansl-Schmuckler,
//  Temperley/Kostka-Payne, Aarden-Essen) on:
//     * accuracy  = % of (key x case) trials whose root AND mode are correct
//     * margin    = mean winner-vs-runner-up confidence on the correct trials
//                   (bigger margin => less run-to-run flicker in the plugin)
//
//  Every profile runs the IDENTICAL decision logic via detail::detectKeyWith-
//  Profiles(), so the only variable is the profile shape.
//
//  Pure std; no JUCE. Compile + run standalone, e.g.:
//     clang++ -std=c++17 -O2 -Icore/include \
//         core/tools/key_detector_bench.cpp -o /tmp/keybench && /tmp/keybench
// =============================================================================

#include <sattari_audio_core/SattariKeyDetector.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using Chroma = std::array<double, 12>;

namespace
{
static const char* kNoteNames[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Add a triad (three pitch classes, given relative to the key tonic) into the
// histogram, with the chord root a touch louder — mirrors how a real chromagram
// integrates chord energy. `dur` scales the whole chord (its metric weight).
void addTriad (Chroma& h, int tonic, int r, int third, int fifth, double dur)
{
    h[(tonic + r)     % 12] += dur * 1.25;   // chord root — most energy
    h[(tonic + third) % 12] += dur * 1.00;
    h[(tonic + fifth) % 12] += dur * 0.90;
}

// Build the base diatonic-progression chroma for a key.
//   Major progression:  I - IV - V - vi - ii - V - I   (weights = metric length)
//   Minor progression:  i - iv - V - VI - ii° - V - i   (harmonic-minor V => leading tone)
Chroma progression (int tonic, bool minor)
{
    Chroma h{}; h.fill (0.0);
    // metric weights: cadential I/i and the dominant get more time.
    if (! minor)
    {
        addTriad (h, tonic, 0, 4, 7,  2.0);  // I
        addTriad (h, tonic, 5, 9, 0,  1.0);  // IV
        addTriad (h, tonic, 7, 11, 2, 1.5);  // V
        addTriad (h, tonic, 9, 0, 4,  1.0);  // vi
        addTriad (h, tonic, 2, 5, 9,  1.0);  // ii
        addTriad (h, tonic, 7, 11, 2, 1.5);  // V
        addTriad (h, tonic, 0, 4, 7,  2.0);  // I
    }
    else
    {
        addTriad (h, tonic, 0, 3, 7,   2.0);  // i
        addTriad (h, tonic, 5, 8, 0,   1.0);  // iv
        addTriad (h, tonic, 7, 11, 2,  1.5);  // V (harmonic — raised 7th = tonic+11)
        addTriad (h, tonic, 8, 0, 3,   1.0);  // VI
        addTriad (h, tonic, 2, 5, 8,   1.0);  // ii° (natural — b6 = tonic+8)
        addTriad (h, tonic, 7, 11, 2,  1.5);  // V
        addTriad (h, tonic, 0, 3, 7,   2.0);  // i
    }
    return h;
}

// Case (b): sprinkle a couple of borrowed / non-diatonic notes at low weight.
Chroma withBorrowed (Chroma h, int tonic, bool minor)
{
    if (! minor)
    {
        h[(tonic + 10) % 12] += 0.35;  // b7 (mixolydian colour / secondary dominant)
        h[(tonic + 3)  % 12] += 0.30;  // b3 (borrowed from parallel minor / blue note)
        h[(tonic + 6)  % 12] += 0.20;  // #4 (secondary-dominant leading tone)
    }
    else
    {
        h[(tonic + 9)  % 12] += 0.35;  // natural 6 (dorian / melodic-minor ascending)
        h[(tonic + 4)  % 12] += 0.30;  // natural 3 (picardy / borrowed major third)
        h[(tonic + 1)  % 12] += 0.20;  // b2 (neapolitan colour)
    }
    return h;
}

// Case (c): mild broadband floor (bleed/reverb/noise) + a slightly off-diagonal
// emphasis (a non-tonic scale degree pushed up, e.g. a prominent melody note).
Chroma withFloorAndSkew (Chroma h, int tonic, bool minor)
{
    double tot = 0.0; for (double v : h) tot += v;
    const double floor = 0.05 * tot / 12.0;      // ~5% of energy spread flat
    for (double& v : h) v += floor;
    // off-diagonal emphasis: push up the 5th (a very common sustained melody/pedal tone)
    h[(tonic + 7) % 12] += 0.30 * (tot / 12.0);
    (void) minor;
    return h;
}

// Case (d): a genuinely relative-confusing chroma. Take the diatonic scale of
// the key with only faint tonic emphasis, so the relative major/minor scores are
// nearly tied and only the tonic-weighting disambiguation can pick the right one.
Chroma relativeConfusion (int tonic, bool minor)
{
    Chroma h{}; h.fill (0.0);
    // the shared 7-note diatonic set (major scale of the relative major)
    const int relMajorTonic = minor ? (tonic + 3) % 12 : tonic;
    const int majScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int d : majScale) h[(relMajorTonic + d) % 12] += 1.0;
    // FAINT tonic emphasis of the ACTUAL key (what really disambiguates in music)
    h[tonic % 12] += 0.35;
    h[(tonic + 7) % 12] += 0.15;  // its fifth, a touch
    return h;
}

// Case (e): dominant-emphasised phrase (authentic-cadence heavy). V is the most
// dwelt-on chord but the tonic is still a clear structural pole — this is the
// realistic version of the "over-reward the dominant, call the key up a fifth"
// trap, without making the tonic vanish (which would be genuinely ambiguous).
Chroma dominantHeavy (int tonic, bool minor)
{
    Chroma h{}; h.fill (0.0);
    addTriad (h, tonic, minor ? 0 : 0, minor ? 3 : 4, 7, 1.8); // i/I  (clear pole)
    addTriad (h, tonic, 7, 11, 2, 2.0);                        // V    (emphasised)
    if (! minor) addTriad (h, tonic, 2, 5, 9, 1.0);            // ii   (predominant)
    else         addTriad (h, tonic, 5, 8, 0, 1.0);            // iv   (predominant)
    addTriad (h, tonic, 7, 11, 2, 1.2);                        // V    again (dwell)
    addTriad (h, tonic, minor ? 0 : 0, minor ? 3 : 4, 7, 1.4); // i/I  resolution
    return h;
}

// Case (f): plagal / no-leading-tone context (IV-I or iv-i vamps, modal pop).
// Weak dominant, so the leading tone is scarce — stresses profiles that lean on
// scale-degree 7 to fix the tonic.
Chroma plagalVamp (int tonic, bool minor)
{
    Chroma h{}; h.fill (0.0);
    if (! minor)
    {
        addTriad (h, tonic, 0, 4, 7, 2.0);  // I
        addTriad (h, tonic, 5, 9, 0, 2.0);  // IV
        addTriad (h, tonic, 9, 0, 4, 1.0);  // vi
        addTriad (h, tonic, 2, 5, 9, 1.0);  // ii
    }
    else
    {
        addTriad (h, tonic, 0, 3, 7, 2.0);  // i
        addTriad (h, tonic, 5, 8, 0, 2.0);  // iv (natural, no leading tone)
        addTriad (h, tonic, 8, 0, 3, 1.0);  // VI
        addTriad (h, tonic, 10, 2, 5, 1.0); // VII (subtonic)
    }
    return h;
}

struct Profile { const char* name; const double* maj; const double* min; };

struct Score { int correct = 0; int total = 0; double marginSum = 0.0; int marginN = 0; };

void run (const Profile& p, Score& s, bool verboseFails)
{
    using namespace sattari::dsp;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < 2; ++m)
        {
            const bool minor = (m == 1);
            std::vector<Chroma> cases;
            Chroma base = progression (tonic, minor);
            cases.push_back (base);                                   // (a) clean diatonic
            cases.push_back (withBorrowed (base, tonic, minor));      // (b) + borrowed notes
            cases.push_back (withFloorAndSkew (base, tonic, minor));  // (c) + floor + skew
            cases.push_back (relativeConfusion (tonic, minor));       // (d) relative confusion
            cases.push_back (dominantHeavy (tonic, minor));           // (e) dominant-heavy phrase
            cases.push_back (plagalVamp (tonic, minor));              // (f) plagal / no leading tone

            for (auto& c : cases)
            {
                auto r = detail::detectKeyWithProfiles (c.data(), p.maj, p.min);
                const bool ok = r.valid && r.root == tonic && r.minor == minor;
                ++s.total;
                if (ok) { ++s.correct; s.marginSum += r.confidence; ++s.marginN; }
                else if (verboseFails)
                    std::printf ("    [%s] MISS %s %-5s -> got %s %-5s (conf %.3f)\n",
                                 p.name, kNoteNames[tonic], minor ? "minor" : "major",
                                 kNoteNames[r.root], r.minor ? "minor" : "major", r.confidence);
            }
        }
}
} // namespace

int main (int argc, char** argv)
{
    const bool verbose = (argc > 1 && std::string (argv[1]) == "-v");
    using namespace sattari::dsp::detail;

    Profile profiles[3] = {
        { "Krumhansl-Schmuckler", kKrumhanslMajor, kKrumhanslMinor },
        { "Temperley/Kostka-Payne", kTemperleyMajor, kTemperleyMinor },
        { "Aarden-Essen",           kAardenMajor,    kAardenMinor    },
    };

    std::printf ("Key-detector profile benchmark — 24 keys x 6 stress cases = 144 trials each\n");
    std::printf ("Cases: (a) I-IV-V-vi-ii-V-I diatonic  (b) +borrowed notes  "
                 "(c) +floor+5th-skew\n       (d) relative-major/minor confusion  "
                 "(e) dominant-heavy  (f) plagal/no-leading-tone\n\n");
    std::printf ("  %-24s  %8s  %10s\n", "Profile set", "Accuracy", "MeanMargin");
    std::printf ("  %-24s  %8s  %10s\n", "------------------------", "--------", "----------");

    Score scores[3];
    int bestIdx = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (verbose) std::printf ("  -- %s --\n", profiles[i].name);
        run (profiles[i], scores[i], verbose);
        const double acc = 100.0 * scores[i].correct / scores[i].total;
        const double mg  = scores[i].marginN ? scores[i].marginSum / scores[i].marginN : 0.0;
        std::printf ("  %-24s  %6.1f %%  %10.4f\n", profiles[i].name, acc, mg);
    }

    // Winner = highest accuracy; ties broken by larger mean margin (less flicker).
    for (int i = 1; i < 3; ++i)
    {
        const bool moreCorrect = scores[i].correct > scores[bestIdx].correct;
        const bool tiedMoreMargin =
            scores[i].correct == scores[bestIdx].correct
            && (scores[i].marginSum / (scores[i].marginN ? scores[i].marginN : 1))
               > (scores[bestIdx].marginSum / (scores[bestIdx].marginN ? scores[bestIdx].marginN : 1));
        if (moreCorrect || tiedMoreMargin) bestIdx = i;
    }
    std::printf ("\n  WINNER: %s  (%d/%d correct)\n",
                 profiles[bestIdx].name, scores[bestIdx].correct, scores[bestIdx].total);
    return 0;
}
