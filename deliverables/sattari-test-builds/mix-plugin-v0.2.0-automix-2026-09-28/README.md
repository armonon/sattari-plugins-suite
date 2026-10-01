# Sattari Mix 0.2

Internal studio automixing build: a linked-track assistant alongside the existing
eight-slot, fourteen-module mixing rack. AU, VST3 and Standalone share the same
processing and state format. This is not an industry-leadership or listening-quality
claim.

## Workflow

1. Insert Mix on each source track to include in the balance. Avoid linking both
   a source and its receiving bus; the planner assumes one direct contribution
   per participating instance. DAW faders, sends and downstream effects are not
   visible to the plugin. Keep those stable while comparing.
2. In Auto Mix, use **New group** on one instance. Its unique code is copied to
   the clipboard. Paste that code on other instances and click **Join**. Use a
   distinct code for each project, including simultaneously open project copies.
3. Select tracks in the group table and assign roles. Mark one lead vocal when
   applicable. Lock tracks whose existing processing should be preserved. Track
   roles are explicit user choices, not claims of automatic source recognition.
4. Play a representative passage and click **Analyze group**. Choose 8, 16 or
   30 seconds. Progress counts processed audio, so the DAW must deliver audio.
   A proposal appears when all participating tracks finish the same pass.
5. Select a row to edit its proposed gain, low EQ, presence EQ and dynamics.
   **Proposal** and **Original** audition the group. **Level match** uses an
   active-level estimate and attenuation only. **Keep mix** retains the proposal
   without temporary comparison attenuation. **Undo mix** restores the baseline.
6. The Signal Chain tab retains the existing rack, presets and individual
   processors. This track's Auto Mix toggle, amount and gain remain available
   for final adjustment and DAW automation.

## Processing

- Analysis listens after the rack and before the assistant, so re-analyzing does
  not feed the assistant's own gain corrections back into its measurements.
- Fixed 100 ms frames measure K-weighted channel power, sample peak, active
  duration, crest factor and coarse low/high spectral energy. Silence is gated
  at an absolute floor and relative to the loudest frame. This is **active
  K-weighted dB**, not standards-compliant integrated LUFS or true peak.
- A deterministic role-based planner proposes bounded gain, gentle 120 Hz and
  2.2 kHz bell cuts on supporting parts, and optional stereo-linked compression
  on high-crest vocals/bass. These are broad spectral heuristics, not a learned
  perceptual masking model. Compression uses JUCE's compressor with 20 ms attack,
  180 ms release and a maximum 2.5:1 ratio.
- The initial plan budgets a conservative sum of captured sample peaks to
  -6 dBFS. This is an estimate, not an output limiter or a guarantee for different
  passages, correlated filter ringing, downstream effects, sends or DAW faders.
  Manual proposal edits update the estimate and may exceed that target.
- Gain and effect transitions are smoothed. The assistant adds no latency.
  Outside level-matched audition, the default and zero-amount states preserve
  the rack's audio exactly.
- Decisions stay fixed until a new analysis or user edit. This preserves musical
  movement within the passage; it does not implement verse/chorus recognition,
  arrangement automation, reference-track matching, panning or reverb decisions.

## Linking And Recall

Group discovery, frozen measurements and commands use atomic local JSON files
under the system temporary directory's `SattariAutoMix-v1` folder, with hashed
group names and separate random runtime instance IDs. No audio leaves the
processor, and no network service is used. The 4 Hz control timer runs without
an open editor and supports separate plugin processes that share the same
temporary-directory namespace. A sandbox that isolates that namespace cannot
join the same group. Up to 64 fresh instances are supported; stale peers expire
after 2.5 seconds.

Commands validate the prior settings and analysis before changing a track.
Locks are checked again at the recipient. Failed group changes attempt to
restore reachable tracks that have not been manually edited. This control
protocol is asynchronous; it is not sample-synchronous automation.

Kept parameters, role, lock, group and track name are recalled by DAW state.
Instance IDs regenerate on construction so duplicated tracks do not overwrite
each other. Original host parameter indices and module selections are preserved;
the nine new parameters are appended. Old sessions default to assistant off.
Analysis and the proposal/undo snapshot are session-runtime data, retained across
editor close/open but not serialized. Temporary level-match attenuation is not
saved, and is cleared when its coordinator disappears from the group. Use
**Keep mix** or **Undo mix** before saving an audition as a final result.

## Build And Verify

```sh
cmake -S plugins/mix -B build/mix -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build/mix --target SattariMix_AU SattariMix_VST3 SattariMix_Standalone SattariMixProductContract SattariMixAutoMixContract -j 4
build/mix/SattariMixProductContract
build/mix/SattariMixAutoMixContract "$PWD/build/mix/screenshots"
auval -v aumf Mx01 Satr
/Applications/pluginval.app/Contents/MacOS/pluginval --validate-in-process --strictness-level 5 "$HOME/Library/Audio/Plug-Ins/VST3/Sattari Mix.vst3"
```

The machine contract covers analysis, DSP gain, silence, stereo cancellation,
roles, locks, peak budgeting, cross-process control, state compatibility,
audition/keep/undo and stale-proposal rejection. Native editor screenshots are
rendered at minimum, default and large sizes.

Human DAW listening, cleared multitrack evaluation, and blind competitive tests
remain required. **LISTENING STATUS: NOT VERIFIED.**
