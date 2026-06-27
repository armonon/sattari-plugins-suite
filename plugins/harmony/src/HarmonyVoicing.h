#pragma once

// =============================================================================
//  Sattari Harmony — diatonic voicing core (pure std, no JUCE)
// -----------------------------------------------------------------------------
//  Given a lead MIDI note and a key (root + scale), compute a harmony note that
//  is a number of *scale degrees* away — so a "third above" stays diatonic to
//  the key (major or minor third depending on where the lead sits), exactly how
//  a real singer harmonises. Pure-std so it unit-tests on its own, the same way
//  the rest of the shared core does.
// =============================================================================

#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId, scaleIntervals, snapMidiToScale

namespace sattari::harmony
{
using sattari::dsp::ScaleId;
using sattari::dsp::scaleIntervals;
using sattari::dsp::snapMidiToScale;

// Map a UI scale-choice index (see HarmonyProcessor::scaleChoices) to a ScaleId.
inline ScaleId scaleIdFromChoice (int choice) noexcept
{
    switch (choice)
    {
        case 0: return ScaleId::Major;
        case 1: return ScaleId::Minor;
        case 2: return ScaleId::Dorian;
        case 3: return ScaleId::Mixolydian;
        case 4: return ScaleId::HarmMinor;
        case 5: return ScaleId::PentaMaj;
        case 6: return ScaleId::PentaMin;
        default: return ScaleId::Major;
    }
}

// Map the cross-plugin bus scale code (0=major, 1=minor, 2=chromatic) to a
// ScaleId. Chromatic falls back to Major so harmonies still stay musical.
inline ScaleId scaleIdFromBus (int busScale) noexcept
{
    return busScale == 1 ? ScaleId::Minor : ScaleId::Major;
}

namespace detail
{
    inline int floorDiv (int a, int b) noexcept
    {
        int q = a / b, r = a % b;
        if (r != 0 && ((r < 0) != (b < 0))) --q;
        return q;
    }
    inline int clampMidi (int m) noexcept { return m < 0 ? 0 : (m > 127 ? 127 : m); }
}

// Shift `leadMidi` by `degrees` steps along the (root, scale) ladder. Positive
// is up. The lead is first snapped into the scale, then we walk the ladder, so
// the interval is always diatonic to the key. For ChromaticOff the degrees are
// treated as plain semitones.
inline int diatonicShift (int leadMidi, int root, ScaleId scale, int degrees) noexcept
{
    int count = 0;
    const int* iv = scaleIntervals (scale, count);
    if (iv == nullptr || count <= 0)
        return detail::clampMidi (leadMidi + degrees);

    const int snapped  = snapMidiToScale (leadMidi, root, scale);
    const int relative = snapped - root;
    const int octave   = detail::floorDiv (relative, 12);
    const int pc       = relative - octave * 12;          // 0..11, equals some iv[i]

    int idx = 0;
    for (int i = 0; i < count; ++i)
        if (iv[i] == pc) { idx = i; break; }

    const int step = octave * count + idx + degrees;
    const int tOct = detail::floorDiv (step, count);
    const int tIdx = step - tOct * count;                 // 0..count-1
    return detail::clampMidi (root + tOct * 12 + iv[tIdx]);
}

} // namespace sattari::harmony
