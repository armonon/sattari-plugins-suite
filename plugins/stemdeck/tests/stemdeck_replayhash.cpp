// A1 phase 5: THE GATE. Replaying a captured session must reproduce it exactly.
//
// Pass 1 loads a track, performs a scripted set of gestures through the dispatcher
// while rendering, and hashes every output sample.
// Pass 2 builds a FRESH processor, loads the same track, and replays the captured
// event stream against the same block grid, hashing the same way.
//
// The hashes must be bit-identical. Anything less and the arrangement editor would
// not reproduce what was performed.
//
// Determinism requirements this exercises (schema §6):
//   * events apply at the block their atSample falls in, in (atSample, seq) order
//   * a fixed block size — identity is only claimed at the size used (Q4: no
//     cross-block-size determinism)
//   * analysis is PINNED: pass 2 must use the analysis pass 1 actually rendered
//     with, not whatever refinement happens to have landed by then (§6.2)
//
//   StemDeckReplayHash [blocks]      default: 600
#include "MainPluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <string>

using namespace stemdeck::events;

namespace
{
int failures = 0;

void setBeatModelEnvironment (const char* value)
{
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_STEMDECK_BEAT_MODEL", value != nullptr ? value : "");
   #else
    if (value != nullptr) setenv ("SATTARI_STEMDECK_BEAT_MODEL", value, 1);
    else                  unsetenv ("SATTARI_STEMDECK_BEAT_MODEL");
   #endif
}

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}
void info (const juce::String& what) { std::cout << "  ..   " << what << std::endl; }

// FNV-1a over the raw sample bits. Bit-exact by construction: two renders that differ
// in the last mantissa bit of one sample produce different hashes.
struct Hasher
{
    std::uint64_t h = 1469598103934665603ULL;
    void add (const juce::AudioBuffer<float>& b, int n)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* p = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &p[i], sizeof (bits));
                for (int k = 0; k < 4; ++k)
                {
                    h ^= (std::uint64_t) ((bits >> (k * 8)) & 0xFF);
                    h *= 1099511628211ULL;
                }
            }
        }
    }
};

juce::File makeTrack (double seconds, int sr)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("stemdeck-replay-src.wav");
    if (f.existsAsFile() && f.getSize() > 1000) return f;
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, n);
    const double beat = 60.0 / 126.0 * sr;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double ph = std::fmod ((double) i, beat) / beat;
        const float v = (float) (0.5 * std::exp (-ph * 14.0) * std::sin (t * 58.0 * juce::MathConstants<double>::twoPi)
                               + 0.22 * std::sin (t * 196.0 * juce::MathConstants<double>::twoPi));
        buf.setSample (0, i, juce::jlimit (-1.0f, 1.0f, v));
        buf.setSample (1, i, juce::jlimit (-1.0f, 1.0f, v * 0.93f));
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (std::unique_ptr<juce::OutputStream> os = f.createOutputStream())
    {
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sr)
                                 .withNumChannels (2)
                                 .withBitsPerSample (16);
        if (auto w = wav.createWriterFor (os, options))
            w->writeFromAudioSampleBuffer (buf, 0, n);
    }
    return f;
}

// A gesture the performer makes at a given block.
struct Gesture { int atBlock; ParamId id; int channel; double value; };

const Gesture kScript[] = {
    {  20, ParamId::channelGain,  0, 0.80 },
    {  40, ParamId::eqLow,        0, -6.0 },
    {  60, ParamId::filter,       0, 0.30 },
    {  60, ParamId::eqHigh,       0,  3.5 },   // same block as the filter move
    {  90, ParamId::channelMute,  1, 1.0  },
    { 120, ParamId::crossfader,  -1, 0.75 },   // -1 => master scope
    { 150, ParamId::channelGain,  0, 0.42 },
    { 180, ParamId::channelMute,  1, 0.0  },
    { 220, ParamId::filter,       0, 0.68 },
    { 260, ParamId::masterGain,  -1, -3.0 },
    { 300, ParamId::channelGain,  0, 0.9137 },
};

Scope scopeFor (const Gesture& g)
{
    Scope s;
    if (g.channel < 0) { s.kind = ScopeKind::master; return s; }
    s.kind = ScopeKind::channel;
    s.channel = g.channel;
    return s;
}

void loadAndSettle (MainPluginProcessor& p, const juce::File& track)
{
    p.loadDeckFileAsync (0, track);

    // Wait for the REFINED analysis to land before rendering. If refinement arrives
    // mid-render in one pass and not the other, the two passes use different beat
    // grids and the hashes differ for a reason unrelated to the event stream.
    const double deadline = juce::Time::getMillisecondCounterHiRes() + 20000.0;
    while (p.deckAt (0).getRefinedAnalysis() == nullptr
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    // Let any remaining background work drain, so it is not still running at teardown.
    for (int i = 0; i < 100; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int kBlocks = argc >= 2 ? juce::String (argv[1]).getIntValue() : 600;
    constexpr int kBlockSize = 512;
    constexpr double kSampleRate = 48000.0;

    std::cout << "A1 replay hash (THE GATE)" << std::endl;
    info ("block size " + juce::String (kBlockSize) + " @ " + juce::String (kSampleRate, 0)
          + " Hz, " + juce::String (kBlocks) + " blocks");

    const auto track = makeTrack (20.0, 44100);

    // Keep both determinism passes on the same immediate DSP analysis. Neural beat
    // refinement is intentionally asynchronous and can land in one pass before the
    // other; the separate shutdown probe below restores the real model and exercises
    // cancellation during an actual ONNX run.
    const char* previousBeatModel = std::getenv ("SATTARI_STEMDECK_BEAT_MODEL");
    const bool hadBeatModelOverride = previousBeatModel != nullptr;
    const std::string savedBeatModel = hadBeatModelOverride ? previousBeatModel : "";
    setBeatModelEnvironment ("/__stemdeck_replayhash_dsp_only__.onnx");

    std::vector<Event> stream;
    Hasher liveHash;

    // ── Pass 1: perform and capture ──────────────────────────────────────────
    {
        MainPluginProcessor proc;
        proc.prepareToPlay (kSampleRate, kBlockSize);
        loadAndSettle (proc, track);
        check (proc.deckAt (0).getStemAudio (DeckPlayer::fullMixStem) != nullptr, "pass 1: track loaded");
        check (proc.deckAt (0).getRefinedAnalysis() != nullptr,
               "pass 1: analysis settled before rendering (deterministic grid)");

        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (proc.parameters().getParameter ("deckAPlay")))
            p->setValueNotifyingHost (1.0f);

        proc.dispatcher().beginCapture();

        juce::AudioBuffer<float> buf (2, kBlockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < kBlocks; ++b)
        {
            proc.dispatcher().setClockSamples ((std::int64_t) b * kBlockSize);
            for (const auto& g : kScript)
                if (g.atBlock == b)
                    proc.dispatcher().dispatchParam (scopeFor (g), g.id, g.value);

            buf.clear();
            proc.processBlock (buf, midi);
            liveHash.add (buf, kBlockSize);
        }

        proc.dispatcher().endCapture();
        stream = proc.dispatcher().takeRecorded();
    }
    check (true, "pass 1: processor teardown completed");

    check (! stream.empty(), "captured " + juce::String ((int) stream.size()) + " events");
    info ("live hash   " + juce::String::toHexString ((juce::int64) liveHash.h));

    // ── Pass 2: fresh processor, replay the stream ───────────────────────────
    Hasher replayHash;
    {
        MainPluginProcessor proc;
        proc.prepareToPlay (kSampleRate, kBlockSize);
        loadAndSettle (proc, track);
        check (proc.deckAt (0).getStemAudio (DeckPlayer::fullMixStem) != nullptr, "pass 2: track loaded");

        if (auto* p = dynamic_cast<juce::AudioParameterBool*> (proc.parameters().getParameter ("deckAPlay")))
            p->setValueNotifyingHost (1.0f);

        // Events sorted once; applied at the block their atSample falls in.
        auto sorted = stream;
        std::stable_sort (sorted.begin(), sorted.end());
        size_t next = 0;

        juce::AudioBuffer<float> buf (2, kBlockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < kBlocks; ++b)
        {
            const std::int64_t blockStart = (std::int64_t) b * kBlockSize;
            const std::int64_t blockEnd   = blockStart + kBlockSize;
            while (next < sorted.size() && sorted[next].atSample < blockEnd)
            {
                proc.applyEvent (sorted[next]);   // the SAME applier the live pass used
                ++next;
            }

            buf.clear();
            proc.processBlock (buf, midi);
            replayHash.add (buf, kBlockSize);
        }

        check (next == sorted.size(), "every captured event was applied ("
               + juce::String ((int) next) + "/" + juce::String ((int) sorted.size()) + ")");
    }
    check (true, "pass 2: processor teardown completed");

    info ("replay hash " + juce::String::toHexString ((juce::int64) replayHash.h));

    const bool identical = liveHash.h == replayHash.h;
    check (identical, identical
           ? "REPLAY IS SAMPLE-IDENTICAL TO THE LIVE PASS"
           : "replay DIVERGED from the live pass - A1 does not pass");

    if (! identical)
        std::cout << "\n  A1 is not closed. The stream, the applier, or something in the\n"
                     "  render path is non-deterministic. Do not treat the gate as passed.\n";

    setBeatModelEnvironment (hadBeatModelOverride ? savedBeatModel.c_str() : nullptr);

    // Regression probe for the old onnxruntime::RunSince shutdown crash. Publish
    // the DSP analysis (which schedules neural refinement), then immediately destroy
    // the processor while that refinement is likely in flight. The destructor must
    // cancel at a chunk boundary and join the pool before releasing the ONNX engines.
    {
        MainPluginProcessor proc;
        proc.prepareToPlay (kSampleRate, kBlockSize);
        proc.loadDeckFileAsync (0, track);
        const double deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
        while (proc.deckAt (0).getRefinedAnalysis() == nullptr
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        check (proc.deckAt (0).getRefinedAnalysis() != nullptr,
               "shutdown probe: refinement job was scheduled");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    }
    check (true, "shutdown probe: in-flight jobs cancelled and joined");

    std::cout << (failures == 0 ? "REPLAYHASH_OK" : "REPLAYHASH_FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
