# Sattari Suite Consolidation

Date: 2026-10-02. Status: implemented development consolidation, not public-release
approval. This supersedes the six-product strategy in `six-product-suite.md`.

## Portfolio

| Offering | Decision | Current implementation |
| --- | --- | --- |
| Modular | Flagship platform | FX and Instrument binaries, one shared chain engine |
| Royal Chain | Separate flagship | Purpose-built vocal finishing workflow; not replaced by generic Vocal presets |
| Auto Pitch | Separate flagship | Tuning and existing harmony; specialist parity remains a separate gate |
| Mix | Separate flagship | Linked-track analysis and editable balance/EQ/dynamics proposals |
| Tools | Supporting bundle | Arp, Sub Conjurer, Side Chain, Align, Scaler |
| Compass | Foundation bundle | Compass, Key, Tuner, Scope, Meter |
| Vocal / Create | Modular collections | All ten native roster modules each, curated filters and starting chains |
| Chop / Granulator / Rhythm Genie | Modular instruments | Three native source engines across four independent layers |
| Ordinary effect wrappers | Module-first | Existing identities retained; removed from new standalone marketing |
| Stack / Vocal / Create / Bus / Vocal Master | Compatibility | Buildable legacy wrappers, no automatic removal or identity replacement |
| Harmony | Migration pending | Preserve original until chord-track/choir workflow parity is proven |
| Maqam | Specialist | Retained as an explicit specialist, not reduced to a scale preset |
| VoxKey / VoxSynth / Morph | Held | Not part of the new standalone launch push |
| StemDeck | Separate | Outside this consolidation's release qualification |

The storefront contains 16 active/specialist binaries, not 16 approved releases.
`config/products.json` schema 2 is authoritative. It covers all 50 plugin source
directories with 51 binary entries, including both Modular variants from one
directory. The Hub excludes StemDeck and keeps 50 product identities.

## Modular 0.2.0

- 22 native effects: Clean, EQ, Comp, Heat, Echo, Space, Pulse, De-Ess, Image,
  Double, Riser, Shimmer, Warble, Warp, Vocoder, 808, Gate, Face, Glass Choir,
  Transient, Multiband and Peak.
- Three native sources: Granulator, Chop and Rhythm Genie. Four layer instances
  have independent sample ownership, level, pan, key ranges, transpose and velocity.
- Eight serial effects, or the existing six-slot chain plus two-slot parallel
  return. Reorder and duplication respect the selected topology.
- Vocal, Create, Instruments, Mix and External library filters; fourteen starting
  chains. Factory chains are not a listening-approved commercial preset bank.
- Up to 64 normalized native control positions per node, with compact detailed
  layouts. Original quick controls and all previous 423 parameter positions and
  ranges remain unchanged. New fields append, totaling 906 parameters.
- State version 4 accepts and migrates versions 1-3. Old scenes and A/B states
  receive defaults for newly added fields; extra layers start empty and the old
  parallel-return topology remains intact.
- Vocal/Create schema-1 JSON preset-file import into Modular FX. Eight slot order,
  bypass and real module values are converted. Previously skipped duplicates stay
  bypassed. One undo restores the old rack and sample ownership. Unsupported active
  legacy LFOs and invalid files are rejected before rack mutation.
- Existing development VST3 instrument/effect hosting now fits four sources and
  eight effects. State, missing-plugin placeholders and relinking remain supported.
- Glass Choir prepares on selection or asynchronously following host automation;
  selected native and external engine latency participates in compensation.
- Native analog interface retains the Sattari logo. Current screenshots and the
  five-second native instrument render replace historical website assets.

## Compatibility Boundaries

This is a portfolio/content merge, not a blanket saved-session converter. Keep
legacy binaries installed for old sessions. No AU/VST3 IDs, names or old parameter
ranges are renamed. New Modular files are not backward-readable by older Modular
binaries. Imported legacy presets need listening comparison; the old and new racks
have different gain/latency paths and are not guaranteed to null.

Stack's complete preset state, dual-source granular editor, sample zones,
capture/MPE and full rhythm editor are not migrated. Harmony's chord-track and choir
workflow is not declared equivalent to Auto Pitch's existing harmony controls.
Bus and Vocal Master remain legacy, not falsely relabeled as DSP-identical Mix and
Royal replacements. Their retirement is a roadmap/default-distribution decision.

External hosting is trusted, in-process VST3 only, with mono/stereo main buses.
No AU/VST2/AAX hosting, crash isolation, multi-output/sidechain routing, forwarding
generated MIDI between plugins, or hosted-parameter macro/automation support.
Plugin binaries, licenses and external libraries are not embedded in presets.

## Hub, Website And Packaging

The catalog generator produces the C++ brand registry, Hub manifest, web catalog
and focused CI matrix. Validation requires both Modular variants, exact source
coverage, unique identities, current groups and reciprocal collection membership.

Default Hub suite/install-all actions exclude module-only, held and legacy entries.
Existing installed legacy receipts can still update. Explicit legacy installs
remain possible when a shipping profile exists. Packaging scripts select the
seven current alpha binaries by default; `SATTARI_INCLUDE_LEGACY=1` also includes
the three historical alpha racks. No installer was built or installed by this pass.
Traceable source and payload guards remain intact.
Modular manifest entries additionally require binary version 0.2.0 in Info.plist;
Hub staging/install and packaging reject stale 0.1.1 bundles. The locally tested
build is in `build/upgrade-modular`, not an automatic replacement of canonical
`build/all` distribution inputs or installed user plugins.

The main site, dedicated Modular page and legacy detail links use the consolidated
lineup. Public downloads stay closed. Web app assets are synchronized by its
existing `sync:public` build step; no remote deployment was performed.

## Verification

Evidence is local and limited to Apple silicon unless otherwise stated:

- Modular fresh Release build: AU, VST3 and standalone targets for FX/Instrument,
  copy-after-build disabled and ad-hoc signatures, not commercial distribution.
- Four native/hosting contracts: `build/upgrade-modular/merge-contracts.xml` and
  `build/upgrade-modular/Testing/Temporary/LastTest.log`.
- Strictness-10 VST3 logs: `build/upgrade-modular/merge-pluginval-fx.log` and
  `build/upgrade-modular/merge-pluginval-instrument.log`.
- Native renders: `build/upgrade-modular/modular-verification/{FX,Instrument}`.
- Hub safety contract: `build/hub-merge-contract.log`, 57 isolated safety checks.
- Catalog/package runner tests: 17 tests under `scripts/tests`; eight Modular
  packaging qualification tests under `plugins/modular/tests`.
- Responsive browser checks: `apps/sattari-web/scripts/verify-consolidation.mjs`,
  320/390/1440/1920-pixel viewports, images, portfolio filtering, collections,
  compatibility detail links and keyboard-accessible instrument tabs. Evidence
  and screenshots: `build/web-consolidation`.
- Site TypeScript check and production build, generated catalog synchronization,
  and shell syntax checks for the changed package scripts.

The manual Product Upgrade Builds workflow targets macOS universal and Windows
x64 for Modular, Mix and Royal. It was not dispatched and does not prove Windows
execution. No new Windows binary, universal/Intel qualification, AU host validation,
commercial signing, notarization, clean-machine installation, crash-isolation or
listening approval is claimed by this pass. These remain release gates alongside
licensing reconciliation and long-session CPU/memory/DAW qualification.

## Remaining Migration Work

1. Establish listening/feature parity for Stack's deeper source workflows before
   adding an importer or removing its compatibility binary.
2. Decide and implement Harmony chord-track/choir parity and Vocal Master specialist
   processing; retain originals until objective comparison passes.
3. Voice and audition the new Modular preset bank, qualify resource-heavy Glass
   Choir/808 chains, and test dense four-layer/hosted sessions in named DAWs.
4. Run the Windows pipeline and native testers, then universal Mac/AU qualification,
   commercial packaging, signing/notarization, licensing and clean-machine gates.

No Git commit, push, installer publication, remote workflow dispatch or website
deployment is included in this local implementation.
