#pragma once

// =============================================================================
//  Sattari Audio Suite — minimal cross-plugin key bus
// -----------------------------------------------------------------------------
//  Plugin instances cannot talk to each other directly inside a DAW. This is a
//  small file-based publish/read channel so a sender (e.g. Auto Pitch's AutoKey)
//  can broadcast the currently-detected key and a reader (e.g. Royal Chain's
//  Tune module) can follow it.
//
//  Storage: <userAppData>/Sattari Audio/cross-plugin/key-bus.json, where
//  userAppData is juce::File::userApplicationDataDirectory — on macOS that is
//  ~/Library (NOT ~/Library/Application Support), on Windows %APPDATA%, on Linux
//  ~/.config. All suite plugins call keyBusFile() so they always agree on the
//  location; the concrete path only matters if you poke the file by hand.
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

// --- Session channel: key + tempo + transient pulse --------------------------
//  The cross-plugin story is bigger than key. A "session" message carries the
//  detected key, the host tempo, and a monotonically-increasing transient pulse
//  id so a reader can tell when the publisher last saw an onset (e.g. Side Chain
//  ducking to a transient map another instance published, or a delay locking to
//  the session tempo). Written to the SAME json object as the key, so existing
//  publishKey / readPublishedKey readers keep working unchanged (they just ignore
//  the extra fields); session readers fall back to sensible defaults when a
//  key-only publisher wrote the file.
struct SessionMessage
{
    int    root = 0;           // 0..11
    int    scale = 0;          // 0=major, 1=minor, 2=chromatic
    double bpm = 0.0;          // host tempo, 0 if unknown
    juce::int64 transientId = 0;   // increments on each published onset
    bool   hasTempo = false;
};

inline void publishSession (const juce::String& publisherId, int root, int scale,
                            double bpm, juce::int64 transientId)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("publisher", publisherId);
    obj->setProperty ("root",  juce::jlimit (0, 11, root));
    obj->setProperty ("scale", juce::jlimit (0, 11, scale));
    obj->setProperty ("bpm",   bpm > 0.0 ? bpm : 0.0);
    obj->setProperty ("transientId", transientId);
    obj->setProperty ("timestampMs",
        (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::system_clock::now().time_since_epoch()).count());
    keyBusFile().replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline std::optional<SessionMessage> readSession (int maxAgeSeconds = 30)
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

    SessionMessage s;
    s.root  = juce::jlimit (0, 11, (int) parsed.getProperty ("root", 0));
    s.scale = juce::jlimit (0, 11, (int) parsed.getProperty ("scale", 0));
    s.bpm   = (double) parsed.getProperty ("bpm", 0.0);
    s.transientId = (juce::int64) parsed.getProperty ("transientId", 0);
    s.hasTempo = s.bpm > 0.0;
    return s;
}

// --- SessionReader: realtime-safe consumer side ------------------------------
//  A consumer plugin holds ONE of these as a member. It owns a low-priority
//  background thread that polls the bus file every ~400 ms and publishes the
//  result into lock-free atomics. The audio thread only ever reads those atomics
//  (current()), so there is NO file I/O on the audio thread and NO shared/static
//  state between instances. Start it in prepareToPlay(), it stops itself on
//  destruction (or call stop() in releaseResources()).
class SessionReader : private juce::Thread
{
public:
    SessionReader() : juce::Thread ("SattariSessionBus") {}
    ~SessionReader() override { stopThread (1000); }

    void start (int pollIntervalMs = 400)
    {
        intervalMs = juce::jmax (50, pollIntervalMs);
        if (! isThreadRunning())
            startThread();
    }
    void stop() { stopThread (1000); }

    // Lock-free snapshot for the audio thread. `valid` is false until a fresh,
    // non-stale session has been read.
    struct Snapshot
    {
        bool        valid = false;
        int         root = 0, scale = 0;
        double      bpm = 0.0;
        juce::int64 transientId = 0;
        bool        hasTempo = false;
    };

    Snapshot current() const noexcept
    {
        Snapshot s;
        s.valid       = hasValue.load (std::memory_order_relaxed);
        s.root        = root.load (std::memory_order_relaxed);
        s.scale       = scale.load (std::memory_order_relaxed);
        s.bpm         = bpm.load (std::memory_order_relaxed);
        s.transientId = tid.load (std::memory_order_relaxed);
        s.hasTempo    = hasTempo.load (std::memory_order_relaxed);
        return s;
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (auto m = readSession())
            {
                root.store (m->root, std::memory_order_relaxed);
                scale.store (m->scale, std::memory_order_relaxed);
                bpm.store (m->bpm, std::memory_order_relaxed);
                tid.store (m->transientId, std::memory_order_relaxed);
                hasTempo.store (m->hasTempo, std::memory_order_relaxed);
                hasValue.store (true, std::memory_order_relaxed);
            }
            else
            {
                hasValue.store (false, std::memory_order_relaxed);
            }
            wait (intervalMs);   // wakes early when stopThread() is called
        }
    }

    int intervalMs = 400;
    std::atomic<int>         root { 0 }, scale { 0 };
    std::atomic<double>      bpm { 0.0 };
    std::atomic<juce::int64> tid { 0 };
    std::atomic<bool>        hasTempo { false }, hasValue { false };
};

} // namespace sattari::xplugin
