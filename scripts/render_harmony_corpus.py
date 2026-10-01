#!/usr/bin/env python3
"""Render cleared local vocals through Auto Pitch's real harmony path.

Audio stays outside the repository. Refuse existing output directories; capture
source/binary hashes and raw safety metrics, never infer a listening score.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(args):
    repo = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output == repo or repo in output.parents:
        raise ValueError("Private audio output must be outside the repository")
    binary = args.binary.resolve(strict=True)
    corpus = json.loads(args.corpus.read_text())
    sources = []
    for clip in corpus["clips"]:
        if not clip.get("cleared_for_internal_eval"):
            raise ValueError(f"Uncleared clip: {clip['clip_id']}")
        if not re.fullmatch(r"[a-zA-Z0-9_-]+", clip["clip_id"]):
            raise ValueError("Unsafe clip ID")
        source = Path(clip["source_path"]).resolve(strict=True)
        sources.append({"id": clip["clip_id"], "path": str(source),
                        "sha256": sha256(source),
                        "permission_status": clip["permission_status"],
                        "public_release_allowed": clip.get("public_release_allowed", False)})
    if not sources or len({s["id"] for s in sources}) != len(sources):
        raise ValueError("Corpus is empty or contains duplicate IDs")
    output.mkdir(mode=0o700, parents=False, exist_ok=False)
    manifest = {"binary": str(binary), "binary_sha256": sha256(binary),
                "sources": sources, "audio_uploaded": False,
                "scope": "finite/unclipped/non-silent smoke, NOT listening or musical-accuracy approval",
                "configuration": {"block": 256, "input_gain_db": -12, "voice_gain_db": -9,
                                  "intervals_chromatic_semitones": [4, -3, 7, -7],
                                  "lead_muted": True, "lead_correction": False,
                                  "formants": "neutral; Studio path active", "normalization": False}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    def render(case):
        source, mode, voices = case
        name = f"{source['id']}-{mode}-{voices}v"
        command = [str(binary), "--harmony-file", source["path"],
                   str(output / (name + ".wav")), mode, str(voices)]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=600,
                                    env=dict(os.environ, SATTARI_SESSION_ID="harmony-" + uuid.uuid4().hex))
            log = result.stdout + result.stderr
            code = result.returncode
        except subprocess.TimeoutExpired as error:
            log, code = str(error), -1
        (output / (name + ".log")).write_text(log)
        metrics = {}
        for line in log.splitlines():
            if line.startswith("harmony-file "):
                metrics = dict(item.split("=", 1) for item in line.split()[1:] if "=" in item)
        print(f"{'PASS' if code == 0 else 'FAIL'} {name} {metrics}", flush=True)
        return {"case": name, "command": command, "exit_code": code, "metrics": metrics}

    cases = [(source, mode, voices) for source in sources
             for mode in ("live", "studio") for voices in (1, 4)]
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        results = list(workers.map(render, cases))
    passed = sum(result["exit_code"] == 0 for result in results)
    manifest.update(results=results, passed=passed, total=len(results))
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Harmony local smoke: {passed}/{len(results)} passed; {output}", flush=True)
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New directory outside git; parent must exist")
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1)
    options = parser.parse_args()
    try:
        raise SystemExit(run(options))
    except (OSError, ValueError, KeyError) as error:
        print(f"Harmony corpus refused: {error}", flush=True)
        raise SystemExit(2)
