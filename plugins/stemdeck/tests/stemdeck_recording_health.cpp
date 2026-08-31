#include "MainPluginProcessor.h"
#include "MasterRecorder.h"
#include "EventStream.h"
#include "ArrangementComping.h"
#include <cmath>
#include <cstdlib>
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc == 3 && juce::String (argv[1]) == "--crash-recording")
    {
        MasterRecorder recorder;
        recorder.prepare (48000.0, 2, 512);
        if (! recorder.beginRecording (juce::File (argv[2])))
            return 70;
        juce::AudioBuffer<float> block (2, 512);
        block.clear();
        block.applyGain (0.25f);
        for (int i = 0; i < 96; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int sample = 0; sample < block.getNumSamples(); ++sample)
                    block.setSample (ch, sample, sample == 0 ? 0.25f : 0.0f);
            recorder.captureBlock (block);
        }
        juce::Thread::sleep (1250); // allow the one-second crash-safe WAV checkpoint
        std::_Exit (77);            // deliberately skip every destructor
    }

    std::cout << "Recording health / intent-stream integration\n";

    const auto constructorStart = juce::Time::getMillisecondCounterHiRes();
    MainPluginProcessor processor;
    const auto constructorMs = juce::Time::getMillisecondCounterHiRes()
                             - constructorStart;
    check (processor.getParameters().size() <= 330,
           "host exposes only reachable A-H deck parameters (<=330 total)");
    check (constructorMs < 1500.0,
           "processor construction remains below the 1.5s cold-start ceiling");
    std::cout << "  info  processor construction " << constructorMs << " ms, "
              << processor.getParameters().size() << " parameters\n";
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    processor.prepareToPlay (sr, block);
    check (processor.getProcessedAudioBlockCount() == 0,
           "audio callback heartbeat starts at zero before the first block");

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("stemdeck-recording-health", {}, false);
    check (dir.createDirectory(), "temporary take directory created");
    const auto master = dir.getChildFile ("take.wav");

    const auto crashedTake = dir.getChildFile ("force-quit.wav");
    juce::ChildProcess crashedRecorder;
    const juce::StringArray crashArgs {
        juce::File::getSpecialLocation (juce::File::currentExecutableFile)
            .getFullPathName(), "--crash-recording", crashedTake.getFullPathName() };
    const bool crashStarted = crashedRecorder.start (crashArgs);
    const bool crashFinished = crashStarted
                            && crashedRecorder.waitForProcessToFinish (5000);
    juce::AudioFormatManager crashFormats;
    crashFormats.registerBasicFormats();
    auto crashedReader = std::unique_ptr<juce::AudioFormatReader> (
        crashFormats.createReaderFor (crashedTake));
    check (crashFinished && crashedRecorder.getExitCode() == 77
               && crashedReader != nullptr && crashedReader->lengthInSamples > 0,
           "force-quit leaves a checkpointed, readable WAV take");

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
    check (midiMap.numCc() > 100 && midiMap.numNote() > 75,
           "controller layer covers master, decks, stems, hot cues, pads and safety");
    check (midiMap.ccName (3) == "Deck A Volume"
               && midiMap.noteName (MainPluginProcessor::numDecks * 2) == "Pad 1",
           "expanded targets preserve the historical index bank for old projects");
    midiMap.beginLearnCc (0);
    juce::MidiBuffer learnMidi;
    learnMidi.addEvent (juce::MidiMessage::controllerEvent (3, 74, 64), 0);
    midiMap.processBlock (learnMidi);
    const auto learnedCc = midiMap.pollLearnResult();
    check (learnedCc.valid && learnedCc.isCc && learnedCc.controller == 74
               && learnedCc.midiChannel == 3,
           "MIDI learn records the controller channel as well as its number");
    const auto controllerPreset = midiMap.serializeNamedPreset ("Health Check");
    midiMap.clearAll();
    midiMap.deserialize ("C7=0;N36=0");
    check (midiMap.ccForTarget (0) == 7 && midiMap.ccChannelForTarget (0) == 0,
           "legacy index-based project mappings still load as any-channel bindings");
    midiMap.clearAll();
    juce::String controllerError;
    check (midiMap.deserializeNamedPreset (controllerPreset, controllerError)
               && midiMap.ccForTarget (0) == 74
               && midiMap.ccChannelForTarget (0) == 3,
           "named controller preset round-trips by target identity");

    auto* crossfaderParam = midiMap.ccParameter (0);
    const float crossfaderBefore = crossfaderParam != nullptr ? crossfaderParam->getValue() : -1.0f;
    juce::MidiBuffer wrongChannelCc;
    wrongChannelCc.addEvent (juce::MidiMessage::controllerEvent (2, 74, 100), 0);
    midiMap.processBlock (wrongChannelCc);
    processor.serviceDeferredActions();
    check (crossfaderParam != nullptr
               && std::abs (crossfaderParam->getValue() - crossfaderBefore) < 0.0001f,
           "a learned CC ignores the same controller number on another MIDI channel");

    juce::MidiBuffer mappedCc;
    mappedCc.addEvent (juce::MidiMessage::controllerEvent (3, 74, 64), 0);  // pickup
    mappedCc.addEvent (juce::MidiMessage::controllerEvent (3, 74, 100), 1);
    midiMap.processBlock (mappedCc);
    check (std::abs (crossfaderParam->getValue() - crossfaderBefore) < 0.0001f,
           "mapped CC never notifies a host parameter from the audio path");
    processor.serviceDeferredActions();
    check (std::abs (crossfaderParam->getValue() - 100.0f / 127.0f) < 0.01f,
           "the processor services mapped CC movement on the message-thread path");

    const int masterVolumeTarget = midiMap.ccTargetForName ("Master Volume");
    midiMap.beginLearnCc (masterVolumeTarget);
    juce::MidiBuffer replacementCc;
    replacementCc.addEvent (juce::MidiMessage::controllerEvent (4, 74, 100), 0);
    midiMap.processBlock (replacementCc);
    check (midiMap.ccForTarget (0) < 0
               && midiMap.ccForTarget (masterVolumeTarget) == 74
               && midiMap.ccChannelForTarget (masterVolumeTarget) == 4,
           "relearning a physical control replaces its old target instead of duplicating it");

    const int syncTarget = midiMap.noteTargetForName ("Deck A Sync");
    midiMap.beginLearnNote (syncTarget);
    juce::MidiBuffer learnSync;
    learnSync.addEvent (juce::MidiMessage::noteOn (2, 42, (juce::uint8) 100), 0);
    midiMap.processBlock (learnSync);
    auto* deckSync = processor.parameters().getParameter ("deckASync");
    if (deckSync != nullptr) deckSync->setValueNotifyingHost (0.0f);
    juce::AudioBuffer<float> controllerAudio (2, block);
    controllerAudio.clear();
    juce::MidiBuffer wrongSync;
    wrongSync.addEvent (juce::MidiMessage::noteOn (1, 42, (juce::uint8) 100), 0);
    processor.processBlock (controllerAudio, wrongSync);
    processor.serviceDeferredActions();
    check (deckSync != nullptr && deckSync->getValue() < 0.5f,
           "a note trigger ignores the mapped note on another MIDI channel");
    controllerAudio.clear();
    juce::MidiBuffer rightSync;
    rightSync.addEvent (juce::MidiMessage::noteOn (2, 42, (juce::uint8) 100), 0);
    processor.processBlock (controllerAudio, rightSync);
    processor.serviceDeferredActions();
    check (deckSync != nullptr && deckSync->getValue() > 0.5f,
           "mapped deck Sync triggers without requiring an open editor");
    juce::MidiBuffer syncFeedback;
    midiMap.writeFeedback (syncFeedback);
    bool foundSyncFeedback = false;
    for (const auto metadata : syncFeedback)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn() && message.getChannel() == 2
            && message.getNoteNumber() == 42)
            foundSyncFeedback = true;
    }
    check (foundSyncFeedback,
           "mapped host-visible triggers send channel-correct LED feedback");
    const auto compactControllerState = midiMap.serialize();
    midiMap.clearAll();
    midiMap.deserialize (compactControllerState);
    check (midiMap.ccForTarget (masterVolumeTarget) == 74
               && midiMap.ccChannelForTarget (masterVolumeTarget) == 4
               && midiMap.noteForTarget (syncTarget) == 42
               && midiMap.noteChannelForTarget (syncTarget) == 2,
           "channel-aware controller assignments survive project-state round-trip");

    const auto recordedBeforeTimelineBlocks = processor.getRecordedSamples();
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
    check (processor.getProcessedAudioBlockCount() > 0,
           "audio callback heartbeat advances only when real blocks are processed");
    check (! processor.isIntentCaptureActive(), "intent capture stops with recording");
    check (processor.getRecordedSamples() - recordedBeforeTimelineBlocks == 12 * block,
           "elapsed sample clock is exact");

    // A disappearing interface must be equivalent to a host stop/start: no stale
    // rate-sized buffers and finite output after re-prepare.
    processor.releaseResources();
    processor.prepareToPlay (44100.0, 256);
    juce::AudioBuffer<float> recoveredAudio (2, 256);
    juce::MidiBuffer recoveredMidi;
    processor.processBlock (recoveredAudio, recoveredMidi);
    bool finiteAfterReconnect = true;
    for (int ch = 0; ch < recoveredAudio.getNumChannels(); ++ch)
        for (int sample = 0; sample < recoveredAudio.getNumSamples(); ++sample)
            finiteAfterReconnect = finiteAfterReconnect
                                  && std::isfinite (recoveredAudio.getSample (ch, sample));
    check (finiteAfterReconnect,
           "audio interface disconnect/reconnect resumes with finite output");

    if (auto* masterParam = processor.parameters().getParameter ("master"))
    {
        const float beforeCorruptLoad = masterParam->getValue();
        const std::array<unsigned char, 9> corruptState { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
        processor.setStateInformation (corruptState.data(), (int) corruptState.size());
        check (std::abs (masterParam->getValue() - beforeCorruptLoad) < 0.0001f,
               "corrupted processor state is rejected without replacing the live set");
    }

    processor.loadDeckFileAsync (7, juce::File ("/missing/session/audio.wav"));
    check (processor.getDeckPath (7).isEmpty(),
           "missing audio is ignored safely and remains available for project relink");

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
    check (std::abs (header.sampleRate - sr) < 0.001 && header.blockSize == block,
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
