#include "MainPluginProcessor.h"
#include "EventStream.h"
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "Recording health / intent-stream integration\n";

    MainPluginProcessor processor;
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    processor.prepareToPlay (sr, block);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("stemdeck-recording-health", {}, false);
    check (dir.createDirectory(), "temporary take directory created");
    const auto master = dir.getChildFile ("take.wav");

    check (processor.beginMasterRecording (master), "recording starts");
    check (processor.isIntentCaptureActive(), "deterministic intent capture starts with recording");
    check (processor.getIntentCaptureCoverage() == 29
           && processor.getIntentCaptureTargetCount() == 29,
           "HUD reports complete capture coverage (29/29)");
    check (processor.getCurrentTakeNumber() == 1, "take number advances");

    if (auto* gain = processor.parameters().getParameter ("master"))
        gain->setValueNotifyingHost (0.35f);

    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < 12; ++i)
    {
        audio.clear();
        processor.processBlock (audio, midi);
    }

    const auto checkpointEvents = processor.getIntentCaptureSnapshot();
    check (! checkpointEvents.empty(), "active take exposes a crash-recovery checkpoint");
    stemdeck::events::Header checkpointHeader;
    checkpointHeader.sampleRate = sr;
    checkpointHeader.blockSize = block;
    const auto checkpointFile = dir.getChildFile ("last-session-intent.sdev");
    check (stemdeck::events::Writer::write (checkpointFile, checkpointHeader, checkpointEvents),
           "active intent checkpoint writes before Stop");

    processor.stopMasterRecording();
    check (! processor.isIntentCaptureActive(), "intent capture stops with recording");
    check (processor.getRecordedSamples() == 12 * block, "elapsed sample clock is exact");

    const auto streamFile = processor.getLastIntentStreamFile();
    check (streamFile.existsAsFile(), "binary .sdev intent stream persisted");
    stemdeck::events::Header header;
    std::vector<stemdeck::events::Event> events;
    const auto read = stemdeck::events::Reader::read (streamFile, header, events);
    check (read == stemdeck::events::LoadResult::ok, "persisted intent stream parses");
    check (! events.empty(), "persisted intent stream contains the performed change");
    check (header.sampleRate == sr && header.blockSize == block,
           "intent stream pins recording sample rate and block size");
    stemdeck::events::Header recoveredHeader;
    std::vector<stemdeck::events::Event> recoveredEvents;
    check (stemdeck::events::Reader::read (checkpointFile, recoveredHeader, recoveredEvents)
               == stemdeck::events::LoadResult::ok
           && recoveredEvents.size() == checkpointEvents.size(),
           "crash-recovery checkpoint parses with every captured action");
    check (processor.getDroppedCaptureEventCount() == 0, "no capture events dropped");

    dir.deleteRecursively();
    if (failures == 0)
        std::cout << "RECORDING_HEALTH_OK\n";
    return failures == 0 ? 0 : 1;
}
