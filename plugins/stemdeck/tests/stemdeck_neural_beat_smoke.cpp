// Neural beat engine smoke test — does the code path RUN, and are its numbers sane?
//
// THIS IS NOT AN ACCURACY TEST. It uses generated audio, which per this repo's
// own standing rule certifies nothing about real music. StemDeckCorpusGate is
// where accuracy is decided.
//
// What it does check is the thing that had never been checked at all: until ONNX
// was enabled, NeuralBeatEngine's entire implementation was #if'd out, so its
// confidence computation had never been COMPILED, let alone executed. Enabling
// ONNX found two defects in a single line of it. The remaining risk is that the
// confidence value is computed but degenerate — pinned at 0 or 1, NaN, or
// identical for a strong beat and for silence — in which case
// kNeuralBeatConfidenceFloor is gating nothing and the §7 "override is now
// gated" claim is hollow.
//
// So: run the model on a click track and on silence, and assert the confidence
// is finite, in range, not pinned, and DISCRIMINATES between the two.
//
// Skips cleanly (exit 0) when ONNX is compiled out or no model is installed, so
// it is safe in the ordinary contract suite.

#include "NeuralBeatEngine.h"
#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// A click track with broadband transients — the model needs onsets to find, and
// a pure tone has none. Still synthetic; still proves nothing about real music.
juce::AudioBuffer<float> clickTrack (double sampleRate, double bpm, int seconds)
{
    const int n = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (4242);
    const double beat = 60.0 / bpm;

    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        const double phase = std::fmod (t, beat);
        // Short decaying noise burst on each beat, with a stronger downbeat every 4.
        if (phase < 0.03)
        {
            const bool downbeat = ((int) (t / beat) % 4) == 0;
            const double env = std::exp (-phase * 140.0) * (downbeat ? 1.0 : 0.6);
            const float s = (float) ((rng.nextDouble() * 2.0 - 1.0) * env * 0.7);
            b.addSample (0, i, s);
            b.addSample (1, i, s);
        }
        // Quiet bed so the track is not digital silence between hits.
        const float bed = (float) (0.02 * std::sin (t * 110.0 * juce::MathConstants<double>::twoPi));
        b.addSample (0, i, bed);
        b.addSample (1, i, bed);
    }
    return b;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    if (! NeuralBeatEngine::isCompiledIn())
    {
        std::cout << "NEURAL_BEAT_SMOKE_SKIPPED (ONNX not compiled in)\n";
        return 0;
    }

    NeuralBeatEngine engine;
    if (! engine.loadDefaultModel() || ! engine.isAvailable())
    {
        std::cout << "NEURAL_BEAT_SMOKE_SKIPPED (no beat model installed at "
                  << NeuralBeatEngine::defaultModelFile().getFullPathName()
                  << "; set SATTARI_STEMDECK_BEAT_MODEL to override)\n";
        return 0;
    }

    constexpr double sr = 44100.0;
    constexpr double bpm = 120.0;

    std::cout << "neural beat engine smoke test (generated audio — NOT an accuracy check)\n";

    const auto beats = clickTrack (sr, bpm, 30);
    const auto r = engine.detect (beats, sr);

    std::cout << "  click track: valid=" << (r.valid ? "true" : "false")
              << " bpm=" << juce::String (r.detectedBpm, 2)
              << " confidence=" << juce::String (r.confidence, 4)
              << " beatsPerBar=" << r.beatsPerBar
              << " gridSize=" << (int) r.beatGridSamples.size() << "\n";

    check (r.valid, "model produced a result");
    check (std::isfinite (r.confidence), "confidence is finite (not NaN/inf)");
    check (r.confidence >= 0.0f && r.confidence <= 1.0f, "confidence is within [0, 1]");
    // The failure this exists to catch: a value that is computed but degenerate.
    check (r.confidence > 0.0f && r.confidence < 1.0f,
           "confidence is not pinned at an endpoint");
    check (r.beatGridSamples.size() >= 4, "grid has at least 4 beats");

    // Silence has no beat. If confidence does not drop, it is not measuring
    // anything and the floor cannot gate anything.
    juce::AudioBuffer<float> silence (2, (int) (sr * 30));
    silence.clear();
    const auto s = engine.detect (silence, sr);
    std::cout << "  silence:     valid=" << (s.valid ? "true" : "false")
              << " confidence=" << juce::String (s.confidence, 4) << "\n";

    check (! s.valid || s.confidence < r.confidence,
           "confidence discriminates: silence scores below a click track");

    // Report where it sits relative to the floor the product actually applies —
    // a click track landing below it would mean the gate rejects everything.
    constexpr float kFloor = 0.35f;   // MainPluginProcessor::kNeuralBeatConfidenceFloor
    std::cout << "  floor check: click-track confidence " << juce::String (r.confidence, 4)
              << (r.confidence >= kFloor ? " >= " : " <  ") << juce::String (kFloor, 2)
              << (r.confidence >= kFloor ? "  (would override DSP)" : "  (DSP would stand)") << "\n";

    std::cout << (failures == 0 ? "NEURAL_BEAT_SMOKE_OK\n" : "NEURAL_BEAT_SMOKE_FAIL\n");
    return failures == 0 ? 0 : 1;
}
