#!/usr/bin/env python3
"""Validate the Auto Pitch trace-to-plugin development contract.

This is a structural handoff gate for the JUCE shell. It does not load audio,
run pitch correction, or prove vocal quality.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRACE = ROOT / "plugins" / "auto-pitch" / "fixtures" / "demo-modern-trace-minimal.json"
DEFAULT_OUT = ROOT / "tmp" / "auto-pitch-trace-contract" / "auto-pitch-ui-state.json"
SOURCE_FILES = [
    ROOT / "plugins" / "auto-pitch" / "src" / "PluginProcessor.h",
    ROOT / "plugins" / "auto-pitch" / "src" / "PluginProcessor.cpp",
    ROOT / "plugins" / "auto-pitch" / "src" / "PluginEditor.h",
    ROOT / "plugins" / "auto-pitch" / "src" / "PluginEditor.cpp",
]

NOTE_ORDER = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
REQUIRED_PARAMETER_IDS = [
    "keyRoot",
    "scale",
    "style",
    "amount",
    "speed",
    "doNoHarm",
    "traceConfidence",
]


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def build_ui_state(trace: dict) -> dict:
    settings = trace.get("settings") if isinstance(trace.get("settings"), dict) else {}
    key_suggestion = trace.get("key_suggestion") if isinstance(trace.get("key_suggestion"), dict) else {}
    compass = trace.get("pitch_compass") if isinstance(trace.get("pitch_compass"), dict) else {}
    summary = trace.get("summary") if isinstance(trace.get("summary"), dict) else {}
    notes = compass.get("notes") if isinstance(compass.get("notes"), list) else []

    note_weights = {}
    enabled_notes = []
    for note in notes:
        if not isinstance(note, dict):
            continue
        name = str(note.get("name", "")).strip()
        if name in NOTE_ORDER:
            note_weights[name] = float(note.get("histogram_weight", 0.0) or 0.0)
            if bool(note.get("enabled", False)):
                enabled_notes.append(name)

    return {
        "truth_label": "internal plugin UI trace-contract check; not audio quality proof; not sale-ready evidence",
        "keyRoot": str(settings.get("root") or key_suggestion.get("root") or "A"),
        "scale": str(settings.get("scale") or key_suggestion.get("scale") or "minor"),
        "style": str(settings.get("style") or "modern"),
        "keySuggestion": {
            "root": str(key_suggestion.get("root") or ""),
            "scale": str(key_suggestion.get("scale") or ""),
            "confidence": float(key_suggestion.get("confidence", 0.0) or 0.0),
        },
        "pitchCompass": {
            "noteCount": len(notes),
            "enabledNotes": enabled_notes,
            "noteWeights": note_weights,
        },
        "summary": {
            "points": int(summary.get("points", 0) or 0),
            "avgAbsCorrectionCents": float(summary.get("avg_abs_correction_cents", 0.0) or 0.0),
            "doNoHarmEvents": int(summary.get("do_no_harm_events", 0) or 0),
        },
    }


def validate_ui_state(state: dict) -> list[str]:
    errors: list[str] = []
    if state["keyRoot"] not in NOTE_ORDER:
        errors.append(f"keyRoot {state['keyRoot']!r} is not a supported note")
    if state["scale"] not in {"major", "minor", "chromatic"}:
        errors.append(f"scale {state['scale']!r} is not supported")
    if state["style"] not in {"natural", "modern", "hard"}:
        errors.append(f"style {state['style']!r} is not supported")
    if not (0.0 <= state["keySuggestion"]["confidence"] <= 1.0):
        errors.append("key suggestion confidence must be 0..1")
    if state["pitchCompass"]["noteCount"] != 12:
        errors.append("pitch compass must carry 12 note bins")
    missing_weights = [note for note in NOTE_ORDER if note not in state["pitchCompass"]["noteWeights"]]
    if missing_weights:
        errors.append(f"pitch compass missing note weights: {', '.join(missing_weights)}")
    if state["summary"]["points"] <= 0:
        errors.append("summary.points must be positive")
    return errors


def validate_source_contract() -> list[str]:
    errors: list[str] = []
    source = "\n".join(path.read_text(encoding="utf-8") for path in SOURCE_FILES if path.exists())
    for parameter_id in REQUIRED_PARAMETER_IDS:
        if f'"{parameter_id}"' not in source:
            errors.append(f"missing plugin parameter ID in source: {parameter_id}")
    # Trace import remains a developer-only seam and is intentionally not laid
    # out in the customer UI. Guard the plumbing, not obsolete button copy.
    for token in ["applyTraceContractJson", "key_suggestion", "pitch_compass", "loadTraceButton"]:
        if token not in source:
            errors.append(f"missing trace-contract source token: {token}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    trace = load_json(args.trace)
    state = build_ui_state(trace)
    errors = validate_ui_state(state) + validate_source_contract()

    report = {
        "pass": not errors,
        "trace": str(args.trace.relative_to(ROOT) if args.trace.is_relative_to(ROOT) else args.trace),
        "output": str(args.out.relative_to(ROOT) if args.out.is_relative_to(ROOT) else args.out),
        "errors": errors,
        "uiState": state,
    }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
