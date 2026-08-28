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

## Control grammar

Every control in the suite is drawn by `SattariControlSurface.h`. The two
LookAndFeels are its only callers, so it is the single place the suite's look
lives — and the single place to change it.

- **Capsule geometry.** Tracks, buttons, switches and fields are fully rounded
  (radius = height/2). This is the trait that carries the look; a 9 px radius on
  a 20 px control reads as a different product.
- **Porcelain for anything you grab.** Thumbs, knob caps, buttons, combo fields.
  Bright, physical, one ambient plus one contact shadow.
- **A single-hue accent gradient for anything you read as a value.** Never a
  rainbow: a fill that shifts hue along its length makes two different settings
  look like two different colours, which is a lie about the data.
- **The thumb rides inside the track**, never straddling it, so a slider stays
  one clean capsule at any size and a row of them lines up.
- **The accent is a parameter, not a constant.** Products carry their own accent
  and every primitive takes it. A Create slot must not look like a Mix slot.

Accents: crimson (performance/transport), mint (default), violet (creative),
amber (sampler/slicing), blue (session/analysis).

Review a change with `SattariControlGallery`, which renders every control, in
every state, in every accent, to one PNG. It fails if a block paints nothing —
an empty tile is the failure mode a screenshot review misses, because empty
looks tidy.

## Visual rules

- Charcoal canvas with a subtle top lift, not decorative glass. Deliberately not
  near-black: porcelain controls need a surface their shadows register against.
- Elevated surfaces use one-pixel separators and a 16 px radius.
- Product color communicates selection, live state and authority.
- Text remains neutral; large colored headings are not product identity.
- Uppercase metadata is 9.5–10.5 pt. Primary values are 16 pt or larger.
- Controls use direct values and host-automation-compatible JUCE attachments.
- No fake hardware panels, screws, brushed metal, neon glow or ornamental meters.
- Empty and waiting states must say what the product needs next.

Alpha product accents establish the family:

- Compass: session blue.
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

### Compass

Compass is the suite authority, so its hero is the current musical context rather
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

### Meter, Scope, Tuner and Key

The compact analysis family shares exact metric cards, restrained plot frames,
truthful waiting states and live header status. Each product still leads with
its distinct decision surface: loudness statistics in Meter, spectrum and
stereo geometry in Scope, pitch centering in Tuner, and session authority in
Key. Default and minimum-size renders are seeded with deterministic audio so
the visual contracts exercise populated states rather than empty shells.

## Rollout sequence

1. Validate Compass, Arp and Sub Conjurer in Logic, Ableton and a high-DPI host.
2. Validate the migrated Mix, Create, Vocal and Stack rack workspaces in hosts,
   including narrow-window, preset and automation passes.
3. Validate the migrated Meter, Scope, Tuner and Key analysis workspaces,
   including long-session telemetry and display-scaling passes.
4. Establish instrument and sample templates for Chop, Granulator and VoxSynth.
5. Migrate remaining effects by workflow family, not alphabetical order.

Every migrated editor needs a PNG render contract, a minimum-size layout check,
its existing DSP/state contract, and host validation before its lifecycle may
advance.
