#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace stemdeck::drag
{

inline constexpr const char* audioKind = "stemdeck.audio";
inline constexpr const char* pluginKind = "stemdeck.plugin";
inline constexpr const char* fileBrowserKind = "stemdeck.file-browser";

inline juce::var makeAudioFiles (const juce::Array<juce::File>& files)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("kind", audioKind);

    juce::Array<juce::var> paths;
    for (const auto& file : files)
        if (file.existsAsFile())
            paths.add (file.getFullPathName());
    payload->setProperty ("paths", juce::var (paths));

    // Keep the original scalar field for compatibility with older drag targets.
    if (! paths.isEmpty())
        payload->setProperty ("path", paths.getFirst());
    return juce::var (payload);
}

inline juce::var makeAudio (const juce::File& file)
{
    return makeAudioFiles ({ file });
}

inline juce::var makePlugin (int pluginIndex, const juce::String& name)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("kind", pluginKind);
    payload->setProperty ("index", pluginIndex);
    payload->setProperty ("name", name);
    return juce::var (payload);
}

inline juce::String kind (const juce::var& payload)
{
    if (auto* object = payload.getDynamicObject())
        return object->getProperty ("kind").toString();
    return payload.toString();
}

inline juce::File audioFile (const juce::var& payload)
{
    if (auto* object = payload.getDynamicObject())
        if (object->getProperty ("kind").toString() == audioKind)
            return juce::File (object->getProperty ("path").toString());
    return {};
}

inline juce::Array<juce::File> audioFiles (const juce::var& payload)
{
    juce::Array<juce::File> result;
    if (auto* object = payload.getDynamicObject())
    {
        if (object->getProperty ("kind").toString() != audioKind)
            return result;
        if (auto* paths = object->getProperty ("paths").getArray())
            for (const auto& path : *paths)
            {
                const juce::File file (path.toString());
                if (file.existsAsFile()) result.add (file);
            }
        if (result.isEmpty())
        {
            const juce::File file (object->getProperty ("path").toString());
            if (file.existsAsFile()) result.add (file);
        }
    }
    return result;
}

inline int pluginIndex (const juce::var& payload)
{
    if (auto* object = payload.getDynamicObject())
        if (object->getProperty ("kind").toString() == pluginKind)
            return (int) object->getProperty ("index");
    return -1;
}

inline juce::String pluginName (const juce::var& payload)
{
    if (auto* object = payload.getDynamicObject())
        if (object->getProperty ("kind").toString() == pluginKind)
            return object->getProperty ("name").toString();
    return {};
}

inline bool isSupportedAudioFile (const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return file.existsAsFile()
        && (ext == ".wav" || ext == ".aif" || ext == ".aiff"
            || ext == ".flac" || ext == ".mp3" || ext == ".m4a"
            || ext == ".ogg");
}

} // namespace stemdeck::drag
