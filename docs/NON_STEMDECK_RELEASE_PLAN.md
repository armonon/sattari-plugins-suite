# Non-StemDeck suite release plan

This plan is generated from the July 2026 suite overhaul. StemDeck is explicitly
outside this program and keeps its separate build, dependency and validation
track.

## Product shape

Do not launch 48 products as though they are equally mature — and do not present
48 things at all. The 48 plugins are the IMPLEMENTATION. What anyone installs,
buys or reasons about is six products, each a complete job rather than a shelf
of parts:

| Product | Verb | Tier | Plugins | What it is |
|---|---|---|---|---|
| **Compass** | knows | free | 4 | The session authority. Absorbs Key, Tuner, Scope. |
| **Auto Pitch** | tunes | paid | 2 | Vocal tuning. Absorbs Maqam. |
| **Instruments** | makes | paid | 8 | Everything that generates material — MIDI, voice-to-MIDI, playable instruments. |
| **Vocal** | sings | paid | 8 | The vocal chain. Absorbs Harmony, Vocal Master, Royal Chain. |
| **Mix** | controls | paid | 14 | Dynamics, tone, metering. |
| **Create** | transforms | paid | 12 | Session-aware creative FX. |

Compass is free on purpose: every install is a node in the session network and
makes everything else work better. That is the distribution mechanism.

Each product names exactly one PRIMARY plugin — the binary you launch to be "in"
that product. Everything else is a component of it. A plugin belongs to exactly
one product or is explicitly excluded; an unassigned plugin is a product with no
shelf to sit on, which is how a catalog drifts back to being a list of 48 things.

**Nothing is deleted to achieve this.** Product codes, parameter identifiers,
state trees and saved sessions are untouched — the grouping is a data field
(`suite` / `suiteRole`) in `config/products.json`, and every consumer regenerates
from it. `scripts/check_brand_registry_sync.py` fails if a plugin escapes a
product or a product gains a second primary.

The canonical lifecycle and release profile live in the same file. Changing a
lifecycle there regenerates C++ branding, Hub metadata, website visibility and
the focused CI matrix.

## Release sequence

### Internal alpha

- Sattari Compass
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

Compass is the highest-authority publisher. Its compatibility-safe internal
publisher id remains `brain`. Song Key, Auto Pitch and Granulator
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
