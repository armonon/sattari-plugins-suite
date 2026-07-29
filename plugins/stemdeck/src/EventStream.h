#pragma once

// EventStream — schema v1, implementing docs/event-stream-schema.md (ACCEPTED).
//
// The live surface writes this stream; the arrangement editor is a deterministic
// replay of it. It is the project's one-way door, so the decisions it encodes are
// spelled out here rather than left to the reader:
//
//   * CAPTURE INTENT, NOT AUDIO. An event records what the user did, never the
//     resulting samples. This is what makes "perform your mix, then produce it"
//     possible — the performance arrives in the editor as editable automation.
//   * EXACT VALUES. Numeric payloads are binary-exact doubles/ints, never formatted
//     text. The old PerformanceEvent stored "0.7350" as a String; a float that
//     round-trips through four decimal places cannot reproduce sample-identically.
//   * STABLE IDENTITY. Targets are addressed by opaque id, never list position.
//     Once channels are addable and reorderable, "deck 2" silently means a different
//     channel after any mutation.
//   * TOTAL ORDER. (atSample, seq) orders the stream completely, so several events in
//     one block replay in a defined sequence.
//   * CLOSED VOCABULARY. EventType and ParamId are enums. An unknown value on load
//     means the file was written by a newer build: REFUSE it, never guess.
//
// Records are fixed-width little-endian so the stream is seekable and mappable.
// Appending to the enums is safe; renumbering or reusing a value is a breaking change
// that needs a schemaVersion bump and a migration (schema §10).

#include <juce_core/juce_core.h>
#include <cstdint>
#include <vector>

namespace stemdeck::events
{

// ── Identity (schema §3) ─────────────────────────────────────────────────────
using ChannelId  = std::int64_t;   // stable for the channel's lifetime; never reused
using InsertId   = std::int64_t;   // stable per FX insert instance
using TrackRefId = std::int64_t;   // entry in the session's source-material pool

enum class ScopeKind : std::uint8_t { master = 0, channel = 1, stem = 2, insert = 3 };

// Stem lanes as they appear in a Scope. Matches DeckPlayer's lane numbering so the
// two never need translating.
enum class StemLane : std::uint8_t { none = 0, drums = 1, bass = 2, music = 3, vocals = 4 };

struct Scope
{
    ScopeKind kind     = ScopeKind::master;
    StemLane  stemLane = StemLane::none;   // stem scope only
    ChannelId channel  = 0;                // channel / stem / insert scopes
    InsertId  insert   = 0;                // insert scope only

    bool operator== (const Scope& o) const noexcept
    { return kind == o.kind && stemLane == o.stemLane && channel == o.channel && insert == o.insert; }
};

// ── Event types (schema §4.1) — APPEND ONLY ──────────────────────────────────
enum class EventType : std::uint8_t
{
    param = 0,
    transport,
    channelAdd,
    channelRemove,
    channelReorder,
    trackLoad,
    trackClear,
    insertAdd,
    insertRemove,
    insertReorder,
    marker,
    tempoPoint,

    count   // not a value; bounds-check only
};

// ── Parameters (schema §4.3) — APPEND ONLY ───────────────────────────────────
enum class ParamId : std::uint16_t
{
    gainStem = 0, stemMute, stemSolo,
    channelGain, channelMute, channelSolo,
    eqLow, eqMid, eqHigh, filter,
    pitchSemitones, keyTarget, keyLockOn,
    bpmSource, tempoInterp, syncMode,
    crossfader, crossfaderCurve, cfSide,
    insertParam, insertBypass,
    inputGain, inputMonitor, inputSource,
    instGain, instMidiChannel,
    masterGain, limiterCeiling, masterKey,

    count
};

enum class TransportAction : std::uint8_t
{
    play = 0, stop, cue, cueRelease, seek, loopOn, loopOff, beatJump,
    count
};

// Continuous-control gesture phase (schema §5). `end` is NEVER coalesced away: the
// endpoint is the value the user actually released on, and dropping it leaves the mix
// at a nearby value so replay diverges slowly instead of obviously.
enum class GestureFlag : std::uint8_t { none = 0, begin = 1, end = 2 };

enum class ChannelKind : std::uint8_t
{ trackDeck = 0, input, midiInstrument, sampler, pluginHost, count };

// ── Payloads (schema §4.2) ───────────────────────────────────────────────────
// One fixed-width union so records stay seekable. Every field is explicitly sized.
struct ParamPayload
{
    ParamId     id      = ParamId::channelGain;
    GestureFlag gesture = GestureFlag::none;
    double      value   = 0.0;    // EXACT — never a formatted string
    std::uint32_t index = 0;      // insertParam: insert position (hi16) + fallback parameter index (lo16)
    std::uint64_t stableId = 0;   // insertParam: hash of the plugin's persistent parameter ID
};

struct TransportPayload
{
    TransportAction action        = TransportAction::play;
    double          positionBeats = 0.0;
    double          lengthBeats   = 0.0;   // loop length / jump distance; 0 when unused
};

struct ChannelAddPayload    { ChannelId id = 0; ChannelKind kind = ChannelKind::trackDeck; std::uint8_t slotIndex = 0; };
struct ChannelRemovePayload { ChannelId id = 0; };
struct ChannelReorderPayload{ ChannelId id = 0; std::uint8_t fromPos = 0, toPos = 0; };
struct TrackLoadPayload
{
    TrackRefId track = 0;
    StemLane   stemLane = StemLane::none;
    // Schema §6.2 — the reason A1 can pass at all. Replay must use the SAME analysis
    // the live pass used; neural refinement lands asynchronously (measured 0.6-5s), so
    // recomputing it makes replay diverge intermittently. The hash identifies the
    // material, the version identifies which analysis of it was in force.
    std::uint64_t contentHash    = 0;
    std::uint32_t analysisVersion = 0;
};
struct InsertAddPayload     { InsertId id = 0; std::uint32_t pluginUid = 0; std::uint8_t position = 0; };
struct InsertRemovePayload  { InsertId id = 0; };
struct InsertReorderPayload { InsertId id = 0; std::uint8_t fromPos = 0, toPos = 0; };
struct MarkerPayload        { std::uint32_t index = 0; double atBeat = 0.0; };
struct TempoPointPayload    { double atBeat = 0.0; double bpm = 0.0; };

struct Event
{
    std::int64_t  atSample = 0;   // master clock, monotonic
    std::uint32_t seq      = 0;   // ties within a sample; strictly increasing, global
    EventType     type     = EventType::param;
    Scope         scope;

    union
    {
        ParamPayload          param;
        TransportPayload      transport;
        ChannelAddPayload     channelAdd;
        ChannelRemovePayload  channelRemove;
        ChannelReorderPayload channelReorder;
        TrackLoadPayload      trackLoad;
        InsertAddPayload      insertAdd;
        InsertRemovePayload   insertRemove;
        InsertReorderPayload  insertReorder;
        MarkerPayload         marker;
        TempoPointPayload     tempoPoint;
    };

    Event() : param() {}

    // (atSample, seq) is a TOTAL order — replay must never depend on insertion order.
    bool operator< (const Event& o) const noexcept
    { return atSample != o.atSample ? atSample < o.atSample : seq < o.seq; }
};

// ── Storage (schema §7) ──────────────────────────────────────────────────────
inline constexpr std::uint32_t kMagic         = 0x56454453;   // "SDEV" little-endian
inline constexpr std::uint16_t kSchemaVersion = 1;

struct Header
{
    std::uint32_t magic         = kMagic;
    std::uint16_t schemaVersion = kSchemaVersion;
    std::uint16_t reserved      = 0;
    double        sampleRate    = 48000.0;
    std::int64_t  createdUtc    = 0;    // ms since epoch; caller supplies (no wall-clock here)
    std::uint32_t blockSize     = 512;  // Q4: identity is only guaranteed at THIS size
    std::uint32_t eventCount    = 0;
};

enum class LoadResult
{
    ok = 0,
    fileNotFound,
    tooSmall,
    badMagic,
    newerSchema,      // written by a newer build — REFUSED, never guessed at
    unknownEnumValue, // closed vocabulary violated
    truncated
};

inline juce::String describe (LoadResult r)
{
    switch (r)
    {
        case LoadResult::ok:               return "ok";
        case LoadResult::fileNotFound:     return "file not found";
        case LoadResult::tooSmall:         return "file smaller than the header";
        case LoadResult::badMagic:         return "not a StemDeck event stream";
        case LoadResult::newerSchema:      return "written by a newer schema version - refusing rather than guessing";
        case LoadResult::unknownEnumValue: return "unknown enum value - closed vocabulary violated";
        case LoadResult::truncated:        return "event data truncated";
    }
    return "unknown";
}

// ── Writer ───────────────────────────────────────────────────────────────────
// Little-endian explicitly, so a stream written on one machine reads on another.
class Writer
{
public:
    static bool write (const juce::File& dest, const Header& headerIn, const std::vector<Event>& events)
    {
        dest.deleteFile();
        auto os = std::unique_ptr<juce::FileOutputStream> (dest.createOutputStream());
        if (os == nullptr || os->failedToOpen())
            return false;

        Header h = headerIn;
        h.magic         = kMagic;
        h.schemaVersion = kSchemaVersion;
        h.eventCount    = (std::uint32_t) events.size();

        os->writeInt ((int) h.magic);
        os->writeShort ((short) h.schemaVersion);
        os->writeShort ((short) h.reserved);
        os->writeDouble (h.sampleRate);
        os->writeInt64 (h.createdUtc);
        os->writeInt ((int) h.blockSize);
        os->writeInt ((int) h.eventCount);

        for (const auto& e : events)
            writeEvent (*os, e);

        os->flush();
        return ! os->failedToOpen();
    }

private:
    static void writeScope (juce::OutputStream& os, const Scope& s)
    {
        os.writeByte ((char) s.kind);
        os.writeByte ((char) s.stemLane);
        os.writeInt64 (s.channel);
        os.writeInt64 (s.insert);
    }

    static void writeEvent (juce::OutputStream& os, const Event& e)
    {
        os.writeInt64 (e.atSample);
        os.writeInt ((int) e.seq);
        os.writeByte ((char) e.type);
        writeScope (os, e.scope);

        // Fixed-width payload region so records stay seekable: every branch writes
        // exactly kPayloadBytes.
        juce::MemoryOutputStream p;
        switch (e.type)
        {
            case EventType::param:
                p.writeShort ((short) e.param.id);
                p.writeByte ((char) e.param.gesture);
                p.writeDouble (e.param.value);
                p.writeInt ((int) e.param.index);
                p.writeInt64 ((std::int64_t) e.param.stableId);
                break;
            case EventType::transport:
                p.writeByte ((char) e.transport.action);
                p.writeDouble (e.transport.positionBeats);
                p.writeDouble (e.transport.lengthBeats);
                break;
            case EventType::channelAdd:
                p.writeInt64 (e.channelAdd.id);
                p.writeByte ((char) e.channelAdd.kind);
                p.writeByte ((char) e.channelAdd.slotIndex);
                break;
            case EventType::channelRemove: p.writeInt64 (e.channelRemove.id); break;
            case EventType::channelReorder:
                p.writeInt64 (e.channelReorder.id);
                p.writeByte ((char) e.channelReorder.fromPos);
                p.writeByte ((char) e.channelReorder.toPos);
                break;
            case EventType::trackLoad:
                p.writeInt64 (e.trackLoad.track);
                p.writeByte ((char) e.trackLoad.stemLane);
                p.writeInt64 ((std::int64_t) e.trackLoad.contentHash);
                p.writeInt ((int) e.trackLoad.analysisVersion);
                break;
            case EventType::trackClear: break;
            case EventType::insertAdd:
                p.writeInt64 (e.insertAdd.id);
                p.writeInt ((int) e.insertAdd.pluginUid);
                p.writeByte ((char) e.insertAdd.position);
                break;
            case EventType::insertRemove: p.writeInt64 (e.insertRemove.id); break;
            case EventType::insertReorder:
                p.writeInt64 (e.insertReorder.id);
                p.writeByte ((char) e.insertReorder.fromPos);
                p.writeByte ((char) e.insertReorder.toPos);
                break;
            case EventType::marker:
                p.writeInt ((int) e.marker.index);
                p.writeDouble (e.marker.atBeat);
                break;
            case EventType::tempoPoint:
                p.writeDouble (e.tempoPoint.atBeat);
                p.writeDouble (e.tempoPoint.bpm);
                break;
            case EventType::count: break;
        }

        os.write (p.getData(), p.getDataSize());
        for (size_t i = p.getDataSize(); i < kPayloadBytes; ++i)
            os.writeByte (0);
    }

public:
    static constexpr size_t kPayloadBytes = 24;   // widest branch (trackLoad) + slack
};

// ── Reader ───────────────────────────────────────────────────────────────────
class Reader
{
public:
    static LoadResult read (const juce::File& src, Header& headerOut, std::vector<Event>& out)
    {
        out.clear();
        if (! src.existsAsFile())
            return LoadResult::fileNotFound;

        juce::FileInputStream is (src);
        if (! is.openedOk())
            return LoadResult::fileNotFound;
        if (is.getTotalLength() < 32)
            return LoadResult::tooSmall;

        headerOut.magic         = (std::uint32_t) is.readInt();
        headerOut.schemaVersion = (std::uint16_t) is.readShort();
        headerOut.reserved      = (std::uint16_t) is.readShort();
        headerOut.sampleRate    = is.readDouble();
        headerOut.createdUtc    = is.readInt64();
        headerOut.blockSize     = (std::uint32_t) is.readInt();
        headerOut.eventCount    = (std::uint32_t) is.readInt();

        if (headerOut.magic != kMagic)
            return LoadResult::badMagic;

        // Refuse a newer stream outright. Guessing at fields we do not understand is
        // how a "mostly working" replay silently diverges.
        if (headerOut.schemaVersion > kSchemaVersion)
            return LoadResult::newerSchema;

        out.reserve (headerOut.eventCount);
        for (std::uint32_t i = 0; i < headerOut.eventCount; ++i)
        {
            if (is.isExhausted())
                return LoadResult::truncated;

            Event e;
            e.atSample = is.readInt64();
            e.seq      = (std::uint32_t) is.readInt();

            const auto rawType = (std::uint8_t) is.readByte();
            if (rawType >= (std::uint8_t) EventType::count)
                return LoadResult::unknownEnumValue;
            e.type = (EventType) rawType;

            const auto rawKind = (std::uint8_t) is.readByte();
            const auto rawLane = (std::uint8_t) is.readByte();
            if (rawKind > (std::uint8_t) ScopeKind::insert || rawLane > (std::uint8_t) StemLane::vocals)
                return LoadResult::unknownEnumValue;
            e.scope.kind     = (ScopeKind) rawKind;
            e.scope.stemLane = (StemLane) rawLane;
            e.scope.channel  = is.readInt64();
            e.scope.insert   = is.readInt64();

            juce::MemoryBlock payload;
            if (is.readIntoMemoryBlock (payload, (ssize_t) Writer::kPayloadBytes) < (int) Writer::kPayloadBytes)
                return LoadResult::truncated;
            juce::MemoryInputStream p (payload, false);

            switch (e.type)
            {
                case EventType::param:
                {
                    const auto rawParam = (std::uint16_t) p.readShort();
                    if (rawParam >= (std::uint16_t) ParamId::count)
                        return LoadResult::unknownEnumValue;
                    e.param.id = (ParamId) rawParam;
                    const auto rawGesture = (std::uint8_t) p.readByte();
                    if (rawGesture > (std::uint8_t) GestureFlag::end)
                        return LoadResult::unknownEnumValue;
                    e.param.gesture = (GestureFlag) rawGesture;
                    e.param.value   = p.readDouble();
                    e.param.index   = (std::uint32_t) p.readInt();
                    e.param.stableId = (std::uint64_t) p.readInt64();
                    break;
                }
                case EventType::transport:
                {
                    const auto rawAction = (std::uint8_t) p.readByte();
                    if (rawAction >= (std::uint8_t) TransportAction::count)
                        return LoadResult::unknownEnumValue;
                    e.transport.action        = (TransportAction) rawAction;
                    e.transport.positionBeats = p.readDouble();
                    e.transport.lengthBeats   = p.readDouble();
                    break;
                }
                case EventType::channelAdd:
                {
                    e.channelAdd.id = p.readInt64();
                    const auto rawCk = (std::uint8_t) p.readByte();
                    if (rawCk >= (std::uint8_t) ChannelKind::count)
                        return LoadResult::unknownEnumValue;
                    e.channelAdd.kind      = (ChannelKind) rawCk;
                    e.channelAdd.slotIndex = (std::uint8_t) p.readByte();
                    break;
                }
                case EventType::channelRemove: e.channelRemove.id = p.readInt64(); break;
                case EventType::channelReorder:
                    e.channelReorder.id      = p.readInt64();
                    e.channelReorder.fromPos = (std::uint8_t) p.readByte();
                    e.channelReorder.toPos   = (std::uint8_t) p.readByte();
                    break;
                case EventType::trackLoad:
                {
                    e.trackLoad.track = p.readInt64();
                    const auto rawLane2 = (std::uint8_t) p.readByte();
                    if (rawLane2 > (std::uint8_t) StemLane::vocals)
                        return LoadResult::unknownEnumValue;
                    e.trackLoad.stemLane        = (StemLane) rawLane2;
                    e.trackLoad.contentHash     = (std::uint64_t) p.readInt64();
                    e.trackLoad.analysisVersion = (std::uint32_t) p.readInt();
                    break;
                }
                case EventType::trackClear: break;
                case EventType::insertAdd:
                    e.insertAdd.id        = p.readInt64();
                    e.insertAdd.pluginUid = (std::uint32_t) p.readInt();
                    e.insertAdd.position  = (std::uint8_t) p.readByte();
                    break;
                case EventType::insertRemove: e.insertRemove.id = p.readInt64(); break;
                case EventType::insertReorder:
                    e.insertReorder.id      = p.readInt64();
                    e.insertReorder.fromPos = (std::uint8_t) p.readByte();
                    e.insertReorder.toPos   = (std::uint8_t) p.readByte();
                    break;
                case EventType::marker:
                    e.marker.index  = (std::uint32_t) p.readInt();
                    e.marker.atBeat = p.readDouble();
                    break;
                case EventType::tempoPoint:
                    e.tempoPoint.atBeat = p.readDouble();
                    e.tempoPoint.bpm    = p.readDouble();
                    break;
                case EventType::count: return LoadResult::unknownEnumValue;
            }

            out.push_back (e);
        }
        return LoadResult::ok;
    }
};

} // namespace stemdeck::events
