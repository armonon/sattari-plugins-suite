# Compass and suite scenes

Sattari should behave like one musical system without becoming an opaque
mega-plugin. Compass is the conductor; specialized products remain independent
and opt into the context they can use truthfully.

## Compatibility

Sattari Compass keeps Brain's existing `brain` slug, product code, target,
state tree and parameter identifiers. The user-facing name changes; saved
sessions and automation contracts do not.

Sattari Key remains a focused detector. It can publish key and host tempo when
Compass is absent, but its lower-priority lease cannot overwrite a live Compass
frame.

## Context flow

1. Compass listens or accepts explicit values.
2. The user confirms key, chord, section, groove, energy and tuning.
3. Compass publishes one coherent snapshot.
4. Each consumer explicitly chooses which dimensions to follow.
5. Missing or stale context falls back to the product's local controls.

## First musical consumer

Sattari Arp is the first scene-aware consumer:

- **Follow groove** maps normalized Compass swing into Arp's safe 0–65% range.
- **Follow energy** shapes velocity and adds one octave in high-energy sections.
- The interface names the current section and shows the received energy/swing.

Both responses default off. Existing sessions remain sonically identical.

## Next consumers

1. Create: section snapshots for space, movement and transition intensity.
2. Vocal: visible harmony/ensemble scenes with explicit per-section capture.
3. Stack: per-section layer mute, level and range snapshots.
4. Mix: user-authored scenes only; never infer a channel's role from the master.

Scene changes must remain visible, reversible and independently editable. A
scene coordinates parameters; it never hides them.
