#pragma once

// KeyLockPlan — dynamic key-lock correction math (perform-screen §6.2, Phase 1).
//
// Given a key-over-time curve (KeyCurve.h) and a target key, produce the semitone
// correction to apply AT EACH POINT, with the two honesty gates the brief demands
// designed in from the start:
//
//   * Threshold intervention: correct only SMALL drift silently. A large modulation
//     (a jump of a fourth into a final chorus is often the song's emotional peak) is
//     FLAGGED, not silently flattened — the default is "let it ride, ask the user".
//   * Quality ceiling: past a correction distance where pitch-shifting audibly
//     degrades, flag it so the user decides rather than hearing a mystery artefact.
//
// Pure + headlessly testable: no audio, no DSP. Phase 2 (formant) and Phase 3 (real-
// time application via TimePitchEngine) consume this plan; they need real-audio
// verification and are NOT part of this header.

#include "KeyCurve.h"

namespace stemdeck
{

struct KeyLockSettings
{
    int  silentMaxSemitones   = 2;   // <= this: corrected silently
    int  qualityCeiling       = 5;   // >  this: flag audible degradation
    bool holdModulations      = true; // large modulations ride unless the user opts in
};

struct KeyCorrection
{
    double atBeat        = 0.0;
    int    semitones     = 0;      // correction to apply (0 if not applied)
    bool   applied       = false;  // will actually be corrected
    bool   modulation    = false;  // large drift -> flagged, needs user consent
    bool   degrade       = false;  // beyond the quality ceiling
};

// Smallest signed semitone step fromPc -> toPc, in [-6, +6].
inline int keyLockSemitoneDelta (int fromPc, int toPc) noexcept
{
    int d = ((toPc - fromPc) % 12 + 12) % 12;
    if (d > 6) d -= 12;
    return d;
}

inline std::vector<KeyCorrection> planKeyLock (const std::vector<KeyObservation>& curve,
                                               int targetPitchClass, KeyLockSettings s = {})
{
    std::vector<KeyCorrection> out;
    out.reserve (curve.size());
    for (const auto& o : curve)
    {
        const int delta = keyLockSemitoneDelta (o.pitchClass, targetPitchClass);
        KeyCorrection c;
        c.atBeat = o.atBeat;
        const int mag = std::abs (delta);

        if (mag == 0)
        {
            c.applied = false;                       // already in key
        }
        else if (mag <= s.silentMaxSemitones)
        {
            c.semitones = delta; c.applied = true;   // small drift -> correct silently
        }
        else
        {
            c.modulation = true;                     // large -> flag, don't auto-correct...
            c.applied    = ! s.holdModulations;      // ...unless the user opted to correct
            if (c.applied) c.semitones = delta;
        }
        if (mag > s.qualityCeiling) c.degrade = true;
        out.push_back (c);
    }
    return out;
}

// The single semitone value to apply at a given beat (0 = none). Used by the RT path.
inline int keyLockCorrectionAtBeat (const std::vector<KeyCorrection>& plan, double beat) noexcept
{
    int semis = 0;
    for (const auto& c : plan)
    {
        if (c.atBeat > beat) break;
        semis = c.applied ? c.semitones : 0;
    }
    return semis;
}

} // namespace stemdeck
