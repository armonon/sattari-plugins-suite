#include "PluginHostManager.h"

namespace
{
juce::StringArray pluginNamesForLog (const juce::Array<juce::PluginDescription>& plugins, int maxItems)
{
    juce::StringArray names;
    for (int i = 0; i < juce::jmin (plugins.size(), maxItems); ++i)
        names.add (plugins.getReference (i).name + " (" + plugins.getReference (i).pluginFormatName + ")");
    return names;
}
}

PluginHostManager::PluginHostManager()
{
    juce::addDefaultFormatsToManager (formatManager);
}

juce::StringArray PluginHostManager::getSupportedPluginFormats() const
{
    juce::StringArray names;
    for (auto* format : formatManager.getFormats())
        if (format != nullptr)
            names.addIfNotAlreadyThere (format->getName());
    return names;
}

bool PluginHostManager::canHostPlugins() const
{
    return formatManager.getNumFormats() > 0;
}

juce::String PluginHostManager::getStatusSummary() const
{
    auto formats = getSupportedPluginFormats();
    if (formats.isEmpty())
        return "Plugin host scaffold built; no JUCE host formats are enabled in this build.";

    return "Plugin host scaffold built; enabled formats: " + formats.joinIntoString (", ")
         + "; known plugins: " + juce::String (knownPluginList.getNumTypes())
         + (loadedPlugin != nullptr ? "; loaded: " + loadedPluginName : "; no plugin loaded");
}

juce::AudioPluginFormat* PluginHostManager::findFormatByName (const juce::String& formatName) const
{
    for (auto* format : formatManager.getFormats())
        if (format != nullptr && format->getName().equalsIgnoreCase (formatName))
            return format;

    return nullptr;
}

juce::FileSearchPath PluginHostManager::getDefaultSearchPathForFormat (const juce::String& formatName) const
{
    if (auto* format = findFormatByName (formatName))
        return format->getDefaultLocationsToSearch();

    return {};
}

juce::File PluginHostManager::getDeadMansPedalFileForFormat (const juce::String& formatName) const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Sattari")
                   .getChildFile ("StemDeck")
                   .getChildFile ("PluginHostScan");
    dir.createDirectory();

    auto safeName = formatName;
    safeName = safeName.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ");
    return dir.getChildFile (safeName.trim().replaceCharacter (' ', '-') + "-dead-mans-pedal.txt");
}

PluginHostManager::ScanResult PluginHostManager::scanDefaultLocations (int maxPluginsPerFormat, bool recursive)
{
    ScanResult result;

    if (! canHostPlugins())
    {
        result.messages.add ("No JUCE plugin-host formats are enabled; scan skipped.");
        return result;
    }

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr)
            continue;

        const auto formatName = format->getName();
        result.scannedFormats.add (formatName);

        if (! format->canScanForPlugins())
        {
            result.messages.add (formatName + ": format does not expose directory scanning.");
            continue;
        }

        auto searchPath = format->getDefaultLocationsToSearch();
        if (searchPath.getNumPaths() == 0)
        {
            result.messages.add (formatName + ": no default file-system search paths.");
            continue;
        }

        juce::PluginDirectoryScanner scanner (knownPluginList,
                                              *format,
                                              searchPath,
                                              recursive,
                                              getDeadMansPedalFileForFormat (formatName),
                                              true);

        juce::String currentlyScanning;
        int scannedCount = 0;
        while (scannedCount < maxPluginsPerFormat && scanner.scanNextFile (true, currentlyScanning))
            ++scannedCount;

        result.blacklistedFiles.addArray (knownPluginList.getBlacklistedFiles());
        result.messages.add (formatName + ": scanned " + juce::String (scannedCount)
                             + " candidate" + (scannedCount == 1 ? "" : "s")
                             + "; next: " + scanner.getNextPluginFileThatWillBeScanned());
    }

    result.plugins = knownPluginList.getTypes();
    const auto sampleNames = pluginNamesForLog (result.plugins, 8);
    if (! sampleNames.isEmpty())
        result.messages.add ("Known plugin sample: " + sampleNames.joinIntoString (", "));

    return result;
}

juce::Array<juce::PluginDescription> PluginHostManager::getKnownPlugins() const
{
    return knownPluginList.getTypes();
}

bool PluginHostManager::hasLoadedPlugin() const noexcept
{
    return loadedPlugin != nullptr;
}

juce::String PluginHostManager::getLoadedPluginName() const
{
    return loadedPluginName;
}

void PluginHostManager::loadPluginAsync (const juce::PluginDescription& description,
                                         double sampleRate,
                                         int blockSize,
                                         LoadCallback callback)
{
    loadedPlugin.reset();
    loadedPluginName.clear();

    formatManager.createPluginInstanceAsync (description, sampleRate, blockSize,
        [this, callback = std::move (callback), requestedName = description.name]
        (std::unique_ptr<juce::AudioPluginInstance> instance, const juce::String& error)
        {
            if (instance == nullptr)
            {
                if (callback)
                    callback (false, error.isNotEmpty() ? error : "Could not instantiate " + requestedName);
                return;
            }

            loadedPluginName = instance->getName();
            loadedPlugin = std::move (instance);

            if (callback)
                callback (true, "Loaded host plugin scaffold instance: " + loadedPluginName);
        });
}

void PluginHostManager::unloadPlugin()
{
    loadedPlugin.reset();
    loadedPluginName.clear();
}
