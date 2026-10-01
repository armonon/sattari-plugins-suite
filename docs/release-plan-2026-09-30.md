# Sattari Audio Suite: Inventory And Release Plan

Review date: 30 September 2026.

## Decision

**Do not launch the entire development suite as a finished bundle yet.** Qualify
Arp and Sub Conjurer for a small pilot, make Modular the first rack flagship,
then expand with Mix, Vocal and Create. Keep Auto Pitch, Royal Chain and Stack
on focused qualification tracks rather than attaching them to the first launch.

This is a ranking of release evidence, product scope and remaining risk, not a
sound-quality ranking. No blind listening panel or competitor comparison was
performed. No inspected package has all the evidence required for public release.

The source tree contains **50 plugin directories**. The master catalog contains
49 entries: **48 suite-visible products in six families, plus excluded StemDeck**.
The additional directory is Modular, which produces **two products, Modular FX
and Modular Instrument**. Directory counts, product counts and loadable rack
modules are different things; none of these numbers means 50 shipping plugins.

Scope: all plugin CMake definitions and catalog entries, release/test wiring,
existing qualification records, targeted rack/processor sources and candidate
manifests. This is not a line-by-line audit of every DSP implementation. Review
base was `76f692998e9d5e6375419a6121d5e32312696b73` with substantial existing
working-tree changes. Only this report was added by this review; plugin code,
installed bundles, licensing, lifecycle flags and distribution were not changed.

## Findings

1. **Release blocker: reconcile distribution licensing.**
   [LICENSING.md](../LICENSING.md) describes GPLv3, while the pinned JUCE 8.0.12
   [license](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md)
   specifies AGPLv3 or commercial terms. Confirm the owner's actual arrangement
   and required source/notices with the appropriate licensing reviewer before
   external distribution. This is an unresolved discrepancy, not a finding of
   infringement. [Modular's release gate](../plugins/modular/RELEASE.md) already
   records it.
2. **Release blocker: final distribution and real-host acceptance are incomplete.**
   Modular 0.1.1 has a Developer ID-signed universal candidate but is not
   notarized. Arp/Sub/Mix/Vocal/Create have an ad-hoc-signed universal candidate
   package. Supported-DAW acceptance, listening approval and clean-machine
   install/update tests remain open. Passing contracts, auval and pluginval is
   valuable but does not close these gates. See
   [candidate packaging](../packaging/candidates/README.md) and
   [Modular acceptance](../plugins/modular/RELEASE.md).
3. **P1: Modular is missing from the master catalog and its consumers.**
   [products.json](../config/products.json) has no Modular entry.
   [The sync checker](../scripts/check_brand_registry_sync.py) hardcodes the
   49-entry count, so a green catalog/Hub/web check does not detect this omission.
   The standalone website page is not equivalent to catalog or Hub integration.
   Register product variants, bundle membership and lifecycle explicitly; add a
   check that discovers uncatalogued plugin directories.
4. **P1: the build-first release aggregate omits Modular's contract targets.**
   [Modular CMake](../plugins/modular/CMakeLists.txt) registers two CTest cases
   but declares their executables `EXCLUDE_FROM_ALL`.
   [Root CMake](../CMakeLists.txt), around `SattariReleaseContracts`, does not
   depend on them. A clean gate can encounter missing executables; an existing
   build can run old binaries. Add both dependencies and consistent labels,
   then test the gate from a fresh build directory. Mix's AutoMix contract is
   now included, so the corresponding September 28 finding is resolved.
5. **P1 before their individual launches: incomplete product-level gate coverage.**
   [Morph](../plugins/morph/CMakeLists.txt) has no dedicated registered product
   contract. [Chop](../plugins/chop/CMakeLists.txt) has an onset probe and shared
   engine/Stack coverage, not a complete wrapper/editor/state contract.
   [Royal Chain](../plugins/royal-chain/CMakeLists.txt) now has real-processor
   tuning and UI render targets, but its newer tune contract is not part of
   the root release/CTest gate. Its local vocal render tool is explicitly not
   a CI gate. Do not describe Royal Chain as having only an aliasing test, or
   treat the existence of the newer executables as automatic release coverage.
6. **P2: StemDeck's separate release tooling has drifted.**
   `scripts/validate_sources.sh` fails on the removed
   `plugins/stemdeck/src/PluginHostManager.cpp`; current CMake uses HostGraph.
   Its README says 0.4.3 while CMake says 0.5.0. Keep this in a separate app
   qualification track and repair its tooling before that product's release.
7. **P2: repair a corrupted checksum in historical release documentation.**
   The SHA text in the [platform report](../validation/release/2026-09-28-platform-builds/README.md)
   contains non-hash text. The actual archive was rehashed successfully below.
   Correct the report from the verified artifact; do not treat prose as an
   integrity check. Historical evidence was left untouched in this review.

## What The Racks Actually Include

The six catalog families are product groupings, **not promises that every family
member can be loaded inside its namesake rack**. Current built-in rosters:

| Rack | Routing | Native contents |
| --- | --- | --- |
| Modular FX / Instrument | Six serial main effects, two serial effects in a post-rack parallel return, two instrument-source layers | Effects: Clean, EQ, Comp, Heat, Echo, Space, Pulse, De-Ess, Image. Sources: Granulator, Rhythm Genie |
| Mix | Eight serial slots | Clean, Comp, De-Ess, EQ, Gate, Heat, Image, Multiband, Meter, Peak, Pulse, Transient, Side Chain Master, Align |
| Vocal | Eight serial slots | De-Ess, Double, Face, Glass Choir, Clean, Gate, EQ, Comp, Heat, Space |
| Create | Eight serial slots | Echo, Heat, Pulse, Riser, Shimmer, Space, Warble, Warp, Vocoder, 808 |
| Stack | Four parallel instrument layers | Chop, Rhythm Genie, Granulator |

Sources: processor constructors for [Modular](../plugins/modular/src/PluginProcessor.cpp),
[Mix](../plugins/mix/src/PluginProcessor.cpp), [Vocal](../plugins/vocal/src/PluginProcessor.cpp),
[Create](../plugins/create/src/PluginProcessor.cpp) and [Stack](../plugins/stack/src/PluginProcessor.cpp).

Modular supports independently configured duplicate effects, eight macros with
two targets each, three scenes, A/B, undo/redo, presets and source recall.
It does **not** currently host external VST/AU binaries, load every Sattari
plugin, provide arbitrary patching, external sidechains, automixing or complete
deep editors for every module. It has no output limiter.

Mix/Vocal/Create use the older shared rack: one DSP instance per module type,
with duplicate occurrences inactive after the first active occurrence. Their
[processor](../core/include/sattari_audio_core/SattariRackProcessor.h) and
[editor](../core/include/sattari_audio_core/SattariRackEditor.h) explicitly handle
this limitation. Do not advertise these as freely duplicable Modular racks.

Mix also has a linked-track mixing assistant: explicit roles, analysis,
gain/EQ/dynamics proposals, locks, compare and keep/undo. It uses deterministic
role-based planning, not learned mixing or arrangement understanding. Its
active K-weighted level estimate is not integrated LUFS, and its peak estimate
is not true peak. Its designed 64-instance scope still needs real-host load
qualification. See [Mix README](../plugins/mix/README.md).

## Every Plugin: Launch Decision

These are recommendations, not catalog lifecycle changes. **Module-first**
means qualify it inside a rack where it is already implemented; do not assume
that also qualifies the separate plugin wrapper. **Hold standalone** does not
mean remove existing identifiers or break old sessions.

### Compass Family: 4

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Compass / `brain` | Session key/chord/context authority | Later free foundation; real-session isolation, source precedence and confidence calibration |
| Sattari Key / `song-key` | Key analysis | Pair with Compass; current-path accuracy and confidence acceptance, file/live host workflow |
| Sattari Tuner / `tuner` | Tuning display | Later free utility; tracking, display stability and transparent audio in hosts |
| Sattari Scope / `scope` | Audio analysis | Later free utility; calibrated display, transparent audio and UI load |

### Auto Pitch Family: 2

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Auto Pitch / `auto-pitch` | Live/Studio tuning, editing and harmony | Later flagship; fresh final wrappers, naturalness/listening, mode transitions, latency and host editing/recall |
| Sattari Maqam / `maqam` | Microtonal tuning | Specialist follow-up; expert intonation/listening, tuning presets and host recall |

### Instruments Family: 8

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Stack / `stack` | Four-layer instrument rack | Later instrument release; media portability, missing-file recovery, layer timing and memory soak |
| Sattari VoxKey / `voxkey` | Voice-to-MIDI | Hold standalone; real voice tracking, latency, MIDI routing and stuck-note tests |
| Sattari VoxSynth / `voxsynth` | Voice instrument | Hold standalone; complete playing, MPE, presets and host qualification |
| Sattari Rhythm Genie / `rhythm-genie` | Rhythm/MIDI/drum generation | Module-first in Modular/Stack; transport, timing and real MIDI host sessions |
| Sattari Chop / `chop` | Slicing sampler | Module-first in Stack; add standalone processor/editor/state gate before separate launch |
| Sattari Arp / `sattari-arp` | MIDI arpeggiator | First pilot candidate; actual host routing, latch, transport, automation, recall and note-off behavior |
| Sattari Scaler / `scaler` | Scale-oriented MIDI utility | Hold standalone; routing, note mapping, transport and stuck-note acceptance |
| Sattari Granulator / `granulator` | Granular instrument | Module-first in Modular/Stack; sample ownership, recall, import limits and sustained CPU/memory |

### Vocal Family: 8

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Vocal / `vocal` | Vocal finishing rack | Expansion wave; fresh package, real-vocal presets, automation, tails, recall and bounce |
| Sattari Royal Chain / `royal-chain` | Integrated vocal chain | Later flagship candidate; wire new processor checks into gate, whole-chain listening and host qualification |
| Sattari Harmony / `harmony` | Vocal harmony | Hold standalone; own processor/listening evidence, not inherited from Auto Pitch's harmony tests |
| Sattari Vocal Master / `vocal-master` | Vocal finishing | Hold standalone; clarify distinct purpose against Vocal/Royal Chain and qualify full chain |
| Sattari Double / `double` | Vocal doubling | Module-first in Vocal; mono translation, phase and vocal listening |
| Sattari De-Ess / `deess` | Sibilance control | Module-first in Modular/Vocal/Mix; real-vocal detection, transparency and extreme settings |
| Sattari Face / `face` | Vocal character/formants | Module-first in Vocal; real-voice artifacts and automation |
| Sattari Glass Choir / `glass-choir` | Creative choir effect | Module-first in Vocal; musical output, CPU and feedback/headroom |

### Mix Family: 14

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Mix / `mix` | Mixing rack and linked-track assistant | Next major expansion; real multitracks, group isolation, proposal/undo and multi-instance performance |
| Sub Conjurer / `sub-conjurer` | Low-end generation/following | First pilot candidate; kick/bass listening, transitions, gain/headroom and sustained host sessions |
| Side Chain Master / `side-chain-master` | Dynamics/ducking | Module-first in Mix; verify supported routing in each host, not assumed external sidechain support in every rack |
| Sattari Bus / `bus` | Bus processing | Hold standalone; define distinct value and qualify bus dynamics, automation and headroom |
| Sattari Align / `align` | Timing alignment | Module-first in Mix; multi-mic accuracy, polarity and recall |
| Sattari Transient / `transient` | Transient shaping | Module-first in Mix; real drums, gain matching and attack behavior |
| Sattari Meter / `meter` | Metering | Module-first in Mix; calibrate each claimed measurement before separate utility release |
| Sattari EQ / `eq` | Equalization | Module-first in Modular/Mix/Vocal; response, automation and extreme parameter behavior |
| Sattari Clean / `clean` | Cleanup/restoration | Module-first in Modular/Mix/Vocal; real noisy material and artifact acceptance |
| Sattari Comp / `comp` | Compression | Module-first in Modular/Mix/Vocal; timing, gain, automation and listening |
| Sattari Peak / `peak` | Peak control/limiting | Module-first in Mix; overload and inter-sample behavior before any mastering claims |
| Sattari Multiband / `multiband` | Multiband dynamics | Module-first in Mix; recombination, phase, crossover and automation |
| Sattari Gate / `gate` | Gating | Module-first in Mix/Vocal; chatter, tails, real material and automation |
| Sattari Image / `imager` | Stereo imaging | Module-first in Modular/Mix; mono compatibility, phase and translation |

### Create Family: 12

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Sattari Create / `create` | Creative effects rack | Expansion wave; fresh package, tempo changes, feedback/tails, presets and host sessions |
| Sattari Morph / `morph` | Voice character | Hold standalone; add processor/editor/state contracts and real-voice listening; not in Create's rack roster |
| Sattari Warp / `warp` | Creative vocal processing | Module-first in Create; listening, extreme settings and host automation |
| Sattari Space / `space` | Reverb/ambience | Module-first in Modular/Vocal/Create; tails, tempo, recall and CPU |
| Sattari Vocoder / `vocoder` | Vocoding | Module-first in Create; verify actual carrier/MIDI/routing options, CPU and intelligibility |
| Sattari Shimmer / `shimmer` | Shimmer ambience | Module-first in Create; feedback stability, tails and musical listening |
| Sattari Echo / `echo` | Delay | Module-first in Modular/Create; tempo changes, feedback, tails and automation |
| Sattari Heat / `heat` | Saturation | Module-first in Modular/Mix/Vocal/Create; level-matched character, aliasing and bypass |
| Sattari Riser / `riser` | Riser effects | Module-first in Create; transport, retrigger and headroom |
| Sattari Pulse / `pulse` | Rhythmic modulation | Module-first in Modular/Mix/Create; requalify affected racks after the current shared-source change |
| Sattari 808 / `eightoheight` | 808/low-end effect | Module-first in Create; trigger/tuning, output level and host recall |
| Sattari Warble / `warble` | Lo-fi/tape modulation | Module-first in Create; musical listening, automation and extreme settings |

### Outside The Current Suite Catalog: 2 Directories

| Product / directory | Purpose | Recommendation and next evidence |
| --- | --- | --- |
| Modular FX + Modular Instrument / `modular` | Analog-styled modular rack | First rack flagship candidate; strongest current exact-package evidence, but catalog, root gate, licensing, DAWs and distribution remain open |
| Sattari StemDeck / `stemdeck` | DJ/remix application | Separate release program; fix source/tooling/version drift and qualify its own formats, dependencies and workflows |

## Candidate Evidence

| Candidate | Existing evidence | Still not proven |
| --- | --- | --- |
| Modular 0.1.1 | Universal macOS; Developer ID signatures; recorded pluginval 10 and auval on arm64/x86_64; exact source and payload manifests; two native contracts rerun successfully today | Notarization, supported DAWs, clean installation, owner listening, sustained performance and Windows |
| Arp 0.1.1 | September 28 universal AU MIDI FX/VST3 package; auval, pluginval 5 and UI checks | Public signing/notarization, clean installation and host MIDI acceptance |
| Sub Conjurer 0.1.1 | Same batch; AU/VST3/standalone, processor/validator/UI evidence | Real material, sustained host use and public distribution |
| Mix 0.2.1 | Same batch; AU/VST3/standalone plus separate linked-track qualification | Fresh package after shared changes, real multitracks and measured host load |
| Vocal 0.1.1 | Same batch; AU/VST3/standalone and product/UI evidence | Fresh-source qualification, real-vocal/DAW and distribution acceptance |
| Create 0.1.1 | Same batch; AU/VST3/standalone and product/UI evidence | Fresh package after shared changes, musical/DAW and distribution acceptance |

The five-product package records 50 passing qualification commands, not 50
released products. Its signatures are ad hoc. Modular's Intel runtime evidence
uses Rosetta, not a physical Intel Mac. Existing versions do not prove that
every installed wrapper contains the latest shared source.

The [Windows candidate run](https://github.com/armonon/sattari-plugins-suite/actions/runs/36530442179)
was rechecked: failed, with no executed job steps. The local platform report
attributes the block to GitHub billing/spending restrictions. No qualified
Windows artifact was established by this review. Native Windows CI already
exists in [candidate-platforms.yml](../.github/workflows/candidate-platforms.yml)
and [modular-platforms.yml](../.github/workflows/modular-platforms.yml); resolve
runner availability, run those workflows, then qualify the outputs on Windows.

## Release Sequence And Gates

### Stage 0: Fix The Release Foundation

Owner: engineering plus product/licensing owner.

- Resolve license terms and notices for the exact dependencies and distribution model.
- Register Modular variants and bundle contents in catalog, Hub and website; keep product IDs and old session compatibility stable.
- Fix Modular build-first dependencies; add Royal Chain's required checks before its release; test inventory coverage from a clean build directory.
- Choose explicit supported OS versions, formats and DAW versions. Keep untested configurations out of the support promise.
- Freeze source and dependencies for each candidate; record hashes and a reproducible build recipe. Any relevant shared-source change invalidates the affected candidate's qualification.

Exit: an agreed launch scope and a fresh-build gate that cannot quietly skip a
shipping product. StemDeck-specific work can remain on its independent track.

### Stage 1: Generate Exact Platform Candidates

Owner: engineering/release owner.

- macOS: universal AU and VST3 where supported; standalone only where useful and implemented. Arp's AU is MIDI FX, not a conventional audio effect.
- Windows: native x64 VST3 and selected standalone applications. AU is not a Windows format. Do not promise AAX as part of this plan.
- Build contracts before running them; validate every final format and architecture. Target pluginval strictness 10 for the launch set and document any justified exception explicitly.
- Record source hash, toolchain, dependencies, commands, logs, package hash, signatures and the runtime architecture actually tested.
- Complete Sattari logo/name/version consistency and analog UI review for launch products, with desktop scaling, minimum window size, contrast and all controls checked. This review did not certify every plugin's branding.

Exit: reproducible private candidates, not public downloads.

### Stage 2: Real DAW And Sound Acceptance

Owner: QA/testers plus product owner for listening approval.

- Proposed initial host matrix: Logic Pro AU on macOS, and REAPER/Ableton Live VST3 on both platforms. Record exact versions; narrow the matrix if the team cannot support it.
- Test scanning, audio/MIDI routing, automation, bypass, editor lifecycle, presets, save/reopen, tempo/transport, freeze and offline bounce.
- Test 44.1/48/96 kHz and 64/128/512/1024-sample buffers where the host/device supports them; measure latency, clipping, callback deadlines and memory.
- Run a proposed minimum two-hour session and 8/16-instance stress cases on declared minimum hardware. Test Mix at its advertised maximum linked-track count before making that promise.
- Use real vocals, drums, bass, instruments and complete mixes. Gain-match comparisons, log artifacts and have the owner approve musical output. Finite/non-silent output is not a listening pass.
- For Modular: independent duplicates, source import/reorder/undo, scenes, macros, return routing, old presets, high-feedback chains and media recall. For Arp: routing/latch/note-off. For Sub: pitch following and headroom.
- For later products: Mix group isolation and proposal/undo; Auto Pitch and Royal Chain tuning naturalness and wet/dry timing; Stack missing-media recovery and source portability.

Exit: zero unresolved release-blocking or high-severity defects, named test
sign-off and retained sessions/results tied to the exact package hash. These
durations and workloads are proposed gates, not results already achieved.

### Stage 3: Distribution Acceptance

Owner: release owner plus QA.

- macOS: Developer ID signing, notarization/stapling, clean-machine installation, permission prompts, update and rollback tests.
- Windows: qualify signing/reputation behavior, installer paths, DAW rescan, clean-machine installation, update and uninstall without losing user presets.
- Verify sample/preset rights, required license/source notices, manuals, supported-system text, known limitations and support contact.
- Exercise Hub install/update behavior only for products actually distributed through Hub. Avoid advertising unfinished entitlement or delivery paths.
- Ensure the public site, installer, manifest and product window agree on names, versions, formats and included effects. No unsupported download buttons or release claims.

Exit: the exact downloadable package is approved, not merely its development
build. A different OS package needs its own approval.

### Stage 4: Controlled Launch, Then Expansion

1. **Pilot:** Arp and Sub Conjurer, to a small named tester cohort after applicable licensing and candidate gates. Prove installation, support and update recovery.
2. **First flagship:** Modular FX + Modular Instrument. Offer it as its own product first; an optional starter bundle with Arp/Sub is a packaging proposal, not current entitlement.
3. **Suite expansion:** Mix, then Vocal and Create after fresh shared-code qualification. Sell these on clear workflows and actual native rosters.
4. **Specialist/flagship follow-ups:** Auto Pitch, Royal Chain, Compass/Key and Stack, ordered by which closes its own quality gates first. Add Tuner/Scope as qualified utilities.
5. **Long tail:** release additional standalone component plugins only when demand and independent wrapper qualification justify the support burden. Keep StemDeck separate.

Prefer a qualified macOS-first launch if Windows gates remain blocked. If
simultaneous platforms are a commercial requirement, hold the launch until both
pass. Do not announce a calendar date until Stages 0-2 produce stable candidates.

## Positioning

Lead with **Sattari Modular: your Sattari effects and instruments in one analog
rack**, followed by an explicit supported module list. Position Mix as assisted
mixing with linked tracks, not autonomous mixing or a replacement for mastering
meters. Present the six families as an expanding product range, not a promise
that every experimental component is bundled, supported and ready today.

Do not claim "best in the industry", competitor superiority, complete external
plugin hosting, all-Sattari loading, zero latency or certified Windows support
without the corresponding evidence. A focused, reliable first release is a
stronger proposition than 50 unevenly qualified plugin entries.

## Checks Performed In This Review

| Check | Result and boundary |
| --- | --- |
| Source/catalog inventory | 50 plugin directories; 49 catalog entries; Modular absent from catalog |
| `check_brand_registry_sync.py` | Pass: catalog 49, Hub 48, web 48, CI 12; does not detect Modular omission |
| `check_rack_presets.py` | Pass: 264 references across 28 modules; not musical listening or all Modular preset qualification |
| General script unit tests | 12/12 pass; intentional failure output in negative fixtures is expected |
| Modular packaging unit tests | 7/7 pass |
| Modular source manifest | 4,559 recorded input hashes match; zero missing or changed inputs |
| Modular payload manifest | 86 recorded file hashes match |
| `ctest --test-dir build/modular --output-on-failure` | 2/2 pass; existing qualified binaries, not a new build in this review |
| Root Ninja dependency inspection | Modular contracts absent from release aggregate; Mix AutoMix present |
| `validate_sources.sh` | Fails on stale StemDeck source path |
| Windows workflow status | Failed; no executed job steps |

Rehashed archives:

- Five-product macOS candidate: `782ab88210b1042609171412075344fedc2c7ab4cfefa7ad82045de8a20d3d44`.
- Modular 0.1.1 macOS candidate: `bfbd0e4b8a00c7dd69308fe580c970c1fab14c9d66adc7ef343da6f7e0ca64b4`.

The [September 28 scan](../validation/release/2026-09-28-suite-scan/README.md)
records 49 installed AU passes and 115 cached contract passes at that time.
Those historical passes were not rerun suite-wide here and do not certify
today's entire dirty source tree. The review also used the
[platform candidate record](../validation/release/2026-09-28-platform-builds/README.md),
[Modular release gate](../plugins/modular/RELEASE.md),
[Mix qualification](../validation/mix/2026-09-28-automix/README.md) and
[Auto Pitch harmony evidence](../validation/auto-pitch/2026-09-21-harmony-fixes.md).

The next concrete work is Stage 0, followed by exact candidate qualification.
This plan is not a public release approval.
