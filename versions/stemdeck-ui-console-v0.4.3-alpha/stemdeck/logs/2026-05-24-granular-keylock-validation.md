# StemDeck granular key-lock validation — 2026-05-24

Truth label: **internal-alpha implementation**, not sale-ready.

## Result

StemDeck v0.3.3 now has a first internal pitch-preserving playback backend behind the existing per-deck `Preserve` control.

When Preserve is enabled and host-sync tempo or key/pitch shifting is active, `TimePitchEngine` routes deck playback to a two-grain Hann overlap granular key-lock path. The deck content timeline advances at the requested tempo while the local grain read position preserves pitch separately; pitch/key moves use the granular path instead of the old single playback-rate multiplier. With Preserve off, StemDeck keeps the older resample fallback.

## Changed

- `TimePitchDecision` now carries clamped tempo ratio, pitch ratio, pitch semitones, fallback source-rate multiplier, backend status, and routing flags.
- `DeckPlayer` now accepts the full `TimePitchDecision` rather than a single playback-rate scalar.
- Added wrapped source-position helpers and Hann-window overlap rendering for granular key-lock playback across prepared stem lanes.
- Bumped StemDeck to v0.3.3 and updated alpha package naming to `0.3.3-granular-keylock`.
- Updated StemDeck README/package truth labels so this is described as an internal alpha backend that still needs listening/DAW validation.

## Verification

From `/Users/agentsattari/.openclaw/workspace/repos/sattari-plugins`:

```bash
./scripts/package_stemdeck_alpha.sh
git diff --check
python3 - <<'PY'
from pathlib import Path
files = [
    'plugins/stemdeck/CMakeLists.txt',
    'plugins/stemdeck/README.md',
    'plugins/stemdeck/src/DeckPlayer.h',
    'plugins/stemdeck/src/DeckPlayer.cpp',
    'plugins/stemdeck/src/MainPluginProcessor.cpp',
    'plugins/stemdeck/src/TimePitchEngine.h',
    'plugins/stemdeck/src/TimePitchEngine.cpp',
    'scripts/package_stemdeck_alpha.sh',
]
for f in files:
    text = Path(f).read_text()
    assert text.endswith('\n'), f
    assert all(line.rstrip() == line for line in text.splitlines()), f
print('touched text newline/trailing-whitespace ok')
PY
```

`package_stemdeck_alpha.sh` reran the full StemDeck gate:

- Source layout check passed.
- CMake configure/build passed for Standalone and VST3.
- VST3 and Standalone ad-hoc codesign verification passed.
- pluginval strictness 5 passed and identified `Sattari StemDeck v0.3.3`.
- Alpha package generated at:
  - `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.3-granular-keylock`
  - `/Users/agentsattari/.openclaw/workspace/deliverables/sattari-test-builds/stemdeck-plugin-alpha-v0.3.3-granular-keylock.tar.gz`
- `git diff --check` passed.
- Touched text newline/trailing-whitespace inspection passed.

## Boundaries / still blocked

- No AI/on-device/live stem separation was added.
- No hosted-plugin audio/MIDI routing was added.
- No manual DAW matrix or real-song listening pass was completed.
- No installer, notarization, EULA, payment, product page, public launch, or sale-ready claim was made.
- The granular key-lock is an internal alpha algorithm; it is build/pluginval-stable but still needs listening validation before it can be described as DJ-grade or sale-candidate.
