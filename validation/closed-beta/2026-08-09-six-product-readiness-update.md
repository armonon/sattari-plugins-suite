# Six-product readiness update — 2026-08-09

Truth label: **repository-controlled qualification complete for this revision;
human, host-matrix, signing and business gates remain open**.

## What is now proved

- The non-StemDeck root lane passed **94/94** tests again in 157.04 seconds.
- Vocal, Mix and Create now run their real primary processors through every
  factory workflow at 44.1, 48 and 96 kHz with 64- and 512-sample blocks. Their
  contracts also prove finite/bounded output, audible processing, host-state
  wiring recall and bit-transparent empty racks.
- Stack's existing layer contract remains green; all four primary rack shells
  therefore have executable product-level evidence.
- Compass remains bit-transparent and now contract-proves automatic energy,
  musical-onset and conservative major/minor chord publishing, plus explicit
  overrides and full session-state round trips.
- Auto Pitch contract-proves deterministic factory workflows: selecting a new
  workflow clears incompatible MIDI, graph, transpose, polyphonic and generated-
  vibrato modes. Natural has a stronger usable calibration and Pitch Center is a
  clean full-correction starting point.
- Auto Pitch objective measurements remain green: 0.01-cent mean / 0.04-cent
  maximum settled error, clean synthetic THD and alias proxies, 5.3 ms transition
  spread across 64–1024-sample buffers, 82% vibrato-depth retention and 3.7x
  neural real-time headroom in the benchmark lane.
- Auto Pitch, Compass, Vocal, Mix, Create and Stack built as universal AU, VST3
  and standalone bundles. Every product passed strict codesign verification,
  Apple `auval` and pluginval strictness 5, including processing and automation
  at 44.1, 48 and 96 kHz across 64–1024-sample blocks.
- The neural Auto Pitch distribution lane passed embedded CREPE provenance and
  inference, live neural-key, runtime-dependency, signature and bundle-integrity
  checks for AU, VST3 and standalone formats.
- Hub production build, manifest contract, 6-product/48-component catalog sync,
  11-bundle alpha payload proof and 49-component freshness check passed.
- The ONNX SDK include is treated as a third-party system dependency, restoring
  useful warning output for Sattari-owned code.
- The rack-preset validator now understands boolean and generated multiband
  parameters; all 264 preset references across 28 modules resolve correctly.

## Signature workflows added or refined

- Auto Pitch: **Natural**, **Pitch Center**, **Fast Natural** and deterministic
  workflow switching.
- Vocal: **Dry Lead** alongside the complete Vocal Studio path.
- Mix: **Low-End Control** for bass stability and translation.
- Create: **Compass Motion** for tempo-following movement and space.

## What “100” still requires outside the repository

These are release blockers, not optional polish:

1. Run the owned/cleared real-vocal corpus through Natural, Pitch Center and Hard
   Tune; perform blind artifact, formant, consonant and correction scoring.
2. Complete Logic Pro, Ableton Live and FL Studio scan/insert/process/automation/
   recall/bounce tests for every supported format and primary product.
3. Have at least two non-developer musicians finish a real project with the six-
   product flow and return structured results.
4. Record a full reference-session proof with multiple instances, project
   isolation, save/reopen and offline bounce.
5. Choose the commercial/open-source licensing model and satisfy JUCE, Rubber
   Band, model and source-offer obligations for that model.
6. Apply Apple Developer ID signing, notarization and stapling to the exact
   immutable release artifacts; then prove clean-machine install, update,
   rollback and uninstall.
7. Restore the GitHub Actions account's available billing/spend so remote CI can
   start; the latest dispatch was blocked before checkout by account billing.

Until those seven items have evidence, the honest status is **strong internal
alpha / closed-beta candidate**, not public release.
