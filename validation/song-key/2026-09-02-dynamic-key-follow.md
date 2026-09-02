---
type: implementation-and-validation-record
product: Sattari Key + Sattari Auto Pitch
date: 2026-09-02
status: DYNAMIC KEY FOLLOW IMPLEMENTED; PROCESSOR/BUS CONTRACTS PASS; MUSICAL CORPUS GATE OPEN
---

# Dynamic Key Follow

## Outcome

Sattari Key can now stay on a live music or master feed, form a fresh
non-overlapping key-evidence window every 1–5 seconds, and send accepted changes
to Auto Pitch. Auto Pitch's existing **Follow Song Key** mode consumes those
updates through the realtime-safe session reader; no file access or key analysis
runs on the vocal audio callback.

Dynamic Follow is opt-in and defaults to a 3-second window. Enabling it returns
Sattari Key to live input and clears incompatible manual/locked states. Fixed
review-and-lock remains the default workflow for a song that does not modulate.
Existing saved sessions remain compatible because both new parameters default to
inactive/safe values.

## Decision policy

- The first result above the shared 0.10 winner-versus-runner-up margin becomes
  the active key.
- A different key with a margin of at least 0.22 is accepted after one complete
  evidence window.
- A different releasable but marginal key must recur in two consecutive windows.
- Silence, percussion-only, and below-threshold/ambiguous windows retain the last
  accepted key and clear partial switch evidence.
- A revision number increments only when the accepted root or mode changes.
- While active, Sattari Key refreshes its publication as a heartbeat. A fresh
  higher-priority Compass frame remains authoritative by the existing bus lease
  rules.
- Disabling Dynamic Follow, disabling publication, or unlocking a fixed decision
  withdraws Sattari Key's frame immediately when it still owns the bus. It cannot
  delete a Compass or other publisher's frame.

The bus adds optional `dynamicKey`, `keyConfidence`, `keyRevision`, and
`keyWindowSeconds` fields without changing schema version 3 or the required
root/scale fields. Legacy readers ignore them; current readers receive safe
defaults when they are absent. Auto Pitch shows a dynamic publisher's real
accepted margin instead of presenting it as 100% manual certainty.

## Timing truth

The musical response is not the Auto Pitch engine latency. At the recommended
3-second setting, a strong modulation is accepted after the next completed
3-second evidence window and reaches Auto Pitch after up to roughly 0.4 seconds
of suite-bus polling. A marginal change can require two windows. The 1-second
setting is faster but necessarily has less harmonic evidence. Auto Pitch's
selected Live/Studio engine latency is unchanged after the new key arrives.

This design prevents silence or a single ambiguous chord from immediately
retuning a singer to a false tonic. It cannot promise that every arbitrary song
modulation will be identified correctly; objective cross-genre modulation and
live-performance listening remain release gates. Manual key, fixed Sattari Key
lock, and Auto Pitch Key Map remain the deliberate fallbacks.

## Machine evidence

Focused clean universal Release build, with the separately gated product excluded:

```sh
cmake -S . -B build-dynamic-key -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DSATTARI_INCLUDE_STEMDECK=OFF
cmake --build build-dynamic-key --target \
  SongKeyRender SattariCrossPluginBusContract AutoPitchCoreContract -j 6
```

Passing contracts on 2026-09-02:

- `SongKeyRender --dynamic-follow-smoke`: actual plugin processor accepted C
  major, changed to G major, published both revisions, then held G through a
  silent window.
- `SattariCrossPluginBusContract`: Dynamic Key metadata round-trip, lock-free
  `SessionReader` snapshot, and legacy default behavior pass.
- `AutoPitchCoreContract`: actual Auto Pitch `SessionReader` followed C major to
  G minor and preserved the published 0.31 and 0.18 margins. All pre-existing
  factory, graph, harmony, latency, acquisition, noise, and AutoKey gates also
  pass.
- Universal AU, VST3, and standalone targets build for both Sattari Key 0.3.0
  and Auto Pitch 0.5.0 and copy to the local plug-in folders.
- Apple `auval` passes Sattari Key (`aumf/Sk01/Satr`) and Auto Pitch
  (`aufx/Ap01/Satr`). `pluginval` strictness 5 passes both installed VST3
  bundles across 44.1/48/96 kHz, 64–1024 sample blocks, state, automation,
  editor, processing, and bus checks.

## Remaining release gates

1. Score dynamic transitions on an untouched labeled corpus containing real
   modulations, with detection delay, false-switch rate, missed-switch rate,
   abstention, and mode accuracy reported separately.
2. Run a singer-monitoring matrix in target DAWs with 1, 2, 3, and 5-second
   windows; listen specifically for false key switches, transition timing, and
   interaction with retune speed/humanize.
3. Test dominant-heavy, relative-major/minor, chromatic, sparse, spoken, silent,
   and drums-only passages. The test must include deliberate wrong-key attacks,
   not only clean sustained chords.
4. Consider a future host-synchronized key timeline with user-editable change
   points for deterministic playback. Automatic live following should complement,
   not replace, an authored map for critical production.
