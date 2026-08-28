// SattariNoteEngine — the cents unification.
//
//  The claim being tested is that twelve-tone and maqam scales are now the SAME
//  data type, so one code path serves both. So every property is checked twice:
//  once on a diatonic scale where the right answer is well known, and once on
//  Rast, where a naive semitone implementation gets it wrong by 50 cents.
#include <sattari_audio_core/SattariNoteEngine.h>

#include <cmath>
#include <cstdio>

namespace
{
int fails = 0;
void check (bool ok, const char* what)
{
    if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; }
}
bool near (double a, double b, double tol = 0.01) { return std::fabs (a - b) <= tol; }

// Rast: neutral (half-flat) 3rd and 7th — the exact degrees a semitone-based
// scale representation cannot hold.
sattari::dsp::CentScale rast (int root = 0)
{
    const int c[] = { 0, 200, 350, 500, 700, 900, 1050 };
    return sattari::dsp::fromCents (c, 7, root);
}
} // namespace

int main()
{
    std::printf ("NoteEngine contract\n");
    using namespace sattari::dsp;

    // --- 1. Diatonic degrees land where music theory says ------------------
    {
        const auto major = majorScale (0);              // C major
        // C4 = MIDI 60 = 6000 cents. Degrees: C D E F G A B, then C5.
        const double expect[] = { 6000, 6200, 6400, 6500, 6700, 6900, 7100, 7200 };
        bool ok = true;
        for (int d = 0; d <= 7; ++d)
            ok = ok && near (degreeCents (major, d, 4), expect[d]);
        check (ok, "C major degrees are not at the expected cents");
    }

    // --- 2. THE POINT: the same call works on a maqam -----------------------
    {
        const auto scale = rast (0);                    // Rast on C
        // The third is NEUTRAL: 350 cents, not 400. A semitone table cannot
        // express this at all, which is why this used to need its own code path.
        check (near (degreeCents (scale, 2, 4), 6350.0),
               "Rast third is not neutral (350 cents)");
        check (near (degreeCents (scale, 6, 4), 7050.0),
               "Rast seventh is not neutral (1050 cents)");
        check (near (degreeCents (scale, 7, 4), 7200.0),
               "Rast octave is not 1200 cents above the root");
    }

    // --- 3. "A third above, in this key" — one code path, both systems ------
    {
        const auto major = majorScale (0);
        // From C4, two scale steps = E4 (a MAJOR third, 400 cents).
        check (near (degreesAbove (major, 6000.0, 2), 6400.0),
               "diatonic third above C is wrong");
        // From D4, two scale steps = F4 (a MINOR third, 300 cents) — the reason
        // this cannot be a fixed semitone count.
        check (near (degreesAbove (major, 6200.0, 2), 6500.0),
               "diatonic third above D should be minor");

        const auto scale = rast (0);
        // Same call, same meaning, neutral answer.
        check (near (degreesAbove (scale, 6000.0, 2), 6350.0),
               "Rast third above the tonic is not neutral");
    }

    // --- 4. Negative degrees walk downward ---------------------------------
    {
        const auto major = majorScale (0);
        check (near (degreesAbove (major, 6000.0, -1), 5900.0),
               "one degree below C4 should be B3");
        check (near (degreeCents (major, -1, 4), 5900.0),
               "degree -1 should be the seventh below");
    }

    // --- 5. Snapping is exact for quarter-tones ----------------------------
    //  A semitone-rounding quantiser would push a 340-cent pitch to 300 or 400.
    //  In Rast the correct answer is 350.
    {
        const auto scale = rast (0);
        check (near (snapToScale (scale, 6340.0), 6350.0),
               "snapping did not find the neutral third");
        check (near (snapToScale (scale, 6010.0), 6000.0),
               "snapping missed the tonic");
        const auto major = majorScale (0);
        check (near (snapToScale (major, 6340.0), 6400.0),
               "snapping in major should reach E, not a neutral third");
    }

    // --- 6. Root transposition moves everything ----------------------------
    {
        const auto dMajor = majorScale (2);             // D
        check (near (degreeCents (dMajor, 0, 4), 6200.0), "D major tonic is wrong");
        check (near (degreeCents (dMajor, 2, 4), 6600.0), "D major third is wrong");
    }

    // --- 7. Semitone bridge preserves the existing tables ------------------
    {
        const int minorSemis[] = { 0, 2, 3, 5, 7, 8, 10 };
        const auto built = fromSemitones (minorSemis, 7, 0);
        const auto direct = minorScale (0);
        bool same = built.count == direct.count;
        for (int i = 0; i < direct.count && same; ++i)
            same = built.cents[i] == direct.cents[i];
        check (same, "the semitone bridge does not reproduce the minor scale");
    }

    // --- 8. Cents <-> Hz round trip ----------------------------------------
    {
        check (near (centsToHz (6900.0), 440.0, 0.001), "A4 is not 440 Hz");
        check (near (hzToCents (440.0), 6900.0, 0.001), "440 Hz is not A4");
        check (near (hzToCents (centsToHz (6350.0)), 6350.0, 0.001),
               "cents -> Hz -> cents did not round-trip");
    }

    // --- 9. THE HONEST BOUNDARY: MIDI cannot hold a neutral third ----------
    //  This is the check that stops quarter-tones being silently rounded into
    //  wrong notes by a MIDI-emitting consumer.
    {
        const auto exact = noteAndBend (6400.0);        // E4, a real MIDI note
        check (exact.midiNote == 64 && exact.exact, "E4 should be exactly MIDI 64");
        check (near (exact.bendCents, 0.0), "E4 should need no bend");

        const auto neutral = noteAndBend (6350.0);      // Rast third
        check (! neutral.exact, "a 350-cent third should NOT be marked exact");
        check (std::fabs (neutral.bendCents) >= 49.0,
               "a neutral third should report ~50 cents of bend");
        std::printf ("  350-cent third -> MIDI %d %+.0f cents (exact=%s)\n",
                     neutral.midiNote, neutral.bendCents, neutral.exact ? "yes" : "no");

        check (! requiresPitchBend (majorScale (0)), "major should not need bend");
        check (requiresPitchBend (rast (0)), "Rast should report that it needs bend");
    }

    // --- 10. Degenerate input must not explode -----------------------------
    {
        CentScale empty;
        check (! empty.valid(), "a default scale should be invalid");
        check (near (degreesAbove (empty, 6000.0, 3), 6000.0),
               "an invalid scale should pass the pitch through unchanged");
        check (near (snapToScale (empty, 6000.0), 6000.0),
               "an invalid scale should not move a snapped pitch");
    }

    if (fails == 0) { std::printf ("NoteEngine contract ok\n"); return 0; }
    std::printf ("NoteEngine contract FAILED (%d)\n", fails);
    return 1;
}
