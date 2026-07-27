// Headless tests for the §6.2 dynamic key-lock cores: key-over-time curve smoothing
// and the correction plan's threshold + quality-ceiling gates.
#include "KeyCurve.h"
#include "KeyLockPlan.h"
#include "KeyChroma.h"
#include <iostream>
#include <cmath>

// Synthesise a sustained triad (root pitch classes) into a buffer, for key detection.
static juce::AudioBuffer<float> makeTriad (double sr, double seconds, std::initializer_list<int> midiNotes)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (1, n);
    b.clear();
    auto* d = b.getWritePointer (0);
    for (int note : midiNotes)
    {
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        for (int i = 0; i < n; ++i)
            d[i] += 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / sr);
    }
    return b;
}

int main()
{
    using namespace stemdeck;
    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    { std::cout << (ok ? "  OK    " : "  FAIL  ") << what << "\n"; if (! ok) ++fails; };

    // ── Windowing ───────────────────────────────────────────────────────────────
    {
        auto w = keyWindowStarts (128.0, 16.0, 4.0);   // 32-beat... 16-bar window, 4-bar hop
        check (! w.empty() && w.front() == 0.0, "key windows start at 0");
        check (w.back() + 16.0 <= 128.0 + 1e-6, "last key window fits inside the track");
        check (keyWindowStarts (8.0, 16.0, 4.0).size() == 1, "track shorter than the window still yields one window");
        check (keyWindowStarts (0.0, 16.0, 4.0).empty(), "zero-length track yields no windows");
    }

    // ── Median smoothing rejects a single-window flip ───────────────────────────
    {
        // A minor (pc 9) throughout, with ONE bad window reading C (pc 0).
        std::vector<KeyObservation> raw;
        for (int i = 0; i < 7; ++i) raw.push_back ({ (double) i * 4.0, 9, true, 0.8f });
        raw[3] = { 12.0, 0, false, 0.4f };   // the flip
        auto sm = smoothKeyCurve (raw, 3);
        check (sm[3].pitchClass == 9 && sm[3].minor, "median smoothing removes a single-window key flip");
        check (! keyCurveModulates (sm), "a smoothed constant-key track does not read as modulating");
    }

    // ── A real modulation survives smoothing + segments ─────────────────────────
    {
        std::vector<KeyObservation> raw;
        for (int i = 0; i < 4; ++i) raw.push_back ({ (double) i * 4.0, 9, true, 0.8f });   // 8A (A min)
        for (int i = 4; i < 8; ++i) raw.push_back ({ (double) i * 4.0, 11, true, 0.8f });  // 9A (B min)
        auto sm = smoothKeyCurve (raw, 3);
        check (keyCurveModulates (sm), "a sustained modulation is detected");
        auto segs = segmentKeyCurve (sm, 32.0);
        check (segs.size() == 2, "modulation segments into two key regions");
        check (segs.front().pitchClass == 9 && segs.back().pitchClass == 11, "segments carry the right keys");
    }

    // ── Correction plan: threshold + quality-ceiling gates ──────────────────────
    {
        // Curve: mostly A minor (9), drifts +1 st (10) briefly, then jumps a fourth (2).
        std::vector<KeyObservation> curve = {
            { 0.0,  9,  true, 0.9f },
            { 4.0,  10, true, 0.7f },   // +1 st from target -> silent correct
            { 8.0,  2,  true, 0.8f },   // big modulation -> flagged, held
        };
        const int target = 9;   // hold to A
        auto plan = planKeyLock (curve, target, { 2, 5, /*holdModulations*/ true });
        check (plan[0].semitones == 0 && ! plan[0].applied, "in-key point needs no correction");
        check (plan[1].applied && std::abs (plan[1].semitones) == 1, "small drift is corrected silently");
        check (plan[2].modulation && ! plan[2].applied, "large modulation is flagged and held, not auto-flattened");

        // With holdModulations=false the user opted to correct the modulation.
        auto plan2 = planKeyLock (curve, target, { 2, 5, false });
        check (plan2[2].applied, "user can opt to correct the modulation");

        // Quality ceiling flags an audibly-degrading correction (delta 2->9 is 5 wait; use 8->2).
        std::vector<KeyObservation> far = { { 0.0, 3, true, 0.9f } };   // pc 3, target 9 -> +6 st
        auto planFar = planKeyLock (far, 9, { 2, 5, false });
        check (planFar[0].degrade, "corrections past the quality ceiling are flagged as degrading");

        // Correction-at-beat lookup holds the last applied value.
        check (keyLockCorrectionAtBeat (plan, 5.0) == plan[1].semitones, "correction-at-beat returns the active correction");
        check (keyLockCorrectionAtBeat (plan, 9.0) == 0, "held modulation contributes no correction at its beat");
    }

    // ── Chroma key detection on synthetic triads ────────────────────────────────
    {
        const double sr = 44100.0;
        // C major triad: C4(60) E4(64) G4(67) -> pc 0, major.
        auto cmaj = makeTriad (sr, 1.0, { 60, 64, 67 });
        auto kc = detectKeyChroma (cmaj, 0, cmaj.getNumSamples(), sr);
        check (kc.pitchClass == 0 && ! kc.minor, "chroma detects C major triad as C major");
        // A minor triad: A3(57) C4(60) E4(64) -> pc 9, minor.
        auto amin = makeTriad (sr, 1.0, { 57, 60, 64 });
        auto ka = detectKeyChroma (amin, 0, amin.getNumSamples(), sr);
        check (ka.pitchClass == 9 && ka.minor, "chroma detects A minor triad as A minor");
        // A modulating track: A minor for the first half, then C major -> curve modulates.
        juce::AudioBuffer<float> track (1, (int) (sr * 4.0));
        track.clear();
        auto half1 = makeTriad (sr, 2.0, { 57, 60, 64 });   // A minor
        auto half2 = makeTriad (sr, 2.0, { 60, 64, 67 });   // C major
        track.copyFrom (0, 0, half1, 0, 0, half1.getNumSamples());
        track.copyFrom (0, half1.getNumSamples(), half2, 0, 0, half2.getNumSamples());
        auto curve = analyzeKeyCurve (track, sr, 8);
        check (! curve.empty(), "key curve is produced");
        check (keyCurveModulates (curve), "a track that changes key reads as modulating");
    }

    std::cout << (fails == 0 ? "KEYLOCK_OK" : "KEYLOCK_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
