#pragma once

// =============================================================================
//  Sattari Audio Suite — minimal cross-plugin key bus
// -----------------------------------------------------------------------------
//  Plugin instances cannot talk to each other directly inside a DAW. This is a
//  small file-based publish/read channel so a sender (e.g. Auto Pitch's AutoKey)
//  can broadcast the currently-detected key and a reader (e.g. Royal Chain's
//  Tune module) can follow it.
//
//  Storage: ~/Library/Application Support/Sattari Audio/cross-plugin/key-bus.json
//  Schema:  { "publisher": "auto-pitch", "root": 0..11, "scale": 0..2,
//             "timestampMs": <epoch ms> }
//
//  Stale entries (older than 30 seconds) are ignored, so a closed session
//  does not pin the key for the next reader.
// =============================================================================

#include <juce_core/juce_core.h>
#include <atomic>
#include <chrono>
#include <optional>

namespace sattari::xplugin
{

struct KeyMessage
{
    int root = 0;    // 0..11
    int scale = 0;   // 0=major, 1=minor, 2=chromatic (matches Auto Pitch convention)
};

inline juce::File keyBusFile()
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Sattari Audio").getChildFile ("cross-plugin");
    root.createDirectory();
    return root.getChildFile ("key-bus.json");
}

inline void publishKey (const juce::String& publisherId, int root, int scale)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("publisher", publisherId);
    obj->setProperty ("root",  juce::jlimit (0, 11, root));
    obj->setProperty ("scale", juce::jlimit (0, 11, scale));
    obj->setProperty ("timestampMs",
        (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::system_clock::now().time_since_epoch()).count());
    keyBusFile().replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline std::optional<KeyMessage> readPublishedKey (int maxAgeSeconds = 30)
{
    auto file = keyBusFile();
    if (! file.existsAsFile()) return std::nullopt;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return std::nullopt;

    const auto ts = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const juce::int64 now = std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - ts > (juce::int64) maxAgeSeconds * 1000)
        return std::nullopt;

    KeyMessage k;
    k.root  = juce::jlimit (0, 11, (int) parsed.getProperty ("root", 0));
    k.scale = juce::jlimit (0, 11, (int) parsed.getProperty ("scale", 0));
    return k;
}

} // namespace sattari::xplugin
