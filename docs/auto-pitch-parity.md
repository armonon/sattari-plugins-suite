# Auto Pitch 0.2 parity record

Comparison target: Auto-Tune Pro 11's published product surface and manual
(checked 2026-08-05):

- <https://www.antarestech.com/products/pitch-correction/pro>
- <https://www.antarestech.com/documentation/auto-tune-pro-11>

This record is an engineering scope map, not a sound-quality or trademark claim.

| Workflow | Sattari Auto Pitch 0.2 | Status |
|---|---|---|
| Realtime automatic tuning | YIN/optional CREPE, Natural through Hard/Robot styles, Flex, Humanize, transition, throat, formant and vibrato controls | Implemented |
| Low-latency tracking | Studio Rubber Band, Live PSOLA and Low-Latency PSOLA with measured/reported latency contract | Implemented |
| Key and scale | Manual, AutoKey, Song Key bus, modulation-aware Key Map, editable pitch classes and 13 scale/mode choices | Implemented |
| Reference pitch | A4 detune ±100 cents or opt-in suite reference | Implemented |
| MIDI | Exact held-note targeting plus the suite MIDI Learn mapper | Implemented |
| Graph-style note editing | Transport-timed note capture, persistent note objects, vertical retarget and per-note bypass | Foundation implemented |
| Harmony | Sattari Harmony provides eight independently voiced parts; Auto Pitch also has simpler Big Harmony/poly harmony colors | Implemented across suite |
| Presets | Factory and user preset manager; dedicated Natural, Hard, Rap, Robot, Live, MIDI, Graph and Vibrato starting points | Implemented |
| Graph pitch curves/lines and detailed per-object controls | Note-region editor exists, but curve/line drawing, split/join and per-object formant/vibrato/retune are not yet present | Future DSP/UI work |
| Graph time correction | No time-warp object engine yet | Future DSP/UI work |
| ARA2 whole-clip access | Realtime capture is the current path | Requires ARA integration and host certification |
| AAX / Pro Tools | AU, VST3 and Standalone are built | Requires the AAX SDK, signing and Pro Tools qualification |
| Multi-track view | Session bus coordinates key/reference, but there is no shared multi-instance editor | Future host/UI architecture |

## Machine evidence

- Core contract covers five sound styles, modal scales, reference detune, MIDI
  targets, YIN noise robustness and UI rendering for Basic, Pro and Edit.
- Offline smoke and High Tracking regression render the real processor.
- Objective benchmark audits pitch accuracy, latency honesty, THD/alias proxy,
  CPU, transitions, vibrato, polyphonic correction and output safety.
- Trace contract checks the expanded public parameter surface.

## Release boundary

Version 0.2 is an internal test candidate. A commercial parity statement needs
real-vocal blind listening against the chosen references, DAW automation/state
round-trips, multi-instance stress, signed/notarized installers, licensed model
distribution, ARA/AAX decisions, and the remaining Graph tools above.
