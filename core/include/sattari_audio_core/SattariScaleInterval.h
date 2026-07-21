#pragma once

// =============================================================================
//  Shared in-key interval walk.
// -----------------------------------------------------------------------------
//  "A third above this note, in this key" is not a fixed semitone count — it is
//  two SCALE steps, so it is +4 semitones off the tonic of a major key and +3 off
//  the second degree. Several plugins need exactly this (Morph's Harmony Voice and
//  Key Echo, Auto Pitch's Poly Harmony), so the walk lives here once.
//
//  Pure-std: no JUCE, so it is covered by a contract test.
// =============================================================================

namespace sattari::dsp
{

// Walk `degrees` scale steps upward from `startMidi`, counting only pitch classes
// that are in the key (mask[pc] == true, pc 0=C .. 11=B). Returns the MIDI note reached.
//
// A mask that admits every pitch class is chromatic — "two scale steps" would then mean
// a whole tone rather than a third, which is musically wrong — so a chromatic (or
// degenerate) mask falls back to the fixed `chromaticSemitones` interval instead.
inline int scaleStepsAbove (int startMidi, const bool* mask, int degrees, int chromaticSemitones) noexcept
{
    if (degrees <= 0)   return startMidi;
    if (mask == nullptr) return startMidi + chromaticSemitones;

    int inKey = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (mask[pc]) ++inKey;
    if (inKey >= 12 || inKey < 2) return startMidi + chromaticSemitones;

    int m = startMidi, found = 0;
    for (int step = 0; step < 128 && found < degrees; ++step)
    {
        ++m;
        if (mask[((m % 12) + 12) % 12]) ++found;
    }
    return m;
}

} // namespace sattari::dsp
