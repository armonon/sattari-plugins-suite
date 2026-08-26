#include "ArrangementBuilder.h"
#include "HostGraph.h"
#include "PerformanceRecorder.h"
#include "PluginSandboxClient.h"
#include "StemProvider.h"
#include "WarpEngine.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

struct StemDeckPluginSandboxTestAccess
{
    static void simulateHelperCrash (PluginSandboxClient& client)
    {
        client.intentionalStop.store (false, std::memory_order_release);
        client.ready.store (true, std::memory_order_release);
        client.lastDescription = {};
        client.handleConnectionLost();
    }
};

namespace
{
[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

void expectContains (const juce::String& text, const juce::String& needle, const std::string& context)
{
    if (! text.contains (needle))
        fail (context + " missing: " + needle.toStdString());
}

juce::File makeTempDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("stemdeck-architecture-contract", {}, false);
    if (! dir.createDirectory())
        fail ("could not create temp directory: " + dir.getFullPathName().toStdString());
    return dir;
}

void expectWarpEngineContract()
{
    WarpEngine warp;
    warp.prepare (48000.0, 512, 2);

    TrackAnalysis analysis;
    analysis.sampleRate = 48000.0;
    analysis.detectedBpm = 90.0f;
    analysis.durationSeconds = 16.0;
    analysis.downbeatSample = 68000.0;
    // A deliberately accelerating grid. At project beat 2.125 (the nominal
    // coordinate of the detected downbeat), Anchor Sync must land on the real
    // analysed sample 68000 rather than the fixed-90-BPM sample 68000 only by
    // coincidence elsewhere in the grid.
    analysis.beatGridSamples = { 0.0, 34000.0, 68000.0, 100000.0 };
    analysis.transientAnchorSamples = { 100.0, 34200.0, 68100.0 };

    const auto grid = BeatSyncEngine::makeGrid (analysis,
                                                90.0f,
                                                0.0f,
                                                0.0f,
                                                BeatSyncEngine::TempoInterpretation::normal,
                                                BeatSyncEngine::SyncMode::strict);

    const auto plan = warp.makePlan ({ 120.0, true, 8.0 }, grid, 48000.0, 48000.0 * 16.0, 3.0f, true, true);
    if (! plan.beatSync.hasLockedSourcePosition)
        fail ("WarpEngine strict beat-sync plan did not lock source position");
    if (std::abs (plan.deck.tempoRatio - (120.0f / 90.0f)) > 0.001f)
        fail ("WarpEngine tempo ratio did not use per-deck source BPM");
    if (! plan.timePitch.pitchPreservingActive)
        fail ("WarpEngine did not preserve pitch for a key-lock tempo move");
    if (! plan.backend.available)
        fail ("WarpEngine selected backend is not available");
    expectContains (plan.statusLabel, "Warp:", "WarpEngine status label");
    expectContains (plan.statusLabel, "backend:", "WarpEngine status label");

    const auto anchorGrid = BeatSyncEngine::makeGrid (
        analysis, 90.0f, 0.0f,
        (float) (-analysis.downbeatSample / 32000.0),
        BeatSyncEngine::TempoInterpretation::normal,
        BeatSyncEngine::SyncMode::anchor);
    const auto anchorPlan = BeatSyncEngine::makePlaybackPlan (
        { 90.0, true, 0.0 }, anchorGrid, 48000.0, 48000.0 * 16.0, true);
    if (! anchorPlan.hasLockedSourcePosition
        || std::abs (anchorPlan.lockedSourcePositionSamples
                     - analysis.downbeatSample) > 1.0)
        fail ("Anchor Sync did not map the detected variable-tempo downbeat to the master downbeat");

    const auto strictGrid = BeatSyncEngine::makeGrid (
        analysis, 90.0f, 0.0f, 0.0f,
        BeatSyncEngine::TempoInterpretation::normal,
        BeatSyncEngine::SyncMode::strict);
    const auto variableAnchorGrid = BeatSyncEngine::makeGrid (
        analysis, 90.0f, 0.0f, 0.0f,
        BeatSyncEngine::TempoInterpretation::normal,
        BeatSyncEngine::SyncMode::anchor);
    double secondBeatProjectCoordinate = 0.0;
    for (const auto& marker : variableAnchorGrid.warpMarkers)
        if (! marker.transientAnchor
            && std::abs (marker.sourceSample - 34000.0) <= 1.0)
            secondBeatProjectCoordinate = marker.projectBeat;
    const auto strictAtAnalysedBeat = BeatSyncEngine::makePlaybackPlan (
        { 90.0, true, secondBeatProjectCoordinate }, strictGrid,
        48000.0, 48000.0 * 16.0, true);
    const auto anchorAtAnalysedBeat = BeatSyncEngine::makePlaybackPlan (
        { 90.0, true, secondBeatProjectCoordinate }, variableAnchorGrid,
        48000.0, 48000.0 * 16.0, true);
    if (std::abs (strictAtAnalysedBeat.lockedSourcePositionSamples
                  - secondBeatProjectCoordinate * 32000.0) > 1.0)
        fail ("Strict Sync no longer follows the nominal fixed-BPM grid");
    if (std::abs (anchorAtAnalysedBeat.lockedSourcePositionSamples - 34000.0) > 1.0)
        fail ("Anchor Sync discarded the analysed variable-tempo beat position");

    auto correctedGrid = variableAnchorGrid;
    BeatSyncEngine::WarpMarker correction {
        secondBeatProjectCoordinate, 0.0, 36000.0, false
    };
    BeatSyncEngine::applyUserWarpMarkers (correctedGrid, { correction });
    const double correctedSample = BeatSyncEngine::sourceSampleForProjectBeat (
        correctedGrid, secondBeatProjectCoordinate, 32000.0, 48000.0 * 16.0);
    if (std::abs (correctedSample - 36000.0) > 1.0)
        fail ("user beat-grid correction was not shared by the authoritative warp map");
    int markersAtBeat = 0;
    for (const auto& marker : correctedGrid.warpMarkers)
        if (std::abs (marker.projectBeat - secondBeatProjectCoordinate) < 0.01)
            ++markersAtBeat;
    if (markersAtBeat != 1)
        fail ("user correction did not replace the analysed marker at its project beat");
}

void expectPluginHelperRecoveryContract()
{
    PluginSandboxClient client;
    client.prepare (48000.0, 512, 2);
    juce::AudioBuffer<float> dry (2, 32);
    for (int ch = 0; ch < dry.getNumChannels(); ++ch)
        for (int i = 0; i < dry.getNumSamples(); ++i)
            dry.setSample (ch, i, (float) (i + 1) / 64.0f);
    const auto before = dry.getSample (0, 11);
    StemDeckPluginSandboxTestAccess::simulateHelperCrash (client);
    juce::MidiBuffer midi;
    client.process (dry, midi);
    if (! client.hasCrashed() || client.isReady()
        || std::abs (dry.getSample (0, 11) - before) > 1.0e-7f
        || ! client.status().containsIgnoreCase ("dry-bypassed"))
        fail ("crashed plugin helper did not fail dry with an explicit recovery state");
}

void expectPerformanceRecorderWritesEditableSourceArrangement()
{
    auto dir = makeTempDir();
    auto master = dir.getChildFile ("set.wav");
    auto mic = dir.getChildFile ("set-mic.wav");
    std::array<juce::File, PerformanceRecorder::numDecks> deckFiles;
    std::array<juce::File, PerformanceRecorder::numDecks> printedDeckFiles;
    std::array<juce::String, PerformanceRecorder::numDecks> channelNames;
    std::array<juce::String, PerformanceRecorder::numDecks> channelTypes;
    std::array<juce::int64, PerformanceRecorder::numDecks> channelIds;
    channelIds.fill (-1);
    for (int i = 0; i < PerformanceRecorder::numDecks; ++i)
    {
        deckFiles[(size_t) i] = dir.getChildFile ("set-deck-" + juce::String::charToString ((juce_wchar) ('A' + i)) + ".wav");
        printedDeckFiles[(size_t) i] = dir.getChildFile ("set-deck-" + juce::String::charToString ((juce_wchar) ('A' + i)) + "-print.wav");
        channelNames[(size_t) i] = "Deck " + juce::String::charToString ((juce_wchar) ('A' + i));
        channelTypes[(size_t) i] = "trackDeck";
        channelIds[(size_t) i] = i;
    }
    channelNames[1] = "Vocal";
    channelTypes[1] = "input";
    channelIds[1] = 99;

    PerformanceRecorder recorder;
    recorder.begin (master, 48000.0);
    recorder.logEvent (0, "track_loaded", "Deck A", "full_mix", "source.wav", dir.getChildFile ("source.wav").getFullPathName());
    recorder.logEvent (4800, "play_start", "Deck A", "play", "on", dir.getChildFile ("source.wav").getFullPathName());
    recorder.logEvent (9600, "parameter_change", "Deck A", "fader", "0.7500");
    recorder.logEvent (10000, "parameter_change", "Deck A", "reverb_mix", "0.4200");
    recorder.logEvent (10400, "parameter_change", "Deck A", "echo_mix", "0.3000");
    recorder.logEvent (12000, "parameter_change", {}, "crossfader", "0.2500");
    recorder.logEvent (12400, "parameter_change", {}, "master_fx_x", "0.6500");
    recorder.logEvent (12800, "effect_toggle", {}, "smart_master_on", "on");
    recorder.logEvent (13200, "parameter_change", {}, "limiter_ceiling", "-1.5000");
    recorder.logEvent (14400, "sync_change", "Deck A", "sync_mode", "2");
    recorder.logEvent (16000, "mic_start", {}, "mic_monitor", "on");
    recorder.logEvent (24000, "play_stop", "Deck A", "play", "off");
    recorder.logEvent (26000, "mic_stop", {}, "mic_monitor", "off");

    const auto arrangement = recorder.finishAndWrite (
        48000, master, mic, deckFiles, printedDeckFiles,
        channelNames, channelTypes, channelIds);
    const auto eventLog = recorder.getEventLogFile();

    if (! arrangement.existsAsFile() || ! eventLog.existsAsFile())
        fail ("PerformanceRecorder did not write arrangement/event log JSON files");

    const auto arrangementText = arrangement.loadFileAsString();
    const auto eventLogText = eventLog.loadFileAsString();
    expectContains (arrangementText, "SattariStemDeck.arrangement.v3", "arrangement JSON");
    expectContains (arrangementText, "Deck A", "arrangement JSON");
    expectContains (arrangementText, "channelId", "arrangement JSON");
    expectContains (arrangementText, "slotIndex", "arrangement JSON");
    expectContains (arrangementText, "input_lane", "arrangement JSON");
    expectContains (arrangementText, "Vocal", "arrangement JSON");
    expectContains (arrangementText, "sourceDeckAudio", "arrangement JSON");
    expectContains (arrangementText, "printedDeckAudio", "arrangement JSON");
    expectContains (arrangementText, "pre_fader_pre_insert", "arrangement JSON");
    expectContains (arrangementText, "reverb_mix", "arrangement JSON");
    expectContains (arrangementText, "echo_mix", "arrangement JSON");
    expectContains (arrangementText, "master_fx_x", "arrangement JSON");
    expectContains (arrangementText, "smart_master_on", "arrangement JSON");
    expectContains (arrangementText, "limiter_ceiling", "arrangement JSON");
    expectContains (arrangementText, "Cues + Loops", "arrangement JSON");
    expectContains (arrangementText, "Master Output", "arrangement JSON");
    expectContains (arrangementText, "Add New Track", "arrangement JSON");
    expectContains (eventLogText, "SattariStemDeck.performanceLog.v3", "event log JSON");
    expectContains (eventLogText, "record_stop", "event log JSON");

    dir.deleteRecursively();
}

void expectStemProviderOfflineCacheBoundary()
{
    auto dir = makeTempDir();
    auto source = dir.getChildFile ("source.wav");
    source.replaceWithText ("not decoded in this contract\n");

    StemProvider provider;
    provider.setCacheRoot (dir.getChildFile ("stem-cache"));

    const auto result = provider.inspectCache (source);
    if (result.allCoreStemsReady)
        fail ("StemProvider claimed all core stems ready without cached stem WAVs");
    if (! result.stems[(size_t) DeckPlayer::fullMixStem].ready)
        fail ("StemProvider did not mark source full mix ready");
    if (result.stems[(size_t) DeckPlayer::drumsStem].ready)
        fail ("StemProvider marked missing drums cache ready");
    if (! result.cacheDirectory.exists())
        fail ("StemProvider did not create cache directory");
    expectContains (result.message, "Offline stem cache inspected", "stem cache result");

    const auto status = provider.getStatusForSource (source);
    if (status.readyCoreStems != 0 || status.allCoreStemsReady)
        fail ("StemProvider premium status claimed uncached stems were ready");
    expectContains (status.headline, "AI", "stem provider premium status");

    dir.deleteRecursively();
}

void expectStemProviderRunsConfiguredOfflineSeparator()
{
    auto dir = makeTempDir();
    auto source = dir.getChildFile ("source.wav");
    source.replaceWithText ("fake audio payload\n");

    auto separator = dir.getChildFile ("fake-separator.sh");
    {
        std::ofstream stream (separator.getFullPathName().toStdString(), std::ios::binary);
        stream << "#!/bin/sh\n"
               << "set -eu\n"
               << "out=\n"
               << "while [ $# -gt 0 ]; do\n"
               << "  case \"$1\" in\n"
               << "    --output-dir) out=\"$2\"; shift 2 ;;\n"
               << "    *) shift ;;\n"
               << "  esac\n"
               << "done\n"
               << "out=$(printf '%s' \"$out\" | sed 's/^\"//;s/\"$//')\n"
               << "mkdir -p \"$out\"\n"
               << "for stem in drums bass music vocals; do printf '%s\\n' \"$stem\" > \"$out/$stem.wav\"; done\n";
    }
    separator.setExecutePermission (true);

    StemProvider provider;
    provider.setCacheRoot (dir.getChildFile ("stem-cache"));
    provider.setSeparatorCommand ("/bin/bash " + separator.getFullPathName());

    juce::String separatorMessage;
    const auto cacheDirectory = provider.getCacheDirectoryForSource (source);
    if (! StemProvider::runSeparatorCommand (provider.getSeparatorCommand(), source, cacheDirectory, separatorMessage))
        fail ("StemProvider configured offline separator command failed: " + separatorMessage.toStdString());

    auto result = provider.inspectCache (source);
    if (! result.allCoreStemsReady)
        fail ("StemProvider configured offline separator did not produce all core stems");
    expectContains (separatorMessage, "Offline separator generated cached stems", "stem separator result");

    const auto status = provider.getStatusForSource (source);
    if (status.readyCoreStems != 4 || ! status.allCoreStemsReady)
        fail ("StemProvider premium status did not report four cached AI stems");
    expectContains (status.headline, "AI stems ready", "stem provider premium ready status");

    for (int lane : { DeckPlayer::drumsStem, DeckPlayer::bassStem, DeckPlayer::musicStem, DeckPlayer::vocalsStem })
        if (! result.stems[(size_t) lane].file.existsAsFile())
            fail ("configured offline separator missing cached stem lane " + std::to_string (lane));

    dir.deleteRecursively();
}

void expectHostGraphNoPluginPassThroughIsSafe()
{
    HostGraph graph;
    graph.prepare (44100.0, 128, 2);

    juce::AudioBuffer<float> buffer (2, 128);
    buffer.clear();
    buffer.setSample (0, 0, 0.25f);
    juce::MidiBuffer midi;
    graph.processChain (HostGraph::Chain::deckA, buffer, midi);

    if (std::abs (buffer.getSample (0, 0) - 0.25f) > 0.0001f)
        fail ("HostGraph empty chain changed audio");
    const auto state = graph.createState();
    if (state.getType() != juce::Identifier ("HOST_GRAPH")
        || (int) state.getProperty ("version", 0) != 3)
        fail ("HostGraph did not create a versioned rack state");
    graph.setChainMix (HostGraph::Chain::deckA, 0.37f);
    graph.setChainBypassed (HostGraph::Chain::deckA, true);
    const auto routedState = graph.createState();
    HostGraph restoredGraph;
    restoredGraph.prepare (44100.0, 128, 2);
    if (! restoredGraph.restoreState (routedState).isEmpty()
        || std::abs (restoredGraph.getChainMix (HostGraph::Chain::deckA) - 0.37f) > 0.001f
        || ! restoredGraph.isChainBypassed (HostGraph::Chain::deckA))
        fail ("HostGraph did not preserve chain mix/bypass state");
    graph.setChainMix (HostGraph::Chain::deckA, 1.0f);
    graph.setChainBypassed (HostGraph::Chain::deckA, false);
    if (! graph.restoreState (state).isEmpty())
        fail ("HostGraph empty rack state produced restore warnings");

    buffer.clear();
    buffer.setSample (0, 0, 0.25f);
    graph.processChain (HostGraph::Chain::deckA, buffer, midi);
    if (std::abs (buffer.getSample (0, 0) - 0.25f) > 0.0001f)
        fail ("HostGraph empty state round-trip changed audio");

    graph.setEmergencyBypass (true);
    if (! graph.isEmergencyBypassed())
        fail ("HostGraph did not enter emergency dry bypass");
    buffer.clear();
    buffer.setSample (0, 0, 0.25f);
    graph.processChain (HostGraph::Chain::deckA, buffer, midi);
    if (std::abs (buffer.getSample (0, 0) - 0.25f) > 0.0001f)
        fail ("HostGraph emergency bypass changed dry audio");
    graph.setEmergencyBypass (false);
    if (graph.isEmergencyBypassed())
        fail ("HostGraph emergency bypass did not disengage");
    expectContains (graph.getStatusSummary(), "HostGraph", "HostGraph status");

    juce::PluginDescription untrusted;
    untrusted.name = "Untrusted Example";
    // Metadata is attacker-controlled: claiming to be Sattari must not bypass
    // the approved product/file identity allowlist.
    untrusted.manufacturerName = "Sattari";
    bool callbackRan = false;
    bool unexpectedlyLoaded = false;
    juce::String rejection;
    graph.loadPluginToChainAsync (
        HostGraph::Chain::deckA, untrusted,
        [&] (bool loaded, juce::String message)
        {
            callbackRan = true;
            unexpectedlyLoaded = loaded;
            rejection = std::move (message);
        });
    for (int tries = 0; tries < 40 && ! callbackRan; ++tries)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    if (! callbackRan || unexpectedlyLoaded)
        fail ("HostGraph did not keep an untrusted plug-in out of the app process");
    expectContains (rejection, "Sandbox helper", "HostGraph sandbox availability");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    expectWarpEngineContract();
    expectPerformanceRecorderWritesEditableSourceArrangement();
    expectStemProviderOfflineCacheBoundary();
    expectStemProviderRunsConfiguredOfflineSeparator();
    expectHostGraphNoPluginPassThroughIsSafe();
    expectPluginHelperRecoveryContract();

    std::cout << "StemDeck architecture contract ok\n"
              << "  warp_engine=ready\n"
              << "  arrangement_v3=editable-source\n"
              << "  offline_stem_cache=guarded\n"
              << "  offline_separator_command=ready\n"
              << "  host_graph_empty_chain=stateful-pass-through\n"
              << "  host_graph_emergency_bypass=reversible-dry-path\n"
              << "  third_party_plugins=helper-process-only\n";
    return 0;
}
