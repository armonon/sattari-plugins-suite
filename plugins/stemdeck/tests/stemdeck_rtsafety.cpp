// A2: the audio thread must not allocate. Enforced, not reviewed.
//
// "No allocation on the audio thread" has been a code-review rule. Review does not
// scale: this session alone added stem FX chains, per-stem PDC and a two-phase load,
// all touching the render path. This arms a global operator new/delete hook for the
// duration of processBlock and FAILS if anything allocates.
//
// It runs the WHOLE processor — decks with real decoded audio, stem separation, FX,
// PDC, master chain — not an isolated component, and it churns the channel list
// concurrently so the snapshot swap is under load while allocation is banned.
//
// Deliberately NOT armed during warm-up: JUCE and Tracktion lazily build buffers on
// the first few blocks, which is legitimate. The ban applies to steady state.
#include "MainPluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <new>
#include <execinfo.h>
#include <map>
#include <string>
#include <vector>

// ── Allocation detector ──────────────────────────────────────────────────────
namespace rtcheck
{
// Thread-local so ONLY the thread pretending to be the audio thread is policed; the
// message thread and Tracktion's background threads allocate freely and legitimately.
thread_local bool armed = false;
std::atomic<int> violations { 0 };
std::atomic<int> violationBytes { 0 };

// Capture a few backtraces so the failure names its own call sites. "The audio thread
// allocates" is not actionable; a stack is.
constexpr int kMaxTraces = 12;
void*  traces[kMaxTraces][24];
int    traceDepth[kMaxTraces] = {};
int    tracesTaken = 0;

struct ScopedArm
{
    ScopedArm()  { armed = true; }
    ~ScopedArm() { armed = false; }
};
} // namespace rtcheck

void* operator new (std::size_t n)
{
    if (rtcheck::armed)
    {
        rtcheck::violations.fetch_add (1, std::memory_order_relaxed);
        rtcheck::violationBytes.fetch_add ((int) n, std::memory_order_relaxed);

        if (rtcheck::tracesTaken < rtcheck::kMaxTraces)
        {
            const int slot = rtcheck::tracesTaken++;
            rtcheck::armed = false;   // backtrace() itself may allocate — do not recurse
            rtcheck::traceDepth[slot] = backtrace (rtcheck::traces[slot], 24);
            rtcheck::armed = true;
        }
    }
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void  operator delete (void* p) noexcept { std::free (p); }
void  operator delete[] (void* p) noexcept { std::free (p); }
void  operator delete (void* p, std::size_t) noexcept { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

juce::File writeTrack (double seconds, int sr)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("stemdeck-rt-" + juce::String ((int) seconds) + "s.wav");
    if (f.existsAsFile() && f.getSize() > 1000) return f;
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, n);
    const double beat = 60.0 / 128.0 * sr;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double ph = std::fmod ((double) i, beat) / beat;
        const float v = (float) (0.5 * std::exp (-ph * 16.0) * std::sin (t * 60.0 * juce::MathConstants<double>::twoPi)
                               + 0.2 * std::sin (t * 240.0 * juce::MathConstants<double>::twoPi));
        buf.setSample (0, i, juce::jlimit (-1.0f, 1.0f, v));
        buf.setSample (1, i, juce::jlimit (-1.0f, 1.0f, v * 0.9f));
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 16, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (buf, 0, n); }
    return f;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "A2 RT-safety: no allocation on the audio thread" << std::endl;

    constexpr int blockSize = 512;
    constexpr double sr = 48000.0;

    auto proc = std::make_unique<MainPluginProcessor>();
    proc->prepareToPlay (sr, blockSize);

    // Real decoded audio on two decks, with playback running — an empty deck exercises
    // almost none of the render path this is meant to police.
    const auto track = writeTrack (20.0, 44100);
    proc->loadDeckFileAsync (0, track);
    proc->loadDeckFileAsync (1, track);
    for (int i = 0; i < 400; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

    check (proc->deckAt (0).getStemAudio (DeckPlayer::fullMixStem) != nullptr, "deck 0 loaded real audio");

    for (int d = 0; d < 2; ++d)
        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (proc->parameters().getParameter (
                juce::String ("deck") + juce::String::charToString ((juce_wchar) ('A' + d)) + "Play")))
            p->setValueNotifyingHost (1.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    const auto recording = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("stemdeck-rtsafety-recording.wav");
    check (proc->beginMasterRecording (recording),
           "master and per-channel recorders started");

    // ── Warm-up: lazy buffers are allowed to be built here ───────────────────
    for (int i = 0; i < 200; ++i) { buf.clear(); proc->processBlock (buf, midi); }

    // ── Steady state: allocation is now a failure ────────────────────────────
    rtcheck::violations.store (0);
    rtcheck::violationBytes.store (0);

    std::atomic<bool> stop { false };
    std::atomic<int>  mutations { 0 };

    // Churn the channel list from another thread so the snapshot swap is under load
    // while the render path is forbidden from allocating.
    std::thread churn ([&]
    {
        while (! stop.load (std::memory_order_relaxed))
        {
            const int idx = proc->addChannel (ChannelType::input);
            if (idx >= 0) { mutations.fetch_add (1); proc->removeChannel (idx); }
            std::this_thread::yield();
        }
    });

    constexpr int kBlocks = 3000;
    int silentBlocks = 0;
    int nonFiniteSamples = 0;
    float minimumBlockPeak = std::numeric_limits<float>::max();
    for (int i = 0; i < kBlocks; ++i)
    {
        buf.clear();
        {
            rtcheck::ScopedArm arm;  // only this thread, only processBlock
            proc->processBlock (buf, midi);
        }

        // The isolated ChannelGraph contract proves sample-exact continuity, but
        // the product gate must also prove that the complete processor does not
        // emit a silent or corrupt block while snapshots are being replaced.
        const float peak = buf.getMagnitude (0, 0, blockSize);
        minimumBlockPeak = juce::jmin (minimumBlockPeak, peak);
        if (! std::isfinite (peak) || peak < 1.0e-5f)
            ++silentBlocks;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto* samples = buf.getReadPointer (ch);
            for (int sample = 0; sample < blockSize; ++sample)
                if (! std::isfinite (samples[sample]))
                    ++nonFiniteSamples;
        }
    }

    stop.store (true);
    churn.join();
    proc->stopMasterRecording();

    const int v = rtcheck::violations.load();
    const int b = rtcheck::violationBytes.load();

    std::cout << "  " << kBlocks << " blocks rendered, "
              << mutations.load() << " concurrent channel add/removes" << std::endl;

    check (v == 0, v == 0 ? "zero allocations on the audio thread"
                          : "audio thread allocated " + juce::String (v) + " time(s), "
                            + juce::String (b) + " bytes");
    check (silentBlocks == 0,
           silentBlocks == 0
               ? "no silent output blocks during full-processor channel churn"
               : juce::String (silentBlocks) + " silent output block(s) during channel churn");
    check (nonFiniteSamples == 0,
           nonFiniteSamples == 0
               ? "all full-processor output samples remain finite"
               : juce::String (nonFiniteSamples) + " non-finite output sample(s)");
    std::cout << "  minimum rendered block peak " << minimumBlockPeak << std::endl;

    // Symbolise the captured stacks, skipping the operator-new frames themselves.
    for (int t = 0; t < rtcheck::tracesTaken && t < 4; ++t)
    {
        std::cout << "\n  --- allocation site #" << (t + 1) << " ---" << std::endl;
        if (char** syms = backtrace_symbols (rtcheck::traces[t], rtcheck::traceDepth[t]))
        {
            for (int f = 1; f < rtcheck::traceDepth[t] && f < 12; ++f)
                std::cout << "    " << syms[f] << std::endl;
            std::free (syms);
        }
    }

    std::cout << (failures == 0 ? "RTSAFETY_OK" : "RTSAFETY_FAILED") << std::endl;
    std::cout.flush();
    std::_Exit (failures == 0 ? 0 : 1);
}
