// =============================================================================
//  Sattari — harmony voice planning with voice leading
// -----------------------------------------------------------------------------
//  Decides WHICH notes four harmony voices should sing. Deliberately separate
//  from any shifter: this is the musical decision, and it is the part that must
//  not be a fixed-interval transposer.
//
//  Three modes, per the product spec:
//    Interval     — a fixed diatonic degree offset (the honest "+3rd" behaviour)
//    Intelligent  — pick the musically appropriate note from the CURRENT CHORD
//                   when one is known, falling back to the scale otherwise
//    Midi         — the voices follow the held notes inside the qualified
//                   one-octave vocal-shifter range
//
//  Voice leading is the reason Intelligent is not just "nearest chord tone".
//  Chosen candidates are scored on (a) closeness to the voice's previous note,
//  so lines move by step rather than leaping, (b) keeping the assigned role's
//  register, and (c) not colliding with another voice or crossing it. That is
//  what makes a harmony sound arranged rather than computed.
//
//  Pure std + the existing voicing core. No allocation in plan().
// =============================================================================
#pragma once

#include <sattari_audio_core/SattariHarmonyVoicing.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace sattari::dsp
{
//  The voicing primitives live in sattari::harmony; use them unqualified here.
using sattari::harmony::ScaleId;
using sattari::harmony::diatonicShift;


enum class HarmonyMode { interval = 0, intelligent, midi };

//  What a voice is FOR. In Intelligent mode the role picks the register and the
//  search direction; it is not an interval.
enum class HarmonyRole { upper = 0, lower, high, low, octaveUp, octaveDown, choir };

// Keep the requested pitch class when MIDI/graph targets exceed the qualified
// octave. Numeric clipping could turn (for example) B into C.
inline int fitHarmonyMidiToRange (int requested, int leadMidi) noexcept
{
    int note = std::clamp (requested, 0, 127);
    const int lo = std::max (0, leadMidi - 12), hi = std::min (127, leadMidi + 12);
    while (note > hi) note -= 12;
    while (note < lo) note += 12;
    return note;
}

inline const char* harmonyRoleName (HarmonyRole r) noexcept
{
    switch (r)
    {
        case HarmonyRole::upper:      return "Upper";
        case HarmonyRole::lower:      return "Lower";
        case HarmonyRole::high:       return "High";
        case HarmonyRole::low:        return "Low";
        case HarmonyRole::octaveUp:   return "Octave Up";
        case HarmonyRole::octaveDown: return "Octave Down";
        case HarmonyRole::choir:      return "Choir";
        default:                      return "";
    }
}

struct HarmonyVoiceSettings
{
    bool enabled = false;
    HarmonyRole role = HarmonyRole::upper;
    int  intervalDegrees = 2;      // Interval mode: diatonic degrees (2 = a 3rd)
    int  octaveOffset = 0;
    float level = 0.7f;
    float pan = 0.0f;
    float formantSemitones = 0.0f;
    float timingMs = 0.0f;         // humanising delay
    float detuneCents = 0.0f;      // humanising pitch offset
};

struct HarmonyPlanVoice
{
    bool  active = false;
    int   midi = -1;               // the note this voice should sing
    double confidence = 0.0;       // inherited from the harmonic evidence used
};

struct HarmonyPlan
{
    std::array<HarmonyPlanVoice, 4> voices {};
};

class HarmonyVoicePlanner
{
public:
    void reset() noexcept { for (auto& p : previousMidi) p = -1; }

    //  leadMidi          : the note the singer is on (-1 = unvoiced -> no plan)
    //  root/scale        : the current key (from the song map or AutoKey)
    //  chordPcs/numPcs   : the current chord's pitch classes, or nullptr/0 when
    //                      unknown — Intelligent then falls back to the scale,
    //                      which is the honest behaviour rather than inventing
    //                      a chord.
    //  chordConfidence   : 0..1; propagated so a caller can gate on it.
    HarmonyPlan plan (int leadMidi, HarmonyMode mode,
                      const HarmonyVoiceSettings* settings, int numVoices,
                      int root, ScaleId scale,
                      const int* chordPcs, int numPcs,
                      double chordConfidence,
                      const int* midiNotes = nullptr, int numMidiNotes = 0) noexcept
    {
        HarmonyPlan out;
        if (leadMidi < 0)
        {
            reset();
            return out;
        }

        const int voiceCount = numVoices < 4 ? numVoices : 4;
        int assigned[4] = { -1, -1, -1, -1 };

        for (int v = 0; v < voiceCount; ++v)
        {
            const auto& s = settings[v];
            if (! s.enabled) { previousMidi[(size_t) v] = -1; continue; }

            int note = -1;
            double conf = 1.0;

            switch (mode)
            {
                case HarmonyMode::midi:
                    //  Whatever is held, in order. No held note = no voice: an
                    //  external controller that says nothing means nothing.
                    if (midiNotes != nullptr && v < numMidiNotes)
                        note = midiNotes[v];
                    break;

                case HarmonyMode::interval:
                    //  The honest fixed offset, still diatonic to the key so it
                    //  is a musical interval rather than a semitone count.
                    note = diatonicShift (leadMidi, root, scale, s.intervalDegrees);
                    break;

                case HarmonyMode::intelligent:
                default:
                {
                    //  Candidate set: the chord's tones when a chord is known,
                    //  otherwise the scale's. Then pick by role + voice leading.
                    const int desired = desiredForRole (leadMidi, s.role, root, scale);
                    if (chordPcs != nullptr && numPcs > 0)
                    {
                        note = chooseWithVoiceLeading (desired, chordPcs, numPcs,
                                                       v, assigned, voiceCount, s.role, leadMidi);
                        conf = chordConfidence;
                    }
                    else
                    {
                        note = desired;
                        conf = 0.5;   // scale-only: a weaker basis, and it says so
                    }
                    break;
                }
            }

            if (note < 0) { previousMidi[(size_t) v] = -1; continue; }
            note += 12 * s.octaveOffset;
            // LiveEpochShifter's qualified range is 0.5x..2x. Clamp here, in
            // the shared musical planner, rather than silently clamping only in
            // the renderer: the Harmony readout and Edit overlays must always
            // show the note that is actually audible. This also gives remote
            // MIDI notes a deterministic nearest-octave interpretation.
            note = fitHarmonyMidiToRange (note, leadMidi);

            assigned[v] = note;
            previousMidi[(size_t) v] = note;
            out.voices[(size_t) v] = { true, note, conf };
        }
        return out;
    }

private:
    //  Where this role WANTS to sit, before the chord gets a say.
    int desiredForRole (int leadMidi, HarmonyRole role, int root, ScaleId scale) const noexcept
    {
        switch (role)
        {
            case HarmonyRole::upper:      return diatonicShift (leadMidi, root, scale, 2);
            case HarmonyRole::lower:      return diatonicShift (leadMidi, root, scale, -2);
            case HarmonyRole::high:       return diatonicShift (leadMidi, root, scale, 4);
            case HarmonyRole::low:        return diatonicShift (leadMidi, root, scale, -4);
            case HarmonyRole::octaveUp:   return leadMidi + 12;
            case HarmonyRole::octaveDown: return leadMidi - 12;
            case HarmonyRole::choir:      return diatonicShift (leadMidi, root, scale, 2);
            default:                      return leadMidi;
        }
    }

    //  Score every chord tone within a sensible window and take the best. This
    //  is where "arranged" comes from: the nearest chord tone to the desired
    //  note is only the starting point.
    int chooseWithVoiceLeading (int desired, const int* pcs, int numPcs,
                                int voiceIndex, const int* assigned, int voiceCount,
                                HarmonyRole role, int leadMidi) const noexcept
    {
        const int prev = previousMidi[(size_t) voiceIndex];
        int best = -1;
        double bestScore = -1.0e9;

        for (int candidate = desired - 7; candidate <= desired + 7; ++candidate)
        {
            if (candidate < 0 || candidate > 127) continue;
            bool inChord = false;
            for (int i = 0; i < numPcs; ++i)
                if (((candidate % 12) + 12) % 12 == ((pcs[i] % 12) + 12) % 12) { inChord = true; break; }
            if (! inChord) continue;

            double score = 0.0;
            //  (a) closeness to what this voice sang last — the core of voice
            //      leading. Common tones and steps beat leaps.
            if (prev >= 0)
                score -= 0.55 * std::abs (candidate - prev);
            //  (b) stay in the role's register.
            score -= 0.30 * std::abs (candidate - desired);
            //  (c) do not double another voice, and do not cross the lead in the
            //      wrong direction for the role.
            for (int o = 0; o < voiceCount; ++o)
                if (o != voiceIndex && assigned[o] == candidate)
                    score -= 6.0;
            const bool wantsAbove = role == HarmonyRole::upper || role == HarmonyRole::high
                                 || role == HarmonyRole::octaveUp || role == HarmonyRole::choir;
            if (wantsAbove && candidate <= leadMidi) score -= 4.0;
            if (! wantsAbove && candidate >= leadMidi) score -= 4.0;

            if (score > bestScore) { bestScore = score; best = candidate; }
        }
        return best >= 0 ? best : desired;
    }

    std::array<int, 4> previousMidi { -1, -1, -1, -1 };
};

} // namespace sattari::dsp
