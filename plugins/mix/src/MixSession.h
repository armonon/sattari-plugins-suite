#pragma once

#include "MixPlanner.h"

namespace sattari::automix
{
inline juce::var object() { return juce::var (new juce::DynamicObject()); }
inline void put (juce::var& value, const char* key, const juce::var& item) { value.getDynamicObject()->setProperty (key, item); }
inline juce::var get (const juce::var& value, const char* key) { return value.getProperty (key, {}); }
inline float number (const juce::var& v, const char* key, float fallback, float lo, float hi)
{
    const auto field = get (v, key);
    if (! field.isDouble() && ! field.isInt() && ! field.isInt64()) return fallback;
    const double n = (double) field;
    return std::isfinite (n) ? (float) juce::jlimit ((double) lo, (double) hi, n) : fallback;
}

inline juce::var encode (const Settings& s)
{
    auto v = object();
    put (v, "gain", s.gain); put (v, "low", s.low); put (v, "presence", s.presence);
    put (v, "compression", s.compression); put (v, "threshold", s.threshold);
    put (v, "amount", s.amount); put (v, "trim", s.trim); put (v, "enabled", s.enabled);
    return v;
}

inline Settings decodeSettings (const juce::var& v)
{
    Settings s;
    s.gain = number (v, "gain", 0, -36, 12); s.low = number (v, "low", 0, -6, 0);
    s.presence = number (v, "presence", 0, -6, 0);
    s.compression = number (v, "compression", 0, 0, 1);
    s.threshold = number (v, "threshold", -18, -48, -6);
    s.amount = number (v, "amount", 1, 0, 1); s.trim = number (v, "trim", 0, -36, 0);
    s.enabled = (bool) get (v, "enabled");
    return s;
}

inline juce::var encode (const Track& t)
{
    auto v = object();
    put (v, "schema", 1); put (v, "id", t.id); put (v, "name", t.name);
    put (v, "capture", t.capture); put (v, "ack", t.ack); put (v, "error", t.error);
    put (v, "role", (int) t.role); put (v, "locked", t.locked); put (v, "settings", encode (t.settings));
    put (v, "isBus", t.isBus);
    const auto& m = t.measured;
    put (v, "loudness", m.loudness); put (v, "peak", m.peakDb); put (v, "low", m.low);
    put (v, "presence", m.presence); put (v, "crest", m.crest);
    put (v, "active", m.activeSeconds); put (v, "seconds", m.seconds);
    put (v, "progress", m.progress); put (v, "complete", m.complete); put (v, "invalid", m.invalid);
    put (v, "time", juce::Time::currentTimeMillis());
    return v;
}

inline Track decodeTrack (const juce::var& v)
{
    Track t;
    t.id = get (v, "id").toString(); t.name = get (v, "name").toString().substring (0, 80);
    t.capture = get (v, "capture").toString(); t.ack = get (v, "ack").toString();
    t.error = get (v, "error").toString();
    t.role = (Role) (int) number (v, "role", 0, 0, 6); t.locked = (bool) get (v, "locked");
    t.isBus = (bool) get (v, "isBus");
    t.settings = decodeSettings (get (v, "settings"));
    auto& m = t.measured;
    m.loudness = number (v, "loudness", -100, -100, 40); m.peakDb = number (v, "peak", -100, -100, 40);
    m.low = number (v, "low", 0, 0, 1); m.presence = number (v, "presence", 0, 0, 1);
    m.crest = number (v, "crest", 0, 0, 40); m.activeSeconds = number (v, "active", 0, 0, 30);
    m.seconds = number (v, "seconds", 0, 0, 30); m.progress = number (v, "progress", 0, 0, 1);
    m.complete = (bool) get (v, "complete"); m.invalid = (bool) get (v, "invalid");
    return t;
}

// Low-rate control and frozen analysis only. Never called by processBlock.
// One file per runtime instance also works across AU/VST3 host processes.
class Session
{
public:
    explicit Session (juce::File directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("SattariAutoMix-v1")) : root (std::move (directory)) {}
    ~Session() { leave(); }

    bool join (const juce::String& code)
    {
        leave();
        if (code.trim().isEmpty()) return true;
        // Hashing prevents path traversal and keeps group names out of filenames.
        folder = root.getChildFile (juce::SHA256 (code.trim().toRawUTF8(),
                                                code.trim().getNumBytesAsUTF8()).toHexString());
        return folder.createDirectory().wasOk();
    }
    void leave()
    {
        if (linked())
        {
            folder.getChildFile (id + ".track").deleteFile();
            folder.getChildFile (id + ".command").deleteFile();
        }
        folder = juce::File();
    }
    bool linked() const { return folder != juce::File(); }
    const juce::String id { juce::Uuid().toDashedString() };

    bool publish (const Track& track) const
    { return linked() && write (folder.getChildFile (id + ".track"), encode (track)); }

    bool isAlive (const juce::String& peer) const
    {
        if (! linked() || ! safeId (peer)) return false;
        const auto v = read (folder.getChildFile (peer + ".track"));
        const auto age = juce::Time::currentTimeMillis() - (juce::int64) get (v, "time");
        return (int) get (v, "schema") == 1 && get (v, "id").toString() == peer && age >= -1000 && age <= 2500;
    }

    std::vector<Track> tracks() const
    {
        std::vector<Track> result;
        if (! linked()) return result;
        const auto files = folder.findChildFiles (juce::File::findFiles, false, "*.track");
        const auto now = juce::Time::currentTimeMillis();
        for (const auto& f : files)
        {
            const auto v = read (f);
            const auto age = now - (juce::int64) get (v, "time");
            if ((int) get (v, "schema") != 1 || age < -1000 || age > 2500) continue;
            auto t = decodeTrack (v);
            if (t.id != f.getFileNameWithoutExtension() || ! safeId (t.id)) continue;
            result.push_back (std::move (t));
            if (result.size() > 64) break;
        }
        std::sort (result.begin(), result.end(), [] (const Track& a, const Track& b)
        { return a.name == b.name ? a.id < b.id : a.name < b.name; });
        return result;
    }

    bool send (const juce::String& target, juce::var command) const
    {
        if (! linked() || ! safeId (target)) return false;
        put (command, "schema", 1); put (command, "target", target);
        put (command, "sender", id); put (command, "time", juce::Time::currentTimeMillis());
        return write (folder.getChildFile (target + ".command"), command);
    }

    juce::var receive() const
    {
        if (! linked()) return {};
        auto command = read (folder.getChildFile (id + ".command"));
        const auto age = juce::Time::currentTimeMillis() - (juce::int64) get (command, "time");
        if ((int) get (command, "schema") != 1 || get (command, "target").toString() != id
            || age < -1000 || age > 15000) return {};
        return command;
    }

private:
    static bool safeId (const juce::String& s)
    { return s.length() == 36 && s.containsOnly ("0123456789abcdef-"); }
    static bool write (const juce::File& file, const juce::var& value)
    {
        juce::TemporaryFile temp (file);
        return temp.getFile().replaceWithText (juce::JSON::toString (value, true))
            && temp.overwriteTargetFileWithTemporary();
    }
    static juce::var read (const juce::File& file)
    { return file.getSize() > 0 && file.getSize() < 32768 ? juce::JSON::parse (file.loadFileAsString()) : juce::var(); }
    juce::File root, folder;
};
} // namespace sattari::automix
