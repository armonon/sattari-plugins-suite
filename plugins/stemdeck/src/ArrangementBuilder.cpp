#include "ArrangementBuilder.h"

juce::var ArrangementBuilder::objectWith (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* object = new juce::DynamicObject();
    for (const auto& [key, value] : properties)
        object->setProperty (key, value);
    return juce::var (object);
}

juce::String ArrangementBuilder::deckName (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, numDecks - 1, deckIndex)));
}

juce::var ArrangementBuilder::eventToVar (const PerformanceEvent& event)
{
    return objectWith ({
        { "sample", (double) event.sample },
        { "timeSeconds", event.timeSeconds },
        { "type", event.type },
        { "deck", event.deck },
        { "parameter", event.parameter },
        { "value", event.value },
        { "path", event.path },
        { "details", event.details }
    });
}

void ArrangementBuilder::addAutomationLane (juce::Array<juce::var>& lanes,
                                            const juce::String& laneId,
                                            const juce::String& laneName,
                                            const juce::Array<juce::var>& points)
{
    lanes.add (objectWith ({
        { "id", laneId },
        { "name", laneName },
        { "editable", true },
        { "placeholder", points.isEmpty() },
        { "points", points }
    }));
}

juce::Array<juce::var> ArrangementBuilder::automationPointsFor (const std::vector<PerformanceEvent>& sourceEvents,
                                                                const juce::StringArray& parameters,
                                                                const juce::String& deck)
{
    juce::Array<juce::var> points;
    for (const auto& event : sourceEvents)
    {
        if (deck.isNotEmpty() && event.deck != deck)
            continue;
        if (! parameters.contains (event.parameter) && ! parameters.contains (event.type))
            continue;

        points.add (objectWith ({
            { "timeSeconds", event.timeSeconds },
            { "sample", (double) event.sample },
            { "parameter", event.parameter.isNotEmpty() ? event.parameter : event.type },
            { "value", event.value },
            { "eventType", event.type }
        }));
    }
    return points;
}

juce::var ArrangementBuilder::buildEventLogJson (const std::vector<PerformanceEvent>& events,
                                                 int64_t endSample,
                                                 double sampleRate,
                                                 const juce::File& masterFile,
                                                 const juce::File& micFile,
                                                 const std::array<juce::File, numDecks>& sourceDeckFiles,
                                                 const std::array<juce::File, numDecks>& printedDeckFiles,
                                                 const std::array<juce::String, numDecks>& channelNames,
                                                 const std::array<juce::String, numDecks>& channelTypes,
                                                 const std::array<juce::int64, numDecks>& channelIds,
                                                 const juce::File& arrangementFile) const
{
    juce::Array<juce::var> eventVars;
    for (const auto& event : events)
        eventVars.add (eventToVar (event));

    juce::Array<juce::var> deckAudio;
    for (int i = 0; i < numDecks; ++i)
        deckAudio.add (objectWith ({
            { "deck", deckName (i) },
            { "name", channelNames[(size_t) i] },
            { "channelType", channelTypes[(size_t) i] },
            { "channelId", channelIds[(size_t) i] },
            { "slotIndex", i },
            { "sourcePath", sourceDeckFiles[(size_t) i].getFullPathName() },
            { "printedPath", printedDeckFiles[(size_t) i].getFullPathName() },
            { "captureStage", "pre_fader_pre_insert" }
        }));

    return objectWith ({
        { "schema", "SattariStemDeck.performanceLog.v3" },
        { "sampleRate", sampleRate },
        { "durationSamples", (double) endSample },
        { "durationSeconds", sampleRate > 0.0 ? (double) endSample / sampleRate : 0.0 },
        { "masterAudio", masterFile.getFullPathName() },
        { "micAudio", micFile.getFullPathName() },
        { "deckAudio", deckAudio },
        { "arrangement", arrangementFile.getFullPathName() },
        { "events", eventVars }
    });
}

juce::var ArrangementBuilder::buildArrangementJson (const std::vector<PerformanceEvent>& sourceEvents,
                                                    int64_t endSample,
                                                    double sampleRate,
                                                    const juce::File& masterFile,
                                                    const juce::File& micFile,
                                                    const std::array<juce::File, numDecks>& sourceDeckFiles,
                                                    const std::array<juce::File, numDecks>& printedDeckFiles,
                                                    const std::array<juce::String, numDecks>& channelNames,
                                                    const std::array<juce::String, numDecks>& channelTypes,
                                                    const std::array<juce::int64, numDecks>& channelIds,
                                                    const juce::File& eventLogFile) const
{
    const double endSeconds = sampleRate > 0.0 ? (double) endSample / sampleRate : 0.0;
    juce::Array<juce::var> lanes;

    for (int deckIndex = 0; deckIndex < numDecks; ++deckIndex)
    {
        const auto deck = deckName (deckIndex);
        const auto channelName =
            channelNames[(size_t) deckIndex].isNotEmpty()
                ? channelNames[(size_t) deckIndex] : deck;
        const auto channelType =
            channelTypes[(size_t) deckIndex].isNotEmpty()
                ? channelTypes[(size_t) deckIndex] : juce::String ("trackDeck");
        juce::String currentPath;
        double clipStart = -1.0;
        juce::Array<juce::var> clips;

        for (const auto& event : sourceEvents)
        {
            if (event.deck != deck)
                continue;

            if (event.type == "track_loaded" && event.path.isNotEmpty())
                currentPath = event.path;

            if (event.type == "play_start" && clipStart < 0.0)
            {
                clipStart = event.timeSeconds;
                if (event.path.isNotEmpty())
                    currentPath = event.path;
            }

            if ((event.type == "play_stop" || event.type == "cue_jump") && clipStart >= 0.0)
            {
                clips.add (objectWith ({
                    { "id", deck.replaceCharacter (' ', '_').toLowerCase() + "_clip_" + juce::String (clips.size() + 1) },
                    { "type", "song_clip" },
                    { "source", currentPath },
                    { "sourceDeckAudio", sourceDeckFiles[(size_t) deckIndex].getFullPathName() },
                    { "printedDeckAudio", printedDeckFiles[(size_t) deckIndex].getFullPathName() },
                    { "startSeconds", clipStart },
                    { "endSeconds", event.timeSeconds },
                    { "durationSeconds", juce::jmax (0.0, event.timeSeconds - clipStart) },
                    { "editable", true }
                }));
                clipStart = event.type == "cue_jump" ? event.timeSeconds : -1.0;
            }
        }

        if (clipStart >= 0.0)
        {
            clips.add (objectWith ({
                { "id", deck.replaceCharacter (' ', '_').toLowerCase() + "_clip_" + juce::String (clips.size() + 1) },
                { "type", "song_clip" },
                { "source", currentPath },
                { "sourceDeckAudio", sourceDeckFiles[(size_t) deckIndex].getFullPathName() },
                { "printedDeckAudio", printedDeckFiles[(size_t) deckIndex].getFullPathName() },
                { "startSeconds", clipStart },
                { "endSeconds", endSeconds },
                { "durationSeconds", juce::jmax (0.0, endSeconds - clipStart) },
                { "editable", true }
            }));
        }

        juce::Array<juce::var> automation;
        addAutomationLane (automation, "fader", "Channel Fader", automationPointsFor (sourceEvents, { "fader" }, deck));
        addAutomationLane (automation, "volume", "Trim/Gain", automationPointsFor (sourceEvents, { "volume" }, deck));
        addAutomationLane (automation, "pitch", "Pitch", automationPointsFor (sourceEvents, { "pitch" }, deck));
        addAutomationLane (automation, "tempo", "Tempo/BPM", automationPointsFor (sourceEvents, { "bpm", "tempo_ratio", "tempo_interpretation" }, deck));
        addAutomationLane (automation, "eq", "EQ", automationPointsFor (sourceEvents, { "eq_low", "eq_mid", "eq_high" }, deck));
        addAutomationLane (automation, "filter", "Filter", automationPointsFor (sourceEvents, { "filter" }, deck));
        addAutomationLane (automation, "loops_cues", "Cues + Loops", automationPointsFor (sourceEvents, { "cue_jump", "cue", "loop", "loop_start", "loop_end" }, deck));
        addAutomationLane (automation, "effects", "FX", automationPointsFor (sourceEvents, { "fx", "reverb_mix", "echo_mix", "key_lock", "live_key", "effect_toggle" }, deck));
        addAutomationLane (automation, "sync", "Sync Changes", automationPointsFor (sourceEvents, { "sync_mode", "beat_offset", "downbeat", "host_sync", "beat_sync", "sync_change" }, deck));

        lanes.add (objectWith ({
            { "id", deck.replaceCharacter (' ', '_').toLowerCase() },
            { "name", channelName },
            { "type", channelType == "input" ? "input_lane"
                      : channelType == "midiInstrument" ? "instrument_lane"
                                                       : "deck_lane" },
            { "channelType", channelType },
            { "channelId", channelIds[(size_t) deckIndex] },
            { "slotIndex", deckIndex },
            { "renderedAudio", sourceDeckFiles[(size_t) deckIndex].getFullPathName() },
            { "printedAudio", printedDeckFiles[(size_t) deckIndex].getFullPathName() },
            { "captureStage", "pre_fader_pre_insert" },
            { "editable", true },
            { "clips", clips },
            { "automationLanes", automation }
        }));
    }

    juce::Array<juce::var> micClips;
    double micStart = -1.0;
    for (const auto& event : sourceEvents)
    {
        if (event.type == "mic_start" && micStart < 0.0)
            micStart = event.timeSeconds;
        if (event.type == "mic_stop" && micStart >= 0.0)
        {
            micClips.add (objectWith ({
                { "id", "mic_clip_" + juce::String (micClips.size() + 1) },
                { "type", "mic_clip" },
                { "source", micFile.getFullPathName() },
                { "startSeconds", micStart },
                { "endSeconds", event.timeSeconds },
                { "durationSeconds", juce::jmax (0.0, event.timeSeconds - micStart) },
                { "editable", true }
            }));
            micStart = -1.0;
        }
    }
    if (micStart >= 0.0)
    {
        micClips.add (objectWith ({
            { "id", "mic_clip_" + juce::String (micClips.size() + 1) },
            { "type", "mic_clip" },
            { "source", micFile.getFullPathName() },
            { "startSeconds", micStart },
            { "endSeconds", endSeconds },
            { "durationSeconds", juce::jmax (0.0, endSeconds - micStart) },
            { "editable", true }
        }));
    }

    juce::Array<juce::var> micAutomation;
    addAutomationLane (micAutomation, "volume", "Mic Volume", automationPointsFor (sourceEvents, { "mic_gain" }));
    lanes.add (objectWith ({
        { "id", "mic" },
        { "name", "Mic / Vocal" },
        { "type", "mic_lane" },
        { "renderedAudio", micFile.getFullPathName() },
        { "editable", true },
        { "clips", micClips },
        { "automationLanes", micAutomation }
    }));

    juce::Array<juce::var> masterClips;
    masterClips.add (objectWith ({
        { "id", "master_reference_clip" },
        { "type", "master_reference" },
        { "source", masterFile.getFullPathName() },
        { "startSeconds", 0.0 },
        { "endSeconds", endSeconds },
        { "durationSeconds", endSeconds },
        { "editable", false }
    }));

    juce::Array<juce::var> masterAutomation;
    addAutomationLane (masterAutomation, "crossfader", "Crossfader", automationPointsFor (sourceEvents, { "crossfader" }));
    addAutomationLane (masterAutomation, "tempo", "Global Master Tempo", automationPointsFor (sourceEvents, { "master_bpm", "host_sync", "beat_sync" }));
    addAutomationLane (masterAutomation, "gain", "Master Gain + Limiter", automationPointsFor (sourceEvents, { "master_gain", "limiter_ceiling" }));
    addAutomationLane (masterAutomation, "fx", "Master FX + Smart Master", automationPointsFor (sourceEvents, { "master_fx", "master_fx_x", "master_fx_y", "smart_master_on", "smart_master_mode", "smart_master_amount", "fx" }));
    lanes.add (objectWith ({
        { "id", "master_reference" },
        { "name", "Master Output" },
        { "type", "master_reference_lane" },
        { "renderedAudio", masterFile.getFullPathName() },
        { "editable", false },
        { "clips", masterClips },
        { "automationLanes", masterAutomation }
    }));

    lanes.add (objectWith ({
        { "id", "add_new_track" },
        { "name", "Add New Track" },
        { "type", "placeholder_lane" },
        { "editable", true },
        { "placeholder", true },
        { "clips", juce::Array<juce::var>() },
        { "automationLanes", juce::Array<juce::var>() }
    }));

    return objectWith ({
        { "schema", "SattariStemDeck.arrangement.v3" },
        { "durationSamples", (double) endSample },
        { "durationSeconds", endSeconds },
        { "sampleRate", sampleRate },
        { "masterAudio", masterFile.getFullPathName() },
        { "micAudio", micFile.getFullPathName() },
        { "eventLog", eventLogFile.getFullPathName() },
        { "editGoal", "Live 4-deck DJ set converted into DAW-style lanes with deck/mic/master renders, clips, cues, loops, and automation." },
        { "lanes", lanes }
    });
}
