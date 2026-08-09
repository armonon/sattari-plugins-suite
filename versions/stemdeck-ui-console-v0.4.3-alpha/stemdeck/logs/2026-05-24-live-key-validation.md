# StemDeck v0.3.5 live-key validation

Truth label: **internal alpha validation evidence**. This is not sale-ready evidence by itself; real DAW/listening tests are still required.

## What changed

- Bumped StemDeck to v0.3.5.
- Added per-deck `LiveKey` parameter and UI toggle.
- Added live key-window analysis around each deck's current playhead.
- When LiveKey is enabled and confidence is available, target-key matching uses the current playback-window key instead of only the load-time full-track key.
- Manual pitch shifting and target-key matching continue to route through Preserve/granular key-lock when enabled, or through the resample fallback when Preserve is off.
- Added thread-safe live-key result atomics so the audio callback can read the latest live key class/confidence without reading UI-owned structs.
- Added deterministic contract coverage that verifies live key-window analysis returns a usable key/confidence result, alongside beat-sync and granular-render checks.
- Updated README, sale-readiness notes, and alpha package naming to `0.3.5-live-key`.

## Current feature truth

- LiveKey analyzes the loaded full-mix window, not separated stems and not live audio input yet.
- The detector is a lightweight chroma/Krumhansl implementation, not commercial-grade polyphonic key detection.
- LiveKey can follow modulating sections better than a single load-time key, but it needs real-song validation for false positives, breakdowns, percussion-heavy sections, and UI/CPU behavior.
- Preserve-on still uses the internal granular key-lock backend; Preserve-off still uses resample fallback.
- Needs DAW/listening validation before sale-candidate language.

## Verification run

Command:

```bash
./scripts/validate_stemdeck.sh
```

Result: passed.

Evidence from output:

- `StemDeck granular render contract ok`
- Contract included granular key-lock render checks, beat-sync BPM/grid-position check, and live key-window analysis check.
- `pluginval --validate-in-process --strictness-level 5` passed.
- pluginval identified `Sattari StemDeck v0.3.5`.
- Codesign verification passed for VST3 and Standalone artefacts.

## Package

Command:

```bash
./scripts/package_stemdeck_alpha.sh
```

Result: passed.

Package outputs:

- `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.5-live-key`
- `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.5-live-key.tar.gz`

## Next recommended DAW pass

1. Load harmonically simple loops and full songs with known keys.
2. Compare load-time key vs LiveKey display during intro/drop/breakdown sections.
3. Set target key on each deck, enable Preserve + LiveKey, and listen for musical/key-shift artifacts.
4. Repeat with Preserve off to document resample behavior honestly.
5. Test modulating songs to confirm LiveKey updates are useful rather than jumpy.
6. Validate CPU/UI responsiveness with four decks enabled.
