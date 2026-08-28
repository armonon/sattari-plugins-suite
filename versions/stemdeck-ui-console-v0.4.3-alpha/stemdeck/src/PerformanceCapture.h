#pragma once

#include <JuceHeader.h>
#include <array>

class PerformanceCapture
{
public:
    static constexpr int numDecks = 4;

    struct Event
    {
        int64_t sample = 0;
        double timeSeconds = 0.0;
        juce::String type;
        juce::String deck;
        juce::String parameter;
        juce::String value;
        juce::String path;
        juce::String details;
    };

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

    juce::File finishAndWrite (int64_t endSample, const juce::File& masterFile, const juce::File& micFile);

private:
    std::atomic<bool> active { false };
    double currentSampleRate = 44100.0;
    juce::File eventLogFile;
    juce::File arrangementFile;
    juce::File masterTargetFile;

    mutable juce::CriticalSection eventLock;
    std::vector<Event> events;

    static juce::String deckName (int deckIndex);
    static juce::var eventToVar (const Event& event);
    static juce::String eventValue (const Event& event, const juce::String& fallback = {});
    static void addAutomationLane (juce::Array<juce::var>& lanes,
                                   const juce::String& laneId,
                                   const juce::String& laneName,
                                   const juce::Array<juce::var>& points = {});
    static juce::Array<juce::var> automationPointsFor (const std::vector<Event>& sourceEvents,
                                                       const juce::StringArray& parameters,
                                                       const juce::String& deck = {});

    juce::var buildEventLogJson (const std::vector<Event>& sourceEvents, int64_t endSample,
                                 const juce::File& masterFile, const juce::File& micFile) const;
    juce::var buildArrangementJson (const std::vector<Event>& sourceEvents, int64_t endSample,
                                    const juce::File& masterFile, const juce::File& micFile) const;
};
