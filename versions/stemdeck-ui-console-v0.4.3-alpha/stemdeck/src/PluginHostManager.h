#pragma once

#include <JuceHeader.h>
#include "RoadmapAdapters.h"

// Early StemDeck plugin-hosting scaffold.
//
// This deliberately stops at discovery + non-audio instantiation ownership. Hosted
// plugin audio/MIDI routing is not wired into DeckPlayer or processBlock yet, so this
// can be built and validated without pretending StemDeck is a finished plugin host.
class PluginHostManager final : public PluginHostingAdapter
{
public:
    struct ScanResult
    {
        juce::StringArray scannedFormats;
        juce::Array<juce::PluginDescription> plugins;
        juce::StringArray blacklistedFiles;
        juce::StringArray messages;
    };

    using LoadCallback = std::function<void(bool success, juce::String message)>;

    PluginHostManager();
    ~PluginHostManager() override = default;

    juce::StringArray getSupportedPluginFormats() const override;
    bool canHostPlugins() const override;

    juce::String getStatusSummary() const;
    juce::FileSearchPath getDefaultSearchPathForFormat (const juce::String& formatName) const;
    ScanResult scanDefaultLocations (int maxPluginsPerFormat = 64, bool recursive = true);

    juce::Array<juce::PluginDescription> getKnownPlugins() const;
    bool hasLoadedPlugin() const noexcept;
    juce::String getLoadedPluginName() const;

    // Creates and retains a plugin instance for future routing work. The instance is
    // not connected to StemDeck audio, not exposed as UI, and not processed yet.
    void loadPluginAsync (const juce::PluginDescription& description,
                          double sampleRate,
                          int blockSize,
                          LoadCallback callback);
    void unloadPlugin();

private:
    juce::AudioPluginFormat* findFormatByName (const juce::String& formatName) const;
    juce::File getDeadMansPedalFileForFormat (const juce::String& formatName) const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::unique_ptr<juce::AudioPluginInstance> loadedPlugin;
    juce::String loadedPluginName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHostManager)
};
