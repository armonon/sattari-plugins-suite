//  Sattari Audio Suite — spectrogram key model (ONNX).
//
//  CNN over the pseudo-CQT log-spectrogram from SattariLogSpec ([84 x 256]) -> 24
//  key logits (index = root*2 + minor). Self-trained: a chroma teacher pseudo-labels
//  a large unlabeled CC corpus, and this student learns from TRUE + pseudo, which is
//  what lets it beat the chroma model (72.1 vs 61.5 MIREX on GiantSteps+GTZAN).
//
//  Inference always goes through detectTTA(): the net is pitch-rotation-augmented, so
//  averaging its 12 transposed views is a free accuracy bump. One semitone == one
//  frequency bin here (12 bins/octave), so a "rotation" is just a bin shift.
//
//  Requires SATTARI_HAS_ONNX. Inert without a model — callers fall back to the chroma
//  key model (SattariKeyNet) and then to the fixed profile.
#pragma once

#include <sattari_audio_core/SattariKeyNet.h>   // KeyNetResult
#include <sattari_audio_core/SattariLogSpec.h>  // kBins / kFrames

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#if SATTARI_HAS_ONNX
 #include <sattari_audio_core/SattariOnnxPath.h>
 #include <onnxruntime_cxx_api.h>
#endif

namespace sattari::dsp
{

class KeyNetSpec
{
public:
    static constexpr int kBins   = LogSpec::kBins;     // 84
    static constexpr int kFrames = LogSpec::kFrames;   // 256

    bool loadModel (const std::string& onnxModelPath)
    {
       #if SATTARI_HAS_ONNX
        try
        {
            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "sattari-keynet-spec");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads (1);
            opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            const auto nativePath = sattari::onnx::nativeModelPath (onnxModelPath);
            session = std::make_unique<Ort::Session> (*env, nativePath.c_str(), opts);
            return session != nullptr;
        }
        catch (...) { session.reset(); return false; }
       #else
        (void) onnxModelPath; return false;
       #endif
    }

    bool available() const noexcept
    {
       #if SATTARI_HAS_ONNX
        return session != nullptr;
       #else
        return false;
       #endif
    }

    // spec = [kBins * kFrames] bin-major, as produced by LogSpec::compute().
    // Runs the 12 pitch-rotations and averages the un-rotated softmaxes.
    KeyNetResult detectTTA (const float* spec)
    {
        KeyNetResult r;
       #if SATTARI_HAS_ONNX
        if (! available()) return r;
        try
        {
            std::array<double, 24> acc {};
            std::vector<float> rot ((size_t) kBins * kFrames);
            for (int k = 0; k < 12; ++k)
            {
                // Shift up by k semitones == k bins; zero-fill the bottom (matches the
                // training-time augmentation exactly).
                std::fill (rot.begin(), rot.end(), 0.0f);
                for (int b = k; b < kBins; ++b)
                    std::copy (spec + (size_t) (b - k) * kFrames,
                               spec + (size_t) (b - k + 1) * kFrames,
                               rot.data() + (size_t) b * (size_t) kFrames);

                std::array<float, 24> logits {};
                if (! runLogits (rot.data(), logits.data())) continue;
                float mx = logits[0];
                for (int i = 0; i < 24; ++i) mx = std::max (mx, logits[(size_t) i]);
                double sum = 0.0; double sm[24];
                for (int i = 0; i < 24; ++i) { sm[i] = std::exp ((double) logits[(size_t) i] - mx); sum += sm[i]; }
                if (sum <= 0.0) continue;
                for (int rt = 0; rt < 12; ++rt)
                    for (int mn = 0; mn < 2; ++mn)
                        acc[(size_t) (((rt - k + 12) % 12) * 2 + mn)] += sm[rt * 2 + mn] / sum;
            }
            int best = 0;
            for (int i = 0; i < 24; ++i) if (acc[(size_t) i] > acc[(size_t) best]) best = i;
            r.root  = best / 2;
            r.minor = (best % 2) == 1;
            r.confidence = (float) (acc[(size_t) best] / 12.0);
            r.valid = true;
        }
        catch (...) { /* leave invalid on any inference error */ }
       #else
        (void) spec;
       #endif
        return r;
    }

private:
   #if SATTARI_HAS_ONNX
    bool runLogits (const float* spec, float outLogits[24])
    {
        Ort::AllocatorWithDefaultOptions alloc;
        const auto inName  = session->GetInputNameAllocated (0, alloc);
        const auto outName = session->GetOutputNameAllocated (0, alloc);
        const char* inNames[]  = { inName.get() };
        const char* outNames[] = { outName.get() };
        auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape { 1, 1, kBins, kFrames };
        auto inTensor = Ort::Value::CreateTensor<float> (memInfo, const_cast<float*> (spec),
                                                         (size_t) kBins * kFrames, shape.data(), shape.size());
        auto out = session->Run (Ort::RunOptions { nullptr }, inNames, &inTensor, 1, outNames, 1);
        const float* logits = out[0].GetTensorMutableData<float>();
        for (int i = 0; i < 24; ++i) outLogits[i] = logits[i];
        return true;
    }
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
   #endif
};

} // namespace sattari::dsp
