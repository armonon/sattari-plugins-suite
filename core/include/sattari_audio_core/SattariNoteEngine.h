#pragma once

// =============================================================================
//  SattariNoteEngine — in-key note generation, in CENTS.
// -----------------------------------------------------------------------------
//  THE UNIFICATION. The suite has held two incompatible ideas of "a scale":
//
//      SattariPitchDetector  int[] of pitch classes 0..11  (semitones)
//      SattariMaqam          int[] of CENTS, 50-cent steps (neutral degrees)
//
//  So a diatonic third and a Rast third were different data types, and every
//  consumer that wanted both had to special-case one. Represent everything in
//  cents and they become the same thing: Major is {0,200,400,500,700,900,1100},
//  Rast is {0,200,350,500,700,900,1050}, and a function that walks scale degrees
//  works on both without knowing which it was handed.
//
//  That is not a tidy-up. It is the load-bearing change for microtonal support:
//  once the scale is cents, "a third above, in this key" is one code path for
//  twelve-tone and maqam alike, and every in-key consumer in the suite inherits
//  quarter-tone capability instead of needing a parallel implementation.
//
//  WHAT CENTS CANNOT FIX. MIDI notes are integers. A 350-cent third is not
//  expressible as a note number, so a MIDI-emitting consumer needs pitch bend or
//  MPE — see noteAndBend(). Audio-side consumers (tuning, harmony, synthesis)
//  have no such limit and can use the cents directly. This header makes that
//  boundary explicit rather than letting it surface as mysteriously-wrong notes.
//
//  Pure std. No JUCE, no allocation, no locks; safe on the audio thread.
// =============================================================================

#include <cmath>
#include <cstdint>

namespace sattari::dsp
{

//  A scale as ascending cent offsets within one octave, starting at 0.
//  Fixed capacity so it is a value type: copying one costs nothing and it can
//  live in a module without an allocation.
struct CentScale
{
    static constexpr int kMaxDegrees = 12;

    int   cents[kMaxDegrees] = { 0 };
    int   count = 0;
    int   rootPitchClass = 0;      // 0 = C
    float octaveCents = 1200.0f;   // non-octave scales exist; make it explicit

    bool valid() const noexcept { return count > 0; }
};

// --- The twelve-tone scales, expressed in the shared representation ----------
inline CentScale majorScale (int root = 0) noexcept
{
    CentScale s; s.rootPitchClass = root; s.count = 7;
    const int c[] = { 0, 200, 400, 500, 700, 900, 1100 };
    for (int i = 0; i < 7; ++i) s.cents[i] = c[i];
    return s;
}
inline CentScale minorScale (int root = 0) noexcept
{
    CentScale s; s.rootPitchClass = root; s.count = 7;
    const int c[] = { 0, 200, 300, 500, 700, 800, 1000 };
    for (int i = 0; i < 7; ++i) s.cents[i] = c[i];
    return s;
}
inline CentScale chromaticScale (int root = 0) noexcept
{
    CentScale s; s.rootPitchClass = root; s.count = 12;
    for (int i = 0; i < 12; ++i) s.cents[i] = i * 100;
    return s;
}

//  Build from a semitone pitch-class list — the bridge from every existing
//  ScaleId table without rewriting them.
inline CentScale fromSemitones (const int* semitones, int count, int root = 0) noexcept
{
    CentScale s; s.rootPitchClass = root;
    s.count = count > CentScale::kMaxDegrees ? CentScale::kMaxDegrees : (count < 0 ? 0 : count);
    for (int i = 0; i < s.count; ++i) s.cents[i] = semitones[i] * 100;
    return s;
}

//  Build from a cent list — the bridge from the maqam tables.
inline CentScale fromCents (const int* cents, int count, int root = 0) noexcept
{
    CentScale s; s.rootPitchClass = root;
    s.count = count > CentScale::kMaxDegrees ? CentScale::kMaxDegrees : (count < 0 ? 0 : count);
    for (int i = 0; i < s.count; ++i) s.cents[i] = cents[i];
    return s;
}

//  Absolute cents (relative to MIDI 0) of scale degree `degree`, which may be
//  negative or beyond the scale length — it wraps into octaves, so degree 7 of a
//  7-note scale is the octave and degree -1 is the seventh below.
inline double degreeCents (const CentScale& scale, int degree, int octave = 4) noexcept
{
    if (! scale.valid()) return 0.0;

    int wrapped = degree % scale.count;
    int octaveShift = degree / scale.count;
    if (wrapped < 0) { wrapped += scale.count; --octaveShift; }

    const double rootCents = (double) ((octave + 1) * 12 + scale.rootPitchClass) * 100.0;
    return rootCents + (double) scale.cents[wrapped] + (double) octaveShift * scale.octaveCents;
}

//  "N scale degrees above this pitch, in this key" — the operation Harmony,
//  Scaler, Arp, Morph and Auto Pitch all need, now working identically for a
//  diatonic third and a Rast neutral third.
inline double degreesAbove (const CentScale& scale, double fromCents_, int degrees) noexcept
{
    if (! scale.valid()) return fromCents_;

    // Locate the nearest degree at or below the incoming pitch, then step.
    const double rootCents = (double) scale.rootPitchClass * 100.0;
    double rel = fromCents_ - rootCents;
    const double oct = std::floor (rel / scale.octaveCents);
    rel -= oct * scale.octaveCents;

    int nearest = 0;
    for (int i = 0; i < scale.count; ++i)
        if ((double) scale.cents[i] <= rel + 0.5) nearest = i;

    const double base = rootCents + oct * scale.octaveCents;
    const int target = nearest + degrees;
    int wrapped = target % scale.count;
    int shift   = target / scale.count;
    if (wrapped < 0) { wrapped += scale.count; --shift; }

    return base + (double) scale.cents[wrapped] + (double) shift * scale.octaveCents;
}

//  Snap an arbitrary pitch to the nearest scale degree. This is what a tuner or
//  a note quantiser wants, and it is correct for quarter-tones because nothing
//  here ever rounds to a semitone.
inline double snapToScale (const CentScale& scale, double cents) noexcept
{
    if (! scale.valid()) return cents;

    const double rootCents = (double) scale.rootPitchClass * 100.0;
    double rel = cents - rootCents;
    const double oct = std::floor (rel / scale.octaveCents);
    rel -= oct * scale.octaveCents;

    double best = (double) scale.cents[0];
    double bestDistance = std::fabs (rel - best);
    for (int i = 1; i < scale.count; ++i)
    {
        const double d = std::fabs (rel - (double) scale.cents[i]);
        if (d < bestDistance) { bestDistance = d; best = (double) scale.cents[i]; }
    }
    // The octave above the last degree is also a candidate.
    const double wrapUp = scale.octaveCents;
    if (std::fabs (rel - wrapUp) < bestDistance) best = wrapUp;

    return rootCents + oct * scale.octaveCents + best;
}

inline double centsToHz (double cents) noexcept
{
    return 440.0 * std::pow (2.0, (cents - 6900.0) / 1200.0);
}
inline double hzToCents (double hz) noexcept
{
    return hz > 0.0 ? 6900.0 + 1200.0 * std::log2 (hz / 440.0) : 0.0;
}

//  Split a cent value into the MIDI note plus the bend needed to reach it.
//
//  THE HONEST PART: a 350-cent third has no MIDI note number. Emitting note 64
//  and pretending is a semitone of error. Callers that can bend (MPE, per-channel
//  pitch bend) use both halves; callers that cannot must either accept the
//  rounding or refuse the scale, and this signature forces that decision to be
//  made rather than silently lost.
struct NoteAndBend
{
    int    midiNote  = 60;
    double bendCents = 0.0;   // add this to reach the true pitch
    bool   exact     = true;  // false => the scale needs bend to be played correctly
};

inline NoteAndBend noteAndBend (double cents) noexcept
{
    NoteAndBend result;
    const double note = cents / 100.0;
    result.midiNote = (int) std::lround (note);
    if (result.midiNote < 0) result.midiNote = 0;
    if (result.midiNote > 127) result.midiNote = 127;
    result.bendCents = cents - (double) result.midiNote * 100.0;
    result.exact = std::fabs (result.bendCents) < 0.5;
    return result;
}

//  Does this scale need bend to be played over MIDI at all? A caller can use
//  this to switch on MPE, or to warn instead of quietly playing the wrong notes.
inline bool requiresPitchBend (const CentScale& scale) noexcept
{
    for (int i = 0; i < scale.count; ++i)
        if (scale.cents[i] % 100 != 0) return true;
    return false;
}

} // namespace sattari::dsp
