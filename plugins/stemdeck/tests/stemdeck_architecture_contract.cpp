#include "ArrangementBuilder.h"
#include "HostGraph.h"
#include "PerformanceRecorder.h"
#include "StemProvider.h"
#include "WarpEngine.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

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
    analysis.beatGridSamples = { 0.0, 32000.0, 64000.0, 96000.0 };
    analysis.transientAnchorSamples = { 100.0, 32200.0, 64100.0 };

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
}

void expectPerformanceRecorderWritesArrangementV2()
{
    auto dir = makeTempDir();
    auto master = dir.getChildFile ("set.wav");
    auto mic = dir.getChildFile ("set-mic.wav");
    std::array<juce::File, PerformanceRecorder::numDecks> deckFiles;
    std::array<juce::String, PerformanceRecorder::numDecks> channelNames;
    std::array<juce::String, PerformanceRecorder::numDecks> channelTypes;
    std::array<juce::int64, PerformanceRecorder::numDecks> channelIds;
    channelIds.fill (-1);
    for (int i = 0; i < PerformanceRecorder::numDecks; ++i)
    {
        deckFiles[(size_t) i] = dir.getChildFile ("set-deck-" + juce::String::charToString ((juce_wchar) ('A' + i)) + ".wav");
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
    recorder.logEvent (12000, "parameter_change", {}, "crossfader", "0.2500");
    recorder.logEvent (14400, "sync_change", "Deck A", "sync_mode", "2");
    recorder.logEvent (16000, "mic_start", {}, "mic_monitor", "on");
    recorder.logEvent (24000, "play_stop", "Deck A", "play", "off");
    recorder.logEvent (26000, "mic_stop", {}, "mic_monitor", "off");

    const auto arrangement = recorder.finishAndWrite (
        48000, master, mic, deckFiles,
        channelNames, channelTypes, channelIds);
    const auto eventLog = recorder.getEventLogFile();

    if (! arrangement.existsAsFile() || ! eventLog.existsAsFile())
        fail ("PerformanceRecorder did not write arrangement/event log JSON files");

    const auto arrangementText = arrangement.loadFileAsString();
    const auto eventLogText = eventLog.loadFileAsString();
    expectContains (arrangementText, "SattariStemDeck.arrangement.v2", "arrangement JSON");
    expectContains (arrangementText, "Deck A", "arrangement JSON");
    expectContains (arrangementText, "channelId", "arrangement JSON");
    expectContains (arrangementText, "slotIndex", "arrangement JSON");
    expectContains (arrangementText, "input_lane", "arrangement JSON");
    expectContains (arrangementText, "Vocal", "arrangement JSON");
    expectContains (arrangementText, "renderedDeckAudio", "arrangement JSON");
    expectContains (arrangementText, "Cues + Loops", "arrangement JSON");
    expectContains (arrangementText, "Master Output", "arrangement JSON");
    expectContains (arrangementText, "Add New Track", "arrangement JSON");
    expectContains (eventLogText, "SattariStemDeck.performanceLog.v2", "event log JSON");
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
    expectContains (graph.getStatusSummary(), "HostGraph", "HostGraph status");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    expectWarpEngineContract();
    expectPerformanceRecorderWritesArrangementV2();
    expectStemProviderOfflineCacheBoundary();
    expectStemProviderRunsConfiguredOfflineSeparator();
    expectHostGraphNoPluginPassThroughIsSafe();

    std::cout << "StemDeck architecture contract ok\n"
              << "  warp_engine=ready\n"
              << "  arrangement_v2=ready\n"
              << "  offline_stem_cache=guarded\n"
              << "  offline_separator_command=ready\n"
              << "  host_graph_empty_chain=pass_through\n";
    return 0;
}
