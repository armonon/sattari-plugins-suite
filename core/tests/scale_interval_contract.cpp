// Contract for the shared in-key interval walk (SattariScaleInterval.h).
//
// Locks the musical facts: a "third" is two SCALE steps, so its semitone size depends
// on the degree you start from (major third off the tonic, minor third off the second),
// it wraps octaves correctly, and a chromatic key falls back to a fixed interval rather
// than degenerating into a whole tone.
#include <sattari_audio_core/SattariScaleInterval.h>

#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}

void checkEq (int got, int want, const std::string& what)
{
    if (got != want)
    {
        std::printf ("  FAIL: %s (got %d, want %d)\n", what.c_str(), got, want);
        ++failures;
    }
}

// pc 0=C .. 11=B
void buildMajor (bool (&mask)[12], int rootPc)
{
    static const int deg[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 12; ++i) mask[i] = false;
    for (int d : deg) mask[((rootPc + d) % 12 + 12) % 12] = true;
}

void buildMinor (bool (&mask)[12], int rootPc)
{
    static const int deg[] = { 0, 2, 3, 5, 7, 8, 10 };
    for (int i = 0; i < 12; ++i) mask[i] = false;
    for (int d : deg) mask[((rootPc + d) % 12 + 12) % 12] = true;
}
} // namespace

int main()
{
    using sattari::dsp::scaleStepsAbove;
    std::printf ("SattariAudioCore scale-interval contract\n");

    bool cMajor[12]; buildMajor (cMajor, 0);          // C D E F G A B
    bool aMinor[12]; buildMinor (aMinor, 9);          // A B C D E F G

    // --- A diatonic third is two scale steps, and its SIZE depends on the degree ----
    checkEq (scaleStepsAbove (60, cMajor, 2, 4), 64, "C major: third above C4 is E4 (+4)");
    checkEq (scaleStepsAbove (62, cMajor, 2, 4), 65, "C major: third above D4 is F4 (+3)");
    checkEq (scaleStepsAbove (64, cMajor, 2, 4), 67, "C major: third above E4 is G4 (+3)");
    checkEq (scaleStepsAbove (65, cMajor, 2, 4), 69, "C major: third above F4 is A4 (+4)");

    // --- Octave wrap ---------------------------------------------------------------
    checkEq (scaleStepsAbove (71, cMajor, 2, 4), 74, "C major: third above B4 wraps to D5");

    // --- A diatonic fifth is four scale steps --------------------------------------
    checkEq (scaleStepsAbove (60, cMajor, 4, 7), 67, "C major: fifth above C4 is G4 (+7)");
    // The leading tone's diatonic fifth is diminished (B->F, +6) — the walk must follow the
    // key rather than blindly adding 7 semitones.
    checkEq (scaleStepsAbove (71, cMajor, 4, 7), 77, "C major: fifth above B4 is F5 (+6, diminished)");

    // --- Minor key ------------------------------------------------------------------
    checkEq (scaleStepsAbove (69, aMinor, 2, 4), 72, "A minor: third above A4 is C5 (+3)");
    checkEq (scaleStepsAbove (72, aMinor, 2, 4), 76, "A minor: third above C5 is E5 (+4)");

    // --- Chromatic / degenerate masks fall back to the fixed interval ---------------
    bool chromatic[12]; for (auto& b : chromatic) b = true;
    checkEq (scaleStepsAbove (60, chromatic, 2, 4), 64,
             "chromatic mask falls back to a fixed major third, not a whole tone");
    bool empty[12]; for (auto& b : empty) b = false;
    checkEq (scaleStepsAbove (60, empty, 2, 4), 64, "empty mask falls back to the fixed interval");
    checkEq (scaleStepsAbove (60, nullptr, 2, 4), 64, "null mask falls back to the fixed interval");

    // --- Degenerate degree counts ---------------------------------------------------
    checkEq (scaleStepsAbove (60, cMajor, 0, 4), 60, "zero degrees is a no-op");
    checkEq (scaleStepsAbove (60, cMajor, -1, 4), 60, "negative degrees is a no-op");

    // --- The walk always lands on an in-key pitch class ------------------------------
    for (int start = 48; start < 84; ++start)
    {
        const int got = scaleStepsAbove (start, cMajor, 2, 4);
        check (cMajor[((got % 12) + 12) % 12], "third above midi " + std::to_string (start) + " is in key");
        check (got > start && got - start <= 5, "third above midi " + std::to_string (start) + " is a sane distance");
    }

    if (failures == 0) { std::printf ("scale-interval contract: PASS\n"); return 0; }
    std::printf ("scale-interval contract: %d FAILURE(S)\n", failures);
    return 1;
}
