#!/usr/bin/env python3
"""Offline Demucs wrapper for Sattari StemDeck.

StemDeck calls this script through SATTARI_STEMDECK_SEPARATOR_CMD, for example:

  export SATTARI_STEMDECK_SEPARATOR_CMD="python3 scripts/stemdeck_separate_demucs.py --input {input} --output-dir {output}"

The script keeps Demucs optional: if the Python `demucs` module is not installed,
it exits non-zero and StemDeck simply keeps using the full mix / existing cache.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate StemDeck cached stems with Demucs")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--model", default="htdemucs")
    parser.add_argument("--format", default="wav", choices=["wav"])
    args = parser.parse_args()

    source = args.input.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    if not source.exists():
        print(f"Input does not exist: {source}", file=sys.stderr)
        return 2

    if shutil.which("python3") is None:
        print("python3 not found", file=sys.stderr)
        return 3

    output_dir.mkdir(parents=True, exist_ok=True)
    started_at = time.time()
    work_dir = output_dir / "demucs-work"
    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)

    command = [
        sys.executable,
        "-m",
        "demucs",
        "--name",
        args.model,
        "--out",
        str(work_dir),
        "--filename",
        "{stem}.{ext}",
        str(source),
    ]
    print("Running:", " ".join(command))
    completed = subprocess.run(command, text=True)
    if completed.returncode != 0:
        print("Demucs failed. Install with: python3 -m pip install demucs", file=sys.stderr)
        return completed.returncode

    # With --filename {stem}.{ext}, Demucs writes directly under work_dir/model or
    # sometimes work_dir/model/track-name depending on version. Search recursively.
    candidates = {p.name.lower(): p for p in work_dir.rglob("*.wav")}
    mapping = {
        "drums.wav": "drums.wav",
        "bass.wav": "bass.wav",
        "other.wav": "music.wav",
        "vocals.wav": "vocals.wav",
    }

    missing = []
    for demucs_name, stemdeck_name in mapping.items():
        source_stem = candidates.get(demucs_name)
        if source_stem is None:
            missing.append(demucs_name)
            continue
        shutil.copy2(source_stem, output_dir / stemdeck_name)

    if missing:
        print(f"Demucs completed but missing stems: {', '.join(missing)}", file=sys.stderr)
        return 4

    metadata = {
        "schema": "SattariStemDeck.demucsSeparation.v1",
        "truthLabel": "local offline AI stem separation metadata; not a bundled product claim",
        "engine": "demucs",
        "model": args.model,
        "source": str(source),
        "durationSeconds": round(time.time() - started_at, 3),
        "stems": {
            "drums": str(output_dir / "drums.wav"),
            "bass": str(output_dir / "bass.wav"),
            "music": str(output_dir / "music.wav"),
            "vocals": str(output_dir / "vocals.wav"),
        },
    }
    (output_dir / "separation-metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    shutil.rmtree(work_dir, ignore_errors=True)
    print(f"Wrote StemDeck stems to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
