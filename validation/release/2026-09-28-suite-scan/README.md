# Plugin Release Scan - 28 September 2026

## Decision

**Qualify Arp and Sub Conjurer first for a small, controlled release. Make Mix
the next full-product release.** Vocal and Create are the next rack candidates.
Auto Pitch is the strongest flagship-development candidate in this scan, but
its broader workflow and sound-quality gates make it a higher-risk first launch.

This ranks engineering evidence, bounded product scope and qualification work,
not sound quality, sales potential or superiority to competitors. No blind
listening comparison was performed. **No product has complete evidence for a
public, production-ready release in this scan.**

## Scope And Results

Scanned all 49 catalog/plugin directories: CMake identities and versions,
test registration, lifecycle, installed AU metadata/signatures/architectures,
release records, and targeted processor/test source. This is not a line-by-line
audit of every DSP implementation. StemDeck was inspected, but its separate
VST3/standalone source was not rebuilt or qualified.

Base commit: `32b7c09155f8a2bd949c60a9c68ce55448ad0309`, with the existing dirty
working tree. No product source, installed bundle, lifecycle or release channel
was changed by this scan. The user's DAW projects were not opened or modified.

| Check | Result | Boundary |
|---|---|---|
| Catalog and plugin structure | 49/49; unique four-character product codes | Structure, not quality |
| Catalog consumers | Pass: catalog 49, Hub/web 48, focused CI 12 | StemDeck intentionally excluded from the suite catalog consumers |
| Rack presets | 264 references across 28 modules pass | Parameter references, not musical quality |
| Installed AU strict signatures | 49/49 pass | No Developer ID authority found in any of these 49 signatures |
| Installed AU architectures | 49/49 contain arm64 and x86_64 | Architecture inspection, not physical Intel testing |
| Installed AU identities | No duplicates found | Current user's Components directory only |
| Apple AU validation | 49/49 pass, executed in this scan | Installed AU binaries, not every source revision or format |
| Existing non-StemDeck contract inventory | 115/115 pass, rerun in this scan | Cached executables; **not a fresh build-first release gate** |
| Mix 0.2.0 separate qualification | Today's product/automix/rack tests, AU and pluginval 5 pass | See the dedicated Mix evidence; automix also passed under Rosetta |
| Arp/Sub archived packages | SHA-256 still matches their 9 August qualification record | Old immutable packages, not a rebuild of today's shared code |
| Auto Pitch 21 September source fingerprints | All recorded inputs still match | That record qualifies test executables, not new installed wrappers |
| Full 20 September source fingerprints | 14 recorded files changed | Expected subsequent Auto Pitch/Mix/shared work; old suite result cannot certify today's source |
| General source-layout script | **Fails**: missing `plugins/stemdeck/src/PluginHostManager.cpp` | Script/README still name the legacy class; current CMake uses HostGraph |
| Working-tree whitespace check | Pass | No compilation implied |

Evidence: [inventory](inventory.json), [AU results with binary hashes](auval-results.json),
[AU summary](auval-summary.log), [cached contract run](cached-contract-smoke.log),
[source-layout failure](source-layout.log), and [current Mix qualification](../../mix/2026-09-28-automix/README.md).
The cached CTest inventory does not include Mix's new automixing contract.

## Release Order

| Priority | Product | Why it is on the shortlist | Required before promotion |
|---|---|---|---|
| 1 | Sattari Arp | Focused MIDI effect; sample-accurate timing contract; explicit AU MIDI FX/VST3 package qualification | Freeze/rebuild the candidate, real host MIDI routing, latch/transport/automation/recall and stuck-note checks, clean-machine distribution |
| 2 | Sub Conjurer | Focused low-end job; tested 60 Hz to 30 Hz following and silence; qualified AU/VST3/standalone package | Real kick/bass listening, pitch-follow transitions, gain/headroom and sustained multi-instance host testing |
| 3 | Sattari Mix | Freshest complete three-format build; 14-module rack plus linked-track proposals, locks, comparison and undo | Register the new automix test in the suite gate; real multitracks, cross-instance host sessions, automation/recall/bounce, 64-track performance qualification |
| 4 | Sattari Vocal | Complete 10-module vocal rack with 10 factory workflows; product-level processor contract | Fresh exact bundles after shared changes; vocal listening, presets, automation, tails, recall and DAW bounce |
| 5 | Sattari Create | Complete 10-module creative rack with 9 factory workflows; product-level processor contract | Fresh exact bundles; musical tempo/context transitions, effect-tail and feedback stress, presets and host sessions |
| 6 | Sattari Auto Pitch | Broadest flagship feature/evidence effort: tuning, Live/Studio, note editing and four-voice harmony; September fixes have processor and corpus safety evidence | Build final wrappers from the qualified source; user listening approval; real host editing/ARA where supported, mode switching and recall; wet/dry timing acceptance |
| 7 | Sattari Compass | Useful free session authority; coherent context and transparent-audio contracts | Calibrate confidence and qualify real-song key/chord/context behavior, session isolation and source precedence in hosts |
| 8 | Sattari Key | Focused analysis workflow with file/live/arrangement tests; measured real-song detector evidence | Independent current-path accuracy/tempo acceptance, confidence calibration and supported host workflows; position within Compass |
| 9 | Sattari Stack | Useful four-layer instrument container; improved Chop/Granulator source recall and lifetime tests | Real-project media portability/missing-file recovery, layer alignment, sustained memory/CPU and instrument-panel coverage |

All nine still need an immutable candidate, exact final-format validation,
supported-DAW acceptance and qualified public distribution. An ad-hoc signature
is not Developer ID signing or notarization. License/source/model notices and
installer/update behavior must be checked for the actual package, not inferred
from a development folder.

The Arp/Sub archived package hashes were rechecked against
[their qualification record](../../alpha/2026-08-09-arp-sub-package-qualification.md).
Vocal/Create and broader suite evidence are described in the
[September closure report](../2026-09-20-release-closure.md).
Auto Pitch's latest specific evidence is the
[September harmony candidate](../../auto-pitch/2026-09-21-harmony-fixes.md).

## Findings

1. **Release blocking: exact-source and distribution qualification remains open.**
   Today's installed-AU passes are genuine, but same version numbers do not prove
   installed wrappers contain the newly qualified source. The historical 115-test
   gate and today's cached rerun cannot replace a build-first final candidate.
   The existing supported-DAW matrix is unfilled. None of the 49 inspected AU
   signatures has a Developer ID authority. Do not promote lifecycle or publish
   a sale-ready claim based on this scan.
2. **High priority: Mix's new automixing contract is outside the root gate.**
   `plugins/mix/CMakeLists.txt` defines `SattariMixAutoMixContract`, but the root
   `SATTARI_CONTRACT_TARGETS` list and release dependency list omit it. It was
   tested separately today; a future suite pass could miss an automix regression.
3. **High priority for separate releases: Morph and Chop lack dedicated registered
   production-processor contracts.** Morph has no plugin test target in its CMake
   file. Chop has an onset probe, while Stack/shared tests cover some Chop engine,
   recall and lifetime behavior. That is not equivalent to a Chop wrapper/editor/
   state workflow contract. Today's auval passes do not fill those gaps.
4. **StemDeck has configuration/documentation/tooling drift.** Its source project
   is 0.5.0 and declares VST3/Standalone; its README says 0.4.3 and the machine has
   an installed 0.5.0 AU. The source-layout script fails on the removed legacy
   `PluginHostManager.cpp` while CMake uses `HostGraph.cpp`. This is a concrete
   release-maintenance issue, not evidence that the current AU crashed. Keep
   StemDeck in its separate qualification lane.
5. **Analysis and vocal quality claims need narrower evidence.** The recorded
   Song Key benchmark is 57.6% exact / 65.12 MIREX on 1,337 labeled tracks, with
   confidence not calibrated; it is not near-perfect key detection. Auto Pitch's
   24/24 real-vocal harmony matrix proves bounded, finite, non-silent output at
   selected gains, not listener preference or naturalness. Mix's loudness/peak
   estimates and deterministic roles are not learned mixing or mastering meters.

## All 49 Products

The suite registry currently has 7 alpha, 2 validation, 39 experimental and
1 excluded product. Experimental does **not** mean unimplemented or untested:
most component plugins have real DSP contracts. It does mean the catalog has not
promoted them to shipping products. The rows below are recommendations, not
lifecycle edits. Every installed AU listed here passed this scan.

| Slug | Product | Recommendation / evidence focus |
|---|---|---|
| `align` | Sattari Align | Mix component; alignment contract, qualify real multi-mic use before separate release |
| `auto-pitch` | Sattari Auto Pitch | Priority 6; flagship candidate, listening and exact wrapper qualification open |
| `brain` | Sattari Compass | Priority 7; free authority, accuracy/confidence and real session acceptance open |
| `bus` | Sattari Bus | Mix component; core contract, no separate product launch yet |
| `chop` | Sattari Chop | Hold separate release; shared/Stack coverage and probe, no dedicated processor contract |
| `clean` | Sattari Clean | Mix component; DSP contract, real material and exact package qualification needed |
| `comp` | Sattari Comp | Mix component; dynamics contract, qualify timing/automation across hosts |
| `create` | Sattari Create | Priority 5; complete rack contract and workflows |
| `deess` | Sattari De-Ess | Vocal component; de-essing contract, real-vocal listening required |
| `double` | Sattari Double | Vocal component; doubling contract, mono/phase and listening qualification |
| `echo` | Sattari Echo | Create component; delay contract, host tempo and tail qualification |
| `eightoheight` | Sattari 808 | Create component; 808 contract, tuning/trigger/headroom session qualification |
| `eq` | Sattari EQ | Mix component; EQ contract, exact-format/automation qualification |
| `face` | Sattari Face | Vocal component; formant contract, real-voice artifact qualification |
| `gate` | Sattari Gate | Mix component; gate contract, real material/automation qualification |
| `glass-choir` | Sattari Glass Choir | Vocal component; choir contract, musical/CPU qualification |
| `granulator` | Sattari Granulator | Instruments component; substantial synthesis/lifetime coverage, source/host soak needed |
| `harmony` | Sattari Harmony | Vocal component; core contract, do not inherit Auto Pitch's separate harmony qualification |
| `heat` | Sattari Heat | Create component; saturation/alias/bypass contract, level-matched listening needed |
| `imager` | Sattari Image | Mix component; stereo/mono contract, translation and phase qualification |
| `maqam` | Sattari Maqam | Auto Pitch component; core contract, specialist intonation/listening acceptance |
| `meter` | Sattari Meter | Good later utility candidate within Mix; meter and editor contracts, calibration qualification |
| `mix` | Sattari Mix | Priority 3; latest complete build and separate automix qualification |
| `morph` | Sattari Morph | Hold separate release; real processor exists but no dedicated registered product contract |
| `multiband` | Sattari Multiband | Mix component; recombination/dynamics contract, phase and automation qualification |
| `peak` | Sattari Peak | Mix component; limiter contract, real overload/inter-sample behavior qualification |
| `pulse` | Sattari Pulse | Create component; modulation contract, tempo/transport qualification |
| `rhythm-genie` | Sattari Rhythm Genie | Instruments component; core and timing contracts, real MIDI/DAW qualification |
| `riser` | Sattari Riser | Create component; riser contract, transport and retrigger qualification |
| `royal-chain` | Sattari Royal Chain | Vocal component; its named contract measures color-stage aliasing, not the complete chain |
| `sattari-arp` | Sattari Arp | Priority 1; focused timing and package evidence; AU MIDI FX/VST3 only |
| `scaler` | Sattari Scaler | Instruments component; MIDI contract, actual host routing and stuck-note qualification |
| `scope` | Sattari Scope | Good later free utility within Compass; transparent analyzer/editor contracts |
| `shimmer` | Sattari Shimmer | Create component; shimmer contract, feedback/tail/listening qualification |
| `side-chain-master` | Side Chain Master | Mix component; core contract, real DAW sidechain routing qualification |
| `song-key` | Sattari Key | Priority 8; analysis evidence, confidence and accuracy acceptance open |
| `space` | Sattari Space | Create component; ambience contract, tails/tempo/listening qualification |
| `stack` | Sattari Stack | Priority 9; source recall fixed, media/host/long-session qualification open |
| `stemdeck` | Sattari StemDeck | Hold from first release; separate source/build/format/dependency qualification |
| `sub-conjurer` | Sub Conjurer | Priority 2; narrow low-end purpose and explicit package/DSP evidence |
| `transient` | Sattari Transient | Mix component; shaping contract, real drums and level-matched listening |
| `tuner` | Sattari Tuner | Good later free utility within Compass; pitch/transparency/editor contracts |
| `vocal` | Sattari Vocal | Priority 4; complete rack contract and vocal workflows |
| `vocal-master` | Sattari Vocal Master | Vocal component; core contract, avoid duplicating the rack as a launch product |
| `vocoder` | Sattari Vocoder | Create component; vocoder contract, real carrier/MIDI/sidechain and CPU qualification |
| `voxkey` | Sattari VoxKey | Instruments component; mapper/core contract, microphone-to-MIDI latency/tracking qualification |
| `voxsynth` | Sattari VoxSynth | Instruments component; MPE contract, complete playing/host/preset qualification |
| `warble` | Sattari Warble | Create component; degradation contract, musical listening/automation qualification |
| `warp` | Sattari Warp | Create component; processing contract, musical listening/host qualification |

## Recommended Next Gate

Freeze Arp, Sub Conjurer and Mix candidates; build and validate their exact
declared formats; complete a small supported-DAW matrix and listening panel;
then qualify signing, installation and update/recovery on a clean machine.
For the first paid full-product launch, focus on Mix rather than presenting
all 49 implementation plugins as equally mature standalone products. Keep
Auto Pitch as the next flagship qualification effort, not a claim of already
proven commercial superiority.
