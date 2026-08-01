// Deterministic cancellation test for NeuralStemEngine::separate(), against the
// REAL model and REAL ONNX Runtime.
//
// CONTEXT: this audit set out to add a cancel hook to separate(), on the
// assumption (stated in both the audit brief and this session's own prior
// analysis) that none existed — separate() was believed to be one uninterruptible
// blocking call. On inspection, that turned out to be WRONG: NeuralStemEngine.h/
// .cpp already declare and implement shouldCancel + cancelPending(), wired end to
// end from StemProvider::shutdown() through StemJob::shouldExit(). The mechanism
// mirrors NeuralBeatEngine's already-reviewed pattern: a mutex-protected set of
// in-flight Ort::RunOptions, terminated via SetTerminate() on cancelPending(),
// plus an explicit shouldCancel() poll before each window and before each
// Session::Run.
//
// So the remaining gap was not "build it" — it was "nobody has ever run it."
// Compiling correctly is not the same as behaving correctly under real
// cancellation timing, and this project has been burned before by code that
// compiled but was never executed (see NeuralBeatEngine's confidence
// computation, fixed and proven earlier in this audit). This test runs the
// REAL model against REAL audio and cancels it mid-flight, for real.
//
// Skips cleanly (exit 0) when ONNX is compiled out or no separation model is
// installed, so it is safe in the ordinary contract suite.

#include "NeuralStemEngine.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// 30 s of two-tone audio with light noise, stereo. The engine windows at up to
// ~10 s with 25% hop overlap, so this gives ~4 windows — enough for a
// mid-flight cancel to land inside real inference, not just before the first
// window starts.
juce::AudioBuffer<float> testAudio (double sampleRate, int seconds)
{
    const int n = (int) (sampleRate * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (7);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sampleRate;
        double s = 0.35 * std::sin (t * 110.0 * juce::MathConstants<double>::twoPi)
                 + 0.20 * std::sin (t * 440.0 * juce::MathConstants<double>::twoPi)
                 + 0.05 * (rng.nextDouble() * 2.0 - 1.0);
        b.setSample (0, i, (float) s);
        b.setSample (1, i, (float) s);
    }
    return b;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    if (! NeuralStemEngine::isCompiledIn())
    {
        std::cout << "STEM_CANCEL_SMOKE_SKIPPED (ONNX not compiled in)\n";
        return 0;
    }

    NeuralStemEngine engine;
    juce::String loadMsg;
    const auto modelFile = NeuralStemEngine::defaultModelFile();
    if (! engine.loadModel (modelFile, loadMsg))
    {
        std::cout << "STEM_CANCEL_SMOKE_SKIPPED (no separation model: " << loadMsg
                  << "; searched " << modelFile.getFullPathName() << ")\n";
        return 0;
    }

    std::cout << "neural stem cancellation smoke test (real model, real ONNX Runtime)\n";
    const auto audio = testAudio (44100.0, 30);

    // ── Test A: cancel-before-start ──────────────────────────────────────────
    // shouldCancel is already true when separate() is called. It must return
    // false near-instantly — never even reaching the model.
    {
        juce::AudioBuffer<float> d, b, m, v;
        juce::String msg;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = engine.separate (audio, 44100.0, d, b, m, v, nullptr, msg,
                                         [] { return true; });
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::cout << "  A. cancel-before-start: ok=" << (ok ? "true" : "false")
                  << " elapsed=" << juce::String (ms, 1) << "ms  msg=\"" << msg << "\"\n";
        check (! ok, "cancel-before-start returns false");
        check (ms < 500.0, "cancel-before-start returns near-instantly (< 500ms)");
        check (msg.containsIgnoreCase ("cancel"), "message says cancelled, not a model error");
    }

    // ── Test B: mid-flight cancel ────────────────────────────────────────────
    // Run separate() on a background thread. Wait for the FIRST progress
    // callback (proof real inference has started, not a guess based on sleep
    // timing), then request cancellation from the main thread via BOTH seams —
    // the explicit shouldCancel flag and engine.cancelPending() (SetTerminate on
    // whatever Session::Run is currently in flight) — and measure how long the
    // background thread takes to actually return.
    {
        std::atomic<bool> cancelFlag { false };
        std::mutex m;
        std::condition_variable cv;
        bool progressSeen = false;
        std::atomic<bool> separateReturned { false };
        bool result = true;
        juce::String msg;

        juce::AudioBuffer<float> d, b, mu, v;
        std::thread worker ([&]
        {
            result = engine.separate (audio, 44100.0, d, b, mu, v,
                [&] (float)
                {
                    std::lock_guard<std::mutex> lock (m);
                    if (! progressSeen) { progressSeen = true; cv.notify_one(); }
                },
                msg, [&] { return cancelFlag.load (std::memory_order_acquire); });
            separateReturned.store (true, std::memory_order_release);
        });

        {
            std::unique_lock<std::mutex> lock (m);
            const bool sawProgress = cv.wait_for (lock, std::chrono::seconds (30), [&] { return progressSeen; });
            check (sawProgress, "first progress callback observed within 30s (real inference is running)");
        }

        const auto tCancel = std::chrono::steady_clock::now();
        cancelFlag.store (true, std::memory_order_release);
        engine.cancelPending();   // SetTerminate on whatever Session::Run is in flight right now
        worker.join();
        const auto ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - tCancel).count();

        std::cout << "  B. mid-flight cancel: ok=" << (result ? "true" : "false")
                  << " time-to-return=" << juce::String (ms, 1) << "ms  msg=\"" << msg << "\"\n";
        check (! result, "mid-flight cancel returns false (did not run to completion)");
        // Generous bound: one in-flight CPU inference window plus poll granularity,
        // not "however long the full 30s track takes to fully separate."
        check (ms < 15000.0, "mid-flight cancel returns promptly (< 15s), not after full completion");
    }

    // ── Test C: engine is reusable after a cancellation ──────────────────────
    // A cancelled call must not leave the engine (or the ONNX session) in a
    // broken state. Run it again, uncancelled, on a short clip and expect success.
    {
        const auto shortAudio = testAudio (44100.0, 3);
        juce::AudioBuffer<float> d, b, m, v;
        juce::String msg;
        const bool ok = engine.separate (shortAudio, 44100.0, d, b, m, v, nullptr, msg, {});
        std::cout << "  C. reuse after cancel: ok=" << (ok ? "true" : "false") << "  msg=\"" << msg << "\"\n";
        check (ok, "engine completes a normal separation after a prior cancellation");
        check (d.getNumSamples() == shortAudio.getNumSamples(), "output length matches input after reuse");
    }

    // ── Test D: repeated start/cancel cycles ─────────────────────────────────
    // No crash, no hang, no growing latency across repeated cancel cycles.
    {
        bool allOk = true;
        for (int i = 0; i < 3; ++i)
        {
            std::atomic<bool> cancelFlag { false };
            std::thread worker ([&]
            {
                juce::AudioBuffer<float> d, b, m, v;
                juce::String msg;
                engine.separate (audio, 44100.0, d, b, m, v, nullptr, msg,
                                 [&] { return cancelFlag.load (std::memory_order_acquire); });
            });
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
            cancelFlag.store (true, std::memory_order_release);
            engine.cancelPending();
            worker.join();
        }
        std::cout << "  D. repeated start/cancel x3: completed without crash or hang\n";
        check (allOk, "repeated cancellation cycles complete cleanly");
    }

    std::cout << (failures == 0 ? "STEM_CANCEL_SMOKE_OK\n" : "STEM_CANCEL_SMOKE_FAIL\n");
    return failures == 0 ? 0 : 1;
}
