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

## Explicit non-claims / current blockers

- No bundled on-device stem model in the distributable package yet: premium AI stems currently require the configured offline separator command/Demucs helper and cached files.
- No sale-grade pitch/time engine claim yet: Preserve routes through Rubber Band when linked and otherwise through the internal granular key-lock fallback, while Preserve-off still uses resample fallback and can change pitch/artifact. The synthetic time/pitch benchmark protects routing/regression basics, but real-song listening validation is still required.
- Live key analysis is lightweight chroma/Krumhansl analysis on the loaded full-mix window, not a commercial-grade polyphonic key detector yet. Validate false positives, CPU/UI stalls, and behavior during breakdowns/modulations.
- No automatic downbeat/transient detection or editable DJ beatgrid yet; Beat Sync currently follows host PPQ plus each deck's Source BPM and manual Beat Grid Offset.
- No plugin-host audio routing: a JUCE AudioPluginFormatManager discovery/load scaffold exists, but hosted VST3/AU audio/MIDI routing and UI are not productized.
- No manual DAW pass completed by this repo-only script.
- No real listening pass completed by this repo-only script.
- No installer/notarization/EULA/payment/product-page work.

## Minimum DAW smoke matrix before sale-candidate language

For each host below, test both fresh insert and restored session:

- Ableton Live: VST3 insert, load four decks, load at least two stem lanes, automate stem gains, save/reopen.
- Logic Pro: Standalone can be checked now; AU is not currently built for StemDeck, so AU support remains blocked until CMake/product config adds it.
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
12. Note the plugin-host scaffold status line; do not count it as hosted plugin processing until audio/MIDI routing is implemented.

## Packaging command

```bash
./scripts/stemdeck_manual_validation_template.sh
./scripts/package_stemdeck_alpha.sh
```

Package output defaults to:

```text
../deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.8-premium-ai-stems/
```

Do not distribute this outside trusted internal testers until DAW/listening evidence is recorded and legal/product packaging is ready.
