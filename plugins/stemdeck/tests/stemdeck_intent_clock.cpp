// The intent stream must carry TIME, not just order.
//
// THE BUG THIS PINS. Dispatcher::setClockSamples() existed, was documented ("the audio
// thread advances it; the message thread reads it when stamping"), and had NO production
// caller. Only three references existed in the whole repo and all three were in tests. So
// every event MainPluginProcessor captured was stamped atSample = 0: the -intent.sdev
// stream recorded the ORDER of a performance but not WHEN anything happened.
//
// That silently voids the two things the event stream exists for (CLAUDE.md §2) — the
// arrangement view has no timeline to place events on, and tempo re-export has no time to
// re-time against.
//
// StemDeckReplayHash did not catch it because that test calls setClockSamples() ITSELF
// before each block; it was the only caller driving the clock, so it certified a code path
// the shipping app never took. This test drives the PRODUCTION path only — it never calls
// setClockSamples — so it fails if the processor ever stops publishing its clock again.
//
// Semantics pinned here: the clock origin is the take, not the session. beginMasterRecording
// resets recordingSampleCounter to 0 and calls beginCapture() together, so atSample is the
// sample offset from the start of the take, on the same origin midiCapture already uses.
#include "MainPluginProcessor.h"
#include <iostream>
#include <vector>

using namespace stemdeck::events;

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

Scope masterScope()
{
    Scope s;
    s.kind = ScopeKind::master;
    return s;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    constexpr int    kBlockSize  = 512;
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlocks     = 12;

    // Dispatch a gesture immediately AFTER these blocks are rendered. processBlock(b)
    // publishes the clock as it enters, using the counter value at the top of that block
    // (b * kBlockSize), and advances the counter afterwards — so an event dispatched
    // between block b and block b+1 belongs at sample b * kBlockSize.
    const std::vector<int> gestureBlocks { 0, 3, 7, 11 };

    std::cout << "--- StemDeck intent clock contract" << std::endl;

    const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("stemdeck-intent-clock");
    tempDir.createDirectory();
    const auto takeFile = tempDir.getChildFile ("take.wav");

    std::vector<Event> stream;
    {
        MainPluginProcessor proc;
        proc.prepareToPlay (kSampleRate, kBlockSize);

        // The real record path: resets the sample counter AND begins intent capture.
        if (! proc.beginMasterRecording (takeFile))
        {
            std::cerr << "FAIL: beginMasterRecording refused to start a take" << std::endl;
            tempDir.deleteRecursively();
            return 1;
        }
        check (true, "master recording started (intent capture is live)");

        juce::AudioBuffer<float> buf (2, kBlockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < kBlocks; ++b)
        {
            buf.clear();
            midi.clear();
            proc.processBlock (buf, midi);

            for (const int gb : gestureBlocks)
                if (gb == b)
                    proc.dispatcher().dispatchParam (masterScope(), ParamId::crossfader,
                                                     0.1 * (double) (b + 1));
        }

        stream = proc.dispatcher().takeRecorded();
        proc.stopMasterRecording();
    }

    // ── The regression itself ────────────────────────────────────────────────
    std::vector<Event> params;
    for (const auto& e : stream)
        if (e.type == EventType::param && e.param.id == ParamId::crossfader)
            params.push_back (e);

    check (params.size() == gestureBlocks.size(),
           "captured every dispatched gesture (" + juce::String ((int) params.size())
               + " of " + juce::String ((int) gestureBlocks.size()) + ")");

    if (params.size() != gestureBlocks.size())
    {
        std::cerr << "cannot verify timestamps without the full gesture set" << std::endl;
        tempDir.deleteRecursively();
        return 1;
    }

    // This is the assertion that would have caught the shipped bug: with the clock
    // unwired, every atSample is 0 and this fails on the second gesture.
    int nonZero = 0;
    for (const auto& e : params)
        if (e.atSample > 0) ++nonZero;

    check (nonZero > 0,
           "events carry a real timeline position, not atSample=0 "
           "(" + juce::String (nonZero) + " of " + juce::String ((int) params.size())
               + " non-zero)");

    for (size_t i = 0; i < params.size(); ++i)
    {
        const std::int64_t expected = (std::int64_t) gestureBlocks[i] * kBlockSize;
        check (params[i].atSample == expected,
               "gesture after block " + juce::String (gestureBlocks[i])
                   + " is stamped at sample " + juce::String (expected)
                   + " (got " + juce::String (params[i].atSample) + ")");
    }

    // Order must remain strictly increasing in time as well as in seq — a stream that is
    // ordered but flat in time is exactly the failure mode above.
    bool strictlyAdvancing = true;
    for (size_t i = 1; i < params.size(); ++i)
        if (params[i].atSample <= params[i - 1].atSample)
            strictlyAdvancing = false;
    check (strictlyAdvancing, "successive gestures advance monotonically in time");

    tempDir.deleteRecursively();

    std::cout << (failures == 0 ? "StemDeck intent clock contract ok"
                                : "StemDeck intent clock contract FAILED")
              << std::endl;
    return failures == 0 ? 0 : 1;
}
