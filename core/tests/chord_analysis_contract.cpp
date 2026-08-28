// Contract for sattari::dsp::ChordAnalyzer and the chord/modulation timeline.
//
// The rules this locks are the product's honesty rules, not just correctness:
//   1. unambiguous triads identify correctly;
//   2. a seventh is only reported when the seventh is actually there
//      (complexity must be EARNED — a wrong maj7 is worse than a right maj);
//   3. atonal mush produces a LOW margin, so the gates can reject it;
//   4. chord regions drop low-confidence spans instead of gluing them on;
//   5. modulation classification distinguishes a key change from a modal
//      shift, a relative shift, and a weak short region (tonicization).
#include "sattari_audio_core/SattariChordAnalysis.h"

#include <cstdio>
#include <vector>

namespace
{
static const char* kNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
int failures = 0;

void expectChord (const char* label, const double* chroma, int wantRoot,
                  sattari::dsp::ChordQuality wantQuality)
{
    sattari::dsp::ChordAnalyzer a;
    const auto r = a.analyse (chroma);
    const bool ok = r.valid && r.root == wantRoot && r.quality == wantQuality;
    std::printf ("  %-26s -> %s%-4s margin %.4f  %s\n", label,
                 r.valid ? kNames[r.root] : "-",
                 sattari::dsp::chordQualityName (r.quality), r.confidence,
                 ok ? "ok" : "FAIL");
    if (! ok) ++failures;
}
} // namespace

int main()
{
    std::printf ("chord analysis contract\n");

    //  1 + 2. Identification, and sevenths only when earned.
    const double cmaj[12]  = { 1,0,0,0, 1,0,0,1, 0,0,0,0 };
    const double amin[12]  = { 1,0,0,0, 1,0,0,0, 0,1,0,0 };
    const double g7[12]    = { 0,0,1,0, 0,1,0,1, 0,0,0,1 };
    const double cmaj7[12] = { 1,0,0,0, 1,0,0,1, 0,0,0,1 };
    const double fsus4[12] = { 0,0,0,0, 0,1,0,0, 0,0,1,0 };   // F Bb C
    expectChord ("C major",  cmaj,  0, sattari::dsp::ChordQuality::major);
    expectChord ("A minor",  amin,  9, sattari::dsp::ChordQuality::minor);
    expectChord ("G7",       g7,    7, sattari::dsp::ChordQuality::dom7);
    expectChord ("Cmaj7",    cmaj7, 0, sattari::dsp::ChordQuality::maj7);
    {
        //  fsus4 has F, Bb, C — the sus4 template. Root F = 5.
        double c[12] = {}; c[5] = 1; c[10] = 1; c[0] = 1;
        expectChord ("F sus4", c, 5, sattari::dsp::ChordQuality::sus4);
    }
    (void) fsus4;

    //  A plain triad must NOT be promoted to a seventh.
    {
        sattari::dsp::ChordAnalyzer a;
        const auto r = a.analyse (cmaj);
        const bool ok = r.quality == sattari::dsp::ChordQuality::major;
        std::printf ("  %-26s -> %s  %s\n", "plain triad stays triad",
                     sattari::dsp::chordQualityName (r.quality), ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    //  3. Mush must produce a low margin (the gate's job, not the analyzer's).
    {
        double flat[12]; for (auto& v : flat) v = 1.0;
        sattari::dsp::ChordAnalyzer a;
        const auto r = a.analyse (flat);
        const bool ok = r.confidence < 0.05;
        std::printf ("  %-26s -> margin %.4f  %s\n", "atonal mush is low-margin",
                     r.confidence, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    //  4. Region building drops low-confidence frames rather than merging them.
    {
        sattari::dsp::ChordAnalyzer a;
        std::vector<sattari::dsp::ChordResult> frames;
        for (int i = 0; i < 8; ++i) frames.push_back (a.analyse (cmaj));
        double flat[12]; for (auto& v : flat) v = 1.0;
        for (int i = 0; i < 4; ++i) frames.push_back (a.analyse (flat));
        for (int i = 0; i < 8; ++i) frames.push_back (a.analyse (g7));
        const auto regions = sattari::dsp::songmodel::buildChordRegions (frames, 0.75, 15.0);
        for (const auto& r : regions)
            std::printf ("      %.1f-%.1f %s%s agreement %.2f\n", r.startSec, r.endSec,
                         kNames[r.chord.root], sattari::dsp::chordQualityName (r.chord.quality),
                         r.chord.confidence);
        const bool ok = regions.size() == 2
                     && regions[0].chord.root == 0 && regions[1].chord.root == 7;
        std::printf ("  %-26s -> %d regions  %s\n", "uncertain span is a GAP",
                     (int) regions.size(), ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    //  5. Modulation classification.
    {
        using sattari::dsp::ScaleRegion;
        using sattari::dsp::ModulationType;
        std::vector<ScaleRegion> regions {
            { 0.0,  60.0, 0, true,  0.30, false },   // C minor
            { 60.0, 120.0, 0, false, 0.30, false },  // C major   -> modal shift
            { 120.0, 180.0, 9, true, 0.30, false },  // A minor   -> relative shift
            { 180.0, 195.0, 2, false, 0.05, false }, // D major short+weak -> tonicization
            { 195.0, 260.0, 5, false, 0.30, false }, // F major   -> key change
        };
        const auto mods = sattari::dsp::songmodel::classifyModulations (regions);
        const bool ok = mods.size() == 4
            && mods[0].type == ModulationType::modalShift
            && mods[1].type == ModulationType::relativeShift
            && mods[2].type == ModulationType::possibleTonicization
            && mods[3].type == ModulationType::keyChange;
        for (const auto& m : mods)
            std::printf ("    %s %s -> %s %s : %s\n",
                         kNames[m.fromRoot], m.fromMinor ? "min" : "maj",
                         kNames[m.toRoot], m.toMinor ? "min" : "maj",
                         sattari::dsp::modulationTypeName (m.type));
        std::printf ("  %-26s -> %s\n", "modulation types", ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    //  Diatonic vs borrowed.
    {
        const bool dia = sattari::dsp::ChordAnalyzer::chordIsDiatonic (
            5, sattari::dsp::ChordQuality::major, 0, false);           // F in C major
        const bool bor = sattari::dsp::ChordAnalyzer::chordIsDiatonic (
            8, sattari::dsp::ChordQuality::major, 0, false);           // Ab in C major
        const bool ok = dia && ! bor;
        std::printf ("  %-26s -> F=%d Ab=%d  %s\n", "diatonic vs borrowed",
                     (int) dia, (int) bor, ok ? "ok" : "FAIL");
        if (! ok) ++failures;
    }

    std::printf (failures == 0 ? "chord analysis contract ok\n"
                               : "chord analysis contract FAILED\n");
    return failures == 0 ? 0 : 1;
}
