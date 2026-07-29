# Non-StemDeck suite release plan

This plan is generated from the July 2026 suite overhaul. StemDeck is explicitly
outside this program and keeps its separate build, dependency and validation
track.

## Product shape

Do not launch 48 products as though they are equally mature. The public suite
should be understood in three layers:

1. **Flagships** — a small set with direct value and complete evidence.
2. **Racks** — Mix, Create, Vocal and Stack as the primary way to use the shared
   module library.
3. **Modules and utilities** — standalone shells remain available for saved
   sessions and focused workflows, but are not all top-level launch stories.

The canonical lifecycle and release profile live in `config/products.json`.
Changing a lifecycle there regenerates C++ branding, Hub metadata, website
visibility and the focused CI matrix.

## Release sequence

### Internal alpha

- Sattari Brain
- Sattari Arp
- Sub Conjurer

This set is intentionally small. It tests the session-authority concept, a
MIDI/creative product and an audio effect without depending on the still-open
real-vocal or rack-usability studies.

### Validation candidates

- Sattari Key: expand the labeled key corpus and publish reproducible metrics.
- Sattari Auto Pitch: run the real-vocal corpus, monitor latency, and complete
  blind listening against dry/reference renders.

### Rack preview

- Mix, Create and Vocal: complete module-inspector usability and preset migration
  sessions in at least two DAWs.
- Stack: validate per-layer Chop/Granulator source recall, keyboard splits,
  latency alignment and source-missing recovery.

## Evidence required before a lifecycle promotion

Every promoted product needs an evidence record containing:

- clean universal build of AU, VST3 and Standalone artifacts;
- its synthetic contract and render smoke profile;
- pluginval and `auval` logs for the exact packaged bundle;
- codesign verification, Developer ID signing, notarization and stapling;
- DAW instantiate/save/reload/automation tests in the supported host matrix;
- listening or task-based evaluation appropriate to the product;
- complete corresponding source link and bundled GPL/upstream notices;
- artifact SHA-256 and byte size in the update manifest.

No website or Hub status may infer these results from a bundle directory or from
another product's validation.

## Session protocol policy

Brain is the highest-authority publisher. Song Key, Auto Pitch and Granulator
are leased fallbacks in that order. Consumers receive one coherent snapshot from
one shared poller per plugin binary.

Do not add more SessionState fields until at least one shipping consumer uses
them musically. Chord, section, energy, maqam and progression are context
vocabulary, not value on their own.

## External gates still requiring owner credentials or human material

- Apple Developer ID certificates and notarytool profile.
- Signed, immutable archive hosting and integrity-pinned feed publication.
- Real vocal, full-mix and DAW-session corpora with redistribution permission.
- Listening panel decisions and supported-host/version policy.
- A tagged, pushed source commit matching every distributed binary.
