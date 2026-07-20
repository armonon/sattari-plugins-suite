# Sattari Session Bus Protocol

**Version 2 · open, vendor-neutral · MIT**

The Sattari Session Bus is a tiny, zero-configuration way for audio plugins in the same
session to agree on the musical context — **key, tempo, tuning, chord, transient pulse**
— without the host, without cable routing, and without a central server. Sattari's own
suite uses it so every plugin "knows your song's key"; this document specifies it so
**any** developer can read or write the same state and interoperate.

It is deliberately humble: one small JSON file, last-writer-wins, poll at UI rate. That
is enough for slow-moving *musical context* (key/tempo change on the order of seconds),
and it fails invisibly — no bus, no problem, the plugin just runs standalone.

Reference implementation: [`core/include/sattari_audio_core/SattariCrossPluginBus.h`](../core/include/sattari_audio_core/SattariCrossPluginBus.h)
(single header, no dependency beyond JUCE `juce_core` for JSON + file paths; the wire
format is plain JSON, so a non-JUCE implementation is trivial).

## Transport

A single UTF-8 JSON file, rewritten atomically on each publish, at a per-user location:

| Platform | Path |
| --- | --- |
| macOS   | `~/Library/Sattari Audio/cross-plugin/key-bus.json` |
| Windows | `%APPDATA%\Sattari Audio\cross-plugin\key-bus.json` |
| Linux   | `~/.config/Sattari Audio/cross-plugin/key-bus.json` |

(the directory is `<userApplicationDataDirectory>/Sattari Audio/cross-plugin/`). A file
was chosen over shared memory for portability and crash-safety: a dead host leaves no
stale lock, and any tool — a DAW script, a CLI, a web bridge — can read it.

## Message schema

```jsonc
{
  "schemaVersion": 2,          // integer; readers MUST tolerate a higher value
  "publisher":     "auto-pitch", // free-form source id (slug/vendor.name)
  "root":          9,          // tonic pitch class, 0=C … 11=B
  "scale":         1,          // 0=major, 1=minor, 2=chromatic/no-key
  "bpm":           128.0,      // host tempo; 0 = unknown
  "transientId":   4213,       // monotonically increments on each detected onset
  "timestampMs":   1737400000000, // Unix ms when written (staleness clock)

  // v2 optional extensions — omit when unknown; readers default them:
  "chordRoot":     -1,         // current chord root 0..11, -1 = none
  "chordQuality":  0,          // 0 maj,1 min,2 dom7,3 min7,4 maj7,5 dim,6 aug,7 sus
  "timeSigNum":    4,
  "timeSigDen":    4,
  "swing":         0.0,        // 0..1
  "sectionId":     0,          // arrangement section index (verse/chorus/…)
  "energy":        0.0,        // 0..1 mix intensity
  "maqamId":       0,          // microtonal system id (0 = 12-TET)
  "referenceHz":   440.0,      // A4 tuning reference
  "progression":   "0,9,5,7"   // optional chord-root list, message-only
}
```

Only `root` and `scale` are truly required to be a useful key publisher; everything else
is additive. A v1 (key-only) writer omitting `bpm`/`transientId` is valid — readers
default the missing fields.

## Semantics

- **Last writer wins.** There is no locking. Whoever publishes most recently is the
  current authority. A well-behaved detector should only publish when it is *actively
  detecting from audio* (not echoing a manual choice), so instances don't fight over the
  slot. A polite writer checks the current record's confidence/recency before stomping it.
- **Staleness.** A reader ignores a record older than **30 s** (`now - timestampMs`), so a
  closed publisher's key doesn't linger forever. Publishers should refresh at least every
  few seconds while active.
- **Threading.** Never read the file on the audio thread. Poll on a timer (Sattari uses
  ~2.5 Hz) and hand the result to the audio thread through a lock-free snapshot. The
  reference header ships `SessionReader`, a background-thread poller exposing a wait-free
  `current()` snapshot for exactly this.
- **Conventions.** `root` 0=C…11=B. `scale` matches Auto Pitch: 0 major / 1 minor / 2
  chromatic. `referenceHz` is A4. These match every Sattari plugin, so a third-party
  reader lands in the same key the suite is following.

## Minimal reader (any language)

```txt
1. read the JSON file (return "no session" if absent/unparseable)
2. if now_ms - timestampMs > 30000  -> "no session"
3. use root, scale, bpm; default any missing v2 field
```

## Versioning

`schemaVersion` is currently **2**. Readers **must** accept any `schemaVersion >= 2` and
simply ignore unknown fields — all future growth is additive into new optional keys, so a
v2 reader keeps working against a v3 writer. A reader that only understands the base
key/tempo fields is always forward-compatible.

## Registering a publisher id

`publisher` is free-form, but to avoid collisions in a shared ecosystem, third parties
should namespace it (`vendor.plugin`, e.g. `acme.supereq`). Sattari plugins use their bare
slug (`auto-pitch`, `song-key`, …). No registration is required to read; publishing is
open to anyone.
