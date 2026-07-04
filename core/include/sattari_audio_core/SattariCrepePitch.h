#pragma once

// =============================================================================
//  Sattari Audio Suite — CREPE-class neural pitch tracker (optional ONNX)
// -----------------------------------------------------------------------------
//  CREPE (Kim et al., ICASSP 2018) is a CNN that maps a 1024-sample frame at
//  16 kHz to a 360-bin pitch "salience" vector (20-cent resolution, C1..B7). It
//  is the current accuracy leader for monophonic pitch — it tracks breathy /
//  noisy vocals without the octave jumps that trip up autocorrelation and even
//  YIN, which is exactly the failure mode that makes correction feel "off".
//
//  This wrapper drives a CREPE-format ONNX model behind the same seam as the
//  stem separator (cmake/SattariOnnx.cmake -> SATTARI_HAS_ONNX). The model file
//  is NOT bundled (weights are ~a few MB for crepe-tiny); drop a `crepe.onnx`
//  exported with input [1,1024] float32 and output [1,360] and call loadModel().
//  When ONNX is absent, or no model is loaded, isAvailable() is false and the
//  caller keeps using the built-in YIN detector — zero new hard dependency.
//
//  The salience->frequency DECODE (the bug-prone part) is a pure-std free
//  function, compiled and unit-tested regardless of the ONNX backend
//  (core/tests/crepe_pitch_contract.cpp).
// =============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#if SATTARI_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
#endif

namespace sattari::dsp
{

// CREPE bin -> cents. The 360 bins are the reference CREPE mapping:
//   cents(i) = 1997.3794084376191 + 20 * i         (20-cent spacing from ~C1)
// and frequency = 10 * 2^(cents / 1200).
inline double crepeBinToCents (int bin) noexcept
{
    return 1997.3794084376191 + 20.0 * (double) bin;
}
inline double crepeCentsToHz (double cents) noexcept
{
    return 10.0 * std::pow (2.0, cents / 1200.0);
}

struct CrepePitch
{
    float frequencyHz = 0.0f;
    float confidence  = 0.0f;   // max salience in [0,1]
    bool  voiced      = false;
};

// Decode a 360-bin CREPE salience vector to a frequency, using the reference
// local-weighted-average of cents around the argmax (±4 bins). Pure std — this
// is what the contract test locks down. `voicedThreshold` gates on the peak
// salience so unvoiced frames return voiced=false.
inline CrepePitch decodeCrepeSalience (const float* salience, int num = 360,
                                       float voicedThreshold = 0.5f) noexcept
{
    CrepePitch r;
    if (salience == nullptr || num <= 0)
        return r;

    int center = 0;
    float peak = salience[0];
    for (int i = 1; i < num; ++i)
        if (salience[i] > peak) { peak = salience[i]; center = i; }

    r.confidence = std::clamp (peak, 0.0f, 1.0f);

    const int start = std::max (0, center - 4);
    const int end   = std::min (num - 1, center + 4);
    double productSum = 0.0, weightSum = 0.0;
    for (int i = start; i <= end; ++i)
    {
        const double w = std::max (0.0f, salience[i]);
        productSum += w * crepeBinToCents (i);
        weightSum  += w;
    }
    if (weightSum <= 1.0e-9)
        return r;

    const double cents = productSum / weightSum;
    r.frequencyHz = (float) crepeCentsToHz (cents);
    r.voiced      = r.confidence >= voicedThreshold;
    return r;
}

// Full neural tracker. Realtime-ish: keeps a native-rate ring, resamples the
// most recent ~64 ms to 16 kHz (1024 samples), runs the model, decodes. Heavy
// relative to YIN — gate analyse() on a hop counter, and prefer running it on a
// worker for offline analysis. isAvailable() is false unless a model loaded.
class CrepePitchTracker
{
public:
    static constexpr int kFrame    = 1024;   // CREPE input length
    static constexpr int kModelSr  = 16000;  // CREPE input rate

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // Native samples spanning one 16 kHz model frame, plus a little slack.
        const int nativeSpan = (int) std::ceil ((double) kFrame * sr / (double) kModelSr) + 8;
        ringLen = 1; while (ringLen < nativeSpan) ringLen <<= 1;
        ring.assign ((size_t) ringLen, 0.0f);
        frame.assign ((size_t) kFrame, 0.0f);
        pos = 0;
    }

    void reset() { std::fill (ring.begin(), ring.end(), 0.0f); pos.store (0, std::memory_order_release); }

    bool isAvailable() const noexcept
    {
       #if SATTARI_HAS_ONNX
        return session != nullptr;
       #else
        return false;
       #endif
    }

    bool loadModel (const std::string& onnxModelPath)
    {
       #if SATTARI_HAS_ONNX
        try
        {
            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "sattari-crepe");
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

    // Realtime-safe (audio thread): just writes the ring and publishes the new
    // write position. analyse() runs on a worker thread — single-producer /
    // single-consumer. The worker reads a `pos` snapshot; a sample being written
    // as it reads is at worst one noisy value, which is fine for pitch.
    void pushBlock (const float* mono, int numSamples)
    {
        if (ringLen <= 0) return;
        int p = pos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) p] = mono[i];
            p = (p + 1) & (ringLen - 1);
        }
        pos.store (p, std::memory_order_release);
    }

    // Resample the newest frame to 16 kHz, normalise, run the model, decode.
    // Returns voiced=false (and does no work) when no model is available.
    CrepePitch analyse (float voicedThreshold = 0.5f)
    {
        CrepePitch r;
       #if SATTARI_HAS_ONNX
        if (! isAvailable() || ringLen <= 0) return r;

        // Linear-resample the most recent kFrame*sr/16000 native samples down to
        // exactly kFrame samples at 16 kHz. Ring holds newest at (pos-1).
        const double step = sr / (double) kModelSr;           // native samples per model sample
        const double span = (double) kFrame * step;           // native samples covered
        const int    p    = pos.load (std::memory_order_acquire);   // snapshot the producer position
        double srcStart = (double) p - span;                  // oldest needed, in ring coords
        const int m = ringLen - 1;                            // pow2 mask: wraps negatives correctly
        double mean = 0.0;
        for (int i = 0; i < kFrame; ++i)
        {
            const double s = srcStart + (double) i * step;
            const int i0 = (int) std::floor (s);
            const double f = s - i0;
            const float a = ring[(size_t) (i0 & m)];
            const float b = ring[(size_t) ((i0 + 1) & m)];
            const float v = (float) ((1.0 - f) * a + f * b);
            frame[(size_t) i] = v;
            mean += v;
        }
        // CREPE normalises each frame to zero mean, unit std.
        mean /= (double) kFrame;
        double var = 0.0;
        for (int i = 0; i < kFrame; ++i) { const double d = frame[(size_t) i] - mean; var += d * d; }
        const double stdv = std::sqrt (std::max (1.0e-8, var / (double) kFrame));
        const float inv = (float) (1.0 / stdv);
        for (int i = 0; i < kFrame; ++i)
            frame[(size_t) i] = (float) ((frame[(size_t) i] - mean) * inv);

        try
        {
            Ort::AllocatorWithDefaultOptions alloc;
            const auto inName  = session->GetInputNameAllocated (0, alloc);
            const auto outName = session->GetOutputNameAllocated (0, alloc);
            const char* inNames[]  = { inName.get() };
            const char* outNames[] = { outName.get() };
            auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
            const std::array<int64_t, 2> shape { 1, (int64_t) kFrame };
            auto inTensor = Ort::Value::CreateTensor<float> (memInfo, frame.data(), frame.size(),
                                                             shape.data(), shape.size());
            auto out = session->Run (Ort::RunOptions { nullptr }, inNames, &inTensor, 1, outNames, 1);
            const float* sal = out[0].GetTensorMutableData<float>();
            r = decodeCrepeSalience (sal, 360, voicedThreshold);
        }
        catch (...) { /* leave r unvoiced on any inference error */ }
       #else
        (void) voicedThreshold;
       #endif
        return r;
    }

private:
    double sr = 44100.0;
    int    ringLen = 0;
    std::atomic<int> pos { 0 };          // producer (audio) write pos; consumer (worker) snapshots it
    std::vector<float> ring, frame;
   #if SATTARI_HAS_ONNX
    std::unique_ptr<Ort::Env>     env;
    std::unique_ptr<Ort::Session> session;
   #endif
};

} // namespace sattari::dsp
