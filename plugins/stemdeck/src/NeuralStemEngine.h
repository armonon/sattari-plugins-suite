#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

// =============================================================================
//  NeuralStemEngine — on-device neural stem separation via ONNX Runtime.
// -----------------------------------------------------------------------------
//  Compiles real inference code (CoreML-accelerated, CPU fallback) only when
//  SATTARI_STEMDECK_HAS_ONNX is defined by the build. Without it — or without a
//  model file — it reports unavailable and callers fall back to the demucs CLI
//  and the RTSS DSP heuristic, so the app always builds and runs.
//
//  Expected model: a waveform-domain 4-source separator exported to ONNX, input
//  [batch, channels, samples], output [batch, 4, channels, samples] with source
//  order drums / bass / other(=music) / vocals (the Demucs convention). The
//  engine introspects the model's I/O shapes at load and processes the track in
//  overlapping windows on a BACKGROUND thread.
// =============================================================================
class NeuralStemEngine
{
public:
    NeuralStemEngine();
    ~NeuralStemEngine();

    // True if the ONNX inference path was compiled into this build.
    static bool isCompiledIn() noexcept;

    // True once a model has been successfully loaded.
    bool isAvailable() const noexcept;

    // Auto-detect a model: env var SATTARI_STEMDECK_NEURAL_MODEL, then the config
    // file <appdata>/Sattari/StemDeck/StemProvider/neural-model.onnx. Returns the
    // resolved file (may not exist).
    static juce::File defaultModelFile();

    // Load a .onnx separation model. Returns false + message on failure.
    bool loadModel (const juce::File& modelFile, juce::String& message);

    // BLOCKING — background thread only. Separates a stereo `input` into four
    // stems (buffers resized to input length). onProgress reports 0..1.
    bool separate (const juce::AudioBuffer<float>& input, double sampleRate,
                   juce::AudioBuffer<float>& outDrums,
                   juce::AudioBuffer<float>& outBass,
                   juce::AudioBuffer<float>& outMusic,
                   juce::AudioBuffer<float>& outVocals,
                   const std::function<void(float)>& onProgress,
                   juce::String& message);

    // Streaming / look-ahead separation. Tiles the track into model-window-sized
    // segments and separates them in PLAYHEAD-FIRST order (the segment under
    // `playheadSample` first, then forward to the end, then wrapping to the head),
    // invoking onSegment as each completes so the caller can publish stems near
    // the playhead within a segment or two of pressing play — instead of waiting
    // for the whole track. Each callback receives the segment's absolute start
    // sample and four [2, segLen] stem buffers (drums / bass / music / vocals).
    // BLOCKING — background thread only. Segments are independent (no overlap-add),
    // so this is the fast preview path; `separate()` remains the full-quality pass.
    using SegmentCallback = std::function<void (int64_t startSample,
                                                const juce::AudioBuffer<float>& drums,
                                                const juce::AudioBuffer<float>& bass,
                                                const juce::AudioBuffer<float>& music,
                                                const juce::AudioBuffer<float>& vocals)>;
    bool separateStreaming (const juce::AudioBuffer<float>& input, double sampleRate,
                            int64_t playheadSample,
                            const SegmentCallback& onSegment,
                            const std::function<void(float)>& onProgress,
                            juce::String& message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuralStemEngine)
};
