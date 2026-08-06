# Auto Pitch 0.3 parity record

Comparison target: Auto-Tune Pro 11's published product surface and manual
(checked 2026-08-05):

- <https://www.antarestech.com/products/pitch-correction/pro>
- <https://www.antarestech.com/documentation/auto-tune-pro-11>

This record is an engineering scope map, not a sound-quality or trademark claim.

| Workflow | Sattari Auto Pitch 0.3 | Status |
|---|---|---|
| Realtime automatic tuning | YIN/optional CREPE, Natural through Hard/Robot styles, Flex, Humanize, transition, throat, formant and vibrato controls | Implemented |
| Low-latency tracking | Studio Rubber Band, Live PSOLA and Low-Latency PSOLA with measured/reported latency contract | Implemented |
| Key and scale | Manual, AutoKey, Song Key bus, modulation-aware Key Map, editable pitch classes and 13 scale/mode choices | Implemented |
| Reference pitch | A4 detune ±100 cents or opt-in suite reference | Implemented |
| MIDI | Exact held-note targeting plus the suite MIDI Learn mapper | Implemented |
| Graph-style note editing | Stable persistent objects, vertical retarget, bypass, split/join/delete and 64-step undo/redo | Implemented |
| Harmony | Sattari Harmony provides eight independently voiced parts; Auto Pitch also has simpler Big Harmony/poly harmony colors | Implemented across suite |
| Presets | Factory and user preset manager; dedicated Natural, Hard, Rap, Robot, Live, MIDI, Graph and Vibrato starting points | Implemented |
| Detailed per-object controls | Amount, retune, transition, humanize, vibrato, formant, gain and ±500 ms timing, with horizontal graph zoom/fit | Implemented |
| Graph pitch curves/lines | Region-level pitch is editable; freehand curve and line tools are not present | Future DSP/UI work |
| Graph time correction | Opt-in latency-compensated fractional-delay engine with 500 ms look-ahead | Implemented; DAW stress qualification pending |
| ARA2 whole-clip access | ARA 2.2 playback renderer feeds the same tuning DSP; build is enabled when the public SDK is present | Implemented; host qualification pending |
| AAX / Pro Tools | Reproducible AAX build target and dependency checks are present | Requires licensed Avid SDK, signing credentials and Pro Tools qualification |
| Multi-track view | Session bus coordinates key/reference, but there is no shared multi-instance editor | Future host/UI architecture |

## Machine evidence

- Core contract covers five sound styles, modal scales, reference detune, MIDI
  targets, YIN noise robustness and UI rendering for Basic, Pro and Edit.
- Offline smoke and High Tracking regression render the real processor.
- Objective benchmark audits pitch accuracy, latency honesty, THD/alias proxy,
  CPU, transitions, vibrato, polyphonic correction and output safety.
- Trace contract checks the expanded public parameter surface.

## Format builds

ARA2 uses the public 2.2 SDK and is enabled automatically when it is installed:

```sh
scripts/fetch_ara_sdk.sh
cmake -S plugins/auto-pitch -B build-auto-pitch -DSATTARI_AUTOPITCH_ENABLE_ARA=ON
```

AAX uses the same processor and editor through JUCE's AAX wrapper, but Avid's SDK
cannot be redistributed. Licensed builders enable it explicitly:

```sh
cmake -S plugins/auto-pitch -B build-auto-pitch-aax \
  -DSATTARI_AUTOPITCH_ENABLE_AAX=ON \
  -DSATTARI_AAX_SDK_PATH=/absolute/path/to/AAX_SDK
```

## Release boundary

Version 0.3 is an internal test candidate. A commercial parity statement needs
real-vocal blind listening against the chosen references, DAW automation/state
round-trips, multi-instance stress, signed/notarized installers, licensed model
distribution, ARA/AAX host certification, and the remaining freehand Graph tools above.
