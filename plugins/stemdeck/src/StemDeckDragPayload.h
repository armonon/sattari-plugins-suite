#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace stemdeck::drag
{

inline constexpr const char* audioKind = "stemdeck.audio";
inline constexpr const char* pluginKind = "stemdeck.plugin";
inline constexpr const char* fileBrowserKind = "stemdeck.file-browser";

inline juce::var makeAudio (const juce::File& file)
{
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("kind", audioKind);
    payload->setProperty ("path", file.getFullPathName());
    return juce::var (payload);
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
