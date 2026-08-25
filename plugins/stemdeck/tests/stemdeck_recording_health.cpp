#include "MainPluginProcessor.h"
#include "EventStream.h"
#include "ArrangementComping.h"
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
    check (processor.getIntentCaptureCoverage()
               == processor.getIntentCaptureTargetCount()
           && processor.getIntentCaptureTargetCount() > 29,
           "HUD reports complete capture coverage for the expanded live surface");
    check (processor.getCurrentTakeNumber() == 1, "take number advances");

    if (auto* gain = processor.parameters().getParameter ("master"))
    {
        gain->setValueNotifyingHost (0.35f);
        check (processor.capturePerformanceScene (0)
                   && processor.hasPerformanceScene (0),
               "performance scene captures the complete control surface");
        gain->setValueNotifyingHost (0.75f);
        check (processor.recallPerformanceScene (0)
                   && std::abs (gain->getValue() - 0.35f) < 0.0001f,
               "performance scene recall restores normalized values through dispatch");
    }

    auto& midiMap = processor.getMidiMapping();
    midiMap.beginLearnCc (0);
    juce::MidiBuffer learnMidi;
    learnMidi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 81), 0);
    midiMap.processBlock (learnMidi);
    const auto controllerPreset = midiMap.serializeNamedPreset ("Health Check");
    midiMap.clearAll();
    juce::String controllerError;
    check (midiMap.deserializeNamedPreset (controllerPreset, controllerError)
               && midiMap.ccForTarget (0) == 74,
           "named controller preset round-trips by target identity");

    juce::AudioBuffer<float> audio (2, block);
    juce::MidiBuffer midi;
    for (int i = 0; i < 12; ++i)
    {
        audio.clear();
        processor.processBlock (audio, midi);
    }

    for (int tries = 0;
         tries < 50 && processor.getRetrospectiveSecondsAvailable() < 0.05;
         ++tries)
        juce::Thread::sleep (2);
    juce::String retrospectiveError;
    const auto retrospectiveFile = dir.getChildFile ("last-audio.wav");
    check (processor.saveRetrospectiveAudio (
               retrospectiveFile, 0.1, retrospectiveError)
               && retrospectiveFile.getSize() > 44,
           "retrospective master history saves a valid WAV tail");

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

    processor.loadDeckFileAsync (0, master);
    for (int tries = 0; tries < 200 && processor.getDeckPath (0).isEmpty(); ++tries)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    check (processor.getDeckPath (0) == master.getFullPathName(),
           "recorded take loads for warp editing");
    processor.beginDeckBeatGridEdit (0, "add test marker");
    const int warpId = processor.addDeckUserWarpMarker (0, 0.03);
    check (warpId > 0 && processor.getDeckUserWarpMarkers (0).size() == 1,
           "multiple-marker warp map accepts a user anchor");
    processor.beginDeckBeatGridEdit (0, "move test marker");
    check (processor.moveDeckUserWarpMarker (0, warpId, 0.05)
               && std::abs (processor.getDeckUserWarpMarkers (0).front().sourceSeconds
                            - 0.05) < 0.001,
           "warp anchor moves without changing its musical beat");
    check (processor.undoDeckBeatGridEdit()
               && std::abs (processor.getDeckUserWarpMarkers (0).front().sourceSeconds
                            - 0.03) < 0.001,
           "beat-grid edit undo restores the previous source transient");
    check (processor.redoDeckBeatGridEdit()
               && std::abs (processor.getDeckUserWarpMarkers (0).front().sourceSeconds
                            - 0.05) < 0.001,
           "beat-grid edit redo restores the corrected marker");

    juce::MemoryBlock stateWithWarp;
    processor.getStateInformation (stateWithWarp);
    MainPluginProcessor restored;
    restored.setStateInformation (stateWithWarp.getData(),
                                  (int) stateWithWarp.getSize());
    check (restored.getDeckUserWarpMarkers (0).size() == 1
               && restored.getDeckUserWarpMarkers (0).front().id == warpId,
           "user warp anchors persist in the project state");
    check (processor.removeDeckUserWarpMarker (0, warpId)
               && processor.getDeckUserWarpMarkers (0).empty(),
           "warp anchor removal updates the editable map");

    juce::ValueTree takeState ("ARRANGEMENT"), takeLane ("LANE"), takeClip ("CLIP");
    takeLane.setProperty ("index", 0, nullptr);
    takeClip.setProperty ("file", master.getFullPathName(), nullptr);
    takeClip.setProperty ("dur", 0.12, nullptr);
    takeClip.setProperty ("tlStart", 0.0, nullptr);
    takeClip.setProperty ("trimStart", 0.0, nullptr);
    takeClip.setProperty ("trimEnd", 0.12, nullptr);
    takeLane.appendChild (takeClip, nullptr);
    takeState.appendChild (takeLane, nullptr);
    const auto compSource = stemdeck::comping::findCoveringSource (
        takeState, 0, 0.03, 0.08);
    check (compSource.isValid()
               && std::abs (compSource.trimStart - 0.03) < 0.001
               && std::abs (compSource.trimEnd - 0.08) < 0.001,
           "region comp resolves exact source bounds from another take");
    check (! stemdeck::comping::findCoveringSource (
                takeState, 0, 0.10, 0.16).isValid(),
           "region comp rejects a take that does not cover the whole edit");

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
