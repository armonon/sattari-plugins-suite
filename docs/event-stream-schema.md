# StemDeck Event Stream — schema v1 (DRAFT FOR REVIEW)

**Status: proposal. Not implemented. Do not code against this until it is agreed.**

This is the **one-way door**. Every control in the live surface writes to this stream,
and the arrangement editor is a deterministic replay of it. Changing it after either
half is built breaks both. It is written down first deliberately (§A1 of the
"Solid Live Play" brief).

---

## 1. Why the existing event log cannot be used

`PerformanceEvent` (ArrangementBuilder.h) is **stringly-typed** —
`{sample, timeSeconds, type, deck, parameter, value, path, details}` with everything
after `deck` a `juce::String`. That is serviceable as a *log*, and unusable as a
*replay source*:

- `value` is a formatted string (`"0.7350"`), so replay re-parses a lossy decimal —
  a float that round-trips through 4 decimal places **will not reproduce sample-identically**.
- No schema version, no event id, no ordering guarantee between events at the same sample.
- No representation for structural changes (channel add/remove) or for FX chain edits.
- `deck` is an `int` index; once channels are addable/removable/reorderable, an index
  silently refers to a different channel after any mutation.

So v1 is a new type, not an extension. The old log stays as a human-readable export.

---

## 2. Core principles

1. **Capture intent, not audio.** An event records *what the user did*, never the
   resulting sample values.
2. **Exact values only.** Numeric payloads are stored binary-exact (`float`/`double`/
   `int64`), never formatted text. This is what makes bit-identical replay possible.
3. **Master-clock timestamps.** `atSample` is on the session master clock, monotonic,
   never wall-clock.
4. **Stable identity.** Targets are addressed by opaque id, never by list position.
5. **Total order.** `(atSample, seq)` is a total order over the stream. `seq` breaks
   ties for events in the same block so replay order is deterministic.
6. **Closed vocabulary.** `EventType` and `ParamId` are enums, not free strings.
   Unknown enum value on load ⇒ refuse the file (never guess).

---

## 3. Identity

```cpp
using ChannelId = juce::int64;   // stable for the channel's lifetime; never reused
using InsertId  = juce::int64;   // stable per FX insert instance
using TrackRefId = juce::int64;  // entry in the session's source-material pool
```

**A channel's id is assigned at creation and never reused, even after removal.** Index
is a view concern; the stream only ever refers to ids. This is the fix for "deck 2"
meaning something different after a removal.

### Scope — where a parameter applies

```cpp
struct Scope {
    enum class Kind : juce::uint8 { master, channel, stem, insert };
    Kind      kind      = Kind::master;
    ChannelId channel   = 0;   // channel/stem/insert scopes
    juce::uint8 stemLane = 0;  // stem scope: 1=drums 2=bass 3=music 4=vocals
    InsertId  insert    = 0;   // insert scope
};
```

One `Scope` covers the master / channel / stem / insert hierarchy the FX panel (§B4)
and the master-stem console (§B2) both need. A "mute all drums" gesture is **four
stem-scope events**, not one special-cased master event — replay stays uniform.

---

## 4. The event

```cpp
struct Event {
    juce::int64  atSample = 0;   // master clock, monotonic
    juce::uint32 seq      = 0;   // tie-break within the same sample; strictly increasing
    EventType    type     = EventType::param;
    Scope        scope;
    Payload      payload;        // tagged union, see below
};
```

`seq` is global and monotonic across the whole session, so `(atSample, seq)` is a
total order even when a gesture writes several events in one block.

### 4.1 Event types

```cpp
enum class EventType : juce::uint8 {
    param,            // a continuous or discrete parameter changed
    transport,        // play / stop / cue / seek / loop / beat-jump
    channelAdd,       // structural
    channelRemove,
    channelReorder,
    trackLoad,        // a TrackRef was loaded into a channel/stem lane
    trackClear,
    insertAdd,        // FX chain structure
    insertRemove,
    insertReorder,
    marker,           // take marker / hot cue set
    tempoPoint,       // retroactive tempo-lane edit (§B3)
};
```

### 4.2 Payloads

```cpp
struct ParamPayload {
    ParamId id;              // enum — closed vocabulary
    double  value;           // EXACT. never a formatted string
    GestureFlag gesture;     // none | begin | end   (see §5)
};

struct TransportPayload {
    TransportAction action;  // play|stop|cue|cueRelease|seek|loopOn|loopOff|beatJump
    double positionBeats;    // where, in beats (seek / jump / loop bounds)
    double lengthBeats;      // loop length / jump distance; 0 when unused
};

struct ChannelAddPayload    { ChannelType type; ChannelId id; juce::uint8 slotIndex; };
struct ChannelRemovePayload { ChannelId id; };
struct TrackLoadPayload     { TrackRefId track; juce::uint8 stemLane; };
struct InsertAddPayload     { InsertId id; juce::uint32 pluginUid; juce::uint8 position; };
struct InsertReorderPayload { InsertId id; juce::uint8 fromPos, toPos; };
struct TempoPointPayload    { double atBeat, bpm; };
```

**`channelAdd` carries the id**, so replay recreates the same id rather than minting a
new one — otherwise every later event addressed to that channel misses.

### 4.3 ParamId (closed enum — extend by appending only)

```
gainStem, stemMute, stemSolo,          // stem scope
channelGain, channelMute, channelSolo, // channel scope
eqLow, eqMid, eqHigh, filter,          // channel or stem scope
pitchSemitones, keyTarget, keyLockOn,  // key
bpmSource, tempoInterp, syncMode,      // tempo
crossfader, crossfaderCurve, cfSide,
insertParam, insertBypass,             // insert scope; index carried in payload
inputGain, inputMonitor, inputSource,  // input channels
instGain, instMidiChannel,
masterGain, limiterCeiling, masterKey,
```

Appending is safe. **Renumbering or reusing a value is a breaking change** and requires
a schema-version bump plus a migration.

---

## 5. Continuous controls — the endpoint rule

Coalesce to ~60 Hz **but always emit the first and last sample of a gesture.**

```
touch  -> Event{param, value=v0, gesture=begin}
drag   -> coalesced samples at <=60Hz, gesture=none
release-> Event{param, value=vN, gesture=end}     // ALWAYS emitted, never coalesced away
```

Dropping the endpoints is the subtle failure the brief warns about: the mix ends up
at a *nearby* value instead of the exact one the user released on, and replay diverges
slowly rather than obviously. `gesture=end` events are exempt from coalescing.

---

## 6. Determinism requirements (what A1's test actually checks)

For replay to be sample-identical, all of the following must hold. Each is a real
constraint on the engine, not just the schema:

1. **No wall-clock, no `Random`, no uninitialised state** anywhere in the render path.
2. **Analysis is pinned, not recomputed.** A replay must use the *same* beat grid and
   key as the live pass. Since neural refinement lands asynchronously (§A3), the
   analysis actually used must be recorded — proposal: `trackLoad` carries the
   `contentHash` **and** an `analysisVersion`, and the refined analysis is written to
   the session so replay loads it rather than re-running the models. *Without this,
   A1 cannot pass — background refinement is inherently non-deterministic in timing.*
3. **Fixed block size for render.** Replay renders at a declared block size; results
   are only guaranteed identical at the same block size, because IIR filter state
   evolves per-block.
4. **Denormal handling identical** (FTZ/DAZ set the same in both passes).
5. **Channel order canonical.** Replay iterates channels in id order, not creation or
   list order.

> **Open question for review:** (3) means the hash test must fix a block size. Do we
> also want *cross-block-size* determinism? That is a much stronger requirement — it
> effectively forbids per-block-dependent DSP — and I do not recommend it.

---

## 7. Storage

- Binary, little-endian, fixed-size records (the union is fixed-width) so the stream is
  seekable and memory-mappable.
- Header: `magic "SDEV"`, `schemaVersion`, `sampleRate`, `createdUtc`, `blockSize`.
- **`schemaVersion` is checked on load. A newer version is refused, not guessed at.**
- The session holds **both** this event stream and recorded-audio assets (live input
  is captured audio; its fader/FX moves are still events — §A1/§A4).

---

## 8. What this obsoletes / touches

| Area | Impact |
|---|---|
| `PerformanceEvent` / `PerformanceRecorder` | Stays as the human-readable export; no longer the source of truth |
| `SessionModel` (beats, `kSchemaVersion=2`) | Superseded for capture; `TrackRef` pool concept carries over |
| ~40 control call sites | All must dispatch instead of mutating APVTS directly |
| Rendering | Must read resulting session state, not APVTS |

---

## 9. Honest cost

Implementing this is **3–5 weeks**: the schema itself is a few days; inverting every
control from direct-mutation to dispatch, making rendering read session state, and
building the replay + hash harness is the bulk. Item §6.2 (pinning analysis) may be
the hardest part and is easy to miss until the hash test fails intermittently.

**Review this before any of it is coded.**
