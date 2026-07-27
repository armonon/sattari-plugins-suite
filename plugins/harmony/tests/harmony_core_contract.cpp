// Pure-C++ contract for the Sattari Harmony diatonic voicing core. No JUCE.
// Builds with:  g++ -std=c++17 -Icore/include plugins/harmony/tests/harmony_core_contract.cpp
#include <sattari_audio_core/SattariHarmonyVoicing.h>
#include <cstdio>
#include <vector>

using namespace sattari::harmony;
using sattari::dsp::ScaleId;

static int failures = 0;
static void check (const char* what, int got, int want)
{
    if (got != want)
    {
        std::printf ("  FAIL %s: got %d want %d\n", what, got, want);
        ++failures;
    }
}

int main()
{
    const int C4 = 60, D4 = 62;
    const ScaleId Cmaj = ScaleId::Major;   // root C = 0

    // A diatonic third above C is E (major third); a third above D is F (minor
    // third) — same +2 scale-degree step, key-correct interval.
    check ("3rd above C (maj3)", diatonicShift (C4, 0, Cmaj, 2), 64);   // E4
    check ("3rd above D (min3)", diatonicShift (D4, 0, Cmaj, 2), 65);   // F4
    check ("5th above C",        diatonicShift (C4, 0, Cmaj, 4), 67);   // G4
    check ("octave above C",     diatonicShift (C4, 0, Cmaj, 7), 72);   // C5
    check ("octave below C",     diatonicShift (C4, 0, Cmaj, -7), 48);  // C3
    check ("3rd below C",        diatonicShift (C4, 0, Cmaj, -2), 57);  // A3

    // In A minor (root 9), a third above A is C (minor third).
    check ("3rd above A (Am)",   diatonicShift (69, 9, ScaleId::Minor, 2), 72); // A4 -> C5

    // Bus-scale mapping: 0 => major, 1 => minor, 2 => chromatic/off.
    check ("bus scale minor", (int) (scaleIdFromBus (1) == ScaleId::Minor), 1);
    check ("bus scale major", (int) (scaleIdFromBus (0) == ScaleId::Major), 1);
    check ("bus scale chromatic", (int) (scaleIdFromBus (2) == ScaleId::ChromaticOff), 1);

    // Chord-follow voicing: snap a desired note onto the nearest chord tone.
    const std::vector<int> CmajTriad { 0, 4, 7 };     // C major triad: C E G
    check ("F snaps to E (Cmaj)",  nearestChordTone (65, CmajTriad), 64);
    check ("C stays C (Cmaj)",     nearestChordTone (60, CmajTriad), 60);
    check ("Bb snaps to C (Cmaj)", nearestChordTone (70, CmajTriad), 72);
    check ("empty chord passes through", nearestChordTone (61, {}), 61);

    if (failures == 0)
        std::printf ("harmony core contract: OK\n");
    else
        std::printf ("harmony core contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
