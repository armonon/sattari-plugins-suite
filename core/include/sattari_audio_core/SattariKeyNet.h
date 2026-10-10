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
 #include <sattari_audio_core/SattariOnnxPath.h>
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

    // feat = 90-dim [mean(12), cov upper-tri incl diag(78)]. Not audio-thread-safe
    // (runs ONNX); call from the same worker/offline path as the CREPE inference.
    KeyNetResult detect (const float feat[90])
    {
        KeyNetResult r;
       #if SATTARI_HAS_ONNX
        if (! available()) return r;
        try
        {
            std::array<float, 24> logits {};
            if (! runLogits (feat, logits.data())) return r;

            int best = 0; float bestV = logits[0];
            double sumExp = 0.0;
            for (int i = 0; i < 24; ++i) { if (logits[(size_t) i] > bestV) { bestV = logits[(size_t) i]; best = i; } }
            for (int i = 0; i < 24; ++i) sumExp += std::exp ((double) logits[(size_t) i] - bestV);
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

    // Test-time augmentation: run inference on all 12 pitch-rotations of the feature
    // and average the (un-rotated) softmaxes. The net is pitch-rotation-augmented, so
    // its 12 rotated views are independent-ish votes on the same key — averaging them
    // is a free accuracy bump (+2.7 MIREX / +2.7 exact on the combined GiantSteps+GTZAN
    // corpus) with no model or feature change, just 12 tiny forward passes. Rotating a
    // key up by k must rotate the predicted root up by k (that IS the training target),
    // so a prediction on the k-rotated feature maps back to the original by (root - k).
    KeyNetResult detectTTA (const float feat[90])
    {
        KeyNetResult r;
       #if SATTARI_HAS_ONNX
        if (! available()) return r;
        try
        {
            // Unpack feat -> mean[12] + symmetric cov[12][12] (upper-tri incl diag, i<=j).
            double mean[12]; double cov[12][12];
            for (int i = 0; i < 12; ++i) mean[i] = feat[i];
            int idx = 12;
            for (int i = 0; i < 12; ++i)
                for (int j = i; j < 12; ++j) { cov[i][j] = cov[j][i] = feat[idx++]; }

            std::array<double, 24> acc {};
            for (int k = 0; k < 12; ++k)
            {
                // Rotate the feature up by k semitones (mean/cov roll by k), repack to 90.
                float fk[90]; int fi = 0;
                for (int i = 0; i < 12; ++i) fk[fi++] = (float) mean[(i - k + 12) % 12];
                for (int i = 0; i < 12; ++i)
                    for (int j = i; j < 12; ++j)
                        fk[fi++] = (float) cov[(i - k + 12) % 12][(j - k + 12) % 12];

                std::array<float, 24> logits {};
                if (! runLogits (fk, logits.data())) continue;
                // softmax, then fold back: rotation k's class r == original class (r-k).
                float mx = logits[0]; for (int i = 0; i < 24; ++i) mx = std::max (mx, logits[(size_t) i]);
                double sum = 0.0; double sm[24];
                for (int i = 0; i < 24; ++i) { sm[i] = std::exp ((double) logits[(size_t) i] - mx); sum += sm[i]; }
                if (sum <= 0.0) continue;
                for (int rt = 0; rt < 12; ++rt)
                    for (int mn = 0; mn < 2; ++mn)
                        acc[(size_t) (((rt - k + 12) % 12) * 2 + mn)] += sm[rt * 2 + mn] / sum;
            }

            int best = 0; for (int i = 0; i < 24; ++i) if (acc[(size_t) i] > acc[(size_t) best]) best = i;
            r.root  = best / 2;
            r.minor = (best % 2) == 1;
            r.confidence = (float) (acc[(size_t) best] / 12.0);   // mean softmax prob of winner
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
    // One forward pass: feat[90] -> logits[24]. Shared by detect() and detectTTA().
    bool runLogits (const float feat[90], float outLogits[24])
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
        for (int i = 0; i < 24; ++i) outLogits[i] = logits[i];
        return true;
    }
   #endif

   #if SATTARI_HAS_ONNX
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
   #endif
};

} // namespace sattari::dsp
