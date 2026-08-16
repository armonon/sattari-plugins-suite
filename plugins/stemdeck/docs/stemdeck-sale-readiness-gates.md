# StemDeck sale-readiness gates

Truth label: **validation plan / not sale-ready evidence**. Passing these gates would support a sale-candidate claim; this document itself does not make one.

## Implemented in this spike

- External stem-lane loading scaffold: each deck can load pre-separated `Full mix`, `Drums`, `Bass`, `Music`, and `Vocals` audio files that share one deck playhead/transport.
- Premium offline AI stem UX scaffold: each deck has an `AI Split` action, cache-aware queued/running/ready status, and ready stem counts.
- Automatable per-deck stem-lane gain parameters: `deck[A-D]Stem[Full|Drums|Bass|Music|Vocals]Gain`.
- Premium generated-stem mix handoff: when all four generated core stems are ready, StemDeck loads them and mutes the Full Mix lane to prevent doubled playback.
- A StemDeck-specific validation script that builds Standalone/VST3, verifies codesigning, and runs pluginval if available.
- A StemDeck-specific alpha package script that emits a truth-labeled package with install/validate helpers and this checklist.
- Lightweight full-mix key/RMS/duration analysis plus automatable manual pitch and target-key controls, rendered through granular key-lock when Preserve is enabled or resample fallback when Preserve is off.
- Host tempo sync with individual per-deck Source BPM controls, plus host-PPQ Beat Sync and per-deck Beat Grid Offset controls for beat-phase nudging.
- Live key analysis/matching toggle per deck: periodically analyzes the current playback-window key and can use that estimate for target-key pitch-shift matching, with load-time analysis as fallback.
- Synthetic time/pitch benchmark contract: renders Preserve/key-lock tempo cases, pitch-shift cases, stress cases, and a resample reference to `build/stemdeck/timepitch-benchmark/` with a metrics manifest for internal listening review.
- Deterministic arranger contracts cover state restore/sanitisation, undo/redo,
  Push/Replace overlap editing, full clear, sample-accurate loop wrap, and
  post-region effect-tail playback.

## Explicit non-claims / current blockers

- On-device model bundling is now SUPPORTED (opt-in): `NeuralStemEngine`/`NeuralBeatEngine::defaultModelFile()` search env-var -> app/plugin bundle `Contents/Resources` -> `~/Library` install, and `package_stemdeck_app_alpha.sh` copies `models/*.onnx` into the app when `STEMDECK_BUNDLE_MODELS=1`. Still a product decision to actually ship it: the fp32 stem model is ~353MB (+84MB beat), so the default package stays lean and premium AI stems otherwise need a `~/Library` install or the offline demucs helper. Verified: bundled model is found and loaded (CPU fp32) ahead of the `~/Library` copy.
- No sale-grade pitch/time engine claim yet: Preserve routes through Rubber Band when linked and otherwise through the internal granular key-lock fallback, while Preserve-off still uses resample fallback and can change pitch/artifact. The synthetic time/pitch benchmark protects routing/regression basics, but real-song listening validation is still required.
- Live key analysis is lightweight chroma/Krumhansl analysis on the loaded full-mix window, not a commercial-grade polyphonic key detector yet. Validate false positives, CPU/UI stalls, and behavior during breakdowns/modulations.
- Automatic onset, beat, tempo-curve, meter, and downbeat analysis is implemented,
  with a DBN/Viterbi-style decoder when neural activations are available. Anchor and
  Groove Sync now warp against the detected variable-tempo grid instead of reducing
  it to a fixed-BPM identity map. This still needs a licensed real-music corpus gate
  (especially ambient intros, breakdowns, live drummers, and tempo changes) and a
  full visual beat-marker editor before commercial DJ-grade claims.
- Plugin-host audio routing is now IMPLEMENTED (`HostGraph`: trusted Sattari in-process graphs plus a dedicated helper process and shared-memory audio ring for untrusted effects) with an in-app FX rack UI. NOT yet productized: some Sattari guest plugins (Echo, Space) crash in their own editor `resized()` when hosted, and hosted processing has had no real-song/DAW listening pass.
- Hosted-plugin parameters now have stable event-stream addressing and deterministic
  dispatch for the trusted graph. Third-party audio effects are process-isolated and
  dry-bypassed if the helper exits; native editors, MIDI, complete state/automation,
  more than one isolated effect per chain, automatic restart, and crash/hang/latency
  soak matrices remain release blockers for unrestricted third-party hosting.
- Editable arrangement replay now follows captured lane EQ/filter/reverb/echo and
  master gain/performance-FX/Smart-Master/limiter automation. A real third-party
  plugin replay/hash matrix and complete live-set stress replay are still required.
- No manual DAW pass completed by this repo-only script.
- No real listening pass completed by this repo-only script.
- A responsive static product page now exists at `apps/stemdeck-web/index.html`.
  Deployment, analytics, privacy/EULA, payments, support operations, credentialed
  notarization, and a tested installer/update channel remain commercial gates.
- The contract suite is comprehensive but expensive to build because excluded test
  executables compile duplicate JUCE/Tracktion objects. Consolidate the contracts or
  share test-support libraries so the full release gate is practical on every change.

## Minimum DAW smoke matrix before sale-candidate language

For each host below, test both fresh insert and restored session:

- Ableton Live: VST3 insert, load four decks, load at least two stem lanes, automate stem gains, save/reopen.
- Logic Pro: the AU target builds and passed `auval -v aumu Sd01 Satr` on 2026-08-09;
  test fresh insert, automation, save/reopen, and hosted-plugin state restoration.
- FL Studio: VST3 insert, transport sync on/off, play/stop host while decks are playing.
- Reaper: VST3 insert, plugin state save/reopen, sample pads, record WAV start/stop.

Record for every run:

- Host/version/macOS/CPU.
- Plugin format and path.
- Crash/hang result.
- Load time and first-play reliability.
- CPU at idle, one deck, four decks, four decks plus stems.
- Any clicks, zipper noise, pitch artifacts, sync drift, or waveform UI issues.

## Listening-pass checklist

Use known, licensed local test audio. For every deck and stem lane:

1. Load a full mix and press Play/Cue repeatedly.
2. Press `AI Split` on a full mix with the local separator configured; verify queued/running/ready status, four cached stem files, and automatic Full Mix mute once generated stems are ready.
3. Load matching pre-separated drums/bass/music/vocals manually and confirm shared start/rewind behavior.
4. Sweep stem gains from -60 dB to +12 dB through DAW automation.
5. Sweep low/mid/high EQ, filter, deck fader, crossfader, master, and limiter.
6. Toggle host sync + beat sync at 90/120/140 BPM, adjust per-deck Source BPM and Beat Grid Offset, and document sync drift / pitch artifacts honestly.
7. Enable LiveKey while playing harmonically simple and harmonically complex tracks; verify the displayed key is plausible and does not cause bad auto-match jumps.
8. Listen through `build/stemdeck/timepitch-benchmark/` and compare Preserve/key-lock tempo renders against the resample reference for pitch stability, transient smear, bass wobble, stereo image, and clicks.
9. Set target keys, sweep manual pitch shift, toggle Preserve on/off, and document musical quality / artifacts.
10. Record a 60-second master WAV and inspect it for clipping/silence/dropouts.
11. Save, close, reopen, and confirm file assignments restored.
12. Load one compatible effect on master, a channel, and a stem destination;
    confirm audio routing, state restore, sandbox status, and dry bypass after
    intentionally terminating the helper process.

## Packaging command

```bash
./scripts/stemdeck_manual_validation_template.sh
./scripts/package_stemdeck_alpha.sh
```

Package output defaults to:

```text
../deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.5.0-studio-workflow/
```

Do not distribute this outside trusted internal testers until DAW/listening evidence is recorded and legal/product packaging is ready.
