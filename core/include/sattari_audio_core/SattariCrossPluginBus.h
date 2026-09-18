#pragma once

// =============================================================================
//  Sattari Audio Suite — minimal cross-plugin key bus
// -----------------------------------------------------------------------------
//  Plugin instances cannot talk to each other directly inside a DAW. This is a
//  small file-based publish/read channel so a sender (e.g. Auto Pitch's AutoKey)
//  can broadcast the currently-detected key and a reader (e.g. Royal Chain's
//  Tune module) can follow it.
//
//  Storage: <userAppData>/Sattari Audio/cross-plugin/key-bus[-<session>].json,
//  where an optional session id comes from SATTARI_SESSION_ID (or an explicit
//  API argument). This isolates concurrently-open DAW projects while preserving
//  the legacy unscoped file when no id is configured.
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
#include <sattari_audio_core/SattariRealtime.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>

namespace sattari::xplugin
{

struct KeyMessage
{
    int root = 0;    // 0..11
    int scale = 0;   // 0=major, 1=minor, 2=chromatic (matches Auto Pitch convention)
};

inline juce::int64 currentTimeMs() noexcept
{
    return (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// A fresh higher-authority publisher owns the shared key/session file for a
// short lease. This prevents opportunistic detectors from immediately
// overwriting Compass while still allowing automatic fallback after it closes.
inline int publisherPriority (const juce::String& publisherId) noexcept
{
    if (publisherId == "brain")       return 100;
    if (publisherId == "song-key")    return 80;
    if (publisherId == "auto-pitch")  return 50;
    if (publisherId == "granulator")  return 40;
    return 10;
}

inline bool mayPublish (const juce::File& file, const juce::String& publisherId,
                        int leaseMs = 2500)
{
    if (! file.existsAsFile())
        return true;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject())
        return true;

    const auto previousPublisher =
        parsed.getProperty ("publisher", juce::String()).toString();
    if (previousPublisher == publisherId)
        return true;

    const auto timestamp = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const auto previousPriority = (int) parsed.getProperty (
        "priority", publisherPriority (previousPublisher));
    const bool leaseIsFresh = timestamp > 0
                           && currentTimeMs() - timestamp <= (juce::int64) leaseMs;
    return ! leaseIsFresh || publisherPriority (publisherId) >= previousPriority;
}

inline juce::InterProcessLock& keyBusPublishLock()
{
    static juce::InterProcessLock lock ("com.sattari.audio.key-bus-publish");
    return lock;
}

inline juce::String sanitiseSessionId (juce::String id)
{
    id = id.trim().retainCharacters (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    return id.substring (0, 48);
}

inline juce::String configuredSessionId()
{
    return sanitiseSessionId (
        juce::SystemStats::getEnvironmentVariable ("SATTARI_SESSION_ID", {}));
}

inline juce::String effectiveSessionId (const juce::String& requestedSessionId)
{
    return requestedSessionId.isNotEmpty() ? sanitiseSessionId (requestedSessionId)
                                           : configuredSessionId();
}

inline juce::File crossPluginBusRoot()
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Sattari Audio").getChildFile ("cross-plugin");
    root.createDirectory();
    return root;
}

inline juce::File keyBusFile (const juce::String& requestedSessionId = {})
{
    const auto sessionId = effectiveSessionId (requestedSessionId);
    const auto suffix = sessionId.isEmpty() ? juce::String() : "-" + sessionId;
    return crossPluginBusRoot().getChildFile ("key-bus" + suffix + ".json");
}

inline bool publishKey (const juce::String& publisherId, int root, int scale,
                        const juce::String& requestedSessionId = {})
{
    juce::InterProcessLock::ScopedLockType publishLock (keyBusPublishLock());
    if (! publishLock.isLocked()) return false;
    const auto sessionId = effectiveSessionId (requestedSessionId);
    const auto file = keyBusFile (sessionId);
    if (! mayPublish (file, publisherId))
        return false;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("publisher", publisherId);
    obj->setProperty ("schemaVersion", 3);
    if (sessionId.isNotEmpty()) obj->setProperty ("sessionId", sessionId);
    obj->setProperty ("priority", publisherPriority (publisherId));
    obj->setProperty ("root",  juce::jlimit (0, 11, root));
    obj->setProperty ("scale", juce::jlimit (0, 2, scale));
    obj->setProperty ("timestampMs", currentTimeMs());
    return file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline std::optional<KeyMessage> readPublishedKey (
    int maxAgeSeconds = 30, const juce::String& requestedSessionId = {})
{
    auto file = keyBusFile (requestedSessionId);
    if (! file.existsAsFile()) return std::nullopt;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return std::nullopt;

    const auto ts = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const juce::int64 now = currentTimeMs();
    if (now - ts > (juce::int64) maxAgeSeconds * 1000)
        return std::nullopt;

    KeyMessage k;
    k.root  = juce::jlimit (0, 11, (int) parsed.getProperty ("root", 0));
    k.scale = juce::jlimit (0, 2, (int) parsed.getProperty ("scale", 0));
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
//  SessionState (schemaVersion 3): the full Compass context. v3 adds an optional
//  project/session namespace; all musical fields beyond the
//  original root/scale/bpm/transientId are ADDITIVE — a key-only or v1 publisher
//  simply omits them and every reader falls back to the safe defaults below, so no
//  existing subscriber breaks. Numeric fields are RT-safe (they ride the SessionReader
//  atomics); `progression` is a message-only convenience (read off the audio thread).
struct SessionMessage
{
    int    schemaVersion = 3;
    juce::String sessionId;       // optional project namespace (message thread only)
    bool   hasSceneContext = false; // scene fields were explicitly published
    int    root = 0;           // 0..11
    int    scale = 0;          // 0=major, 1=minor, 2=chromatic
    double bpm = 0.0;          // host tempo, 0 if unknown
    juce::int64 transientId = 0;   // increments on each published onset
    bool   hasTempo = false;
    // --- SessionState v2 additions (all optional, safe defaults) ---------------
    int    chordRoot = -1;         // -1 = no chord published, else 0..11
    int    chordQuality = 0;       // 0=none,1=maj,2=min,3=dom7,4=min7,5=maj7,6=dim,7=aug,8=sus
    int    timeSigNum = 4;
    int    timeSigDen = 4;
    float  swing = 0.0f;           // 0..1 groove/swing amount
    int    sectionId = 0;          // 0=none,1=intro,2=verse,3=prechorus,4=chorus,5=bridge,6=outro
    float  energy = 0.0f;          // 0..1 arrangement energy
    int    maqamId = 0;            // 0=western/none, else a maqam/dastgah id
    double referenceHz = 440.0;    // reference tuning (A4)
    bool   dynamicKey = false;      // key may change while the session is running
    float  keyConfidence = 0.0f;    // publisher's gated margin; 0 means unspecified
    int    keyRevision = 0;         // increments only when the accepted key changes
    double keyWindowSeconds = 0.0;  // evidence window used for the accepted decision
    juce::String progression;      // e.g. "0,9,5,7" chord roots (message-only, not in the RT snapshot)
};

inline bool publishSession (const juce::String& publisherId, int root, int scale,
                            double bpm, juce::int64 transientId,
                            const juce::String& requestedSessionId = {},
                            const juce::String& ownerToken = {})
{
    juce::InterProcessLock::ScopedLockType publishLock (keyBusPublishLock());
    if (! publishLock.isLocked()) return false;
    const auto sessionId = effectiveSessionId (requestedSessionId);
    const auto file = keyBusFile (sessionId);
    if (! mayPublish (file, publisherId))
        return false;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", 3);
    obj->setProperty ("publisher", publisherId);
    if (ownerToken.isNotEmpty()) obj->setProperty ("ownerToken", ownerToken);
    if (sessionId.isNotEmpty()) obj->setProperty ("sessionId", sessionId);
    obj->setProperty ("priority", publisherPriority (publisherId));
    obj->setProperty ("root",  juce::jlimit (0, 11, root));
    obj->setProperty ("scale", juce::jlimit (0, 2, scale));
    obj->setProperty ("bpm",   bpm > 0.0 ? bpm : 0.0);
    obj->setProperty ("transientId", transientId);
    obj->setProperty ("timestampMs", currentTimeMs());
    return file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

// Full SessionState publisher (the Compass path). Writes every field; base-field-only
// readers ignore the extras, SessionState readers pick up whatever is present.
inline bool publishSessionState (const juce::String& publisherId, const SessionMessage& s,
                                 const juce::String& requestedSessionId = {},
                                 const juce::String& ownerToken = {})
{
    juce::InterProcessLock::ScopedLockType publishLock (keyBusPublishLock());
    if (! publishLock.isLocked()) return false;
    const auto sessionId = effectiveSessionId (
        requestedSessionId.isNotEmpty() ? requestedSessionId : s.sessionId);
    const auto file = keyBusFile (sessionId);
    if (! mayPublish (file, publisherId))
        return false;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", 3);
    obj->setProperty ("publisher", publisherId);
    if (ownerToken.isNotEmpty()) obj->setProperty ("ownerToken", ownerToken);
    if (sessionId.isNotEmpty()) obj->setProperty ("sessionId", sessionId);
    obj->setProperty ("priority", publisherPriority (publisherId));
    obj->setProperty ("root",  juce::jlimit (0, 11, s.root));
    obj->setProperty ("scale", juce::jlimit (0, 2, s.scale));
    obj->setProperty ("bpm",   s.bpm > 0.0 ? s.bpm : 0.0);
    obj->setProperty ("transientId", s.transientId);
    obj->setProperty ("chordRoot",    juce::jlimit (-1, 11, s.chordRoot));
    obj->setProperty ("chordQuality", s.chordQuality);
    obj->setProperty ("timeSigNum",   juce::jmax (1, s.timeSigNum));
    obj->setProperty ("timeSigDen",   juce::jmax (1, s.timeSigDen));
    obj->setProperty ("swing",        juce::jlimit (0.0f, 1.0f, s.swing));
    obj->setProperty ("sectionId",    s.sectionId);
    obj->setProperty ("energy",       juce::jlimit (0.0f, 1.0f, s.energy));
    obj->setProperty ("maqamId",      s.maqamId);
    obj->setProperty ("referenceHz",  s.referenceHz > 0.0 ? s.referenceHz : 440.0);
    obj->setProperty ("dynamicKey",   s.dynamicKey);
    obj->setProperty ("keyConfidence", juce::jlimit (0.0f, 1.0f, s.keyConfidence));
    obj->setProperty ("keyRevision",  juce::jmax (0, s.keyRevision));
    obj->setProperty ("keyWindowSeconds", juce::jmax (0.0, s.keyWindowSeconds));
    if (s.progression.isNotEmpty()) obj->setProperty ("progression", s.progression);
    obj->setProperty ("timestampMs", currentTimeMs());
    return file.replaceWithText (juce::JSON::toString (juce::var (obj)));
}

// Remove a session frame only when it still belongs to this publisher. This is
// used when a detector is explicitly disabled/unlocked, so followers stop now
// instead of consuming its otherwise-valid 30-second stale tail. A different
// authority's frame can never be removed by this call. Supplying an instance
// token also protects newer frames from another instance of the same product;
// callers without a token retain the legacy product-level ownership contract.
inline bool withdrawSessionState (const juce::String& publisherId,
                                  const juce::String& requestedSessionId = {},
                                  const juce::String& ownerToken = {})
{
    juce::InterProcessLock::ScopedLockType publishLock (keyBusPublishLock());
    if (! publishLock.isLocked()) return false;
    const auto file = keyBusFile (requestedSessionId);
    if (! file.existsAsFile()) return false;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()
        || parsed.getProperty ("publisher", juce::String()).toString() != publisherId
        || (ownerToken.isNotEmpty() && parsed.getProperty ("ownerToken", juce::String()).toString() != ownerToken))
        return false;
    return file.deleteFile();
}

inline std::optional<SessionMessage> readSession (
    int maxAgeSeconds = 30, const juce::String& requestedSessionId = {})
{
    auto file = keyBusFile (requestedSessionId);
    if (! file.existsAsFile()) return std::nullopt;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return std::nullopt;

    const auto ts = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const juce::int64 now = currentTimeMs();
    if (now - ts > (juce::int64) maxAgeSeconds * 1000)
        return std::nullopt;

    SessionMessage s;
    s.schemaVersion = (int) parsed.getProperty ("schemaVersion", 1);
    s.sessionId = parsed.getProperty ("sessionId", juce::String()).toString();
    s.hasSceneContext =
        parsed.hasProperty ("swing")
        || parsed.hasProperty ("sectionId")
        || parsed.hasProperty ("energy");
    s.root  = juce::jlimit (0, 11, (int) parsed.getProperty ("root", 0));
    s.scale = juce::jlimit (0, 2, (int) parsed.getProperty ("scale", 0));
    s.bpm   = (double) parsed.getProperty ("bpm", 0.0);
    s.transientId = (juce::int64) parsed.getProperty ("transientId", 0);
    s.hasTempo = s.bpm > 0.0;
    // v2 additions — absent in v1/key-only publications, so default safely.
    s.chordRoot    = juce::jlimit (-1, 11, (int) parsed.getProperty ("chordRoot", -1));
    s.chordQuality = (int) parsed.getProperty ("chordQuality", 0);
    s.timeSigNum   = juce::jmax (1, (int) parsed.getProperty ("timeSigNum", 4));
    s.timeSigDen   = juce::jmax (1, (int) parsed.getProperty ("timeSigDen", 4));
    s.swing        = juce::jlimit (0.0f, 1.0f, (float) (double) parsed.getProperty ("swing", 0.0));
    s.sectionId    = (int) parsed.getProperty ("sectionId", 0);
    s.energy       = juce::jlimit (0.0f, 1.0f, (float) (double) parsed.getProperty ("energy", 0.0));
    s.maqamId      = (int) parsed.getProperty ("maqamId", 0);
    s.referenceHz  = (double) parsed.getProperty ("referenceHz", 440.0);
    s.dynamicKey   = (bool) parsed.getProperty ("dynamicKey", false);
    s.keyConfidence = juce::jlimit (0.0f, 1.0f,
        (float) (double) parsed.getProperty ("keyConfidence", 0.0));
    s.keyRevision = juce::jmax (0, (int) parsed.getProperty ("keyRevision", 0));
    s.keyWindowSeconds = juce::jmax (0.0,
        (double) parsed.getProperty ("keyWindowSeconds", 0.0));
    s.progression  = parsed.getProperty ("progression", juce::String()).toString();
    return s;
}

struct SessionSnapshot
{
    bool        valid = false;
    bool        hasSceneContext = false;
    int         root = 0, scale = 0;
    double      bpm = 0.0;
    juce::int64 transientId = 0;
    bool        hasTempo = false;
    int         chordRoot = -1, chordQuality = 0;
    int         timeSigNum = 4, timeSigDen = 4;
    float       swing = 0.0f;
    int         sectionId = 0;
    float       energy = 0.0f;
    int         maqamId = 0;
    double      referenceHz = 440.0;
    bool        dynamicKey = false;
    float       keyConfidence = 0.0f;
    int         keyRevision = 0;
    double      keyWindowSeconds = 0.0;
};

namespace detail
{
class SharedSessionPoller final : private juce::Thread
{
public:
    SharedSessionPoller() : juce::Thread ("SattariSessionBus") {}
    ~SharedSessionPoller() override { stopThread (1000); }

    static SharedSessionPoller& instance()
    {
        static SharedSessionPoller poller;
        return poller;
    }

    void acquire (int requestedIntervalMs)
    {
        const std::lock_guard<std::mutex> guard (clientsMutex);
        const int requested = juce::jmax (50, requestedIntervalMs);
        intervalMs.store (clients == 0 ? requested
                                       : juce::jmin (intervalMs.load(), requested));
        ++clients;
        if (! isThreadRunning())
            startThread();
        notify();
    }

    void release()
    {
        const std::lock_guard<std::mutex> guard (clientsMutex);
        clients = juce::jmax (0, clients - 1);
        if (clients == 0)
        {
            stopThread (1000);
            frame.store ({});
            intervalMs.store (400);
        }
    }

    SessionSnapshot current() const noexcept { return frame.load(); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (auto m = readSession())
            {
                SessionSnapshot fresh;
                fresh.valid = true;
                fresh.hasSceneContext = m->hasSceneContext;
                fresh.root = m->root;
                fresh.scale = m->scale;
                fresh.bpm = m->bpm;
                fresh.transientId = m->transientId;
                fresh.hasTempo = m->hasTempo;
                fresh.chordRoot = m->chordRoot;
                fresh.chordQuality = m->chordQuality;
                fresh.timeSigNum = m->timeSigNum;
                fresh.timeSigDen = m->timeSigDen;
                fresh.swing = m->swing;
                fresh.sectionId = m->sectionId;
                fresh.energy = m->energy;
                fresh.maqamId = m->maqamId;
                fresh.referenceHz = m->referenceHz;
                fresh.dynamicKey = m->dynamicKey;
                fresh.keyConfidence = m->keyConfidence;
                fresh.keyRevision = m->keyRevision;
                fresh.keyWindowSeconds = m->keyWindowSeconds;
                frame.store (fresh);
            }
            else
                frame.store ({});
            wait (intervalMs.load()); // wakes early when stopThread() is called
        }
    }

    mutable std::mutex clientsMutex;
    int clients = 0;
    std::atomic<int> intervalMs { 400 };
    sattari::rt::AtomicSnapshot<SessionSnapshot> frame;
};
} // namespace detail

// --- SessionReader: realtime-safe consumer side ------------------------------
// All instances in one plugin binary share one low-priority file poller. Each
// audio thread reads a coherent lock-free snapshot; it never performs file I/O.
class SessionReader
{
public:
    using Snapshot = SessionSnapshot;
    SessionReader() = default;
    ~SessionReader() { stop(); }

    void start (int pollIntervalMs = 400)
    {
        if (started) return;
        detail::SharedSessionPoller::instance().acquire (pollIntervalMs);
        started = true;
    }

    void stop()
    {
        if (! started) return;
        detail::SharedSessionPoller::instance().release();
        started = false;
    }

    Snapshot current() const noexcept
    {
        return detail::SharedSessionPoller::instance().current();
    }

private:
    bool started = false;
    JUCE_DECLARE_NON_COPYABLE (SessionReader)
};

// =============================================================================
//  Tempo channel — a SEPARATE file from the key bus
// -----------------------------------------------------------------------------
//  Tempo + transient pulses live in their own file with their own timestamp, so
//  a tempo publisher never clobbers the key (and a key that keeps republishing
//  never keeps a dead tempo alive). A rhythmic plugin (Rhythm Genie) publishes
//  the session tempo; a transient detector (Side Chain) can bump the pulse id.
//  Consumers (Arp, Space) read it as a fallback when the host reports no tempo.
// =============================================================================
inline juce::File tempoBusFile (const juce::String& requestedSessionId = {})
{
    const auto sessionId = effectiveSessionId (requestedSessionId);
    const auto suffix = sessionId.isEmpty() ? juce::String() : "-" + sessionId;
    return crossPluginBusRoot().getChildFile ("tempo-bus" + suffix + ".json");
}

struct TempoMessage { double bpm = 0.0; juce::int64 transientId = 0; bool hasTempo = false; };

inline void publishTempo (const juce::String& publisherId, double bpm,
                          juce::int64 transientId,
                          const juce::String& requestedSessionId = {})
{
    const auto sessionId = effectiveSessionId (requestedSessionId);
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", 3);
    obj->setProperty ("publisher", publisherId);
    if (sessionId.isNotEmpty()) obj->setProperty ("sessionId", sessionId);
    obj->setProperty ("bpm", bpm > 0.0 ? bpm : 0.0);
    obj->setProperty ("transientId", transientId);
    obj->setProperty ("timestampMs",
        (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::system_clock::now().time_since_epoch()).count());
    tempoBusFile (sessionId).replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline std::optional<TempoMessage> readTempo (
    int maxAgeSeconds = 10, const juce::String& requestedSessionId = {})
{
    auto file = tempoBusFile (requestedSessionId);
    if (! file.existsAsFile()) return std::nullopt;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return std::nullopt;
    const auto ts = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const juce::int64 now = currentTimeMs();
    if (now - ts > (juce::int64) maxAgeSeconds * 1000) return std::nullopt;
    TempoMessage t;
    t.bpm = (double) parsed.getProperty ("bpm", 0.0);
    t.transientId = (juce::int64) parsed.getProperty ("transientId", 0);
    t.hasTempo = t.bpm > 0.0;
    return t;
}

struct TempoSnapshot
{
    bool valid = false;
    double bpm = 0.0;
    juce::int64 transientId = 0;
};

namespace detail
{
class SharedTempoPoller final : private juce::Thread
{
public:
    SharedTempoPoller() : juce::Thread ("SattariTempoBus") {}
    ~SharedTempoPoller() override { stopThread (1000); }

    static SharedTempoPoller& instance()
    {
        static SharedTempoPoller poller;
        return poller;
    }

    void acquire (int requestedIntervalMs)
    {
        const std::lock_guard<std::mutex> guard (clientsMutex);
        const int requested = juce::jmax (50, requestedIntervalMs);
        intervalMs.store (clients == 0 ? requested
                                       : juce::jmin (intervalMs.load(), requested));
        ++clients;
        if (! isThreadRunning())
            startThread();
        notify();
    }

    void release()
    {
        const std::lock_guard<std::mutex> guard (clientsMutex);
        clients = juce::jmax (0, clients - 1);
        if (clients == 0)
        {
            stopThread (1000);
            frame.store ({});
            intervalMs.store (300);
        }
    }

    TempoSnapshot current() const noexcept { return frame.load(); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (auto m = readTempo())
            {
                frame.store ({ m->hasTempo, m->bpm, m->transientId });
            }
            else frame.store ({});
            wait (intervalMs.load());
        }
    }

    mutable std::mutex clientsMutex;
    int clients = 0;
    std::atomic<int> intervalMs { 300 };
    sattari::rt::AtomicSnapshot<TempoSnapshot> frame;
};
} // namespace detail

class TempoReader
{
public:
    using Snapshot = TempoSnapshot;
    TempoReader() = default;
    ~TempoReader() { stop(); }

    void start (int pollIntervalMs = 300)
    {
        if (started) return;
        detail::SharedTempoPoller::instance().acquire (pollIntervalMs);
        started = true;
    }

    void stop()
    {
        if (! started) return;
        detail::SharedTempoPoller::instance().release();
        started = false;
    }

    Snapshot current() const noexcept
    {
        return detail::SharedTempoPoller::instance().current();
    }

private:
    bool started = false;
    JUCE_DECLARE_NON_COPYABLE (TempoReader)
};

} // namespace sattari::xplugin
