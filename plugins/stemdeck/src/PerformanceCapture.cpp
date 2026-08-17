#include "PerformanceCapture.h"

namespace
{
juce::File withSuffix (const juce::File& file, const juce::String& suffix, const juce::String& extension)
{
    const auto parent = file.getParentDirectory();
    const auto base = file.getFileNameWithoutExtension();
    return parent.getChildFile (base + suffix).withFileExtension (extension);
}

juce::var objectWith (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* object = new juce::DynamicObject();
    for (const auto& [key, value] : properties)
        object->setProperty (key, value);
    return juce::var (object);
}

void writeJsonFile (const juce::File& file, const juce::var& json)
{
    if (file.getFullPathName().isEmpty())
        return;

    auto parent = file.getParentDirectory();
    if (! parent.exists())
        parent.createDirectory();

    file.replaceWithText (juce::JSON::toString (json, true), false, false, "\n");
}

}

void PerformanceCapture::begin (const juce::File& masterFile, double sampleRate)
{
    reset();
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    masterTargetFile = masterFile;
    eventLogFile = withSuffix (masterFile, "-events", ".json");
    arrangementFile = withSuffix (masterFile, "-arrangement", ".json");
    active.store (true);
    logEvent (0, "record_start", {}, {}, {}, masterFile.getFullPathName(), "Performance capture started");
}

void PerformanceCapture::reset()
{
    active.store (false);
    const juce::ScopedLock lock (eventLock);
    events.clear();
}

void PerformanceCapture::logEvent (int64_t sample,
                                   const juce::String& type,
                                   const juce::String& deck,
                                   const juce::String& parameter,
                                   const juce::String& value,
                                   const juce::String& path,
                                   const juce::String& details)
{
    if (! active.load() && type != "record_start")
        return;

    Event event;
    event.sample = juce::jmax<int64_t> (0, sample);
    event.timeSeconds = currentSampleRate > 0.0 ? (double) event.sample / currentSampleRate : 0.0;
    event.type = type;
    event.deck = deck;
    event.parameter = parameter;
    event.value = value;
    event.path = path;
    event.details = details;

    const juce::ScopedLock lock (eventLock);
    events.push_back (std::move (event));
}

juce::File PerformanceCapture::finishAndWrite (int64_t endSample, const juce::File& masterFile, const juce::File& micFile)
{
    if (! active.load())
        return arrangementFile;

    logEvent (endSample, "record_stop", {}, {}, {}, masterFile.getFullPathName(), "Performance capture stopped");
    active.store (false);

    std::vector<Event> snapshot;
    {
        const juce::ScopedLock lock (eventLock);
        snapshot = events;
    }

    writeJsonFile (eventLogFile, buildEventLogJson (snapshot, endSample, masterFile, micFile));
    writeJsonFile (arrangementFile, buildArrangementJson (snapshot, endSample, masterFile, micFile));
    return arrangementFile;
}

juce::String PerformanceCapture::deckName (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, numDecks - 1, deckIndex)));
}

juce::var PerformanceCapture::eventToVar (const Event& event)
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

juce::String PerformanceCapture::eventValue (const Event& event, const juce::String& fallback)
{
    return event.value.isNotEmpty() ? event.value : fallback;
}

void PerformanceCapture::addAutomationLane (juce::Array<juce::var>& lanes,
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

juce::Array<juce::var> PerformanceCapture::automationPointsFor (const std::vector<Event>& sourceEvents,
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

juce::var PerformanceCapture::buildEventLogJson (const std::vector<Event>& sourceEvents, int64_t endSample,
                                                 const juce::File& masterFile, const juce::File& micFile) const
{
    juce::Array<juce::var> eventVars;
    for (const auto& event : sourceEvents)
        eventVars.add (eventToVar (event));

    return objectWith ({
        { "schema", "SattariStemDeck.performanceLog.v1" },
        { "sampleRate", currentSampleRate },
        { "durationSamples", (double) endSample },
        { "durationSeconds", currentSampleRate > 0.0 ? (double) endSample / currentSampleRate : 0.0 },
        { "masterAudio", masterFile.getFullPathName() },
        { "micAudio", micFile.getFullPathName() },
        { "arrangement", arrangementFile.getFullPathName() },
        { "events", eventVars }
    });
}

juce::var PerformanceCapture::buildArrangementJson (const std::vector<Event>& sourceEvents, int64_t endSample,
                                                    const juce::File& masterFile, const juce::File& micFile) const
{
    const double endSeconds = currentSampleRate > 0.0 ? (double) endSample / currentSampleRate : 0.0;
    juce::Array<juce::var> lanes;

    for (int deckIndex = 0; deckIndex < numDecks; ++deckIndex)
    {
        const auto deck = deckName (deckIndex);
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
                { "startSeconds", clipStart },
                { "endSeconds", endSeconds },
                { "durationSeconds", juce::jmax (0.0, endSeconds - clipStart) },
                { "editable", true }
            }));
        }

        juce::Array<juce::var> automation;
        addAutomationLane (automation, "volume", "Volume", automationPointsFor (sourceEvents, { "volume", "fader" }, deck));
        addAutomationLane (automation, "pitch", "Pitch", automationPointsFor (sourceEvents, { "pitch" }, deck));
        addAutomationLane (automation, "tempo", "Tempo/BPM", automationPointsFor (sourceEvents, { "bpm", "tempo_interpretation" }, deck));
        addAutomationLane (automation, "eq", "EQ", automationPointsFor (sourceEvents, { "eq_low", "eq_mid", "eq_high" }, deck));
        addAutomationLane (automation, "filter", "Filter", automationPointsFor (sourceEvents, { "filter" }, deck));
        addAutomationLane (automation, "effects", "Effects", automationPointsFor (sourceEvents, { "key_lock", "live_key", "effect_toggle" }, deck));
        addAutomationLane (automation, "sync", "Sync Changes", automationPointsFor (sourceEvents, { "project_sync", "sync_mode", "beat_offset", "downbeat", "host_sync", "beat_sync" }, deck));

        lanes.add (objectWith ({
            { "id", deck.replaceCharacter (' ', '_').toLowerCase() },
            { "name", deck },
            { "type", "deck_lane" },
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
        { "name", "Mic" },
        { "type", "mic_lane" },
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
    addAutomationLane (masterAutomation, "crossfader", "Transition Handoff", automationPointsFor (sourceEvents, { "crossfader" }));
    addAutomationLane (masterAutomation, "transition_route", "Transition From / To / Curve",
                       automationPointsFor (sourceEvents, { "transition_from", "transition_to", "transition_curve" }));
    addAutomationLane (masterAutomation, "master_stems", "Master Stem Buses",
                       automationPointsFor (sourceEvents, {
                           "master_stem_drums_gain", "master_stem_drums_mute", "master_stem_drums_solo",
                           "master_stem_bass_gain", "master_stem_bass_mute", "master_stem_bass_solo",
                           "master_stem_music_gain", "master_stem_music_mute", "master_stem_music_solo",
                           "master_stem_vocals_gain", "master_stem_vocals_mute", "master_stem_vocals_solo" }));
    addAutomationLane (masterAutomation, "tempo", "Global Master Tempo", automationPointsFor (sourceEvents, { "master_bpm", "host_sync", "beat_sync" }));
    lanes.add (objectWith ({
        { "id", "master_reference" },
        { "name", "Master Reference" },
        { "type", "master_reference_lane" },
        { "editable", false },
        { "clips", masterClips },
        { "automationLanes", masterAutomation }
    }));

    lanes.add (objectWith ({
        { "id", "new_track_placeholder" },
        { "name", "Add New Track" },
        { "type", "new_track_placeholder" },
        { "editable", true },
        { "clips", juce::Array<juce::var>{} },
        { "automationLanes", juce::Array<juce::var>{} }
    }));

    return objectWith ({
        { "schema", "SattariStemDeck.arrangement.v1" },
        { "durationSamples", (double) endSample },
        { "durationSeconds", endSeconds },
        { "sampleRate", currentSampleRate },
        { "masterAudio", masterFile.getFullPathName() },
        { "micAudio", micFile.getFullPathName() },
        { "eventLog", eventLogFile.getFullPathName() },
        { "editGoal", "Live 4-deck DJ set converted into DAW-style lanes for cutting, slicing, fades, correction, automation, and export." },
        { "lanes", lanes }
    });
}
