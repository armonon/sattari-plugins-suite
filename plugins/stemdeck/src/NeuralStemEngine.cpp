#include "NeuralStemEngine.h"
#include "ModelLocator.h"

#if SATTARI_STEMDECK_HAS_ONNX
 #include <onnxruntime_cxx_api.h>
 #include <coreml_provider_factory.h>
 #include <vector>
 #include <cmath>
 #include <mutex>
 #include <algorithm>
#endif

// ─────────────────────────────────────────────────────────────────────────────
namespace
{
juce::File configModelFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Sattari").getChildFile ("StemDeck")
             .getChildFile ("StemProvider").getChildFile ("neural-model.onnx");
}
}

juce::File NeuralStemEngine::defaultModelFile()
{
    // 1) explicit override, 2) model shipped in the bundle, 3) user ~/Library install.
    const auto env = juce::SystemStats::getEnvironmentVariable ("SATTARI_STEMDECK_NEURAL_MODEL", {}).trim();
    if (env.isNotEmpty())
        return juce::File (env);
    if (const auto bundled = stemdeck::bundledModelFile ("neural-model.onnx"); bundled.existsAsFile())
        return bundled;
    return configModelFile();
}

bool NeuralStemEngine::isCompiledIn() noexcept
{
#if SATTARI_STEMDECK_HAS_ONNX
    return true;
#else
    return false;
#endif
}

// ═════════════════════════════════════════════════════════════════════════════
#if SATTARI_STEMDECK_HAS_ONNX

struct NeuralStemEngine::Impl
{
    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "SattariStemDeck" };
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions alloc;
    std::string inputName, outputName;
    int64_t windowSamples = 0;     // fixed model window (0 = flexible)
    int64_t numSources = 4;
    bool ready = false;
    bool usingCpu = true;          // CoreML fp16 overflows the STFT reductions
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
            runs = activeRuns;
        }
        for (const auto& run : runs)
            try { run->SetTerminate(); } catch (...) {}
    }

    bool load (const juce::File& modelFile, juce::String& message)
    {
        ready = false;
        cancellationRequested.store (false, std::memory_order_release);
        if (! modelFile.existsAsFile())
        {
            message = "Neural model not found: " + modelFile.getFullPathName();
            return false;
        }
        try
        {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads (juce::SystemStats::getNumCpus());
            opts.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
            // CoreML runs the graph in fp16 on the ANE/GPU, which overflows the
            // wide DFT reductions in the STFT/ISTFT and yields NaNs. Default to the
            // fp32 CPU EP for correctness; CoreML is opt-in once validated per model.
            usingCpu = true;
            const bool wantCoreML = juce::SystemStats::getEnvironmentVariable ("SATTARI_STEMDECK_NEURAL_COREML", {}).isNotEmpty();
            if (wantCoreML)
            {
                try { Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_CoreML (opts, 0)); usingCpu = false; }
                catch (...) { usingCpu = true; }
            }

            session = std::make_unique<Ort::Session> (env, modelFile.getFullPathName().toRawUTF8(), opts);

            {
                auto in  = session->GetInputNameAllocated (0, alloc);
                auto out = session->GetOutputNameAllocated (0, alloc);
                inputName  = in.get();
                outputName = out.get();
            }
            // Introspect the input sample dimension (last dim). A positive value is
            // a fixed window; -1 (dynamic) means we choose our own.
            const auto shape = session->GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
            if (! shape.empty() && shape.back() > 0)
                windowSamples = shape.back();

            ready = true;
            message = "Neural model loaded (" + modelFile.getFileName() + ", "
                    + (usingCpu ? "CPU fp32" : "CoreML") + ")";
            return true;
        }
        catch (const Ort::Exception& e) { message = juce::String ("ONNX load failed: ") + e.what(); }
        catch (const std::exception& e) { message = juce::String ("ONNX load failed: ") + e.what(); }
        return false;
    }

    // Run one window [2, N] through the model; writes 4 stems [2, N] to `outWin`.
    bool runWindow (const float* interleavedLR, int64_t n,
                    std::array<std::vector<float>, 4>& outWin,
                    const std::function<bool()>& shouldCancel)
    {
        if ((shouldCancel && shouldCancel())
            || cancellationRequested.load (std::memory_order_acquire))
            return false;
        // Model input layout: [batch=1, channels=2, samples=n], de-interleaved.
        std::vector<float> input ((size_t) (2 * n), 0.0f);
        for (int64_t i = 0; i < n; ++i)
        {
            input[(size_t) i]       = interleavedLR[(size_t) (2 * i)];       // L
            input[(size_t) (n + i)] = interleavedLR[(size_t) (2 * i + 1)];   // R
        }
        std::array<int64_t, 3> inShape { 1, 2, n };
        auto memInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        auto inTensor = Ort::Value::CreateTensor<float> (memInfo, input.data(), input.size(),
                                                         inShape.data(), inShape.size());
        const char* inNames[]  = { inputName.c_str() };
        const char* outNames[] = { outputName.c_str() };
        auto runOptions = beginRun();
        std::vector<Ort::Value> outputs;
        try
        {
            outputs = session->Run (*runOptions, inNames, &inTensor, 1, outNames, 1);
        }
        catch (...)
        {
            endRun (runOptions);
            throw;
        }
        endRun (runOptions);
        if (outputs.empty()) return false;

        // Output layout: [1, sources, channels, samples].
        const float* od = outputs[0].GetTensorData<float>();
        const auto os   = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int64_t sources = os.size() >= 4 ? os[1] : numSources;
        const int64_t chans   = os.size() >= 4 ? os[2] : 2;
        const int64_t samps   = os.size() >= 4 ? os[3] : n;
        const int64_t use     = juce::jmin (samps, n);

        for (int s = 0; s < 4; ++s)
        {
            outWin[(size_t) s].assign ((size_t) (2 * n), 0.0f);
            const int64_t srcIdx = juce::jmin<int64_t> (s, sources - 1);
            for (int64_t i = 0; i < use; ++i)
            {
                const float l = od[((srcIdx * chans + 0) * samps) + i];
                const float r = chans > 1 ? od[((srcIdx * chans + 1) * samps) + i] : l;
                outWin[(size_t) s][(size_t) i]       = l;
                outWin[(size_t) s][(size_t) (n + i)] = r;
            }
        }
        return true;
    }
};

NeuralStemEngine::NeuralStemEngine() : impl (std::make_unique<Impl>()) {}
NeuralStemEngine::~NeuralStemEngine() = default;

bool NeuralStemEngine::isAvailable() const noexcept { return impl && impl->ready; }

bool NeuralStemEngine::loadModel (const juce::File& modelFile, juce::String& message)
{
    return impl->load (modelFile, message);
}

void NeuralStemEngine::cancelPending() noexcept
{
    if (impl != nullptr)
        impl->cancelPending();
}

bool NeuralStemEngine::separate (const juce::AudioBuffer<float>& input, double /*sampleRate*/,
                                 juce::AudioBuffer<float>& outDrums,
                                 juce::AudioBuffer<float>& outBass,
                                 juce::AudioBuffer<float>& outMusic,
                                 juce::AudioBuffer<float>& outVocals,
                                 const std::function<void(float)>& onProgress,
                                 juce::String& message,
                                 std::function<bool()> shouldCancel)
{
    if (! isAvailable()) { message = "Neural engine has no model loaded"; return false; }

    const int total = input.getNumSamples();
    if (total < 16) { message = "Input too short"; return false; }

    juce::AudioBuffer<float>* outs[4] = { &outDrums, &outBass, &outMusic, &outVocals };
    for (auto* b : outs) { b->setSize (2, total, false, false, true); b->clear(); }

    // Window/hop with 25% overlap and a Hann cross-fade (constant-power sum).
    const int64_t win = impl->windowSamples > 0 ? impl->windowSamples
                                                : (int64_t) juce::jmin (total, (int) (10.0 * 44100.0));
    const int64_t hop = juce::jmax<int64_t> (1, win - win / 4);
    std::vector<float> norm ((size_t) total, 0.0f);
    std::vector<float> interleaved ((size_t) (2 * win));
    std::array<std::vector<float>, 4> outWin;

    const int ch = input.getNumChannels();
    try
    {
        for (int64_t start = 0; start < total; start += hop)
        {
            if (shouldCancel && shouldCancel()) { message = "Neural separation cancelled"; return false; }
            const int64_t n = juce::jmin<int64_t> (win, total - start);
            for (int64_t i = 0; i < win; ++i)
            {
                const int64_t idx = start + i;
                const float l = idx < total ? input.getSample (0, (int) idx) : 0.0f;
                const float r = (ch > 1 && idx < total) ? input.getSample (1, (int) idx) : l;
                interleaved[(size_t) (2 * i)]     = l;
                interleaved[(size_t) (2 * i + 1)] = r;
            }
            if (! impl->runWindow (interleaved.data(), win, outWin, shouldCancel))
            { message = "Inference failed"; return false; }

            for (int64_t i = 0; i < n; ++i)
            {
                // Periodic (DFT-even) Hann: non-zero at both endpoints, so global
                // sample 0 (and the final sample) aren't forced to silence, and it
                // sums to a constant under 50% overlap (proper COLA).
                const double x = win > 0 ? ((double) i + 0.5) / (double) win : 0.5;
                const float w = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * x));
                const int64_t idx = start + i;
                if (idx >= total) break;
                norm[(size_t) idx] += w;
                for (int s = 0; s < 4; ++s)
                {
                    outs[s]->addSample (0, (int) idx, w * outWin[(size_t) s][(size_t) i]);
                    outs[s]->addSample (1, (int) idx, w * outWin[(size_t) s][(size_t) (win + i)]);
                }
            }
            if (onProgress) onProgress (juce::jlimit (0.0f, 1.0f, (float) (start + n) / (float) total));
        }
        // Normalise the overlap-add.
        for (int i = 0; i < total; ++i)
        {
            if ((i & 0x3ffff) == 0 && shouldCancel && shouldCancel())
            { message = "Neural separation cancelled"; return false; }
            const float g = norm[(size_t) i] > 1.0e-6f ? 1.0f / norm[(size_t) i] : 1.0f;
            for (auto* b : outs) { b->setSample (0, i, b->getSample (0, i) * g); b->setSample (1, i, b->getSample (1, i) * g); }
        }
    }
    catch (const Ort::Exception& e) { message = juce::String ("ONNX run failed: ") + e.what(); return false; }
    catch (const std::exception& e) { message = juce::String ("ONNX run failed: ") + e.what(); return false; }

    message = "Neural separation complete";
    if (onProgress) onProgress (1.0f);
    return true;
}

bool NeuralStemEngine::separateStreaming (const juce::AudioBuffer<float>& input, double /*sampleRate*/,
                                          int64_t playheadSample,
                                          const SegmentCallback& onSegment,
                                          const std::function<void(float)>& onProgress,
                                          juce::String& message,
                                          std::function<bool()> shouldCancel)
{
    if (! isAvailable()) { message = "Neural engine has no model loaded"; return false; }

    const int64_t total = input.getNumSamples();
    if (total < 16) { message = "Input too short"; return false; }

    const int64_t win = impl->windowSamples > 0 ? impl->windowSamples
                                                : (int64_t) juce::jmin ((int) total, (int) (10.0 * 44100.0));
    const int64_t numSegs = (total + win - 1) / win;

    // Playhead-first segment order: the segment containing the playhead, forward to
    // the end, then wrap to cover everything before it.
    const int64_t startSeg = juce::jlimit<int64_t> (0, numSegs - 1, playheadSample / win);
    std::vector<int64_t> order;
    order.reserve ((size_t) numSegs);
    for (int64_t s = startSeg; s < numSegs; ++s) order.push_back (s);
    for (int64_t s = 0; s < startSeg; ++s)       order.push_back (s);

    std::vector<float> interleaved ((size_t) (2 * win));
    std::array<std::vector<float>, 4> outWin;
    juce::AudioBuffer<float> segBuf[4];
    const int ch = input.getNumChannels();

    try
    {
        for (size_t k = 0; k < order.size(); ++k)
        {
            if (shouldCancel && shouldCancel()) { message = "Neural streaming separation cancelled"; return false; }
            const int64_t segStart = order[k] * win;
            const int64_t n = juce::jmin (win, total - segStart);   // last segment is short
            for (int64_t i = 0; i < win; ++i)
            {
                const int64_t idx = segStart + i;
                const float l = idx < total ? input.getSample (0, (int) idx) : 0.0f;
                const float r = (ch > 1 && idx < total) ? input.getSample (1, (int) idx) : l;
                interleaved[(size_t) (2 * i)]     = l;
                interleaved[(size_t) (2 * i + 1)] = r;
            }
            if (! impl->runWindow (interleaved.data(), win, outWin, shouldCancel))
            { message = "Inference failed"; return false; }

            for (int s = 0; s < 4; ++s)
            {
                segBuf[s].setSize (2, (int) n, false, false, true);
                for (int64_t i = 0; i < n; ++i)
                {
                    segBuf[s].setSample (0, (int) i, outWin[(size_t) s][(size_t) i]);
                    segBuf[s].setSample (1, (int) i, outWin[(size_t) s][(size_t) (win + i)]);
                }
            }
            if (onSegment) onSegment (segStart, segBuf[0], segBuf[1], segBuf[2], segBuf[3]);
            if (onProgress) onProgress (juce::jlimit (0.0f, 1.0f, (float) (k + 1) / (float) order.size()));
        }
    }
    catch (const Ort::Exception& e) { message = juce::String ("ONNX run failed: ") + e.what(); return false; }
    catch (const std::exception& e) { message = juce::String ("ONNX run failed: ") + e.what(); return false; }

    message = "Streaming separation complete";
    if (onProgress) onProgress (1.0f);
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
#else   // ONNX not compiled in — stub that always reports unavailable.

struct NeuralStemEngine::Impl {};
NeuralStemEngine::NeuralStemEngine() = default;
NeuralStemEngine::~NeuralStemEngine() = default;
bool NeuralStemEngine::isAvailable() const noexcept { return false; }
bool NeuralStemEngine::loadModel (const juce::File&, juce::String& message)
{ message = "Built without ONNX Runtime"; return false; }
bool NeuralStemEngine::separate (const juce::AudioBuffer<float>&, double,
                                 juce::AudioBuffer<float>&, juce::AudioBuffer<float>&,
                                 juce::AudioBuffer<float>&, juce::AudioBuffer<float>&,
                                 const std::function<void(float)>&, juce::String& message,
                                 std::function<bool()>)
{ message = "Built without ONNX Runtime"; return false; }
bool NeuralStemEngine::separateStreaming (const juce::AudioBuffer<float>&, double, int64_t,
                                          const SegmentCallback&,
                                          const std::function<void(float)>&, juce::String& message,
                                          std::function<bool()>)
{ message = "Built without ONNX Runtime"; return false; }
void NeuralStemEngine::cancelPending() noexcept {}

#endif
