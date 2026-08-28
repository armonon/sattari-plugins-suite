// =============================================================================
//  Sattari — chord analysis: chroma-template matching with honest complexity
// -----------------------------------------------------------------------------
//  Matches a 12-bin chroma frame against chord templates. Two design rules,
//  both consequences of the suite's No Result philosophy:
//
//  1. COMPLEXITY MUST BE EARNED. Every seventh/sus template competes against
//     its parent triad with a handicap: "Cmaj7" is only reported when the
//     major-seventh bin genuinely adds evidence, because a wrong "maj7" label
//     is worse than a right "maj". (Extended chords beyond sevenths/sus are
//     future work — better absent than guessed.)
//
//  2. CONFIDENCE IS A MARGIN, the same winner-minus-runner-up quantity every
//     other detector in the suite reports, so the existing gates keep their
//     meaning when they meet chord data.
//
//  Shared by the offline song analysis (chord timeline) and the live path
//  (current chord). Header-only, allocation-free per call after construction.
// =============================================================================
#pragma once

#include <sattari_audio_core/SattariSongModel.h>

#include <array>
#include <cstring>

namespace sattari::dsp
{

class ChordAnalyzer
{
public:
    //  chroma: 12 bins, any non-negative scale (normalised internally).
    //  scaleRoot/scaleMinor: the surrounding key, used only as a mild DIATONIC
    //  PRIOR — a borrowed chord still wins when the audio says so, it just needs
    //  slightly more evidence than a diatonic one. Pass scaleRoot = -1 for none.
    ChordResult analyse (const double* chroma, int scaleRoot = -1,
                         bool scaleMinor = false) const noexcept
    {
        double norm[12] {};
        double total = 0.0;
        for (int i = 0; i < 12; ++i) total += chroma[i] > 0.0 ? chroma[i] : 0.0;
        if (total < 1.0e-9) return {};
        for (int i = 0; i < 12; ++i) norm[i] = (chroma[i] > 0.0 ? chroma[i] : 0.0) / total;

        double best = -1.0, second = -1.0;
        int bestRoot = 0; ChordQuality bestQ = ChordQuality::major;

        for (int root = 0; root < 12; ++root)
        {
            for (int q = 0; q < (int) ChordQuality::count; ++q)
            {
                const auto& t = templates()[(size_t) q];
                double score = 0.0;
                for (int n = 0; n < t.count; ++n)
                    score += t.weight[n] * norm[(root + t.interval[n]) % 12];
                //  Energy OUTSIDE the chord argues against it.
                double outside = 0.0;
                for (int i = 0; i < 12; ++i)
                {
                    bool inChord = false;
                    for (int n = 0; n < t.count; ++n)
                        if ((root + t.interval[n]) % 12 == i) { inChord = true; break; }
                    if (! inChord) outside += norm[i];
                }
                score -= 0.35 * outside;
                //  Complexity handicap: sevenths and sus must EARN their label.
                score -= t.handicap;
                //  Diatonic prior: mild, so borrowed chords remain reachable.
                if (scaleRoot >= 0 && ! chordIsDiatonic (root, (ChordQuality) q, scaleRoot, scaleMinor))
                    score -= 0.02;

                if (score > best)
                {
                    //  The runner-up must be a genuinely different CHORD, not the
                    //  same triad under another quality name at the same root —
                    //  otherwise Cmaj vs Cmaj7 competition destroys the margin of
                    //  an unambiguous C chord.
                    if (root != bestRoot) second = best;
                    best = score; bestRoot = root; bestQ = (ChordQuality) q;
                }
                else if (score > second && root != bestRoot)
                {
                    second = score;
                }
            }
        }

        ChordResult r;
        r.valid = best > 0.05;
        r.root = bestRoot;
        r.quality = bestQ;
        r.confidence = juce::jlimit (0.0, 1.0, best - (second > -1.0 ? second : 0.0));
        const auto& t = templates()[(size_t) bestQ];
        for (int n = 0; n < t.count && n < 4; ++n)
            r.notes[(size_t) n] = (bestRoot + t.interval[n]) % 12;
        return r;
    }

    //  Is this chord built purely from the given key's scale tones?
    static bool chordIsDiatonic (int root, ChordQuality q,
                                 int scaleRoot, bool scaleMinor) noexcept
    {
        //  Natural minor for minor keys — the honest default; harmonic-minor
        //  chords (V7 in minor) will read as "borrowed", which is arguably the
        //  more informative label anyway.
        static constexpr int majorPcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static constexpr int minorPcs[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* pcs = scaleMinor ? minorPcs : majorPcs;
        const auto& t = templates()[(size_t) q];
        for (int n = 0; n < t.count; ++n)
        {
            const int pc = ((root + t.interval[n]) - scaleRoot % 12 + 24) % 12;
            bool inScale = false;
            for (int i = 0; i < 7; ++i) if (pcs[i] == pc) { inScale = true; break; }
            if (! inScale) return false;
        }
        return true;
    }

private:
    struct Template
    {
        int count;
        int interval[4];
        double weight[4];
        double handicap;
    };

    static const std::array<Template, (size_t) ChordQuality::count>& templates() noexcept
    {
        static const std::array<Template, (size_t) ChordQuality::count> t = { {
            { 3, { 0, 4, 7, 0 },  { 1.0, 0.85, 0.9, 0.0 },  0.00 },   // major
            { 3, { 0, 3, 7, 0 },  { 1.0, 0.85, 0.9, 0.0 },  0.00 },   // minor
            { 3, { 0, 3, 6, 0 },  { 1.0, 0.85, 0.85, 0.0 }, 0.015 },  // dim
            { 3, { 0, 4, 8, 0 },  { 1.0, 0.85, 0.85, 0.0 }, 0.015 },  // aug
            { 3, { 0, 2, 7, 0 },  { 1.0, 0.8, 0.9, 0.0 },   0.02 },   // sus2
            { 3, { 0, 5, 7, 0 },  { 1.0, 0.8, 0.9, 0.0 },   0.02 },   // sus4
            { 4, { 0, 4, 7, 11 }, { 1.0, 0.8, 0.85, 0.75 }, 0.03 },   // maj7
            { 4, { 0, 3, 7, 10 }, { 1.0, 0.8, 0.85, 0.75 }, 0.03 },   // min7
            { 4, { 0, 4, 7, 10 }, { 1.0, 0.8, 0.85, 0.75 }, 0.03 },   // dom7
        } };
        return t;
    }
};

namespace songmodel
{
    //  The pitch classes of a chord, so a region's notes always match its label.
    inline std::array<int, 4> notesFor (int root, ChordQuality q) noexcept
    {
        std::array<int, 4> n { -1, -1, -1, -1 };
        static const int iv[(size_t) ChordQuality::count][4] = {
            { 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 3, 6, -1 }, { 0, 4, 8, -1 },
            { 0, 2, 7, -1 }, { 0, 5, 7, -1 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 },
            { 0, 4, 7, 10 },
        };
        const auto& row = iv[(size_t) q];
        for (int i = 0; i < 4; ++i)
            n[(size_t) i] = row[i] >= 0 ? ((root + row[i]) % 12) : -1;
        return n;
    }

    //  Chord frames -> regions.
    //
    //  A per-frame chord margin is NOT a usable confidence on real polyphonic
    //  audio: measured across GiantSteps tracks the median margin is ~0.012 and
    //  the max ~0.06, because real chords share most of their notes and an HPCP
    //  frame is never as clean as a template. Thresholding that raw number would
    //  either pass everything or nothing.
    //
    //  So confidence here is AGREEMENT, the same idea that fixed the Key Map
    //  (167 flicker segments -> 1): majority-vote each frame against its
    //  neighbours, then report what fraction of a region's frames actually voted
    //  for its chord. That is a calibrated 0..1 quantity, comparable with every
    //  other confidence in the suite, and a chord nobody agrees on scores low no
    //  matter how confidently a single frame asserted it.
    inline std::vector<ChordRegion> buildChordRegions (
        const std::vector<ChordResult>& framesIn, double frameSeconds,
        double durationSec, double minAgreement = 0.60, double minChordSec = 0.9)
    {
        std::vector<ChordRegion> out;
        if (framesIn.empty() || frameSeconds <= 0.0) return out;

        auto id = [] (const ChordResult& c)
        { return c.valid ? c.root * (int) ChordQuality::count + (int) c.quality : -1; };

        //  1) 5-frame majority vote: one confused frame cannot split a chord,
        //     one lucky frame cannot create one.
        std::vector<int> ids (framesIn.size());
        for (size_t i = 0; i < framesIn.size(); ++i) ids[i] = id (framesIn[i]);
        std::vector<int> voted (ids.size());
        for (size_t i = 0; i < ids.size(); ++i)
        {
            const size_t lo = i >= 2 ? i - 2 : 0;
            const size_t hi = std::min (ids.size() - 1, i + 2);
            int bestId = -1, bestCount = 0;
            for (size_t a = lo; a <= hi; ++a)
            {
                if (ids[a] < 0) continue;
                int count = 0;
                for (size_t b = lo; b <= hi; ++b) if (ids[b] == ids[a]) ++count;
                if (count > bestCount) { bestCount = count; bestId = ids[a]; }
            }
            voted[i] = bestCount >= 2 ? bestId : -1;
        }

        //  2) run-length encode the voted stream.
        size_t runStart = 0;
        for (size_t i = 1; i <= voted.size(); ++i)
        {
            if (i < voted.size() && voted[i] == voted[runStart]) continue;
            if (voted[runStart] >= 0)
            {
                const double start = (double) runStart * frameSeconds;
                const double end = std::min (durationSec > 0.0 ? durationSec : 1.0e9,
                                             (double) i * frameSeconds);
                //  3) agreement = fraction of the ORIGINAL frames in this span
                //     that independently chose this chord.
                int agree = 0, total = 0;
                for (size_t j = runStart; j < i; ++j)
                { ++total; if (ids[j] == voted[runStart]) ++agree; }
                const double agreement = total > 0 ? (double) agree / (double) total : 0.0;

                if (end - start >= minChordSec && agreement >= minAgreement)
                {
                    ChordRegion r;
                    r.startSec = start; r.endSec = end;
                    r.chord = framesIn[runStart];
                    r.chord.root    = voted[runStart] / (int) ChordQuality::count;
                    r.chord.quality = (ChordQuality) (voted[runStart] % (int) ChordQuality::count);
                    r.chord.valid = true;
                    r.chord.confidence = agreement;
                    //  Recompute the notes FROM the voted chord. Copying them
                    //  from the run's first raw frame published a chord whose
                    //  displayed name and note list could disagree — and harmony
                    //  planning reads those notes.
                    r.chord.notes = notesFor (r.chord.root, r.chord.quality);
                    out.push_back (r);
                }
            }
            runStart = i;
        }
        return out;
    }

    //  Classify what happens at each scale-region boundary. One chromatic chord
    //  can NOT create one of these — regions already passed the smoothing/merge
    //  discipline, so a boundary here is at least ~20 s of consistent evidence.
    inline std::vector<ModulationEvent> classifyModulations (
        const std::vector<ScaleRegion>& regions, double shortRegionSec = 25.0)
    {
        std::vector<ModulationEvent> out;
        for (size_t i = 1; i < regions.size(); ++i)
        {
            const auto& a = regions[i - 1];
            const auto& b = regions[i];
            ModulationEvent e;
            e.atSec = b.startSec;
            e.fromRoot = a.root; e.fromMinor = a.minor;
            e.toRoot = b.root;   e.toMinor = b.minor;
            e.confidence = juce::jmin (a.confidence, b.confidence);

            const bool shortAndWeak = (b.endSec - b.startSec) < shortRegionSec
                                   && b.confidence < 0.15;
            if (shortAndWeak)
                e.type = ModulationType::possibleTonicization;
            else if (a.root == b.root && a.minor != b.minor)
                e.type = ModulationType::modalShift;
            else if (a.minor != b.minor
                     && ((! a.minor && b.root == (a.root + 9) % 12)
                         || (a.minor && b.root == (a.root + 3) % 12)))
                e.type = ModulationType::relativeShift;
            else
                e.type = ModulationType::keyChange;
            out.push_back (e);
        }
        return out;
    }
} // namespace songmodel

} // namespace sattari::dsp
