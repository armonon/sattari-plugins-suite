#!/usr/bin/env python3
"""Build a BLINDED, level-matched listening package for Auto Pitch.

WHY BLINDED AND WHY LEVEL-MATCHED
---------------------------------
A listening test that reveals which file is which measures expectation, not
audio. And level is the loudest cue there is: louder reliably reads as "better"
long before anyone notices an artifact. These renders make that acute --
dickey-prairie's outputs peak at 0.030 while slay-demons peaks at 0.995, a 30 dB
spread that would identify every item on sight.

So within each set the three conditions are matched to a common RMS and then
scaled together by ONE shared gain, which preserves the match while keeping the
loudest peak safely below full scale. Matching per-file instead would destroy
the very difference being judged.

Condition order inside each set is shuffled, and the mapping is written to a
SEPARATE key file that must not be opened until scoring is finished.

    python scripts/build_listening_package.py
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import random
import sys
import wave

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = REPO / "validation/corpus/auto-pitch.json"
DEFAULT_RENDERS = REPO / "validation/auto-pitch/renders"
# Audio never enters the repository, ignored or not.
DEFAULT_OUT = pathlib.Path("/Users/lillypad/Desktop/vocal test/listening-package")

SEED = 20260810          # reproducible; the key file is the secret, not the seed
TARGET_PEAK_DBFS = -1.0

CONDITIONS = [
    ("bypass",      "{clip}.wav",              "source"),
    ("transparent", "{clip}-natural.wav",      "render"),
    ("aggressive",  "{clip}-natural_hard.wav", "render"),
]


def load(path: pathlib.Path):
    """Read uncompressed integer PCM WAV without a third-party audio package."""
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        sr = wav.getframerate()
        frames = wav.getnframes()
        raw = wav.readframes(frames)

    if width == 1:
        audio = (np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0) / 128.0
    elif width == 2:
        audio = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        packed = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = packed[:, 0] | (packed[:, 1] << 8) | (packed[:, 2] << 16)
        values = (values ^ 0x800000) - 0x800000
        audio = values.astype(np.float64) / 8388608.0
    elif width == 4:
        audio = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unsupported WAV sample width ({width} bytes): {path}")

    audio = audio.reshape(-1, channels)
    return audio.mean(axis=1), sr


def write_pcm24(path: pathlib.Path, audio: np.ndarray, sr: int) -> None:
    """Write a mono 24-bit PCM WAV deterministically using the standard library."""
    values = np.clip(np.rint(audio * 8388607.0), -8388608, 8388607).astype(np.int32)
    unsigned = values.astype(np.uint32) & 0xFFFFFF
    packed = np.empty((len(unsigned), 3), dtype=np.uint8)
    packed[:, 0] = unsigned & 0xFF
    packed[:, 1] = (unsigned >> 8) & 0xFF
    packed[:, 2] = (unsigned >> 16) & 0xFF
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(3)
        wav.setframerate(sr)
        wav.writeframes(packed.tobytes())


def active_rms(x: np.ndarray, sr: int) -> float:
    """RMS over the loud half of the file only.

    Whole-file RMS is dominated by silence between phrases, and the three
    conditions do not share the same silence: correction alters what the tail of
    a note does. Measuring where the voice actually is keeps the match on the
    voice.
    """
    win = max(1, int(0.05 * sr))
    n = len(x) // win
    if n < 4:
        return float(np.sqrt(np.mean(x ** 2))) or 1e-12
    frames = x[: n * win].reshape(n, win)
    rms = np.sqrt(np.mean(frames ** 2, axis=1))
    loud = rms[rms >= np.median(rms)]
    return float(np.sqrt(np.mean(loud ** 2))) or 1e-12


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument ("--renders", type=pathlib.Path, default=DEFAULT_RENDERS,
                         help="directory containing the Natural/Hard candidate renders")
    parser.add_argument ("--output", type=pathlib.Path, default=DEFAULT_OUT,
                         help="destination package; use a new directory for a new candidate")
    parser.add_argument ("--candidate", default="uncommitted candidate",
                         help="revision or immutable candidate label written into the sheet")
    parser.add_argument ("--force", action="store_true",
                         help="replace an existing package (never use after scoring begins)")
    args = parser.parse_args()
    renders = args.renders.expanduser().resolve()
    out = args.output.expanduser().resolve()

    manifest = json.loads(MANIFEST.read_text())
    src_root = pathlib.Path(manifest["clips"][0]["source_path"]).parent
    if out.exists() and not args.force and any(out.iterdir()):
        print(f"ERROR: refusing to overwrite sealed/non-empty package: {out}", file=sys.stderr)
        print("Choose a new --output directory for every candidate.", file=sys.stderr)
        return 2
    out.mkdir(parents=True, exist_ok=True)

    rng = random.Random(SEED)
    manifest_clips = manifest["clips"]
    clips = [c["clip_id"] for c in manifest_clips]
    required_count = int(manifest.get("minimum_real_clip_count", 5))
    required_tags = set(manifest.get("coverage_requirements", []))
    covered_tags = {tag for clip in manifest_clips for tag in clip.get("vocal_tags", [])}
    missing_tags = sorted(required_tags - covered_tags)
    set_order = clips[:]
    rng.shuffle(set_order)                      # clip identity hidden too

    key_rows, sheet_sets = [], []
    for set_index, clip in enumerate(set_order):
        set_name = chr(ord("A") + set_index)

        loaded = []
        for cond, pattern, kind in CONDITIONS:
            path = (src_root if kind == "source" else renders) / pattern.format(clip=clip)
            if not path.is_file():
                print(f"ERROR: missing {path}", file=sys.stderr)
                return 2
            x, sr = load(path)
            loaded.append((cond, x, sr))

        # Match to the trio's mean active RMS, then one shared safety gain.
        target = float(np.mean([active_rms(x, sr) for _, x, sr in loaded]))
        scaled = [(cond, x * (target / active_rms(x, sr)), sr) for cond, x, sr in loaded]
        peak = max(float(np.max(np.abs(x))) for _, x, _ in scaled)
        headroom = (10.0 ** (TARGET_PEAK_DBFS / 20.0)) / max(peak, 1e-12)
        scaled = [(cond, x * headroom, sr) for cond, x, sr in scaled]

        order = list(range(len(scaled)))
        rng.shuffle(order)                      # condition order hidden
        items = []
        for slot, idx in enumerate(order, start=1):
            cond, x, sr = scaled[idx]
            name = f"{set_name}{slot}.wav"
            write_pcm24(out / name, x, sr)
            items.append(name)
            key_rows.append((f"{set_name}{slot}", clip, cond,
                             20 * np.log10(max(float(np.max(np.abs(x))), 1e-12))))
        sheet_sets.append((set_name, items))
        print(f"  SET {set_name}: 3 items, matched RMS, peak {TARGET_PEAK_DBFS:.0f} dBFS")

    # --- the sealed key -----------------------------------------------------
    key = ["# LISTENING KEY — DO NOT OPEN UNTIL SCORING IS COMPLETE", "",
           "Opening this before scoring invalidates the session.", "",
           "| item | clip | condition | peak dBFS |", "|---|---|---|---|"]
    key += [f"| {i} | {c} | {cond} | {pk:.1f} |" for i, c, cond, pk in key_rows]
    (out / "KEY-DO-NOT-OPEN.md").write_text("\n".join(key) + "\n")

    # --- the scoring sheet --------------------------------------------------
    crit = [("pitch naturalness", "does the tuning sound like a person or a machine"),
            ("artifacts", "warble, crackle, metallic edges, block-boundary modulation"),
            ("vibrato preservation", "is intentional vibrato kept or flattened"),
            ("transitions / slides", "do note changes lag, overshoot, or snap"),
            ("consonants / transients", "plosives, sibilants, breaths still natural"),
            ("timbre change", "does the voice still sound like the same person"),
            ("overall preference", "which would you actually use")]

    sheet = [
        "---", "type: listening-test-report", "product: auto-pitch",
        f"candidate_revision: {args.candidate}", f"date: {dt.date.today().isoformat()}",
        "truth_label: internal validation only; not sale-ready", "---", "",
        "# Auto Pitch — blind listening session", "",
        "## Before you start", "",
        "- [ ] Level-matched already: do NOT adjust gain between items in a set.",
        "- [ ] Listen on the monitoring you actually mix on; note it below.",
        "- [ ] Score every item in a set before moving to the next set.",
        "- [ ] Do NOT open `KEY-DO-NOT-OPEN.md` until every score is filled in.", "",
        "One item in each set is unprocessed. Which one is not disclosed, and it",
        "is not always in the same position. If you cannot tell it apart from the",
        "processed items, that is a result worth recording, not a failed test.", "",
        "Monitoring: ______________________   Date: ____________", "",
        "## Scale", "",
        "1 = unacceptable · 3 = usable with caveats · 5 = excellent.",
        "**Artifacts are reversed**: 1 = severe, 5 = inaudible.", "",
    ]
    for set_name, items in sheet_sets:
        sheet += [f"## SET {set_name}", "",
                  "| criterion | " + " | ".join(i[:-4] for i in items) + " |",
                  "|---|" + "---|" * len(items)]
        for name, hint in crit:
            sheet.append(f"| {name} <br><sub>{hint}</sub> | " + " | ".join([" "] * len(items)) + " |")
        sheet += ["", f"Artifacts heard in SET {set_name}, with timestamps:", "",
                  "```", "", "```", ""]

    if len(clips) >= required_count and not missing_tags:
        coverage_note = [
            f"**Corpus coverage.** This session contains {len(clips)} cleared clips against",
            f"a {required_count}-clip minimum and covers all {len(required_tags)} required tags.",
            "The listening result must still be limited to this corpus; it is not a",
            "universal quality claim.",
        ]
    else:
        missing = ", ".join(missing_tags) or "none"
        coverage_note = [
            f"**Corpus limitation.** This session contains {len(clips)} cleared clips against",
            f"a {required_count}-clip minimum. Missing required tags: {missing}.",
            "This session cannot speak for material its corpus does not contain.",
        ]

    sheet += [
        "## After scoring", "",
        "- [ ] All sets scored", "- [ ] Artifact notes written",
        "- [ ] Only now open `KEY-DO-NOT-OPEN.md`", "",
        "## Not included, and why", "",
        "**No commercial reference.** A fair comparison needs the competing",
        "product licensed and driven to equivalent settings on this same audio.",
        "That has not been done, so no reference is included rather than a",
        "misleading one. Any parity claim needs that session to happen first.", "",
    ] + coverage_note + [""]
    (out / "SCORING-SHEET.md").write_text("\n".join(sheet) + "\n")

    print(f"\npackage: {out}")
    print(f"  {len(key_rows)} items, {len(sheet_sets)} sets")
    print("  SCORING-SHEET.md   fill this in")
    print("  KEY-DO-NOT-OPEN.md do not open until scoring is complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
