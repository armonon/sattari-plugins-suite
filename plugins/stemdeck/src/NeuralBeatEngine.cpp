#include "NeuralBeatEngine.h"
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

    // ── Tempo-continuity decoding ─────────────────────────────────────────────
    // A fixed 0.4 peak threshold fragments a grid whenever a breakdown is quieter
    // than a chorus. Decode one globally coherent path instead: estimate the beat
    // period from activation autocorrelation, choose its best phase, then snap each
    // predicted beat to the strongest nearby activation. This is the small,
    // deterministic Viterbi/DBN-style decoder used by the app when madmom is not
    // embedded: emission strength competes with a strong tempo-continuity prior.
    const double fps = kModelSampleRate / (double) kHop;
    const double secPerFrame = 1.0 / fps;
    const int minLag = juce::jmax (1, (int) std::floor (fps * 60.0 / 200.0));
    const int maxLag = juce::jmax (minLag + 1, (int) std::ceil (fps * 60.0 / 60.0));

    std::vector<float> sortedActivation = beatAct;
    std::sort (sortedActivation.begin(), sortedActivation.end());
    const float activationMedian = sortedActivation[sortedActivation.size() / 2];

    int bestLag = 0;
    double bestPeriodScore = -1.0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double score = 0.0;
        int terms = 0;
        for (int i = lag; i < T; ++i)
        {
            score += (double) beatAct[(size_t) i] * beatAct[(size_t) (i - lag)];
            ++terms;
        }
        // Normalise for track length and use a very gentle 120-BPM prior only to
        // break half/double-tempo ties; the activations remain authoritative.
        const double bpm = 60.0 * fps / (double) lag;
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 1.5, 2.0));
        score = terms > 0 ? score / (double) terms * (0.9 + 0.1 * prior) : 0.0;
        if (score > bestPeriodScore) { bestPeriodScore = score; bestLag = lag; }
    }
    if (bestLag <= 0) return r;

    int bestPhase = 0;
    double bestPhaseScore = -1.0;
    for (int phase = 0; phase < bestLag; ++phase)
    {
        double score = 0.0;
        int terms = 0;
        for (int f = phase; f < T; f += bestLag) { score += beatAct[(size_t) f]; ++terms; }
        score = terms > 0 ? score / (double) terms : 0.0;
        if (score > bestPhaseScore) { bestPhaseScore = score; bestPhase = phase; }
    }

    std::vector<int> beatFrames;
    const int snapRadius = juce::jmax (1, bestLag / 5);
    for (int predicted = bestPhase; predicted < T; predicted += bestLag)
    {
        const int lo = juce::jmax (0, predicted - snapRadius);
        const int hi = juce::jmin (T - 1, predicted + snapRadius);
        int strongest = lo;
        for (int f = lo + 1; f <= hi; ++f)
            if (beatAct[(size_t) f] > beatAct[(size_t) strongest])
                strongest = f;
        if (beatFrames.empty() || strongest > beatFrames.back())
            beatFrames.push_back (strongest);
    }
    if ((int) beatFrames.size() < 4) return r;

    double selectedStrength = 0.0;
    double intervalError = 0.0;
    for (size_t i = 0; i < beatFrames.size(); ++i)
    {
        selectedStrength += beatAct[(size_t) beatFrames[i]];
        if (i > 0)
        {
            const double observed = (double) (beatFrames[i] - beatFrames[i - 1]);
            intervalError += std::abs (observed - bestLag) / (double) bestLag;
        }
    }
    const double meanStrength = selectedStrength / (double) beatFrames.size();
    const double regularity = 1.0 / (1.0 + intervalError / (double) juce::jmax<size_t> (1, beatFrames.size() - 1));
    r.confidence = (float) juce::jlimit (0.0, 1.0,
        ((meanStrength - activationMedian) / juce::jmax (0.05, 1.0 - (double) activationMedian)) * regularity);

    for (int f : beatFrames)
        r.beatGridSamples.push_back ((double) f * secPerFrame * sampleRate);

    // Tempo = 60 / median inter-beat interval.
    std::vector<double> iois;
    for (size_t i = 1; i < beatFrames.size(); ++i)
        iois.push_back ((double) (beatFrames[i] - beatFrames[i - 1]) * secPerFrame);
    std::sort (iois.begin(), iois.end());
    const double medIoi = iois[iois.size() / 2];
    r.detectedBpm = medIoi > 1.0e-6 ? (float) (60.0 / medIoi) : 0.0f;

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
