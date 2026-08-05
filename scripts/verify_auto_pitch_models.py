#!/usr/bin/env python3
"""Audit Auto Pitch's external ONNX models without granting distribution rights.

Local testing only needs the files to exist and be fingerprinted. Distribution
is intentionally stricter: every included model must match a release record
that names its source, license, and explicit redistribution clearance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

MODEL_ROLES = {
    "crepe.onnx": "monophonic neural pitch",
    "keynet.onnx": "live neural key classification",
    "keynet_spec.onnx": "spectral Key Map classification",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_record(path: Path | None) -> dict[str, dict]:
    if path is None:
        return {}
    data = json.loads(path.read_text())
    if data.get("schemaVersion") != 1:
        raise ValueError("model release record schemaVersion must be 1")
    models = data.get("models")
    if not isinstance(models, list):
        raise ValueError("model release record must contain a models array")
    records: dict[str, dict] = {}
    for entry in models:
        filename = entry.get("filename")
        if filename not in MODEL_ROLES:
            raise ValueError(f"unknown Auto Pitch model filename: {filename!r}")
        if filename in records:
            raise ValueError(f"duplicate model release record: {filename}")
        records[filename] = entry
    return records


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--model-dir",
        type=Path,
        default=Path.home() / "Library/Sattari Audio/models",
    )
    parser.add_argument("--record", type=Path)
    parser.add_argument(
        "--require",
        action="append",
        choices=sorted(MODEL_ROLES),
        default=[],
        help="fail when this local model is absent (repeatable)",
    )
    parser.add_argument(
        "--distribution",
        action="store_true",
        help="require complete, matching redistribution clearance for every present model",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    try:
        records = load_record(args.record)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"model release record error: {error}", file=sys.stderr)
        return 2

    report: dict = {
        "schemaVersion": 1,
        "modelDirectory": str(args.model_dir.expanduser().resolve()),
        "distributionMode": args.distribution,
        "models": [],
        "ok": True,
    }
    errors: list[str] = []

    for filename, role in MODEL_ROLES.items():
        path = args.model_dir.expanduser() / filename
        present = path.is_file()
        actual_hash = sha256(path) if present else None
        entry = records.get(filename)
        expected_hash = (entry or {}).get("sha256") or None
        hash_matches = expected_hash is None or actual_hash == expected_hash.lower()
        cleared = bool((entry or {}).get("redistributionCleared"))
        source = str((entry or {}).get("sourceURL") or "").strip()
        license_name = str((entry or {}).get("license") or "").strip()

        if filename in args.require and not present:
            errors.append(f"required local model is missing: {path}")
        if present and expected_hash and not hash_matches:
            errors.append(f"SHA-256 mismatch for {filename}")
        if args.distribution and present:
            if entry is None:
                errors.append(f"no release record for distributable model: {filename}")
            elif not (expected_hash and hash_matches and cleared and source and license_name):
                errors.append(
                    f"distribution clearance incomplete for {filename}: "
                    "need matching sha256, sourceURL, license, and redistributionCleared=true"
                )

        report["models"].append(
            {
                "filename": filename,
                "role": role,
                "present": present,
                "bytes": path.stat().st_size if present else 0,
                "sha256": actual_hash,
                "recordedSha256": expected_hash,
                "hashMatchesRecord": hash_matches if present else None,
                "redistributionCleared": cleared,
                "packagePolicy": "eligible" if args.distribution and present and cleared else "external-only",
            }
        )

    report["ok"] = not errors
    report["errors"] = errors
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for item in report["models"]:
            state = "present" if item["present"] else "missing"
            fingerprint = f" sha256={item['sha256']}" if item["sha256"] else ""
            print(f"{item['filename']}: {state}; policy={item['packagePolicy']}{fingerprint}")
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
        elif not args.distribution:
            print("Local model audit passed; this does not grant redistribution rights.")
        else:
            print("Model distribution audit passed.")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
