# Product Upgrade Implementation

Date: 1 October 2026. This records development work following the
[critical review](product-upgrade-review-2026-10-01.md), not a commercial release
approval. The full roadmap is not complete. Existing plugin identities,
installations, candidate packages and public website have not been replaced.

## Consolidation Follow-Up

The later 0.2.0 consolidation and its exact boundaries are recorded in
`suite-consolidation-2026-10-02.md`. The counts and verification paths below are
historical upgrade/hosting evidence, not certification of the merged build.

## Implemented

| Offering | Working changes | Compatibility boundary |
| --- | --- | --- |
| Modular FX | Double, Riser, Shimmer, Warble, Warp, Vocoder and 808; expandable effect controls; MIDI per effect; optional sample safety clip; three new collection presets | Original 160 parameters and module-selection ranges retained; added controls append; version 1 states migrate to version 2 |
| Modular Instrument | Same effects; per-layer level, pan, key range, transpose and velocity curve; smoothed level/pan; mapping editor | Still two sources/layers; loaded source ownership follows duplication, moves, undo and recall |
| Royal Chain | Unified 18-program host/editor bank; nine explicit stage bypasses; latency-aligned original audition; attenuation-only RMS comparison; power controls | Original eight program indices and 16 parameter positions retained; new processing toggles default off in old states |
| Mix | Declared summing-bus warning; evidence-based decision explanations; bounded persisted history; draft invalidation; analysis cancellation/progress handling | Original assistant parameter positions retained; bus flag appends; saved history cannot replay commands or revive stale runtime IDs |
| Vocal and Create | Shared rack/module boolean recall fix; their binaries and sessions remain supported | No removal, renamed plugin IDs, engine replacement or silent migration into Modular |
| Stack | Remains the supported four-layer reference | Not replaced or retired; Modular has not reached source/workflow parity |

Modular now contains 16 effects and two source engines. Its new Create effects
reuse the existing module DSP rather than claiming new algorithms. Detailed
controls derive names, ranges, choices and defaults from those modules. The
existing eight-control macro encoding remains unchanged; advanced controls are
host-automatable but not macro destinations. Automation remains slot-based.

The shared boolean fix makes a discrete parameter report 0 or 1 even when a
host sends a fractional automation value. This addresses a reproduced pluginval
state-restoration failure in Mix. IDs, constructor attributes, defaults, threshold
semantics and parameter ordering are unchanged. Product contracts exercise every
boolean in the tested rack rosters, not just the newly added controls.

## Important Limits

- Modular still prepares all available modules per slot. The expanded library
  needs a resource/loading redesign before additional high-latency modules.
- Gate, Face and Glass Choir are not yet in Modular. Double is the first Vocal
  collection integration, not the completed Vocal collection.
- 808 pitch analysis is expensive in the local automation profile. Finite output
  and zero counted C++ allocations do not demonstrate a reliable callback budget.
- Safety clip is an optional +/-0.98 sample clamp. It is not a transparent limiter,
  true-peak limiter, or protection from clipping inside earlier effects.
- Royal's Width remains stereo-width processing, not generated mono doubling.
  Bypass runs histories continuously and preserves Tune latency. Clean/Finish
  bypass also bypasses their input/output trims. Existing macro voicing remains.
- Royal's level comparison only attenuates the processed signal, by up to 24 dB,
  using smoothed RMS. It does not boost a quieter chain or measure integrated LUFS.
- Mix bus membership is declared, not detected. Faders, sends and DAW routing
  are still invisible. History is the latest 16 informational records, not a
  durable cross-session proposal/undo graph.
- Instrument key mapping controls incoming MIDI, not Rhythm Genie's own pattern.
  Sample zones, full source editors, Chop, four-layer parity, MPE and a finished
  sound library remain separate work.
- No Vocal/Create/Stack preset converter or DAW automation converter is provided.
  Version 2 Modular presets cannot be opened by old version 1 readers.

## Verification

Native builds and evidence live in these ignored build directories:

- `build/upgrade-modular`: both Modular variants, native contracts, UI PNGs and audio renders.
- `build/upgrade-royal`: Royal Chain, product and tuning contracts, UI PNGs and audio renders.
- `build/upgrade-mix`: Mix, product/assistant/UI contracts and minimum/default/large renders.
- `build/upgrade-root`: fresh root configure and release-contract dependency inventory.
- `build/all`: rebuilt Vocal, Create and shared-rack compatibility contracts only;
  existing installed plugin bundles were not replaced.

The Modular tests cover the original 0.1.0 fixture and migrated inactive scenes,
state fuzzing, independent duplicates, all exposed controls under automation,
source recall, MIDI effect routing, layer mapping, output protection and editor
controls. The Royal tests cover canonical presets, old-state defaults, oversized
buffers, bypass timing, comparison level and the existing synthetic tuning gate.
Mix tests cover linked processors/processes, proposals, locks, audition/keep/undo,
history, bus declarations and stale-state rejection. These do not replace human
listening, real multitracks, DAW projects or Windows execution.

Confirmed on the current local development builds:

| Check | Result |
| --- | --- |
| Modular FX and Instrument native contracts | 2/2 passed |
| Mix product, assistant and UI contracts | 3/3 passed |
| Royal Chain product and synthetic tuning contracts | 2/2 passed |
| Vocal, Create and shared-rack compatibility contracts | 3/3 passed |
| pluginval strictness 10 on Apple Silicon | Modular FX, Modular Instrument, Mix and Royal Chain all passed after the boolean fix |
| Modular packaging regression tests | 7/7 passed |
| Release contract runner regression tests | 6/6 passed |
| New workflow YAML | Parsed locally; no remote execution |

The VST3 validator logs are `pluginval-fx.log` and `pluginval-instrument.log`
under `build/upgrade-modular`, plus `pluginval.log` under the Mix and Royal build
directories. They are not AU validation, a Windows result, or a signing certificate.
The shared parameter change was exercised through these representative products;
the entire standalone catalog has not been rebuilt or requalified.

Fresh root configuration includes Modular FX/Instrument and Royal's product and
tuning contracts in `SattariReleaseContracts`. The existing optional elastique
and Signalsmith SDK warnings concern Auto Pitch, not these three builds.

## Windows and Mac Builds

The new manual **Product Upgrade Builds** GitHub Actions workflow builds Modular,
Mix and Royal Chain on Windows x64 and macOS universal, runs their native contracts,
and uploads development archives. VST3 and Standalone exist on both platforms;
AU is macOS-only. It does not publish, install, notarize or commercially sign.
The workflow is added locally; it has not been pushed or executed remotely.

Local Mix/Modular builds target Apple Silicon; local Royal targets both Apple
Silicon and Intel. Compiling a universal binary does not prove Intel execution.
Existing version numbers are still development placeholders, not newly approved
release versions. Before shipping, assign new versions and update Modular's
qualification packaging version instead of reusing the old 0.1.1 package record.

## Remaining Release Sequence

1. Freeze stable detailed-control mappings, profile initialization/memory and
   worst-case callback times, then redesign inactive-module preparation. Require
   a bounded budget for 808 and multi-instance Double before release inclusion.
2. Qualify Double against the standalone module on mono/stereo recordings and
   duplicate-instance chains; add a versioned legacy preset import report.
3. Resolve latency/tail/reset contracts for Gate, Face and Glass Choir, then
   integrate and qualify them. Build curated Vocal/Create banks with owner listening.
4. Run Royal vocal comparisons on cleared recordings: dynamics, sibilance,
   noise, ambience and mono behavior. Advanced stage controls or a new doubler
   require separate sonic acceptance, not replacement of old preset behavior.
5. Port the missing Stack source/layer workflows and establish sample portability
   and audio reproduction evidence before any consolidation or retirement.
6. Evaluate Mix on real multitracks, host grouping/isolation, bus layouts and
   multi-instance load. Section-aware analysis and reference matching follow
   reliable baseline decisions, not a larger effect menu.
7. Run Windows and Intel Mac execution, DAW reopen/automation tests, accessibility,
   crash recovery, signing/notarization and installer checks on the exact frozen
   sources. Update product catalog, storefront and prices only after those gates.

No industry-leading, release-ready or listening-approved claim follows from
these development changes alone.

## Follow-up: Modular VST3 Hosting

The subsequent hosting request is implemented in both Modular variants as a
development feature. External Instrument slots use the two source layers;
External Effect slots use the six serial and two return positions. The UI scans
a selected installed VST3, opens its editor and exposes missing/loading status.
Independent instances and opaque plugin state follow duplication, moves, undo,
scenes, A/B and session recall. Missing plugins retain their saved settings and
can be relinked. State format is now version 3; versions 1 and 2 remain readable.

Source, bypass and return paths account for hosted latency. Main mono/stereo
buses and stereo-only plugins in a mono parent are covered. Hosted instruments
receive mapped layer MIDI; tempo/transport and offline mode reach hosted plugins.
The parent parameter list remains unchanged; external plugin parameters are not
yet mapped to Modular macros or parent-host automation.

Final local checks after this follow-up: four native/hosting contracts passed,
eight packaging regressions passed, and both Modular VST3s passed pluginval
strictness 10 again. An installed Vital -> ValhallaVintageVerb chain generated
nonzero finite audio and rendered again after state recall. The hosting tests
are required by root release-contract builds and the packaging inventory; their
fixture VST3s are excluded from development archives. See the
[Modular README](../plugins/modular/README.md#external-instruments-and-effects)
for usage, evidence locations and exact restrictions.

This does not add AU/VST2/AAX hosting, crash isolation, a whole-library scanner,
multi-output routing, external sidechains or seamless scene switching. Loading
and scanning are in-process and may stall the UI; a faulty plugin can crash the
DAW. Large plugin states, vendor compatibility, real DAW sessions and Windows
execution still require qualification. Existing installed plugins and public
release packages were not replaced.
