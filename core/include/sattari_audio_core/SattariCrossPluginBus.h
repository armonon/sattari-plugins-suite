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
//  SessionState (schemaVersion 2): the full "Brain" context. All fields beyond the
//  original root/scale/bpm/transientId are ADDITIVE — a key-only or v1 publisher
//  simply omits them and every reader falls back to the safe defaults below, so no
//  existing subscriber breaks. Numeric fields are RT-safe (they ride the SessionReader
//  atomics); `progression` is a message-only convenience (read off the audio thread).
struct SessionMessage
{
    int    schemaVersion = 2;
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
    juce::String progression;      // e.g. "0,9,5,7" chord roots (message-only, not in the RT snapshot)
};

inline void publishSession (const juce::String& publisherId, int root, int scale,
                            double bpm, juce::int64 transientId)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", 2);
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

// Full SessionState publisher (the Brain path). Writes every field; base-field-only
// readers ignore the extras, SessionState readers pick up whatever is present.
inline void publishSessionState (const juce::String& publisherId, const SessionMessage& s)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", 2);
    obj->setProperty ("publisher", publisherId);
    obj->setProperty ("root",  juce::jlimit (0, 11, s.root));
    obj->setProperty ("scale", juce::jlimit (0, 11, s.scale));
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
    if (s.progression.isNotEmpty()) obj->setProperty ("progression", s.progression);
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
    s.schemaVersion = (int) parsed.getProperty ("schemaVersion", 1);
    s.root  = juce::jlimit (0, 11, (int) parsed.getProperty ("root", 0));
    s.scale = juce::jlimit (0, 11, (int) parsed.getProperty ("scale", 0));
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
    s.progression  = parsed.getProperty ("progression", juce::String()).toString();
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
        // SessionState v2 (safe defaults when the publisher is key-only)
        int         chordRoot = -1, chordQuality = 0;
        int         timeSigNum = 4, timeSigDen = 4;
        float       swing = 0.0f;
        int         sectionId = 0;
        float       energy = 0.0f;
        int         maqamId = 0;
        double      referenceHz = 440.0;
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
        s.chordRoot    = chordRoot.load (std::memory_order_relaxed);
        s.chordQuality = chordQuality.load (std::memory_order_relaxed);
        s.timeSigNum   = timeSigNum.load (std::memory_order_relaxed);
        s.timeSigDen   = timeSigDen.load (std::memory_order_relaxed);
        s.swing        = swing.load (std::memory_order_relaxed);
        s.sectionId    = sectionId.load (std::memory_order_relaxed);
        s.energy       = energy.load (std::memory_order_relaxed);
        s.maqamId      = maqamId.load (std::memory_order_relaxed);
        s.referenceHz  = referenceHz.load (std::memory_order_relaxed);
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
                chordRoot.store (m->chordRoot, std::memory_order_relaxed);
                chordQuality.store (m->chordQuality, std::memory_order_relaxed);
                timeSigNum.store (m->timeSigNum, std::memory_order_relaxed);
                timeSigDen.store (m->timeSigDen, std::memory_order_relaxed);
                swing.store (m->swing, std::memory_order_relaxed);
                sectionId.store (m->sectionId, std::memory_order_relaxed);
                energy.store (m->energy, std::memory_order_relaxed);
                maqamId.store (m->maqamId, std::memory_order_relaxed);
                referenceHz.store (m->referenceHz, std::memory_order_relaxed);
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
    // SessionState v2 atomics
    std::atomic<int>         chordRoot { -1 }, chordQuality { 0 };
    std::atomic<int>         timeSigNum { 4 }, timeSigDen { 4 };
    std::atomic<float>       swing { 0.0f }, energy { 0.0f };
    std::atomic<int>         sectionId { 0 }, maqamId { 0 };
    std::atomic<double>      referenceHz { 440.0 };
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
inline juce::File tempoBusFile()
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Sattari Audio").getChildFile ("cross-plugin");
    root.createDirectory();
    return root.getChildFile ("tempo-bus.json");
}

struct TempoMessage { double bpm = 0.0; juce::int64 transientId = 0; bool hasTempo = false; };

inline void publishTempo (const juce::String& publisherId, double bpm, juce::int64 transientId)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("publisher", publisherId);
    obj->setProperty ("bpm", bpm > 0.0 ? bpm : 0.0);
    obj->setProperty ("transientId", transientId);
    obj->setProperty ("timestampMs",
        (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::system_clock::now().time_since_epoch()).count());
    tempoBusFile().replaceWithText (juce::JSON::toString (juce::var (obj)));
}

inline std::optional<TempoMessage> readTempo (int maxAgeSeconds = 10)
{
    auto file = tempoBusFile();
    if (! file.existsAsFile()) return std::nullopt;
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return std::nullopt;
    const auto ts = (juce::int64) parsed.getProperty ("timestampMs", 0);
    const juce::int64 now = std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now - ts > (juce::int64) maxAgeSeconds * 1000) return std::nullopt;
    TempoMessage t;
    t.bpm = (double) parsed.getProperty ("bpm", 0.0);
    t.transientId = (juce::int64) parsed.getProperty ("transientId", 0);
    t.hasTempo = t.bpm > 0.0;
    return t;
}

// Realtime-safe consumer: background thread polls the tempo file into atomics.
class TempoReader : private juce::Thread
{
public:
    TempoReader() : juce::Thread ("SattariTempoBus") {}
    ~TempoReader() override { stopThread (1000); }
    void start (int pollIntervalMs = 300) { intervalMs = juce::jmax (50, pollIntervalMs); if (! isThreadRunning()) startThread(); }
    void stop() { stopThread (1000); }

    struct Snapshot { bool valid = false; double bpm = 0.0; juce::int64 transientId = 0; };
    Snapshot current() const noexcept
    {
        Snapshot s;
        s.valid = hasValue.load (std::memory_order_relaxed);
        s.bpm   = bpm.load (std::memory_order_relaxed);
        s.transientId = tid.load (std::memory_order_relaxed);
        return s;
    }
private:
    void run() override
    {
        while (! threadShouldExit())
        {
            if (auto m = readTempo())
            {
                bpm.store (m->bpm, std::memory_order_relaxed);
                tid.store (m->transientId, std::memory_order_relaxed);
                hasValue.store (m->hasTempo, std::memory_order_relaxed);
            }
            else hasValue.store (false, std::memory_order_relaxed);
            wait (intervalMs);
        }
    }
    int intervalMs = 300;
    std::atomic<double>      bpm { 0.0 };
    std::atomic<juce::int64> tid { 0 };
    std::atomic<bool>        hasValue { false };
};

} // namespace sattari::xplugin
