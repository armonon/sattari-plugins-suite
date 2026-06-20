# Sattari Auto Pitch

Realtime JUCE alpha for the Auto Pitch flagship lane.

## Truth label

This is a local AU/VST3/Standalone realtime monophonic tuning alpha. It now performs audible pitch correction, but it is still **not** Antares-quality proof, not a public beta, not notarized, and not sale-ready.

## Current scope

- Audio effect plugin with live pitch detection, selected-key/scale snapping, and Rubber Band realtime pitch shifting when Rubber Band is available at build time.
- AU, VST3, and Standalone targets.
- UI controls for Key Root, Scale, Style, Correction On, AutoKey Follow, Amount, Speed, Do No Harm, gain, and trace confidence.
- Key Detection tab bridge to Sattari Harmonic Lab: the plugin now links against `SattariAudioCore` and exposes Sattari Wheel code, confidence, compatibility labels, and “Send to Tune” state from the shared harmonic/key-analysis contracts.
- Five performer modes for tester comparison: Natural, Classic, Modern Rap, Robot, and Big Harmony. These alter correction strength/retune behavior and add first-pass mode color; Big Harmony is a lightweight flavor layer, not a finished multi-voice harmony engine yet.
- AutoKey Follow is an early live pitch-class histogram follower; Adapt Mode remains offline/prototype-gated.
- Clipboard trace import path for JSON traces shaped like `prototypes/auto-pitch/out/demo-modern-trace.json`.
- Fixture contract smoke test: `python3 scripts/auto_pitch_trace_contract_check.py`.
- Core realtime contract test: `cmake --build build/auto-pitch-ready --target AutoPitchCoreContract && ./build/auto-pitch-ready/plugins/auto-pitch/AutoPitchCoreContract`.
- Runtime dependency bundling helper: `scripts/bundle_auto_pitch_runtime_deps.sh` copies Rubber Band/libsamplerate into built AU/VST3/app bundles and rewrites install names for tester portability.

## Verified on 2026-06-02

- `AutoPitchCoreContract` passed with Rubber Band realtime engine and a detuned A4 test moving toward A4.
- `python3 scripts/auto_pitch_trace_contract_check.py` passed.
- `./scripts/validate_sources.sh` passed.
- Apple `auval -v aufx Ap01 Satr` passed after local install.
- `/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5` passed for VST3.
- Codesign verification passed for bundled AU, VST3, and standalone app bundles.

## Next gates

1. Run real vocal DAW/manual listening validation in Logic and Ableton.
2. Tune pitch-detection latency, retune smoothing, and artifacts against owned/cleared dry vocal clips.
3. Promote AutoKey from early histogram follower to robust offline/live key detection with confidence display.
4. Feed the Key Detection tab with full offline `TrackAnalysis` data: Sattari Wheel alternates, segment keys, energy zones, cue points, and compatible reference-track suggestions.
5. Add proper preset bank and vocalist-friendly UX polish.
6. Replace ad-hoc signing with Developer ID signing/notarization before any public beta or sale-ready language.

## Non-goals

- No Antares-quality claim yet.
- No public release claim.
- No paid beta.
- No bundled copyrighted/uncleared vocal material.
