#pragma once

// =============================================================================
//  SattariHarmonyVoicing — diatonic voicing core (pure std, no JUCE)
// -----------------------------------------------------------------------------
//  Given a lead MIDI note and a key (root + scale), compute a harmony note that
//  is a number of *scale degrees* away — so a "third above" stays diatonic to
//  the key (major or minor third depending on where the lead sits), exactly how
//  a real singer harmonises. Pure-std so it unit-tests on its own, the same way
//  the rest of the shared core does.
//
//  Promoted to core from plugins/harmony/src: Glass Choir reached it through a
//  relative `../../harmony/src/` include, which is fine between two plugins but
//  impossible for a core module — and Glass Choir needs to be one. Shared DSP
//  belongs in core, not behind a path that walks out of one plugin into another.
// =============================================================================

#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId, scaleIntervals, snapMidiToScale
#include <vector>

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
// ScaleId. Chromatic means no scale snap; don't silently force a major key.
inline ScaleId scaleIdFromBus (int busScale) noexcept
{
    if (busScale == 1) return ScaleId::Minor;
    if (busScale == 2) return ScaleId::ChromaticOff;
    return ScaleId::Major;
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

// Snap a desired MIDI note to the nearest note whose pitch-class belongs to a
// chord (a set of 0-11 pitch classes, e.g. the notes held on a chord track).
// This is what lets the harmony follow the *chords*, not just the scale — each
// voice lands on the closest real chord tone to where the scale would send it.
inline int nearestChordTone (int desiredMidi, const int* pitchClasses, int numPitchClasses) noexcept
{
    if (pitchClasses == nullptr || numPitchClasses <= 0)
        return detail::clampMidi (desiredMidi);

    int best = desiredMidi, bestDist = 1 << 30;
    for (int i = 0; i < numPitchClasses; ++i)
    {
        const int raw = pitchClasses[i];
        const int pc = ((raw % 12) + 12) % 12;
        const int k = (int) std::lround ((double) (desiredMidi - pc) / 12.0);
        const int cand = pc + 12 * k;                     // nearest octave of pc to desired
        const int d = cand > desiredMidi ? cand - desiredMidi : desiredMidi - cand;
        if (d < bestDist) { bestDist = d; best = cand; }
    }
    return detail::clampMidi (best);
}

inline int nearestChordTone (int desiredMidi, const std::vector<int>& pitchClasses) noexcept
{
    return nearestChordTone (desiredMidi, pitchClasses.data(), (int) pitchClasses.size());
}

} // namespace sattari::harmony
