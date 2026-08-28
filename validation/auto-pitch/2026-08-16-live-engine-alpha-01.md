---
type: validation-evidence
product: Sattari Auto Pitch
engine: Sattari Live
version: alpha 0.1
date: 2026-08-16
status: OBJECTIVE_FOUNDATION_PASS_LISTENING_REQUIRED
truth_label: isolated core; not connected to the shipping plugin
---

# Sattari Live engine — alpha 0.1 foundation

This starts a new low-latency engine without changing Auto Pitch 0.5.0 RC3's
shipping Studio HQ path. The old grid-based live PSOLA remains inactive. The new
core is in `SattariLivePitchEngine.h` and cannot be selected from the plugin UI or
automation surface.

## Architecture now implemented

- Causal YIN at a sample-rate-invariant ~24 kHz analysis rate.
- Voiced entry/hold hysteresis, octave continuity and three-frame median control.
- Waveform-refined epochs rather than an assumed sample-zero pitch grid.
- Fixed-latency TD-PSOLA with precomputed Hann windows and no allocation after
  `prepare()`.
- Exact delayed dry at unity and while unvoiced.
- A 2.5 ms voiced/unvoiced wet transition for consonants and note attacks.
- Chromatic or arbitrary pitch-class masks ready to consume Sattari Key output.
- Note hysteresis, transition re-anchoring, retune trajectory and independent
  vibrato-centre/residual control.
- A private `AutoPitchRender --live-alpha-file` path for local corpus work only.

## Core contract

`SattariAudioCoreLivePitchContract` passes:

| Gate | Result |
|---|---:|
| Tracker, 100/147/220/440/880 Hz | 0.00 / 0.04 / 0.09 / -0.03 / 3.36 cents |
| 44.1/96 kHz sample-rate checks | 0.13 / 0.09 cents |
| Broadband noise | 0 / 375 voiced blocks |
| Noisy harmonic voice | 100% voiced, 0.51-cent error |
| Unity delayed null | exact, maximum error 0 |
| Normal-vocal latency, 48 kHz / 100 Hz floor | 728 samples / 15.17 ms |
| Known two-semitone shift, blocks 64/256 | 0.14 / 0.08-cent error |
| Vibrato | Natural retains 89%; Hard retains 16% |
| Note leap | correct new target; 0-cent overshoot in the controlled case |
| Hard chromatic boundary | A held at +55 cents; A# selected at +57 cents |
| Formant-envelope proxy | 24% of naive-resampling centroid displacement |
| 48 kHz / 64-sample CPU, ordinary user thread | mean 0.7%, p95 2.9%, p99 3.1% of one core in the recorded run |

The complete local suite also passes: **97/97 non-StemDeck tests**, including
the serialized Live callback-percentile contract.

The timing test reports maximum callback time but does not gate it because this
standalone process does not receive a host realtime thread. Mean, p95 and p99 are
gated. A real DAW stress pass remains mandatory.

## Private real-vocal corpus

All outputs are local at:

`~/Desktop/vocal test/live-alpha-0.1-renders/`

Wide-vocal mode uses a 70 Hz floor and reports 21.60–21.61 ms. All twelve files
are finite and unclipped. The table is the sustained-frame distance to the
nearest chromatic note; it is diagnostic, not a listening score.

| Clip | Input median | Natural median | Hard median | Hard max request |
|---|---:|---:|---:|---:|
| dickey-prairie | 22.1 ct | 21.3 ct | 20.5 ct | 55.7 ct |
| guidos-pizza | 19.3 ct | 19.3 ct | 19.3 ct | 55.8 ct |
| slay-demons | 24.0 ct | 23.3 ct | 19.3 ct | 55.9 ct |
| vs-belt-m6 | 39.5 ct | 8.7 ct | 2.1 ct | 56.0 ct |
| vs-melisma-m3 | 24.3 ct | 21.2 ct | 9.4 ct | 55.9 ct |
| vs-vibrato-f5 | 23.3 ct | 19.0 ct | 15.0 ct | 56.0 ct |

The first transition implementation requested as much as 786.7 cents on a
real-vocal boundary. Re-anchoring and current-pitch note selection reduced the
final Hard maximum to 55.7–56.0 cents across all six clips. This regression is
now locked by the planner contracts.

## Honest boundary

This is a credible low-latency foundation, not a state-of-the-art quality claim.
The objective result is weaker than RC3 Studio HQ on several rap/noisy sustained
metrics, and no human has judged grain, sibilance, plosives, timbre or preference.
The core is mono; shared stereo detection/phase, plugin integration, automation,
host PDC, transport resets and DAW buffer stress are not implemented or qualified.

Do not expose the Live controls until the private real-vocal renders pass human
listening and the new path wins a controlled non-inferiority comparison against
the disabled legacy engine and the Studio HQ reference.
