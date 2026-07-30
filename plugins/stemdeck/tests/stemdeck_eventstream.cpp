// A1 phase 1: the event stream round-trips EXACTLY, and refuses what it must.
//
// The whole point of this schema is that a replay reproduces a performance
// sample-identically. That is impossible if the stream itself is lossy, so this pins:
//
//   * every payload variant survives write -> read unchanged
//   * doubles are BIT-exact, not near — the old PerformanceEvent stored "0.7350" as a
//     String, and a value that round-trips through four decimals cannot reproduce
//   * (atSample, seq) is a total order and sorting is stable against it
//   * a newer schemaVersion is REFUSED, not guessed at
//   * an out-of-range enum is REFUSED — the vocabulary is closed
//   * truncated data is detected rather than read as garbage
#include "EventStream.h"
#include <iostream>
#include <algorithm>
#include <cstring>

using namespace stemdeck::events;

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

juce::File tempFile (const char* name)
{
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile (juce::String ("stemdeck-events-") + name + ".sdev");
}

// Bit-exact, not approximate. A replay that is "close" is a replay that is wrong.
bool bitEqual (double a, double b) noexcept
{
    return std::memcmp (&a, &b, sizeof (double)) == 0;
}
} // namespace

int main()
{
    std::cout << "A1 event stream (schema v1)" << std::endl;

    Header h;
    h.sampleRate = 48000.0;
    h.blockSize  = 512;
    h.createdUtc = 1785000000000LL;   // caller-supplied; no wall-clock in the writer

    std::vector<Event> src;

    // One of every payload variant, with awkward values on purpose.
    {
        Event e; e.atSample = 0; e.seq = 1; e.type = EventType::channelAdd;
        e.scope.kind = ScopeKind::master;
        e.channelAdd.id = 9007199254740993LL;   // > 2^53: would be mangled by a double
        e.channelAdd.kind = ChannelKind::input;
        e.channelAdd.slotIndex = 7;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 480000; e.seq = 2; e.type = EventType::param;
        e.scope.kind = ScopeKind::stem; e.scope.channel = 42; e.scope.stemLane = StemLane::vocals;
        e.param.id = ParamId::gainStem;
        e.param.gesture = GestureFlag::end;      // endpoints are never coalesced (§5)
        e.param.value = 0.73499999999999999;     // deliberately not representable in 4dp
        e.param.index = 3;
        e.param.stableId = 0xF00DCAFEDEADBEEFULL;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 480000; e.seq = 3; e.type = EventType::transport;
        e.scope.kind = ScopeKind::channel; e.scope.channel = 42;
        e.transport.action = TransportAction::beatJump;
        e.transport.positionBeats = -16.25;
        e.transport.lengthBeats   = 32.0;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 960000; e.seq = 4; e.type = EventType::trackLoad;
        e.scope.kind = ScopeKind::channel; e.scope.channel = 42;
        e.trackLoad.track = 77;
        e.trackLoad.stemLane = StemLane::drums;
        e.trackLoad.contentHash = 0xDEADBEEFCAFEF00DULL;   // §6.2 analysis pinning
        e.trackLoad.analysisVersion = 3;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 960001; e.seq = 5; e.type = EventType::insertAdd;
        e.scope.kind = ScopeKind::insert; e.scope.channel = 42; e.scope.insert = 5150;
        e.insertAdd.id = 5150; e.insertAdd.pluginUid = 0xABCD1234u; e.insertAdd.position = 2;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 1000000; e.seq = 6; e.type = EventType::tempoPoint;
        e.tempoPoint.atBeat = 128.5; e.tempoPoint.bpm = 127.999999;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 1000001; e.seq = 7; e.type = EventType::channelReorder;
        e.channelReorder.id = 42; e.channelReorder.fromPos = 3; e.channelReorder.toPos = 0;
        src.push_back (e);
    }
    {
        Event e; e.atSample = 1000002; e.seq = 8; e.type = EventType::marker;
        e.marker.index = 4; e.marker.atBeat = 64.125;
        src.push_back (e);
    }

    // ── Round trip ───────────────────────────────────────────────────────────
    const auto f = tempFile ("roundtrip");
    check (Writer::write (f, h, src), "stream written");

    Header rh; std::vector<Event> out;
    const auto r = Reader::read (f, rh, out);
    check (r == LoadResult::ok, "stream read back (" + describe (r) + ")");
    check (out.size() == src.size(), "event count preserved ("
           + juce::String ((int) out.size()) + "/" + juce::String ((int) src.size()) + ")");

    check (rh.sampleRate == h.sampleRate && rh.blockSize == h.blockSize
           && rh.createdUtc == h.createdUtc, "header round-trips");
    check (rh.schemaVersion == kSchemaVersion, "schema version stamped");

    if (out.size() == src.size())
    {
        bool allOk = true, doublesExact = true, idsExact = true;
        for (size_t i = 0; i < src.size(); ++i)
        {
            const auto& a = src[i]; const auto& b = out[i];
            allOk = allOk && a.atSample == b.atSample && a.seq == b.seq
                          && a.type == b.type && a.scope == b.scope;

            switch (a.type)
            {
                case EventType::param:
                    allOk = allOk && a.param.id == b.param.id
                                  && a.param.gesture == b.param.gesture
                                  && a.param.index == b.param.index
                                  && a.param.stableId == b.param.stableId;
                    doublesExact = doublesExact && bitEqual (a.param.value, b.param.value);
                    break;
                case EventType::transport:
                    allOk = allOk && a.transport.action == b.transport.action;
                    doublesExact = doublesExact
                        && bitEqual (a.transport.positionBeats, b.transport.positionBeats)
                        && bitEqual (a.transport.lengthBeats, b.transport.lengthBeats);
                    break;
                case EventType::channelAdd:
                    idsExact = idsExact && a.channelAdd.id == b.channelAdd.id;
                    allOk = allOk && a.channelAdd.kind == b.channelAdd.kind
                                  && a.channelAdd.slotIndex == b.channelAdd.slotIndex;
                    break;
                case EventType::channelReorder:
                    idsExact = idsExact && a.channelReorder.id == b.channelReorder.id;
                    allOk = allOk && a.channelReorder.fromPos == b.channelReorder.fromPos
                                  && a.channelReorder.toPos == b.channelReorder.toPos;
                    break;
                case EventType::trackLoad:
                    allOk = allOk && a.trackLoad.track == b.trackLoad.track
                                  && a.trackLoad.stemLane == b.trackLoad.stemLane
                                  && a.trackLoad.contentHash == b.trackLoad.contentHash
                                  && a.trackLoad.analysisVersion == b.trackLoad.analysisVersion;
                    break;
                case EventType::insertAdd:
                    allOk = allOk && a.insertAdd.id == b.insertAdd.id
                                  && a.insertAdd.pluginUid == b.insertAdd.pluginUid
                                  && a.insertAdd.position == b.insertAdd.position;
                    break;
                case EventType::tempoPoint:
                    doublesExact = doublesExact && bitEqual (a.tempoPoint.atBeat, b.tempoPoint.atBeat)
                                               && bitEqual (a.tempoPoint.bpm, b.tempoPoint.bpm);
                    break;
                case EventType::marker:
                    allOk = allOk && a.marker.index == b.marker.index;
                    doublesExact = doublesExact && bitEqual (a.marker.atBeat, b.marker.atBeat);
                    break;
                default: break;
            }
        }
        check (allOk,        "every payload field round-trips");
        check (doublesExact, "doubles are BIT-exact (not merely close)");
        check (idsExact,     "int64 ids survive beyond 2^53");
    }

    // ── v1 positional-channel migration ─────────────────────────────────────
    {
        Header legacy;
        legacy.schemaVersion = 1;
        std::vector<Event> old (2);
        old[0].type = EventType::param;
        old[0].scope.kind = ScopeKind::channel;
        old[0].scope.channel = 0;
        old[1].type = EventType::param;
        old[1].scope.kind = ScopeKind::stem;
        old[1].scope.channel = 2;
        const bool migrated = migrateV1PositionalScopes (
            legacy, old, { 101, 205, 999 });
        check (migrated && old[0].scope.channel == 101
               && old[1].scope.channel == 999,
               "schema v1 positional scopes migrate through saved slots");
    }

    // ── Total order ──────────────────────────────────────────────────────────
    {
        auto shuffled = out;
        std::reverse (shuffled.begin(), shuffled.end());
        std::stable_sort (shuffled.begin(), shuffled.end());
        bool ordered = true;
        for (size_t i = 0; i + 1 < shuffled.size(); ++i)
            ordered = ordered && ! (shuffled[i + 1] < shuffled[i]);
        check (ordered, "(atSample, seq) is a total order");

        // The two events sharing atSample=480000 must come back in seq order.
        bool tieBroken = false;
        for (size_t i = 0; i + 1 < shuffled.size(); ++i)
            if (shuffled[i].atSample == 480000 && shuffled[i + 1].atSample == 480000)
                tieBroken = shuffled[i].seq < shuffled[i + 1].seq;
        check (tieBroken, "same-sample events replay in seq order");
    }

    // ── Refusals ─────────────────────────────────────────────────────────────
    {
        const auto nf = tempFile ("newer");
        Header future = h;
        Writer::write (nf, future, src);
        // Patch schemaVersion to a future value in place (offset 4).
        {
            juce::MemoryBlock mb; nf.loadFileAsData (mb);
            auto* bytes = static_cast<std::uint8_t*> (mb.getData());
            bytes[4] = (std::uint8_t) (kSchemaVersion + 1); bytes[5] = 0;
            nf.replaceWithData (mb.getData(), mb.getSize());
        }
        Header oh; std::vector<Event> oe;
        const auto rr = Reader::read (nf, oh, oe);
        check (rr == LoadResult::newerSchema, "a NEWER schema is refused, not guessed at");
        nf.deleteFile();
    }
    {
        const auto bf = tempFile ("badenum");
        Writer::write (bf, h, src);
        {
            juce::MemoryBlock mb; bf.loadFileAsData (mb);
            auto* bytes = static_cast<std::uint8_t*> (mb.getData());
            // Header is 32 bytes; first event's type byte sits after atSample(8)+seq(4).
            bytes[32 + 12] = 250;   // not a valid EventType
            bf.replaceWithData (mb.getData(), mb.getSize());
        }
        Header oh; std::vector<Event> oe;
        const auto rr = Reader::read (bf, oh, oe);
        check (rr == LoadResult::unknownEnumValue, "an unknown EventType is refused");
        bf.deleteFile();
    }
    {
        const auto tf = tempFile ("truncated");
        Writer::write (tf, h, src);
        {
            juce::MemoryBlock mb; tf.loadFileAsData (mb);
            mb.setSize (mb.getSize() - 20);   // lop off the tail
            tf.replaceWithData (mb.getData(), mb.getSize());
        }
        Header oh; std::vector<Event> oe;
        const auto rr = Reader::read (tf, oh, oe);
        check (rr == LoadResult::truncated, "truncated data is detected");
        tf.deleteFile();
    }
    {
        const auto gf = tempFile ("garbage");
        gf.replaceWithText ("this is not an event stream at all, not even close");
        Header oh; std::vector<Event> oe;
        const auto rr = Reader::read (gf, oh, oe);
        check (rr == LoadResult::badMagic, "a non-stream file is rejected by magic");
        gf.deleteFile();
    }

    f.deleteFile();
    std::cout << (failures == 0 ? "EVENTSTREAM_OK" : "EVENTSTREAM_FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
