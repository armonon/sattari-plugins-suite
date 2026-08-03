#include "NeuralBeatEngine.h"
#include "BeatGridDecoder.h"
#include "ModelLocator.h"

#if SATTARI_STEMDECK_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
 #include <algorithm>
 #include <cmath>
 #include <mutex>

namespace
{
juce::File configBeatModelFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Sattari").getChildFile ("StemDeck")
             .getChildFile ("StemProvider").getChildFile ("beat-model.onnx");
}
} // namespace

struct NeuralBeatEngine::Impl
{
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    Ort::SessionOptions options;
    std::string inputName, beatName, downName;
    bool ready = false;
    std::mutex activeRunsMutex;
    std::vector<std::shared_ptr<Ort::RunOptions>> activeRuns;
    std::atomic<bool> cancellationRequested { false };

    std::shared_ptr<Ort::RunOptions> beginRun()
    {
        auto run = std::make_shared<Ort::RunOptions>();
        const std::lock_guard<std::mutex> lock (activeRunsMutex);
        activeRuns.push_back (run);
        if (cancellationRequested.load (std::memory_order_acquire))
            run->SetTerminate();
        return run;
    }

    void endRun (const std::shared_ptr<Ort::RunOptions>& run)
    {
        const std::lock_guard<std::mutex> lock (activeRunsMutex);
        activeRuns.erase (std::remove (activeRuns.begin(), activeRuns.end(), run), activeRuns.end());
    }

    void cancelPending() noexcept
    {
        cancellationRequested.store (true, std::memory_order_release);
        std::vector<std::shared_ptr<Ort::RunOptions>> runs;
        {
            const std::lock_guard<std::mutex> lock (activeRunsMutex);
            runs = activeRuns;   // keep every options object alive while terminating it
        }
        for (const auto& run : runs)
            try { run->SetTerminate(); } catch (...) {}
    }

    // Call at the top of detect(). Same bug and same fix as
    // NeuralStemEngine::Impl::beginOperation() (see that comment for the full
    // reasoning): cancellationRequested was write-only via cancelPending() and
    // had NO reset anywhere in this file — not even on reload — so one
    // cancellation made every future detect() call on this engine terminate
    // immediately, forever. StemDeckStemCancelSmoke's "reuse after cancel" case
    // is what exposed this pattern in the sibling engine; this one shares the
    // exact same defect and the exact same fix.
    void beginOperation() noexcept
    {
        cancellationRequested.store (false, std::memory_order_release);
    }

    bool load (const juce::File& modelFile, juce::String& message)
    {
        if (! modelFile.existsAsFile()) { message = "No beat model at " + modelFile.getFullPathName(); return false; }
        try
        {
            env = std::make_unique<Ort::Env> (ORT_LOGGING_LEVEL_WARNING, "sattari-beatnet");
            options.SetIntraOpNumThreads (1);
            options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            session = std::make_unique<Ort::Session> (*env, modelFile.getFullPathName().toRawUTF8(), options);

            Ort::AllocatorWithDefaultOptions alloc;
            inputName = session->GetInputNameAllocated (0, alloc).get();
            beatName  = session->GetOutputNameAllocated (0, alloc).get();
            downName  = session->GetOutputNameAllocated (1, alloc).get();
            ready = true;
            message = "Neural beat model loaded";
            return true;
        }
        catch (const std::exception& e) { message = juce::String ("beat model load failed: ") + e.what(); return false; }
    }
};

NeuralBeatEngine::NeuralBeatEngine() : impl (std::make_unique<Impl>()) {}
NeuralBeatEngine::~NeuralBeatEngine() = default;

bool NeuralBeatEngine::isCompiledIn() noexcept { return true; }
bool NeuralBeatEngine::isAvailable() const noexcept { return impl && impl->ready; }

juce::File NeuralBeatEngine::defaultModelFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SATTARI_STEMDECK_BEAT_MODEL", {}).trim();
    if (env.isNotEmpty())
        return juce::File (env);
    if (const auto bundled = stemdeck::bundledModelFile ("beat-model.onnx"); bundled.existsAsFile())
        return bundled;
    return configBeatModelFile();
}

bool NeuralBeatEngine::loadDefaultModel()
{
    juce::String msg;
    return impl->load (defaultModelFile(), msg);
}

void NeuralBeatEngine::cancelPending() noexcept
{
    if (impl != nullptr)
        impl->cancelPending();
}

NeuralBeatEngine::Result NeuralBeatEngine::detect (const juce::AudioBuffer<float>& audio, double sampleRate,
                                                   std::function<bool()> shouldCancel)
{
    Result r;
    if (! isAvailable() || sampleRate <= 0.0 || audio.getNumSamples() < (int) sampleRate)   // need >= ~1s
        return r;
    impl->beginOperation();

    const auto cancelled = [&shouldCancel] { return shouldCancel && shouldCancel(); };

    // ── Mono sum ──────────────────────────────────────────────────────────────
    const int n  = audio.getNumSamples();
    const int ch = juce::jmax (1, audio.getNumChannels());
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        if (cancelled()) return r;
        const auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            mono[(size_t) i] += d[i];
            if ((i & 0x3ffff) == 0 && cancelled()) return r;
        }
    }
    for (size_t i = 0; i < mono.size(); ++i)
    {
        mono[i] /= (float) ch;
        if ((i & 0x3ffff) == 0 && cancelled()) return r;
    }

    // ── Anti-alias pre-filter ─────────────────────────────────────────────────
    // LagrangeInterpolator is an INTERPOLATOR, not a decimator: it has no
    // band-limiting of its own, so decimating 44.1/48 kHz straight to 22.05 kHz
    // folded everything above 11.025 kHz back down into the audible band. Hi-hats,
    // cymbals and general HF energy — exactly the content whose transients drive
    // the onset envelope — aliased onto the low frequencies the beat model reads.
    //
    // A 6th-order Butterworth (three biquads at the standard Q triplet) below the
    // new Nyquist is cheap next to the model itself and removes the fold-down.
    // Only needed when actually downsampling; at or below 22.05 kHz there is no
    // image to remove and filtering would just cost bandwidth.
    const double ratio = sampleRate / kModelSampleRate;                 // in-samples per out-sample
    if (ratio > 1.0)
    {
        // 0.90 x the target Nyquist: enough transition band for a 6th-order roll-off
        // to be well down by 11.025 kHz without eating usable cymbal energy.
        //
        // These are causal biquads, so they add group delay and therefore shift the
        // detected beat positions. At a ~9.9 kHz cutoff the low-frequency group
        // delay is a few samples — well under a millisecond against beats spaced
        // ~500 ms apart — so it is not worth a zero-phase forward/reverse pass here.
        const double cutoff = 0.90 * (kModelSampleRate * 0.5);
        static constexpr double kButterworthQ[3] { 0.51763809, 0.70710678, 1.93185165 };

        for (const double q : kButterworthQ)
        {
            if (cancelled()) return r;
            juce::IIRFilter lp;
            lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, cutoff, (float) q));
            lp.processSamples (mono.data(), (int) mono.size());
        }
    }

    // ── Resample to 22.05 kHz (the model's rate) ──────────────────────────────
    const int outLen = (int) std::floor ((double) n / ratio);
    if (outLen < 64) return r;
    std::vector<float> rs ((size_t) outLen);
    {
        if (cancelled()) return r;
        juce::LagrangeInterpolator interp;                              // decent quality, RT-clean
        interp.process (ratio, mono.data(), rs.data(), outLen);
    }
    if (cancelled()) return r;

    // ── Chunked inference ─────────────────────────────────────────────────────
    // beat_this has a fixed usable sequence length and is designed to run on
    // 1500-frame (~30 s) chunks with a 6-frame border overlap; feeding a whole
    // song fails ("invalid expand shape") past ~2 min. So we mirror beat_this's
    // split_piece: run fixed-size audio chunks and stitch the activation tracks,
    // discarding the border frames on each side to avoid seam artifacts.
    const int T = (int) (rs.size() / (size_t) kHop) + 1;               // total frames
    if (T < 8) return r;

    constexpr int kChunkFrames  = 1500;                                // matches beat_this
    constexpr int kBorderFrames = 6;
    const int chunkSamples = kChunkFrames * kHop;

    std::vector<float> beatAct ((size_t) T, 0.0f), downAct ((size_t) T, 0.0f);
    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        const char* inNames[]  = { impl->inputName.c_str() };
        const char* outNames[] = { impl->beatName.c_str(), impl->downName.c_str() };
        const int stride = (kChunkFrames - 2 * kBorderFrames) * kHop;  // advance per chunk

        for (int startSamp = 0; startSamp < (int) rs.size(); startSamp += stride)
        {
            if (cancelled()) return r;
            const int len = juce::jmin (chunkSamples, (int) rs.size() - startSamp);
            if (len < kHop * 8) break;                                 // trailing scrap
            const int startFrame = startSamp / kHop;

            std::array<int64_t, 1> shape { (int64_t) len };
            auto input = Ort::Value::CreateTensor<float> (mem, rs.data() + startSamp, (size_t) len,
                                                          shape.data(), shape.size());
            auto runOptions = impl->beginRun();
            std::vector<Ort::Value> outs;
            try
            {
                outs = impl->session->Run (*runOptions, inNames, &input, 1, outNames, 2);
            }
            catch (...)
            {
                impl->endRun (runOptions);
                return r;
            }
            impl->endRun (runOptions);
            if (cancelled()) return r;
            if (outs.size() < 2) return r;

            const float* bp = outs[0].GetTensorData<float>();
            const float* dp = outs[1].GetTensorData<float>();
            const int chT   = (int) outs[0].GetTensorTypeAndShapeInfo().GetElementCount();

            // Keep the interior frames (skip border on both sides, except the very
            // first/last chunk which own the track edges).
            const bool firstChunk = (startSamp == 0);
            const bool lastChunk  = (startSamp + stride >= (int) rs.size());
            const int lo = firstChunk ? 0 : kBorderFrames;
            const int hi = lastChunk  ? chT : chT - kBorderFrames;
            for (int j = lo; j < hi; ++j)
            {
                const int g = startFrame + j;
                if (g >= 0 && g < T) { beatAct[(size_t) g] = bp[j]; downAct[(size_t) g] = dp[j]; }
            }
            if (lastChunk) break;
        }
    }
    catch (const std::exception&) { return r; }

    // ── Probabilistic tempo-state decoding ────────────────────────────────────
    // The neural network supplies emissions, not a finished grid. Decode an
    // explicit (beat frame, beat period) state path so quiet intros/breakdowns
    // retain clock continuity and gradual live-drum tempo movement remains
    // editable. BeatGridDecoder is deterministic and independently contract-
    // tested; it replaces both fixed-threshold peak picking and the old single-
    // period autocorrelation snapper.
    const double fps = kModelSampleRate / (double) kHop;
    const double secPerFrame = 1.0 / fps;
    const auto decoded = stemdeck::BeatGridDecoder::decode (beatAct, fps);
    if (! decoded) return r;
    const auto& beatFrames = decoded.frames;
    r.confidence = (float) decoded.confidence;

    for (const int f : beatFrames)
        r.beatGridSamples.push_back ((double) f * secPerFrame * sampleRate);
    r.detectedBpm = (float) decoded.bpm;

    // ── Downbeat + meter ──────────────────────────────────────────────────────
    // Downbeats are the beats with the strongest 'down' activation. Meter = the
    // rounded median gap (in beats) between consecutive strong downbeats.
    std::vector<float> sortedDown = downAct;
    std::sort (sortedDown.begin(), sortedDown.end());
    const float downThreshold = juce::jlimit (0.25f, 0.65f,
                                              sortedDown[sortedDown.size() * 3 / 4]);
    std::vector<int> downbeatIdx;
    for (size_t bi = 0; bi < beatFrames.size(); ++bi)
        if (downAct[(size_t) beatFrames[bi]] >= downThreshold)
            downbeatIdx.push_back ((int) bi);

    if (! downbeatIdx.empty())
    {
        r.downbeatSample = r.beatGridSamples[(size_t) downbeatIdx.front()];
        if (downbeatIdx.size() >= 2)
        {
            std::vector<int> gaps;
            for (size_t i = 1; i < downbeatIdx.size(); ++i)
                gaps.push_back (downbeatIdx[i] - downbeatIdx[i - 1]);
            std::sort (gaps.begin(), gaps.end());
            const int med = gaps[gaps.size() / 2];
            r.beatsPerBar = (med == 3) ? 3 : 4;   // 3/4 vs 4/4; default 4
        }
    }
    else
    {
        r.downbeatSample = r.beatGridSamples.front();
        r.beatsPerBar = 4;
    }

    r.valid = true;
    return r;
}

// ═════════════════════════════════════════════════════════════════════════════
#else   // ONNX not compiled in — stub.

struct NeuralBeatEngine::Impl {};
NeuralBeatEngine::NeuralBeatEngine() = default;
NeuralBeatEngine::~NeuralBeatEngine() = default;
bool NeuralBeatEngine::isCompiledIn() noexcept { return false; }
bool NeuralBeatEngine::isAvailable() const noexcept { return false; }
juce::File NeuralBeatEngine::defaultModelFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Sattari").getChildFile ("StemDeck")
             .getChildFile ("StemProvider").getChildFile ("beat-model.onnx");
}
bool NeuralBeatEngine::loadDefaultModel() { return false; }
NeuralBeatEngine::Result NeuralBeatEngine::detect (const juce::AudioBuffer<float>&, double,
                                                   std::function<bool()>) { return {}; }
void NeuralBeatEngine::cancelPending() noexcept {}

#endif
