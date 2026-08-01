// Neural key engine smoke test — does the code path RUN, and are its numbers sane?
//
// THIS IS NOT AN ACCURACY TEST, for the same reason stemdeck_neural_beat_smoke.cpp
// is not one: the audio is generated, and this repo's standing rule is that
// synthetic material certifies nothing about real music. StemDeckCorpusGate is
// where key accuracy is decided.
//
// What this checks: until this audit, NeuralKeyEngine::defaultModelFile() had no
// env-var override and no bundled-Resources check (unlike NeuralBeatEngine and
// NeuralStemEngine), so the only way to load a model was the exact hardcoded
// ~/Library/Sattari Audio/models/keynet.onnx path — untestable in isolation and
// unbundlable into a packaged app. This test proves the model, once reachable,
// actually loads and produces a confidence value that discriminates between
// clearly-tonal and non-tonal material, i.e. that kNeuralKeyConfidenceFloor is
// gating something real rather than a value nobody has ever looked at.
//
// Skips cleanly (exit 0) when ONNX is compiled out or no model is installed, so
// it is safe in the ordinary contract suite.

#include "NeuralKeyEngine.h"
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

// A sustained C major triad (C4/E4/G4) plus a C2 bass note and light harmonic
// content — an unambiguous, textbook tonal center. Still synthetic; still
// proves nothing about real music, per the file header.
juce::AudioBuffer<float> tonalCMajor (double sampleRate, int seconds)
{
    const int n = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    const double freqs[] { 65.41 /*C2*/, 261.63 /*C4*/, 329.63 /*E4*/, 392.00 /*G4*/ };
    const double gains[] { 0.35, 0.22, 0.18, 0.18 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        double s = 0.0;
        for (size_t h = 0; h < 4; ++h)
        {
            // Fundamental + a light 2nd harmonic per partial, so the spectrum
            // isn't pure sine tones (a bit more like a real instrument).
            s += gains[h] * std::sin (t * freqs[h] * juce::MathConstants<double>::twoPi);
            s += gains[h] * 0.15 * std::sin (t * freqs[h] * 2.0 * juce::MathConstants<double>::twoPi);
        }
        b.setSample (0, i, (float) (s * 0.6));
        b.setSample (1, i, (float) (s * 0.6));
    }
    return b;
}

// White noise: no harmonic content, no tonal center at all.
juce::AudioBuffer<float> noise (double sampleRate, int seconds)
{
    const int n = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (99117);
    for (int i = 0; i < n; ++i)
    {
        const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.3f;
        b.setSample (0, i, s);
        b.setSample (1, i, s);
    }
    return b;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // NeuralKeyEngine has no static isCompiledIn() (unlike NeuralBeatEngine and
    // NeuralStemEngine) — SattariKeyNet::available() already reports false safely
    // when SATTARI_HAS_ONNX is off, so loadDefaultModel()/isAvailable() alone is
    // the correct and sufficient compiled-in-or-not check for this engine.
    NeuralKeyEngine engine;
    if (! engine.loadDefaultModel() || ! engine.isAvailable())
    {
        std::cout << "NEURAL_KEY_SMOKE_SKIPPED (no key model installed; searched env "
                  << "SATTARI_STEMDECK_KEY_MODEL, bundled Resources, and "
                  << NeuralKeyEngine::defaultModelFile().getFullPathName() << ")\n";
        return 0;
    }

    constexpr double sr = 44100.0;
    std::cout << "neural key engine smoke test (generated audio — NOT an accuracy check)\n";

    const auto tonal = tonalCMajor (sr, 20);
    const auto r = engine.detect (tonal, sr);

    static const char* const kNames[12] {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::cout << "  C-major triad: valid=" << (r.valid ? "true" : "false")
              << " root=" << (r.valid ? kNames[juce::jlimit (0, 11, r.root)] : "?")
              << (r.minor ? "m" : "")
              << " confidence=" << juce::String (r.confidence, 4) << "\n";

    check (r.valid, "model produced a result");
    check (std::isfinite (r.confidence), "confidence is finite (not NaN/inf)");
    check (r.confidence >= 0.0f && r.confidence <= 1.0f, "confidence is within [0, 1]");
    check (r.confidence > 0.0f && r.confidence < 1.0f,
           "confidence is not pinned at an endpoint");

    const auto flatNoise = noise (sr, 20);
    const auto nres = engine.detect (flatNoise, sr);
    std::cout << "  white noise:   valid=" << (nres.valid ? "true" : "false")
              << " confidence=" << juce::String (nres.confidence, 4) << "\n";

    check (! nres.valid || nres.confidence < r.confidence,
           "confidence discriminates: noise scores below a clean tonal signal");

    constexpr float kFloor = 0.45f;   // MainPluginProcessor::kNeuralKeyConfidenceFloor
    std::cout << "  floor check: tonal-signal confidence " << juce::String (r.confidence, 4)
              << (r.confidence >= kFloor ? " >= " : " <  ") << juce::String (kFloor, 2)
              << (r.confidence >= kFloor ? "  (would override DSP)" : "  (DSP would stand)") << "\n";

    // Root correctness on generated audio is explicitly NOT asserted — see the
    // file header. A real accuracy claim needs StemDeckCorpusGate and real music.
    std::cout << (failures == 0 ? "NEURAL_KEY_SMOKE_OK\n" : "NEURAL_KEY_SMOKE_FAIL\n");
    return failures == 0 ? 0 : 1;
}
