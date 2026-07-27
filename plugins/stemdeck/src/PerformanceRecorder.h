#pragma once

#include <JuceHeader.h>
#include <array>
#include "ArrangementBuilder.h"

class PerformanceRecorder
{
public:
    static constexpr int numDecks = 8;   // keep in sync with MainPluginProcessor::numDecks

    void begin (const juce::File& masterFile, double sampleRate);
    void reset();

    bool isActive() const noexcept { return active.load(); }
    double getSampleRate() const noexcept { return currentSampleRate; }
    juce::File getEventLogFile() const { return eventLogFile; }
    juce::File getArrangementFile() const { return arrangementFile; }

    void logEvent (int64_t sample,
                   const juce::String& type,
                   const juce::String& deck = {},
                   const juce::String& parameter = {},
                   const juce::String& value = {},
                   const juce::String& path = {},
                   const juce::String& details = {});

    juce::File finishAndWrite (int64_t endSample,
                               const juce::File& masterFile,
                               const juce::File& micFile,
                               const std::array<juce::File, numDecks>& deckFiles);

private:
    std::atomic<bool> active { false };
    double currentSampleRate = 44100.0;
    juce::File eventLogFile;
    juce::File arrangementFile;
    juce::File masterTargetFile;

    mutable juce::CriticalSection eventLock;
    std::vector<PerformanceEvent> events;
    ArrangementBuilder arrangementBuilder;

    static juce::File withSuffix (const juce::File& file, const juce::String& suffix, const juce::String& extension);
    static void writeJsonFile (const juce::File& file, const juce::var& json);
};
