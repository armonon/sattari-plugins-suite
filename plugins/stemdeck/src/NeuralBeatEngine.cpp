#include "NeuralBeatEngine.h"

#if SATTARI_STEMDECK_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
 #include <algorithm>
 #include <cmath>

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
    return configBeatModelFile();
}

bool NeuralBeatEngine::loadDefaultModel()
{
    juce::String msg;
    return impl->load (defaultModelFile(), msg);
}

NeuralBeatEngine::Result NeuralBeatEngine::detect (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    Result r;
    if (! isAvailable() || sampleRate <= 0.0 || audio.getNumSamples() < (int) sampleRate)   // need >= ~1s
        return r;

    // ── Mono sum ──────────────────────────────────────────────────────────────
    const int n  = audio.getNumSamples();
    const int ch = juce::jmax (1, audio.getNumChannels());
    std::vector<float> mono ((size_t) n, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        const auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i];
    }
    for (auto& v : mono) v /= (float) ch;

    // ── Resample to 22.05 kHz (the model's rate) ──────────────────────────────
    const double ratio = sampleRate / kModelSampleRate;                 // in-samples per out-sample
    const int outLen = (int) std::floor ((double) n / ratio);
    if (outLen < 64) return r;
    std::vector<float> rs ((size_t) outLen);
    {
        juce::LagrangeInterpolator interp;                              // decent quality, RT-clean
        interp.process (ratio, mono.data(), rs.data(), outLen);
    }

    // ── Inference ─────────────────────────────────────────────────────────────
    std::vector<float> beatAct, downAct;
    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 1> shape { (int64_t) rs.size() };
        auto input = Ort::Value::CreateTensor<float> (mem, rs.data(), rs.size(), shape.data(), shape.size());
        const char* inNames[]  = { impl->inputName.c_str() };
        const char* outNames[] = { impl->beatName.c_str(), impl->downName.c_str() };
        auto outs = impl->session->Run (Ort::RunOptions { nullptr }, inNames, &input, 1, outNames, 2);
        if (outs.size() < 2) return r;

        auto grab = [] (Ort::Value& v, std::vector<float>& dst)
        {
            const auto info = v.GetTensorTypeAndShapeInfo();
            const int64_t total = info.GetElementCount();
            const float* p = v.GetTensorData<float>();
            dst.assign (p, p + total);                                  // [1, T] -> T
        };
        grab (outs[0], beatAct);
        grab (outs[1], downAct);
    }
    catch (const std::exception&) { return r; }

    const int T = (int) beatAct.size();
    if (T < 8) return r;

    // ── Peak-pick beats ───────────────────────────────────────────────────────
    // Frame f -> time (f * hop / modelRate) sec -> input-rate sample.
    const double fps = kModelSampleRate / (double) kHop;
    const double secPerFrame = 1.0 / fps;
    const int minGap = juce::jmax (1, (int) std::floor (fps * 60.0 / 200.0));   // <= 200 BPM
    std::vector<int> beatFrames;
    for (int i = 1; i < T - 1; ++i)
    {
        if (beatAct[(size_t) i] > 0.4f
            && beatAct[(size_t) i] >= beatAct[(size_t) (i - 1)]
            && beatAct[(size_t) i] >  beatAct[(size_t) (i + 1)])
        {
            if (beatFrames.empty() || i - beatFrames.back() >= minGap)
                beatFrames.push_back (i);
        }
    }
    if ((int) beatFrames.size() < 4) return r;

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
    std::vector<int> downbeatIdx;
    for (size_t bi = 0; bi < beatFrames.size(); ++bi)
        if (downAct[(size_t) beatFrames[bi]] > 0.4f)
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
NeuralBeatEngine::Result NeuralBeatEngine::detect (const juce::AudioBuffer<float>&, double) { return {}; }

#endif
