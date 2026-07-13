//  Sattari Audio Suite — neural key model (ONNX).
//
//  A small feed-forward classifier over the plugin's own HPCP chroma feature:
//  a 90-dim vector = mean chroma (12) + chroma covariance upper-triangle incl. diag
//  (78). The covariance encodes note CO-OCCURRENCE (chord / harmonic structure), which
//  is what lets a learned model beat a fixed key profile on real audio. Output is 24
//  logits (index = root*2 + minor). Trained on GiantSteps (scripts/train_keynet_cov.py),
//  exported to keynet.onnx, loaded at runtime next to crepe.onnx.
//
//  Requires SATTARI_HAS_ONNX (same as the CREPE tracker). Absent a model / ONNX it is
//  inert (available() == false) and callers fall back to the fixed-profile detector.
#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <string>

#if SATTARI_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
#endif

namespace sattari::dsp
{

struct KeyNetResult
{
    int  root  = 0;      // 0..11
    bool minor = false;
    float confidence = 0.0f;   // softmax prob of the winner
    bool valid = false;
};

class KeyNet
{
public:
    KeyNet() = default;

    bool loadModel (const std::string& onnxModelPath)
    {
       #if SATTARI_HAS_ONNX
        try
        {
            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "sattari-keynet");
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads (1);
            opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            session = std::make_unique<Ort::Session> (*env, onnxModelPath.c_str(), opts);
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

    // feat = 90-dim [mean(12), cov upper-tri incl diag(78)]. Not audio-thread-safe
    // (runs ONNX); call from the same worker/offline path as the CREPE inference.
    KeyNetResult detect (const float feat[90])
    {
        KeyNetResult r;
       #if SATTARI_HAS_ONNX
        if (! available()) return r;
        try
        {
            std::array<float, 90> in {};
            for (int i = 0; i < 90; ++i) in[(size_t) i] = feat[i];

            Ort::AllocatorWithDefaultOptions alloc;
            const auto inName  = session->GetInputNameAllocated (0, alloc);
            const auto outName = session->GetOutputNameAllocated (0, alloc);
            const char* inNames[]  = { inName.get() };
            const char* outNames[] = { outName.get() };
            auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            const std::array<int64_t, 2> shape { 1, 90 };
            auto inTensor = Ort::Value::CreateTensor<float> (memInfo, in.data(), in.size(),
                                                             shape.data(), shape.size());
            auto out = session->Run (Ort::RunOptions { nullptr }, inNames, &inTensor, 1, outNames, 1);
            const float* logits = out[0].GetTensorMutableData<float>();

            int best = 0; float bestV = logits[0];
            double sumExp = 0.0;
            for (int i = 0; i < 24; ++i) { if (logits[i] > bestV) { bestV = logits[i]; best = i; } }
            for (int i = 0; i < 24; ++i) sumExp += std::exp ((double) logits[i] - bestV);
            r.root  = best / 2;
            r.minor = (best % 2) == 1;
            r.confidence = (float) (1.0 / (sumExp > 0.0 ? sumExp : 1.0));   // softmax prob of winner
            r.valid = true;
        }
        catch (...) { /* leave invalid on any inference error */ }
       #else
        (void) feat;
       #endif
        return r;
    }

private:
   #if SATTARI_HAS_ONNX
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
   #endif
};

} // namespace sattari::dsp
