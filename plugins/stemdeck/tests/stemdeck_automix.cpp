// Automix planner check: Camelot harmonic-compatibility math, phrase-aligned mix
// point selection from per-phrase energy, tempo octave folding, and the
// key-clash pitch-shift suggestion. Uses hand-built TrackAnalysis so the planner
// logic is exercised deterministically.
#include "AutomixEngine.h"
#include "TrackAnalysis.h"
#include <iostream>
#include <string>
#include <vector>

using stemdeck::AutomixEngine;

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}

// Build an analysis with a regular grid at `bpm` and a given key.
TrackAnalysis makeAnalysis (double bpm, int tonicPc, bool minor, int numBeats, double sr = 44100.0)
{
    TrackAnalysis a;
    a.sampleRate = sr;
    a.detectedBpm = (float) bpm;
    const double spacing = sr * 60.0 / bpm;
    for (int i = 0; i < numBeats; ++i)
        a.beatGridSamples.push_back ((double) i * spacing);
    a.downbeatSample = 0.0;
    a.beatsPerBar = 4;
    a.durationSeconds = numBeats * spacing / sr;
    a.keyPitchClass = tonicPc;
    a.keyIsMinor = minor;
    a.keyConfidence = 0.9f;
    return a;
}
} // namespace

int main()
{
    // ── Camelot numbering (C major = 8B, G major = 9B, A minor = 8A, E minor = 9A)
    check (AutomixEngine::camelotNumber (0, false) == 8,  "C major -> Camelot 8");
    check (AutomixEngine::camelotNumber (7, false) == 9,  "G major -> Camelot 9");
    check (AutomixEngine::camelotNumber (9, true)  == 8,  "A minor -> Camelot 8");
    check (AutomixEngine::camelotNumber (4, true)  == 9,  "E minor -> Camelot 9");

    // ── Compatibility rules
    {
        auto C  = makeAnalysis (128, 0, false, 8);
        auto G  = makeAnalysis (128, 7, false, 8);   // 9B, adjacent -> compatible
        auto Am = makeAnalysis (128, 9, true,  8);   // 8A, relative minor -> compatible
        auto Cs = makeAnalysis (128, 1, false, 8);   // 3B, distant -> clash
        check (AutomixEngine::keysCompatible (C, G),  "C major + G major compatible (adjacent)");
        check (AutomixEngine::keysCompatible (C, Am), "C major + A minor compatible (relative)");
        check (! AutomixEngine::keysCompatible (C, Cs), "C major + C# major incompatible");
    }

    // ── Phrase-aligned mix points from energy structure
    {
        auto A = makeAnalysis (128, 0, false, 128);   // 128 beats -> 8 phrases of 16
        auto B = makeAnalysis (128, 7, false, 128);
        const auto phrasesA = AutomixEngine::phraseBoundaries (A, 16);
        const auto phrasesB = AutomixEngine::phraseBoundaries (B, 16);
        check (phrasesA.size() == 8, "8 phrase boundaries from 128 beats @ 16/phrase");

        // intro low, main high, outro low.
        std::vector<float> eA = { 0.2f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.3f, 0.2f };
        std::vector<float> eB = { 0.2f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.3f, 0.2f };

        const auto plan = AutomixEngine::planTransition (A, B, eA, eB);
        check (plan.valid, "plan is valid");
        // Out-point: last-third phrase with lowest energy -> phrase index 7 (0.2)
        // among the searchable range [5, n-2]=[5,6]; lowest there is index 6 (0.3).
        check (plan.outPointSampleA == phrasesA[6], "out-point on A lands in the low-energy outro phrase");
        // In-point: first phrase >= median (0.8) after intro -> phrase 1.
        check (plan.inPointSampleB == phrasesB[1], "in-point on B skips intro to first main phrase");
        check (plan.keyCompatible, "plan reports C->G harmonic");
        std::cout << "    plan: " << plan.description.toStdString() << "\n";
    }

    // ── Tempo octave folding
    {
        auto A   = makeAnalysis (128, 0, false, 64);
        auto Bhi = makeAnalysis (64,  7, false, 64);   // half tempo -> fold to x1.0
        auto Bmid= makeAnalysis (100, 7, false, 64);   // 1.28 -> stretch the short way
        const auto p1 = AutomixEngine::planTransition (A, Bhi, {}, {});
        const auto p2 = AutomixEngine::planTransition (A, Bmid, {}, {});
        check (std::abs (p1.tempoRatioBtoA - 1.0) < 1e-9, "64 BPM B folds to x1.0 against 128 BPM A");
        check (std::abs (p2.tempoRatioBtoA - 1.28) < 1e-6, "100 BPM B -> x1.28 against 128 BPM A");
    }

    // ── Clash -> suggested shift makes B compatible
    {
        auto A  = makeAnalysis (128, 0, false, 64);    // C major (8B)
        auto Cs = makeAnalysis (128, 1, false, 64);    // C# major (3B) clash
        const auto plan = AutomixEngine::planTransition (A, Cs, {}, {});
        check (! plan.keyCompatible, "C major + C# major flagged as clash");
        auto shifted = Cs; shifted.keyPitchClass = ((Cs.keyPitchClass + plan.suggestedSemitoneShiftB) % 12 + 12) % 12;
        check (plan.suggestedSemitoneShiftB != 0 && AutomixEngine::keysCompatible (A, shifted),
               "suggested shift makes B harmonically compatible");
        std::cout << "    suggested shift: " << plan.suggestedSemitoneShiftB << " st\n";
    }

    std::cout << (fails == 0 ? "AUTOMIX_OK" : "AUTOMIX_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
