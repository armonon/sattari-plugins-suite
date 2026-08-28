#!/usr/bin/env python3
"""Render and inventory a cleared real-vocal corpus for Auto Pitch qualification.

The manifest is deliberately separate from the audio. Real recordings stay out of
git; this tool records hashes and generated artifact paths without publishing the
source material.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import pathlib
import subprocess
import sys


COVERAGE = {
    "rap",
    "melismatic",
    "soft_singing",
    "belting",
    "vibrato",
    "male_vocal",
    "female_vocal",
    "noisy_vocal",
    "fast_note_transitions",
}
REAL_POLICIES = {"owned", "cleared", "public-domain"}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(2)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--renderer", type=pathlib.Path,
                        default=pathlib.Path("build/auto-pitch-neural/AutoPitchRender"))
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("validation/auto-pitch/renders"))
    parser.add_argument("--report", type=pathlib.Path)
    parser.add_argument("--inventory-only", action="store_true")
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    try:
        manifest = json.loads(manifest_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read manifest: {exc}")

    clips = manifest.get("clips")
    if not isinstance(clips, list) or not clips:
        fail("manifest must contain at least one clip")
    if not manifest.get("truth_label") or not manifest.get("audio_policy"):
        fail("truth_label and audio_policy are required")

    real = [clip for clip in clips if clip.get("permission_status") in REAL_POLICIES]
    required_count = int(manifest.get("minimum_real_clip_count", 5))
    tags = {tag for clip in real for tag in clip.get("vocal_tags", [])}
    required_tags = set(manifest.get("coverage_requirements", sorted(COVERAGE)))
    missing_tags = sorted(required_tags - tags)

    if not args.inventory_only and not args.renderer.is_file():
        fail(f"renderer not found: {args.renderer}")

    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []

    for clip in clips:
        clip_id = str(clip.get("clip_id", "")).strip()
        if not clip_id:
            fail("every clip needs clip_id")
        if clip.get("cleared_for_internal_eval") is not True:
            fail(f"{clip_id}: cleared_for_internal_eval must be true")
        source = pathlib.Path(str(clip.get("source_path", "")))
        if not source.is_absolute():
            source = (manifest_path.parent / source).resolve()
        if not source.is_file():
            fail(f"{clip_id}: source not found: {source}")

        natural = output_dir / f"{clip_id}-natural.wav"
        hard = output_dir / f"{clip_id}-natural_hard.wav"
        log = output_dir / f"{clip_id}.log"
        status = "inventory only"
        if not args.inventory_only:
            completed = subprocess.run(
                [str(args.renderer.resolve()), "--file", str(source), str(natural)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            log.write_text(completed.stdout)
            if completed.returncode != 0:
                fail(f"{clip_id}: render failed; see {log}")
            if not natural.is_file() or not hard.is_file():
                fail(f"{clip_id}: renderer did not produce both Natural and Hard outputs")
            status = "rendered"

        rows.append({
            "id": clip_id,
            "tags": ", ".join(clip.get("vocal_tags", [])),
            "policy": str(clip.get("permission_status")),
            "source_hash": sha256(source),
            "natural": str(natural),
            "hard": str(hard),
            "log": str(log),
            "status": status,
        })

    gate_ready = len(real) >= required_count and not missing_tags
    report_path = args.report or output_dir / (
        f"qualification-{dt.datetime.now().strftime('%Y-%m-%d-%H%M%S')}.md"
    )
    report_path = report_path.resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    table = "\n".join(
        f"| {row['id']} | {row['tags']} | {row['policy']} | `{row['source_hash']}` | "
        f"{row['status']} | TBD | TBD | TBD | hold |"
        for row in rows
    )
    report_path.write_text(f"""# Auto Pitch real-vocal qualification

Truth label: **{manifest['truth_label']}**

Generated: {dt.datetime.now().astimezone().isoformat(timespec='seconds')}
Manifest: `{manifest_path}`
Renderer: `{args.renderer.resolve()}`

## Corpus gate

- Real cleared clips: **{len(real)} / {required_count} required**
- Required coverage tags: {', '.join(sorted(required_tags)) or 'none'}
- Missing coverage tags: {', '.join(missing_tags) or 'none'}
- Corpus gate: **{'READY FOR LISTENING' if gate_ready else 'BLOCKED'}**

Synthetic fixtures do not count toward this gate. A ready corpus is not a listening
pass; a human must score every rendered pair below on level-matched monitoring.

## Render inventory and listening scores

Scores use 1 = unacceptable, 3 = usable with caveats, 5 = excellent. Artifact
severity is reversed: 1 = severe, 5 = inaudible.

| ID | Tags | Permission | Source SHA-256 | Render | Musicality | Artifacts | Timing | Decision |
|---|---|---|---|---|---:|---:|---:|---|
{table}

## Required listening checks

- [ ] Natural correction centers sustained notes without flattening intentional vibrato.
- [ ] Hard correction reaches the target without crackle or block-boundary modulation.
- [ ] Plosives, sibilants, breaths, and unvoiced consonants remain natural.
- [ ] Formants remain stable through octave errors, belts, and low-register vocals.
- [ ] Fast note changes do not lag, overshoot, or snap to a transient.
- [ ] Offline bounce matches realtime behavior within the documented latency mode.
- [ ] Output is level-matched and contains no clipping, NaN, silence, or dropouts.

## Verdict

- [ ] Hold
- [ ] Promote to validated alpha
- [ ] Promote to release candidate

Rationale:
""")
    print(report_path)
    return 0 if gate_ready else 3


if __name__ == "__main__":
    raise SystemExit(main())
