# Sattari Studio UI System

Sattari Studio is the interface architecture for the non-StemDeck product
suite. It is designed to feel quiet, fast and immediately understandable while
keeping expert controls close at hand.

The goal is not to imitate macOS controls. The useful Apple qualities are
hierarchy, restraint, legibility, direct manipulation, excellent defaults and
consistent behavior.

## Product grammar

Every product uses the same three layers:

1. **Command header** — product identity, role and truthful live status.
2. **Primary workspace** — the product's unique musical job.
3. **Control surface** — grouped automation-ready parameters and detail.

The shared implementation lives in
`core/include/sattari_audio_core/SattariStudio.h`.

## Visual rules

- Near-black canvas with a subtle top lift, not decorative glass.
- Elevated surfaces use one-pixel separators and a 14 px radius.
- Product color communicates selection, live state and authority.
- Text remains neutral; large colored headings are not product identity.
- Uppercase metadata is 9.5–10.5 pt. Primary values are 16 pt or larger.
- Controls use direct values and host-automation-compatible JUCE attachments.
- No fake hardware panels, screws, brushed metal, neon glow or ornamental meters.
- Empty and waiting states must say what the product needs next.

Alpha product accents establish the family:

- Brain: session blue.
- Sattari Arp: performance violet.
- Sub Conjurer: synthesis mint.

## Interaction rules

- Editors are resizable and declare useful minimum and maximum sizes.
- Every interactive control has a title and accessible description.
- Custom controls must support keyboard focus and activation.
- State changes use restrained color and live text, not continuous decoration.
- Animations and visual telemetry read coherent lock-free snapshots. They never
  lock, allocate or call the filesystem from the audio thread.
- Existing parameter identifiers, state trees, preset values and automation
  behavior are preserved through visual migrations.

## Reference workspaces

### Brain

Brain is the suite authority, so its hero is the current musical context rather
than a parameter wall. Confidence, tempo, reference and meter are visible
without editing. Explicit overrides and published session context are distinct.

### Sattari Arp

Arp visualizes the generated 16-step result, then places key constraints, rate,
pattern and velocity beside it. Performance shaping is a compact macro row. The
visual sequence is truthful: it previews the algorithm and highlights live
steps; it does not imply per-step editing that the engine does not provide.

### Sub Conjurer

Sub Conjurer leads with its generated signal and four tone-defining controls.
Tracking, key following, response and limiter behavior sit in the detail
surface. Live pitch and input/output metrics come from the processor snapshot.

### Mix, Create and Vocal

The effect-rack products share an eight-slot serial workspace. The ordered
signal chain stays visible on the left and the selected module's complete
automation surface stays visible on the right. Accent color identifies the
workflow; it does not alter the interaction model.

### Stack

Stack uses the same command language but a parallel workspace. Each layer keeps
instrument choice, mute, level, pan, key range and transpose together as one
playable unit. Source loading and the selected instrument's detailed controls
remain contextual to the focused layer.

## Rollout sequence

1. Validate Brain, Arp and Sub Conjurer in Logic, Ableton and a high-DPI host.
2. Validate the migrated Mix, Create, Vocal and Stack rack workspaces in hosts,
   including narrow-window, preset and automation passes.
3. Establish compact utility and meter templates for analysis products.
4. Establish instrument and sample templates for Chop, Granulator and VoxSynth.
5. Migrate remaining effects by workflow family, not alphabetical order.

Every migrated editor needs a PNG render contract, a minimum-size layout check,
its existing DSP/state contract, and host validation before its lifecycle may
advance.
