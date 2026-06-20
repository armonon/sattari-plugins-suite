#!/usr/bin/env python3
"""Lightweight Sattari Audio suite gate inventory.

This intentionally checks structure, not product quality. Gate movement still requires
machine validation, export artifacts, and human listening/visual reports.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    ROOT / "core" / "include" / "sattari_audio_core" / "SattariAudioCore.h",
    ROOT / "validation" / "templates" / "listening-test-report.md",
    ROOT / "validation" / "templates" / "product-validation-report.md",
    ROOT / "validation" / "schemas" / "render_manifest.schema.json",
    ROOT / "validation" / "schemas" / "auto_pitch_corpus_manifest.schema.json",
]

PRODUCTS = {
    "auto-pitch": "flagship pitch-correction lane; JUCE shell exists, vocal quality remains gated by real-corpus proof",
    "rhythm-genie": "DAW-ready MIDI drum generator lane",
    "pure-tone-generator-pro": "premium tone/noise/sweep utility lane",
    "cymatic-generator": "deterministic audio-reactive render lane",
}


def main() -> int:
    missing = [str(p.relative_to(ROOT)) for p in REQUIRED if not p.exists()]
    report = {
        "suite": "sattari-audio",
        "truth_label": "suite structure check only; not sale-ready evidence",
        "required_files_checked": [str(p.relative_to(ROOT)) for p in REQUIRED],
        "missing": missing,
        "products": PRODUCTS,
        "pass": not missing,
    }
    print(json.dumps(report, indent=2))
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
