# Auto Pitch 0.5.0 RC3 parity record

Comparison target: AutoTune Pro 11.6, AutoTune 2026, and AutoKey 2.6 published
product surfaces, manuals, and latency guidance (checked 2026-08-31):

- <https://www.antarestech.com/products/pitch-correction/pro>
- <https://www.antarestech.com/documentation/auto-tune-pro-11>
- <https://www.antarestech.com/products/pitch-correction/auto-key>
- <https://www.antarestech.com/documentation/auto-key>
- <https://help.antarestech.com/hc/en-us/articles/42858099043092-AutoTune-2026-Best-Practices>

This record is an engineering scope map, not a sound-quality or trademark claim.

| Workflow | Sattari Auto Pitch 0.5.0 | Status |
|---|---|---|
| Realtime automatic tuning | YIN/optional CREPE, Natural through Hard/Robot styles, Flex, Humanize, transition, throat, formant and vibrato controls | Implemented |
| Audibly validated vocal quality | Synthetic target math, controlled Hard-mode modulation, safety, and six real-vocal corpus renders pass; the 18-item blind package still has no user scores | **Unproven; blocking** |
| Low-latency tracking | Live Mode offers Extreme 2MS (96 samples / 2.00 ms at 48 kHz), Ultra Quality C3+ (571 / 11.90 ms), and Extended Range (1,037 / 21.60 ms). Reported and impulse-measured delay agree exactly. Extreme measures 1.97-cent mean / 6.04-cent max synthetic error across 40 E2-C5 detune cases, 0.19% corrected-tone THD and exact unity dry; dual-range acquisition reaches voiced at 24 ms and >=50-cent correction at 28 ms. Ultra/Range retain ~0.08-cent mean accuracy. All pass the qualified 44.1/48 kHz 64-1024 CPU matrix; Extreme is outside at 96 kHz / 64. Antares publishes 2.3 ms for its Modern low-latency mode, so Sattari now meets that numeric latency target, not Antares' 0.77 ms Classic figure. Neural fusion reaches the causal planner. Legacy Live/Low-Latency parameters remain inactive for old-session compatibility | **Engine and objective gates implemented; subjective Antares parity, real-vocal and DAW listening/stress qualification still blocking** |
| Key and scale | Manual, AutoKey, Song Key bus, modulation-aware Key Map, editable pitch classes and 13 scale/mode choices | Implemented |
| AutoKey + Sattari Key handoff | Auto Pitch's internal fallback remains Aarden/chroma and retains the manual key below its 0.10 margin. Sattari Key can publish an explicitly reviewed lock or Dynamic Engine v2 decisions. V2 analyses every second and fuses full-band HPCP, 40–260 Hz bass evidence, the balanced 16 kHz estimator, beat/onset-weighted chord evidence, and a 24-state temporal decoder. Context-rich changes need two observations; other changes use the selected 1–5 second stability horizon. Isolated chords, silence, and ambiguity hold the accepted key. Confidence/revision metadata reaches Auto Pitch through the realtime-safe session reader. No neural key model is shipped in either path | Implemented and processor-contract tested; wider held-out modulation/confidence calibration still required |
| Reference pitch | A4 detune ±100 cents or opt-in suite reference | Implemented |
| MIDI | Exact held-note targeting plus the suite MIDI Learn mapper | Implemented |
| Graph-style note editing | Stable persistent objects, octave-aware vertical retarget, bypass, split/join/delete and 64-step undo/redo; exact targets and bypass reach both Studio and causal Live planners | Implemented |
| Harmony | Four continuous vocal voices with Intelligent chord/scale planning, diatonic Interval and four-note MIDI modes; per voice enable/solo, role, interval/octave, level, pan, delay, detune, vibrato and Studio-only formant shift; global glide, sample-clock pitch/timing humanize, voice-only audition, filtering, and feed-forward stereo choir amount/depth/rate/width. H1-H4 regions support independent pitch/mute overrides, undo/redo and project recall; timing follows the parent lead note. MIDI/edited targets outside the qualified one-octave range are octave-folded, preserving pitch class. The readout uses audio-owned targets. Extreme, Ultra and Range/Studio have separate prepared banks; Studio formants consume existing alignment headroom, not extra reported latency. Whole-mix transpose/polyphonic modes take precedence. See [harmony controls and limits](auto-pitch-harmony.md) | Implemented in the new candidate; real-vocal arrangement listening and DAW qualification still required |
| Presets | Factory and user preset manager; dedicated Natural, Hard, Rap, Robot, Live, MIDI, Graph, Vibrato, Pop Stack, Wide Choir and MIDI Choir starting points | Implemented |
| Detailed per-object controls | Amount, retune, transition, humanize, vibrato, formant, gain and ±500 ms timing, with horizontal graph zoom/fit; live retune/transition/vibrato and graph targets are covered by the causal planner contract | Implemented |
| Graph pitch curves/lines | Region-level pitch is editable; freehand curve and line tools are not present | Future DSP/UI work |
| Graph time correction | Opt-in latency-compensated fractional-delay engine with 500 ms look-ahead | Implemented; DAW stress qualification pending |
| ARA2 whole-clip access | ARA 2.2 foundation builds when the SDK is present | **Host workflow and correction/state qualification pending** |
| AAX / Pro Tools | Reproducible AAX build target and dependency checks are present | Requires licensed Avid SDK, signing credentials and Pro Tools qualification |
| Multi-track view | Session bus coordinates key/reference, but there is no shared multi-instance editor | Future host/UI architecture |

## Machine evidence

- Core contract covers five sound styles, modal scales, reference detune, MIDI
  targets, YIN noise robustness, four harmony target notes (including the octave
  edge), 64/512-sample harmony continuity, processor-level harmony audio/readout
  agreement, Extreme's 96-sample delay and 24/28 ms detector/correction
  acquisition contract, and UI rendering for Tune, Key, Edit, Advanced, Harmony and
  minimum-size layouts, including persistent-header pixels.
- The cross-plugin contract round-trips Dynamic Key flag, margin, revision and
  window metadata with legacy-safe defaults. Auto Pitch's processor contract
  follows successive C-major and G-minor bus revisions and preserves both
  published margins. Sattari Key's processor smoke accepts C major, follows a
  G-major modulation, and retains G through a silent evidence window.
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
