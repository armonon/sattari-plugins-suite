// =============================================================================
//  Sattari — the shared song model
// -----------------------------------------------------------------------------
//  ONE representation of "what is happening musically in this song", consumed by
//  every workspace (KEY reads all of it, TUNE will follow its scale regions,
//  HARMONY will read its chords once Phase 2 adds them). The alternative — each
//  UI page re-deriving its own view of the same audio — is how the suite ended
//  up with two key detectors publishing different answers to one bus.
//
//  Phase 1 carries what the current analysers can HONESTLY produce:
//  global key / tempo / tuning, time-aware scale regions, neutral section
//  boundaries, a display waveform, and a confidence for every conclusion.
//  Chords, modulation classification, and named sections (Verse/Chorus) are
//  Phase 2: the structures leave room for them, but nothing here pretends to
//  know them yet. Uncertain regions carry their uncertainty; they are never
//  silently promoted to fact.
// =============================================================================
#pragma once

#include <sattari_audio_core/SattariKeyAnalysis.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace sattari::dsp
{

//  One contiguous span in which a single key/scale best explains the music.
struct ScaleRegion
{
    double startSec = 0.0;
    double endSec   = 0.0;
    int    root     = 0;        // 0..11 (C..B)
    bool   minor    = false;
    double confidence = 0.0;    // mean winner-minus-runner-up margin of its span
    //  A user edit outranks detection permanently and survives re-analysis of
    //  everything around it. Manual regions also report full confidence to the
    //  trust gate: the user asserting a key IS the evidence.
    bool   userSet = false;
};

//  A structural span. Phase 1 names are NEUTRAL ("Section A") — the analyser can
//  hear boundaries (key changes, sustained level shifts) long before it can
//  defend calling something a Chorus, and the No Result philosophy applies to
//  labels too. The user renames them; `userNamed` protects that from re-analysis.
struct SongSection
{
    double startSec = 0.0;
    double endSec   = 0.0;
    std::string name;           // "Section A", or whatever the user typed
    bool   userNamed = false;
};

enum class ChordQuality
{
    major = 0, minor, dim, aug, sus2, sus4, maj7, min7, dom7,
    count
};

inline const char* chordQualityName (ChordQuality q) noexcept
{
    switch (q)
    {
        case ChordQuality::major: return "";
        case ChordQuality::minor: return "m";
        case ChordQuality::dim:   return "dim";
        case ChordQuality::aug:   return "aug";
        case ChordQuality::sus2:  return "sus2";
        case ChordQuality::sus4:  return "sus4";
        case ChordQuality::maj7:  return "maj7";
        case ChordQuality::min7:  return "m7";
        case ChordQuality::dom7:  return "7";
        case ChordQuality::count: return "";
    }
    return "";
}

struct ChordResult
{
    bool  valid = false;
    int   root  = 0;                       // 0..11
    ChordQuality quality = ChordQuality::major;
    double confidence = 0.0;               // winner-minus-runner-up margin
    std::array<int, 4> notes { -1, -1, -1, -1 };   // pitch classes, -1 = unused
};

//  --- timeline structures, appended to the song model --------------------------

struct ChordRegion
{
    double startSec = 0.0, endSec = 0.0;
    ChordResult chord;
    bool diatonic = true;      // vs the scale region it sits in
};

enum class ModulationType { keyChange, modalShift, relativeShift, possibleTonicization };

inline const char* modulationTypeName (ModulationType t) noexcept
{
    switch (t)
    {
        case ModulationType::keyChange:            return "key change";
        case ModulationType::modalShift:           return "modal shift";
        case ModulationType::relativeShift:        return "relative shift";
        case ModulationType::possibleTonicization: return "possible tonicization";
        default:                                   return "";
    }
}

struct ModulationEvent
{
    double atSec = 0.0;
    int fromRoot = 0; bool fromMinor = false;
    int toRoot = 0;   bool toMinor = false;
    ModulationType type = ModulationType::keyChange;
    double confidence = 0.0;   // min of the two regions' margins
};


struct SongAnalysis
{
    bool   valid = false;
    double durationSec = 0.0;

    KeyAnalysisResult globalKey;        // tonal centre of the WHOLE file
    KeyCandidate      alternative;      // runner-up, for the honesty display

    double bpm = 0.0;                   // 0 = tempo not established
    double tempoConfidence = 0.0;
    bool   tempoOctaveAmbiguous = false;

    double detuneCents = 0.0;           // global tuning offset vs A440
    double tuningHz() const noexcept { return 440.0 * std::pow (2.0, detuneCents / 1200.0); }

    std::vector<ScaleRegion> scaleRegions;
    std::vector<SongSection> sections;

    //  Phase 2. Declared here so every workspace sees one model; populated by
    //  the offline analysis. Empty is a legitimate state and means "not known",
    //  never "no chords".
    std::vector<ChordRegion>     chordRegions;
    std::vector<ModulationEvent> modulations;

    //  Peak envelope for drawing, fixed bin count; empty until analysed.
    std::vector<float> waveform;
};

namespace songmodel
{
    //  Collapse the estimator's fixed ~15 s section verdicts into musically
    //  meaningful scale regions. Same discipline as the validated Key Map scan
    //  (167 flicker segments -> 1 true segment on a real song): smooth single
    //  outliers by neighbour vote, then run-length-encode, then merge regions
    //  shorter than minRegionSec into whichever neighbour they agree with more.
    inline std::vector<ScaleRegion> buildScaleRegions (
        const std::vector<KeyAnalysisResult>& sectionKeys,
        double sectionSeconds, double durationSec, double minRegionSec = 20.0)
    {
        std::vector<ScaleRegion> out;
        if (sectionKeys.empty() || sectionSeconds <= 0.0) return out;

        //  1) copy the per-section verdicts, smoothing single-section outliers:
        //     a section disagreeing with BOTH identical neighbours takes theirs.
        auto keyId = [] (const KeyAnalysisResult& k) { return k.root * 2 + (k.minor ? 1 : 0); };
        std::vector<int> ids;
        ids.reserve (sectionKeys.size());
        for (const auto& k : sectionKeys) ids.push_back (k.valid ? keyId (k) : -1);
        for (size_t i = 1; i + 1 < ids.size(); ++i)
            if (ids[i] != ids[i - 1] && ids[i - 1] == ids[i + 1] && ids[i - 1] >= 0)
                ids[i] = ids[i - 1];

        //  2) run-length encode into regions, averaging the margins.
        size_t runStart = 0;
        for (size_t i = 1; i <= ids.size(); ++i)
        {
            if (i < ids.size() && ids[i] == ids[runStart]) continue;
            if (ids[runStart] >= 0)
            {
                ScaleRegion r;
                r.startSec = (double) runStart * sectionSeconds;
                r.endSec   = juce::jmin (durationSec > 0.0 ? durationSec : 1.0e9,
                                         (double) i * sectionSeconds);
                r.root  = ids[runStart] / 2;
                r.minor = (ids[runStart] % 2) != 0;
                double sum = 0.0; int n = 0;
                for (size_t j = runStart; j < i; ++j)
                    if (sectionKeys[j].valid) { sum += sectionKeys[j].confidence; ++n; }
                r.confidence = n > 0 ? sum / n : 0.0;
                out.push_back (r);
            }
            runStart = i;
        }

        //  3) absorb short regions into the more-agreeing neighbour. A 15 s
        //     wobble between two long same-key spans is detector noise, not a
        //     modulation — this is exactly the flicker the Key Map fix removed.
        bool merged = true;
        while (merged && out.size() > 1)
        {
            merged = false;
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (out[i].endSec - out[i].startSec >= minRegionSec) continue;
                const bool hasPrev = i > 0, hasNext = i + 1 < out.size();
                if (! hasPrev && ! hasNext) break;
                const size_t into = ! hasPrev ? i + 1
                                  : ! hasNext ? i - 1
                                  : (out[i - 1].endSec - out[i - 1].startSec
                                         >= out[i + 1].endSec - out[i + 1].startSec ? i - 1 : i + 1);
                if (into < i) out[into].endSec = out[i].endSec;
                else          out[into].startSec = out[i].startSec;
                out.erase (out.begin() + (long) i);
                merged = true;
                break;
            }
        }
        //  Re-join neighbours that became identical after absorption.
        for (size_t i = 1; i < out.size();)
        {
            if (out[i].root == out[i - 1].root && out[i].minor == out[i - 1].minor)
            {
                out[i - 1].endSec = out[i].endSec;
                out[i - 1].confidence = 0.5 * (out[i - 1].confidence + out[i].confidence);
                out.erase (out.begin() + (long) i);
            }
            else ++i;
        }
        if (! out.empty() && durationSec > 0.0)
            out.back().endSec = durationSec;
        return out;
    }

    //  Neutral structural sections. Phase 1 hears two kinds of boundary it can
    //  defend: a scale-region change, and a sustained loudness shift (the
    //  arrangement getting bigger or smaller). Names are "Section A/B/C" —
    //  calling something a Chorus needs repetition analysis this phase does not
    //  have, and a wrong confident label is worse than a neutral one.
    inline std::vector<SongSection> deriveSections (
        const std::vector<ScaleRegion>& regions,
        const std::vector<float>& rmsPerBin, double durationSec,
        double minSectionSec = 12.0)
    {
        std::vector<double> boundaries { 0.0 };
        for (const auto& r : regions)
            if (r.startSec > 1.0) boundaries.push_back (r.startSec);

        //  Sustained level shifts: compare 8-bin windows either side of each bin;
        //  a step of >= 6 dB that HOLDS is an arrangement change, not a hit.
        if (rmsPerBin.size() > 32 && durationSec > 0.0)
        {
            const double binSec = durationSec / (double) rmsPerBin.size();
            const int w = 8;
            for (int i = w; i + w < (int) rmsPerBin.size(); ++i)
            {
                double before = 0.0, after = 0.0;
                for (int j = 0; j < w; ++j)
                {
                    before += rmsPerBin[(size_t) (i - 1 - j)];
                    after  += rmsPerBin[(size_t) (i + j)];
                }
                before /= w; after /= w;
                if (before < 1.0e-6 && after < 1.0e-6) continue;
                const double ratio = (after + 1.0e-9) / (before + 1.0e-9);
                if (ratio > 2.0 || ratio < 0.5)
                {
                    const double t = (double) i * binSec;
                    bool near = false;
                    for (const double b : boundaries)
                        if (std::abs (b - t) < minSectionSec) { near = true; break; }
                    if (! near) boundaries.push_back (t);
                }
            }
        }

        std::sort (boundaries.begin(), boundaries.end());
        std::vector<SongSection> out;
        for (size_t i = 0; i < boundaries.size(); ++i)
        {
            SongSection s;
            s.startSec = boundaries[i];
            s.endSec   = i + 1 < boundaries.size() ? boundaries[i + 1] : durationSec;
            if (s.endSec - s.startSec < 1.0) continue;
            s.name = "Section " + std::string (1, (char) ('A' + (int) (out.size() % 26)));
            out.push_back (s);
        }
        return out;
    }
} // namespace songmodel

} // namespace sattari::dsp
