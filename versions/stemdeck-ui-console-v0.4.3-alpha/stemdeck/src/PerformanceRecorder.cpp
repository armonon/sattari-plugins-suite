#include "PerformanceRecorder.h"

juce::File PerformanceRecorder::withSuffix (const juce::File& file, const juce::String& suffix, const juce::String& extension)
{
    return file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + suffix).withFileExtension (extension);
}

void PerformanceRecorder::writeJsonFile (const juce::File& file, const juce::var& json)
{
    if (file.getFullPathName().isEmpty())
        return;

    auto parent = file.getParentDirectory();
    if (! parent.exists())
        parent.createDirectory();

    file.replaceWithText (juce::JSON::toString (json, true), false, false, "\n");
}

void PerformanceRecorder::begin (const juce::File& masterFile, double sampleRate)
{
    reset();
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    masterTargetFile = masterFile;
    eventLogFile = withSuffix (masterFile, "-events", ".json");
    arrangementFile = withSuffix (masterFile, "-arrangement", ".json");
    {
        const juce::ScopedLock lock (eventLock);
        events.reserve (8192);
    }
    active.store (true);
    logEvent (0, "record_start", {}, {}, {}, masterFile.getFullPathName(), "Performance recorder started");
}

void PerformanceRecorder::reset()
{
    active.store (false);
    const juce::ScopedLock lock (eventLock);
    events.clear();
}

void PerformanceRecorder::logEvent (int64_t sample,
                                    const juce::String& type,
                                    const juce::String& deck,
                                    const juce::String& parameter,
                                    const juce::String& value,
                                    const juce::String& path,
                                    const juce::String& details)
{
    if (! active.load() && type != "record_start")
        return;

    PerformanceEvent event;
    event.sample = juce::jmax<int64_t> (0, sample);
    event.timeSeconds = currentSampleRate > 0.0 ? (double) event.sample / currentSampleRate : 0.0;
    event.type = type;
    event.deck = deck;
    event.parameter = parameter;
    event.value = value;
    event.path = path;
    event.details = details;

    const juce::ScopedTryLock lock (eventLock);
    if (! lock.isLocked())
        return;
    events.push_back (std::move (event));
}

juce::File PerformanceRecorder::finishAndWrite (int64_t endSample,
                                                const juce::File& masterFile,
                                                const juce::File& micFile,
                                                const std::array<juce::File, numDecks>& deckFiles)
{
    if (! active.load())
        return arrangementFile;

    logEvent (endSample, "record_stop", {}, {}, {}, masterFile.getFullPathName(), "Performance recorder stopped");
    active.store (false);

    std::vector<PerformanceEvent> snapshot;
    {
        const juce::ScopedLock lock (eventLock);
        snapshot = events;
    }

    writeJsonFile (eventLogFile, arrangementBuilder.buildEventLogJson (snapshot, endSample, currentSampleRate,
                                                                       masterFile, micFile, deckFiles, arrangementFile));
    writeJsonFile (arrangementFile, arrangementBuilder.buildArrangementJson (snapshot, endSample, currentSampleRate,
                                                                            masterFile, micFile, deckFiles, eventLogFile));
    return arrangementFile;
}
