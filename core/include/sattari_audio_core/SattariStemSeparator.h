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

#include <array>
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
        if (! isAvailable()) { r.error = "no model loaded"; return r; }
        // ---- inference pipeline (skeleton — see docs/ml-stem-separation.md) ----
        //  1. resample to the model rate (Demucs = 44.1 kHz) if needed
        //  2. split into overlapping segments (htdemucs ~7.8 s, ~25% overlap)
        //  3. per segment, build input tensor [1, channels, segLen] and Run()
        //  4. model returns [1, numStems, channels, segLen]
        //  5. windowed overlap-add each stem back into r.stems
        r.error = "ONNX pipeline not yet wired (model + STFT/overlap remain)";
        (void) input; (void) sampleRate; (void) progress;
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
