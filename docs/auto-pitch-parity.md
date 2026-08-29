# Auto Pitch 0.5.0 RC3 parity record

Comparison target: AutoTune Pro 11.6 and AutoKey 2.6 published product surfaces
and manuals (checked 2026-08-16):

- <https://www.antarestech.com/products/pitch-correction/pro>
- <https://www.antarestech.com/documentation/auto-tune-pro-11>
- <https://www.antarestech.com/products/pitch-correction/auto-key>
- <https://www.antarestech.com/documentation/auto-key>

This record is an engineering scope map, not a sound-quality or trademark claim.

| Workflow | Sattari Auto Pitch 0.5.0 | Status |
|---|---|---|
| Realtime automatic tuning | YIN/optional CREPE, Natural through Hard/Robot styles, Flex, Humanize, transition, throat, formant and vibrato controls | Implemented |
| Audibly validated vocal quality | Synthetic target math, controlled Hard-mode modulation, safety, and six real-vocal corpus renders pass; the 18-item blind package still has no user scores | **Unproven; blocking** |
| Low-latency tracking | Visible Live Mode offers explicit Ultra C3+ (571 samples / 11.90 ms at 48 kHz, 0.08-cent mean synthetic error) and Extended Range (1,037 samples / 21.60 ms, 70 Hz floor, 0.08-cent mean from E2-C5). Both report and measure the same delay, measure 0.69% corrected-tone THD, preserve exact unity dry and pass qualified 44.1/48 kHz CPU coverage from 64-1024 samples. Neural fusion now reaches the causal planner instead of consuming CPU without steering it. Legacy Live/Low-Latency parameters remain inactive for old-session compatibility | **Implemented; real-vocal and DAW listening/stress qualification still blocking** |
| Key and scale | Manual, AutoKey, Song Key bus, modulation-aware Key Map, editable pitch classes and 13 scale/mode choices | Implemented |
| AutoKey + Sattari Key handoff | Auto Pitch's frozen internal fallback remains Aarden/chroma and retains the manual key below its 0.10 margin. Sattari Key uses a specialized balanced full-mix engine, applies the same conservative margin policy, and sends only an explicitly locked result over the session bus. No neural model is shipped in either path | Implemented; wider held-out calibration still required |
| Reference pitch | A4 detune ±100 cents or opt-in suite reference | Implemented |
| MIDI | Exact held-note targeting plus the suite MIDI Learn mapper | Implemented |
| Graph-style note editing | Stable persistent objects, octave-aware vertical retarget, bypass, split/join/delete and 64-step undo/redo; exact targets and bypass reach both Studio and causal Live planners | Implemented |
| Harmony | Sattari Harmony provides eight independently voiced parts; Auto Pitch also has simpler Big Harmony/poly harmony colors | Implemented across suite |
| Presets | Factory and user preset manager; dedicated Natural, Hard, Rap, Robot, Live, MIDI, Graph and Vibrato starting points | Implemented |
| Detailed per-object controls | Amount, retune, transition, humanize, vibrato, formant, gain and ±500 ms timing, with horizontal graph zoom/fit; live retune/transition/vibrato and graph targets are covered by the causal planner contract | Implemented |
| Graph pitch curves/lines | Region-level pitch is editable; freehand curve and line tools are not present | Future DSP/UI work |
| Graph time correction | Opt-in latency-compensated fractional-delay engine with 500 ms look-ahead | Implemented; DAW stress qualification pending |
| ARA2 whole-clip access | ARA 2.2 foundation builds when the SDK is present | **Host workflow and correction/state qualification pending** |
| AAX / Pro Tools | Reproducible AAX build target and dependency checks are present | Requires licensed Avid SDK, signing credentials and Pro Tools qualification |
| Multi-track view | Session bus coordinates key/reference, but there is no shared multi-instance editor | Future host/UI architecture |

## Machine evidence

- Core contract covers five sound styles, modal scales, reference detune, MIDI
  targets, YIN noise robustness and UI rendering for Tune, Key, Edit, Advanced,
  Harmony Preview and minimum-size layouts, including persistent-header pixels.
- Offline smoke and High Tracking regression render the real processor.
- Objective benchmark audits pitch accuracy, latency honesty, THD/alias proxy,
  CPU, transitions, vibrato, polyphonic correction and output safety.
- Trace contract checks the expanded public parameter surface.

Machine evidence does not establish vocal sound quality. A sine can land within
0.01 cents while a real voice still exhibits warble, consonant damage, formant
drift, transient blur or note-transition artifacts.

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

Version 0.4.1 remains preserved at `7fef544a6d49`; version 0.5.0 is the new RC3
working line created before any 0.4.1 listening scores existed. A
commercial parity statement needs real-vocal blind listening against AutoTune Pro
on the same level-matched material, Live and Studio qualification in target DAWs,
a wider held-out and calibrated shared key detector, DAW automation/state
round-trips, multi-instance stress, ARA/AAX host qualification, and the remaining
freehand Graph tools above. Signing/notarization is intentionally outside the
current work scope.
