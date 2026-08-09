---
type: product-strategy
category: sattari
area: audio-suite
status: active
created: 2026-08-09
updated: 2026-08-09
---

# Sattari Audio — six-product suite

Truth label: **product architecture and qualification plan**. Product scope is
real in source; only Compass, Auto Pitch, Arp, and Sub Conjurer are installable
internal alphas. A component's presence in the catalog is not release evidence.

## Product line

| Product | Promise | Primary shell | Implementation components | Current state |
|---|---|---|---|---|
| Compass | Knows the session and makes musical context shared infrastructure. | Compass | Key, Tuner, Scope | Alpha |
| Auto Pitch | Tunes monophonic performance in realtime or as editable note objects. | Auto Pitch | Maqam | Alpha |
| Instruments | Makes playable and generated musical material. | Stack | Arp, Chop, Granulator, Rhythm Genie, Scaler, VoxKey, VoxSynth | Preview; Arp alpha |
| Vocal | Finishes a voice through one ordered, purpose-built workflow. | Vocal | Royal Chain, Harmony, Vocal Master, Double, De-Ess, Face, Glass Choir | Preview |
| Mix | Controls dynamics, tone, translation, timing, metering, and low end. | Mix | Sub Conjurer, Side Chain Master, Bus, Align, Transient, Meter, EQ, Clean, Comp, Peak, Multiband, Gate, Image | Preview; Sub Conjurer alpha |
| Create | Transforms sound while following key, time, section, and groove. | Create | Morph, Warp, Space, Vocoder, Shimmer, Echo, Heat, Riser, Pulse, 808, Warble | Preview |

## Intended session flow

1. **Compass listens and the user approves the session truth.** It detects
   major/minor key and reference tuning, reads host tempo, and combines that
   evidence with user-approved groove, maqam, sections, energy, and chords.
2. **Instruments generates or performs into that context.** Arp and future Stack
   layers create material without repeated key and tempo setup.
3. **Auto Pitch corrects performances.** Realtime correction handles speed;
   note-object editing handles decisions that need precision.
4. **Vocal turns the corrected performance into a finished vocal.** Cleanup,
   dynamics, tone, doubling, harmony, character, and space are ordered by intent.
5. **Mix controls the production.** Its behavior remains stable while its
   assistants, labels, and musically constrained choices can use session context.
6. **Create supplies transitions and signature moments.** Effects follow musical
   context so experimentation does not require constant corrective setup.

Compass should be a recommendation bus, not a hidden dictator. Every follower
needs a clear source indicator, a local override, and a one-click return to the
approved session value. A session must never leak into a different DAW project.

## Product discipline

- Six names are marketed. The 48 implementation plugins are technologies,
  modules, and optional specialist tools behind those six products.
- A rack earns release status only when its end-to-end workflow is better than
  loading its components separately.
- Shared visual language means consistent hierarchy, typography, interaction,
  metering, presets, A/B, undo, help, and accessibility—not identical layouts.
- No component graduates from experimental because it compiles. It graduates
  after functional contracts, machine format validation, manual host validation,
  owned-material listening, preset recall, automation, and performance evidence.
- Public distribution requires the commercial/open-source licensing decision,
  signed/notarized packaging, a verified update channel, support policy, and
  customer-facing documentation.

## Qualification sequence

### Gate A — four-plugin alpha

- Fresh Release builds and product contracts for Compass, Auto Pitch, Arp, and
  Sub Conjurer.
- `codesign --verify`, AU validation where applicable, and pluginval strictness 5.
- Logic, Ableton, and FL scan/insert/process/automation/recall/bounce passes.
- Auto Pitch scored on an owned/cleared real-vocal corpus.
- At least two non-developer testers complete a real project and return notes.

### Gate B — one complete six-product flow

- Stack, Vocal, Mix, and Create primary racks reach alpha independently.
- One reference session proves the entire Compass-to-Create flow with saved
  state, automation, offline bounce, four-instance load, and project isolation.
- Component controls exposed in racks remain controllable without producing a
  wall of sliders; basic intent comes first, detail appears on demand.

### Gate C — customer beta

- Product naming, presets, tutorials, diagrams, and Hub all describe the same six
  products.
- Installer, update, crash-reporting/diagnostics, support intake, and rollback
  behavior are exercised on clean machines.
- Licensing and dependency obligations match the intended business model.

### Gate D — public candidate

- Signed and notarized AU/VST3/app/package artifacts.
- Clean-machine install/update/uninstall evidence.
- Fixed release build, immutable hashes, known-issues list, support owner, and a
  go/no-go record based on evidence rather than schedule.

## Immediate order of work

1. Close the four-plugin alpha machine gates, then the manual DAW and listening
   gates; do not add flagship features while correction quality is unresolved.
2. Make Vocal the first complete rack. It is the clearest bridge from Auto Pitch
   to a finished result and the strongest way to demonstrate suite flow.
3. Make Stack the second complete rack around Arp and voice-to-MIDI; prove one
   compelling performance workflow before expanding its instrument count.
4. Reduce Mix to the smallest set of modules that audibly wins on a reference
   session; specialist components can stay optional.
5. Give Create three signature, Compass-aware transformations with immediate
   before/after value. Breadth comes after identity.
6. Run the commercial-license, notarization, clean-machine, and support work in
   parallel with Gate B so finished DSP is not stranded behind distribution.
