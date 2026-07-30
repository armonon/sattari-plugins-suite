#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>

struct PerformanceEvent
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

class ArrangementBuilder
{
public:
    static constexpr int numDecks = 8;   // keep in sync with MainPluginProcessor::numDecks

    juce::var buildEventLogJson (const std::vector<PerformanceEvent>& events,
                                 int64_t endSample,
                                 double sampleRate,
                                 const juce::File& masterFile,
                                 const juce::File& micFile,
                                 const std::array<juce::File, numDecks>& sourceDeckFiles,
                                 const std::array<juce::File, numDecks>& printedDeckFiles,
                                 const std::array<juce::String, numDecks>& channelNames,
                                 const std::array<juce::String, numDecks>& channelTypes,
                                 const std::array<juce::int64, numDecks>& channelIds,
                                 const juce::File& arrangementFile) const;

    juce::var buildArrangementJson (const std::vector<PerformanceEvent>& events,
                                    int64_t endSample,
                                    double sampleRate,
                                    const juce::File& masterFile,
                                    const juce::File& micFile,
                                    const std::array<juce::File, numDecks>& sourceDeckFiles,
                                    const std::array<juce::File, numDecks>& printedDeckFiles,
                                    const std::array<juce::String, numDecks>& channelNames,
                                    const std::array<juce::String, numDecks>& channelTypes,
                                    const std::array<juce::int64, numDecks>& channelIds,
                                    const juce::File& eventLogFile) const;

private:
    static juce::String deckName (int deckIndex);
    static juce::var objectWith (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties);
    static juce::var eventToVar (const PerformanceEvent& event);
    static void addAutomationLane (juce::Array<juce::var>& lanes,
                                   const juce::String& laneId,
                                   const juce::String& laneName,
                                   const juce::Array<juce::var>& points = {});
    static juce::Array<juce::var> automationPointsFor (const std::vector<PerformanceEvent>& sourceEvents,
                                                       const juce::StringArray& parameters,
                                                       const juce::String& deck = {});
};
