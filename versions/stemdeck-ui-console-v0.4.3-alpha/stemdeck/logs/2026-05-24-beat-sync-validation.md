# StemDeck v0.3.4 beat-sync validation

Truth label: **internal alpha validation evidence**. This is not sale-ready evidence by itself; real DAW/listening tests are still required.

## What changed

- Bumped StemDeck to v0.3.4.
- Added global `beatSync` / Host Beat/Grid Sync parameter and UI toggle.
- Added per-deck `deck[A-D]BeatOffset` parameter and visible `GRID` slider for manual beat-phase nudging.
- Beat Sync now uses host PPQ position plus each deck's individual Source BPM to align deck playback to the host beat grid.
- Existing Host Sync still controls tempo ratio from host BPM / deck Source BPM.
- Added a deterministic contract test that confirms beat sync jumps to the expected per-deck BPM beat-grid position, in addition to the existing granular key-lock render checks.
- Updated README, sale-readiness notes, and alpha package naming to `0.3.4-beat-sync`.

## Current feature truth

- This is **not** automatic transient/downbeat detection or full DJ beatgrid editing yet.
- The user still enters/adjusts each deck's Source BPM.
- Beat Grid Offset handles manual alignment corrections in beats.
- Preserve-on still uses the internal granular key-lock backend; Preserve-off still uses resample fallback.
- Needs real-song listening and DAW matrix validation before sale-candidate language.

## Verification run

Command:

```bash
./scripts/validate_stemdeck.sh
```

Result: passed.

Evidence from output:

- `StemDeck granular render contract ok`
- Contract included beat-sync BPM/grid-position check plus granular key-lock checks.
- `pluginval --validate-in-process --strictness-level 5` passed.
- pluginval identified `Sattari StemDeck v0.3.4`.
- Codesign verification passed for VST3 and Standalone artefacts.

## Package

Command:

```bash
./scripts/package_stemdeck_alpha.sh
```

Result: passed.

Package outputs:

- `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.4-beat-sync`
- `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.4-beat-sync.tar.gz`

## Next recommended DAW pass

1. Insert VST3 in Ableton/Reaper/FL.
2. Load two loops with known different BPM values.
3. Set each deck's Source BPM correctly.
4. Turn on Host Sync and Beat Sync.
5. Adjust Beat Grid Offset until the downbeats line up.
6. Test host tempos 90/120/140 BPM.
7. Repeat with Preserve on/off and document drift/artifacts.
