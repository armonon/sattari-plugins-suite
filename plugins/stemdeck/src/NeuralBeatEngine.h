#pragma once

// =============================================================================
//  NeuralBeatEngine — trained neural beat/downbeat tracking for StemDeck.
// -----------------------------------------------------------------------------
//  StemDeck's built-in beat tracker (TrackAnalyzer) is a strong classical DSP
//  pipeline (multiband spectral-flux onsets -> autocorrelation tempo -> Ellis
//  dynamic-programming beat tracker). This adds the trained model djay-class
//  apps use — beat_this (Foscarini/Böck 2024, transformer, SOTA F-measure),
//  exported to ONNX by tools/beat_onnx/export_beat_audioin.py.
//
//  The exported graph is AUDIO-IN: the log-mel front-end (torchaudio slaney mel,
//  verified to 3e-5) is baked into the ONNX via a real conv-STFT, so there is NO
//  C++ mel to keep in sync — the engine only resamples to 22.05 kHz mono and
//  feeds samples. Output is two per-frame activation tracks (beat, downbeat) at
//  ~50 fps that we peak-pick into a beat grid.
//
//  Compiles the real inference only when SATTARI_STEMDECK_HAS_ONNX is defined;
//  otherwise it reports unavailable and callers keep the DSP beat grid. Applied
//  OUTSIDE TrackAnalysis.cpp (which stays ORT-free so the light DSP test targets
//  build standalone) — same pattern as NeuralKeyEngine.
// =============================================================================

#include <JuceHeader.h>
#include <memory>
#include <vector>

class NeuralBeatEngine
{
public:
    NeuralBeatEngine();
    ~NeuralBeatEngine();

    static constexpr double kModelSampleRate = 22050.0;
    static constexpr int    kHop             = 441;      // ~50 fps, matches export

    // Model path: env SATTARI_STEMDECK_BEAT_MODEL, else
    // <userApplicationDataDirectory>/Sattari/StemDeck/StemProvider/beat-model.onnx
    static juce::File defaultModelFile();

    static bool isCompiledIn() noexcept;
    bool isAvailable() const noexcept;

    // Loads the model if present; safe to call when ONNX isn't compiled in.
    bool loadDefaultModel();

    struct Result
    {
        bool valid = false;
        float detectedBpm = 0.0f;
        double downbeatSample = 0.0;         // in the INPUT sample rate
        int beatsPerBar = 4;
        std::vector<double> beatGridSamples; // input-rate sample positions
    };

    // BLOCKING — background thread only. Resamples to 22.05 kHz, runs the model,
    // peak-picks the beat/downbeat activations, and returns a grid in input-rate
    // samples. Returns an invalid Result if unavailable or the audio is too short.
    Result detect (const juce::AudioBuffer<float>& audio, double sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralBeatEngine)
};
