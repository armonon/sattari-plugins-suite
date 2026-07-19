# StemDeck Session & Capture Schema (build-order #2 — the one-way door)

This is the format everything downstream renders from. Per the brief: **capture
intent, not audio.** The arrangement view, transition editing, retroactive mixing,
tempo re-export, and deterministic render are all *views/operations* over this data.

## Core decision: events are timestamped in BEATS, not samples

Every event carries `atBeat` — musical time on the master clock. This is what makes
**tempo re-export** fall out for free: change `masterTempo` and every event's wall-
clock time recomputes as `atBeat * 60 / tempo`. Sample positions are derived at
render time, never stored as the primary timestamp.

(Scratch/scrub, which is inherently sample-domain, is captured as a parameter
envelope like anything else; we do not need sample-accurate event timestamps for
v1. If a future feature needs sub-beat sample accuracy, `atBeat` is a double and a
tempo map already exists, so it converts — no schema break.)

## Objects

```
Session
  schemaVersion : int          # bumped on any breaking change; loader migrates
  sampleRate    : double
  masterTempo   : double       # BPM. Changing this re-times the whole session.
  createdMs     : int64
  tracks  : [ TrackRef ]       # the pool of source material referenced by events
  events  : [ Event ]          # ordered by atBeat
  regions : [ Region ]         # higher-level groupings (transitions), retimeable as a unit

TrackRef                       # identity + analysis snapshot, NOT the audio
  id            : string       # stable within the session
  path          : string
  contentHash   : string       # AnalysisCache hash — survives moves/renames
  durationSec   : double
  bpm           : double
  downbeatSample: double
  keyPitchClass : int          # -1 = unknown
  keyIsMinor    : bool

Event
  id     : int64
  atBeat : double              # master musical time
  type   : "deckLoad" | "transport" | "param"
  deck   : int
  # type-specific:
  trackId      : string        # deckLoad -> which TrackRef
  transport    : "play"|"pause"|"cue"|"seek"
  positionBeat : double        # transport seek target (musical time into the track)
  paramId      : string        # param -> "fader","low","mid","high","filter",
                               #          "stemDrumsGain","pitch","key", insert params...
  value        : double

Region                         # a transition is a region of events
  id        : int64
  kind      : string           # "transition" (extensible)
  startBeat : double
  endBeat   : double
  eventIds  : [ int64 ]        # the events this region owns
```

## Operations the schema must support (and why each headline feature needs it)

| Operation | Feature it enables |
|---|---|
| `moveRegion(id, Δbeats)` — shift region + all contained events | **Transitions as editable objects** (drag a blend two bars later) |
| `stretchRegion(id, newLenBeats)` — scale contained events around the region start | **Transitions as editable objects** (32→64-beat blend, automation re-times with it) |
| `setMasterTempo(bpm)` — no event data changes; render time recomputes | **Export at a different tempo than performed** |
| edit a param envelope's end (a `param` event's `atBeat`) | **Retroactive mixing** (run the outgoing vocal longer) |
| the event list *is* the edit history | **Deep session-wide undo** |
| `toJSON` is deterministic; `fromJSON(toJSON(x)) == x` | **Session-file integrity / render determinism** |

## Rendering contract

`sampleTimeForBeat(beat) = beat * (60 / masterTempo) * sampleRate`. The renderer walks
events in `atBeat` order, converts to sample time with the *current* tempo, and applies
each (load, transport, param) to a deterministic mixer graph. Same session + same
master tempo ⇒ identical render (item: render determinism).

## Versioning / migration (how a one-way door is de-risked)

`schemaVersion` is stored. The loader routes old versions through explicit migration
steps before use; a version it doesn't understand is refused rather than silently
misread. This is what lets the format evolve without corrupting old sessions — the
one-way door has a hinge.

## Not in v1 (deliberately deferred, but the schema leaves room)

- Insert-plugin params per stem lane: same `param` event with a namespaced `paramId`
  (e.g. `insert:laneVocals:EQ:band1Gain`). No schema change needed to add later.
- Comping/takes: a Region gains a `takeGroup` id; alternative regions share it.
- Cross-deck stem routing: a `param` event `route:laneBass` = source deck.

All three fit the existing Event/Region shape, so adding them is data, not a break.
