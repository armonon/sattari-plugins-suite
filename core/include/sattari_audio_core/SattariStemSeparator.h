#pragma once

// =============================================================================
//  Sattari Audio Suite — ML stem separation interface (offline)
// -----------------------------------------------------------------------------
//  A Demucs/MDX-class 4-stem separator for StemDeck's *offline* analysis path
//  (load a track → separate once → cache), the same way Moises / RipX / Serato
//  work. It is deliberately decoupled from the realtime engine.
//
//  The ONNX Runtime backend is opt-in: cmake/SattariOnnx.cmake defines
//  SATTARI_HAS_ONNX=1 for a target when ONNX Runtime is found (and the developer
//  passes -DSATTARI_USE_ONNX=ON). Otherwise the no-op fallback below compiles, so
//  the suite always builds with zero new dependencies. SEE docs/ml-stem-separation.md
//  for the model, tensor shapes, and the remaining pipeline work.
// =============================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if SATTARI_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
#endif

namespace sattari::ml
{

enum class Stem { Drums = 0, Bass, Other, Vocals, kNumStems };

inline const char* stemName (Stem s) noexcept
{
    switch (s) { case Stem::Drums: return "Drums"; case Stem::Bass: return "Bass";
                 case Stem::Other: return "Other"; case Stem::Vocals: return "Vocals";
                 default: return "?"; }
}

struct StemResult
{
    bool        ok = false;
    int         numChannels = 0;
    int         numSamples = 0;
    std::string error;
    // stems[stem][channel] -> samples. Empty unless ok.
    std::array<std::vector<std::vector<float>>, (size_t) Stem::kNumStems> stems;
};

// Offline 4-stem separator. Heavy (seconds per track) — always call from a
// background thread, never the audio thread. `progress` is invoked in [0,1].
class StemSeparator
{
public:
    using Progress = std::function<void (float)>;

    float segmentSeconds = 7.8f;    // Demucs segment length
    float overlap        = 0.25f;   // overlap-add fraction

    bool isAvailable() const noexcept
    {
       #if SATTARI_HAS_ONNX
        return session != nullptr;
       #else
        return false;
       #endif
    }

    // Load an exported model (htdemucs / MDX-Net). Returns false when the build
    // has no ONNX backend or the file can't be opened.
    bool loadModel (const std::string& onnxModelPath)
    {
       #if SATTARI_HAS_ONNX
        try
        {
            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "sattari-stem");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads (4);
            opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            // On Apple silicon, append the CoreML EP here for acceleration.
            session = std::make_unique<Ort::Session> (*env, onnxModelPath.c_str(), opts);
            return session != nullptr;
        }
        catch (...) { session.reset(); return false; }
       #else
        (void) onnxModelPath;
        return false;
       #endif
    }

    // Separate `numChannels` x `numSamples` of audio at `sampleRate`.
    StemResult separate (const float* const* input, int numChannels, int numSamples,
                         double sampleRate, Progress progress = {})
    {
        StemResult r;
        r.numChannels = numChannels;
        r.numSamples  = numSamples;

       #if SATTARI_HAS_ONNX
        if (! isAvailable())      { r.error = "no model loaded"; return r; }
        if (numChannels <= 0 || numSamples <= 0) { r.error = "empty input"; return r; }
        // Demucs is a waveform model (no STFT here); input expected at 44.1 kHz.
        (void) sampleRate;

        const int    segLen = (int) (segmentSeconds * 44100.0);
        const int    hop    = std::max (1, (int) (segLen * (1.0f - overlap)));
        const size_t nS     = (size_t) Stem::kNumStems;

        // Normalise by the mixture standard deviation (Demucs convention).
        double sum = 0.0, sumSq = 0.0; size_t cnt = 0;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i) { const double x = input[ch][i]; sum += x; sumSq += x * x; ++cnt; }
        const double mean = cnt ? sum / (double) cnt : 0.0;
        const double var  = cnt ? std::max (1.0e-8, sumSq / (double) cnt - mean * mean) : 1.0;
        const float  ref  = (float) std::sqrt (var);
        const float  inv  = ref > 0.0f ? 1.0f / ref : 1.0f;

        // Output accumulators + a triangular overlap-add weight per sample.
        for (size_t s = 0; s < nS; ++s)
        {
            r.stems[s].assign ((size_t) numChannels, std::vector<float> ((size_t) numSamples, 0.0f));
        }
        std::vector<float> weight ((size_t) numSamples, 0.0f);

        Ort::AllocatorWithDefaultOptions alloc;
        const auto inName  = session->GetInputNameAllocated (0, alloc);
        const auto outName = session->GetOutputNameAllocated (0, alloc);
        const char* inNames[]  = { inName.get() };
        const char* outNames[] = { outName.get() };
        auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<float> inData ((size_t) numChannels * (size_t) segLen);
        const std::array<int64_t, 3> shape { 1, (int64_t) numChannels, (int64_t) segLen };

        for (int start = 0; start < numSamples; start += hop)
        {
            // Window the (normalised, zero-padded) segment into the input tensor.
            std::fill (inData.begin(), inData.end(), 0.0f);
            const int len = std::min (segLen, numSamples - start);
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < len; ++i)
                    inData[(size_t) ch * (size_t) segLen + (size_t) i] = (input[ch][start + i] - (float) mean) * inv;

            auto inTensor = Ort::Value::CreateTensor<float> (memInfo, inData.data(), inData.size(),
                                                             shape.data(), shape.size());
            auto out = session->Run (Ort::RunOptions { nullptr }, inNames, &inTensor, 1, outNames, 1);
            const float* od = out[0].GetTensorMutableData<float>();   // [1, S, C, segLen]

            for (int i = 0; i < len; ++i)
            {
                const float w = 1.0f - std::fabs ((float) i - segLen * 0.5f) / (segLen * 0.5f);  // triangular
                weight[(size_t) (start + i)] += w;
                for (size_t st = 0; st < nS; ++st)
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        const size_t idx = ((st * (size_t) numChannels + (size_t) ch) * (size_t) segLen + (size_t) i);
                        r.stems[st][(size_t) ch][(size_t) (start + i)] += od[idx] * w;
                    }
            }
            if (progress) progress (std::min (1.0f, (float) start / (float) numSamples));
        }

        // Normalise the overlap-add and undo the input scaling.
        for (size_t st = 0; st < nS; ++st)
            for (int ch = 0; ch < numChannels; ++ch)
                for (int i = 0; i < numSamples; ++i)
                {
                    const float wsum = weight[(size_t) i];
                    r.stems[st][(size_t) ch][(size_t) i] *= (wsum > 1.0e-6f ? ref / wsum : 0.0f);
                }

        r.ok = true;
        if (progress) progress (1.0f);
        return r;
       #else
        (void) input; (void) sampleRate; (void) progress;
        r.error = "built without ONNX — configure with -DSATTARI_USE_ONNX=ON";
        return r;
       #endif
    }

private:
   #if SATTARI_HAS_ONNX
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
   #endif
};

} // namespace sattari::ml
