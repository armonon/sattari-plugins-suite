#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out_dir="${1:-$repo_root/plugins/stemdeck/logs}"
stamp="$(date +%Y-%m-%d-%H%M%S)"
out_file="$out_dir/stemdeck-manual-daw-listening-$stamp.md"

mkdir -p "$out_dir"

cat > "$out_file" <<EOF_REPORT
# StemDeck manual DAW + listening validation run — $stamp

Truth label: **manual validation template / not passed until filled in**.

## Environment

- Tester:
- Date/time:
- macOS:
- CPU / machine:
- StemDeck build/commit: $(git -C "$repo_root" rev-parse --short HEAD 2>/dev/null || echo unknown)
- Package/source path:
- Audio interface / buffer size / sample rate:
- Test audio ownership/license confirmed: yes/no

## Machine gate commands run first

- [ ] ./scripts/validate_stemdeck.sh
- [ ] /Applications/pluginval.app/Contents/MacOS/pluginval strictness 5 on VST3
- [ ] Package validate_local.sh, if testing packaged build

## DAW smoke matrix

For each host, record pass/fail plus notes. Do not mark sale-candidate unless every required row passes.

| Host | Version | Format | Fresh insert | Load 4 decks | Stems | Automation | Save/reopen | Transport sync | Pads | Record WAV | Crash/hang | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Ableton Live | | VST3 | | | | | | | | | | |
| Reaper | | VST3 | | | | | | | | | | |
| FL Studio | | VST3 | | | | | | | | | | |
| Logic Pro | | Standalone only until AU build exists | | | | | | | | | | |

## Listening checklist

- [ ] Full mix loads on Deck A/B/C/D and plays without silence/dropouts.
- [ ] Drums/Bass/Music/Vocals pre-separated lanes share each deck playhead and Cue behavior.
- [ ] Stem gain automation sweeps -60 dB to +12 dB without zipper noise or crashes.
- [ ] Low/Mid/High EQ, filter, fader, crossfader, master, and limiter behave as expected.
- [ ] Pitch/key controls are truth-labeled as resample fallback, not DJ-grade pitch-preserving time-stretch.
- [ ] Host sync at 90/120/140 BPM documented, including pitch artifacts and drift.
- [ ] 60-second Record WAV output inspected for silence, clipping, and dropouts.
- [ ] Close/reopen restores deck/pad paths where local files still exist.
- [ ] Mic monitor checked with safe levels and no feedback loop.
- [ ] Plugin-host scan scaffold status noted; no hosted audio routing claim made.

## CPU / reliability observations

- Idle CPU:
- One deck CPU:
- Four deck CPU:
- Four decks + stems CPU:
- Load time / first-play reliability:
- UI responsiveness:

## Issues found

1.
2.
3.

## Verdict

- [ ] Manual DAW/listening pass completed
- [ ] Blocked — details above
- [ ] Not sale-ready / further work required

EOF_REPORT

echo "$out_file"
