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

import json
import pathlib
import random
import sys

import numpy as np
import soundfile as sf

REPO = pathlib.Path(__file__).resolve().parent.parent
MANIFEST = REPO / "validation/corpus/auto-pitch.json"
RENDERS = REPO / "validation/auto-pitch/renders"
# Audio never enters the repository, ignored or not.
OUT = pathlib.Path("/Users/lillypad/Desktop/vocal test/listening-package")

SEED = 20260810          # reproducible; the key file is the secret, not the seed
TARGET_PEAK_DBFS = -1.0

CONDITIONS = [
    ("bypass",      "{clip}.wav",              "source"),
    ("transparent", "{clip}-natural.wav",      "render"),
    ("aggressive",  "{clip}-natural_hard.wav", "render"),
]


def load(path: pathlib.Path):
    audio, sr = sf.read(str(path), always_2d=True, dtype="float64")
    return audio.mean(axis=1), sr


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
    manifest = json.loads(MANIFEST.read_text())
    src_root = pathlib.Path(manifest["clips"][0]["source_path"]).parent
    OUT.mkdir(parents=True, exist_ok=True)

    rng = random.Random(SEED)
    clips = [c["clip_id"] for c in manifest["clips"]]
    set_order = clips[:]
    rng.shuffle(set_order)                      # clip identity hidden too

    key_rows, sheet_sets = [], []
    for set_index, clip in enumerate(set_order):
        set_name = chr(ord("A") + set_index)

        loaded = []
        for cond, pattern, kind in CONDITIONS:
            path = (src_root if kind == "source" else RENDERS) / pattern.format(clip=clip)
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
            sf.write(str(OUT / name), x.astype(np.float32), sr, subtype="PCM_24")
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
    (OUT / "KEY-DO-NOT-OPEN.md").write_text("\n".join(key) + "\n")

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
        "candidate_revision: 7fef5442", "date: 2026-08-10",
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

    sheet += [
        "## After scoring", "",
        "- [ ] All sets scored", "- [ ] Artifact notes written",
        "- [ ] Only now open `KEY-DO-NOT-OPEN.md`", "",
        "## Not included, and why", "",
        "**No commercial reference.** A fair comparison needs the competing",
        "product licensed and driven to equivalent settings on this same audio.",
        "That has not been done, so no reference is included rather than a",
        "misleading one. Any parity claim needs that session to happen first.", "",
        "**Three sets, not five.** The corpus stands at 3/5 clips; belting, rnb,",
        "vibrato and male_vocal are uncovered. This session cannot speak for",
        "material it does not contain.", "",
    ]
    (OUT / "SCORING-SHEET.md").write_text("\n".join(sheet) + "\n")

    print(f"\npackage: {OUT}")
    print(f"  {len(key_rows)} items, {len(sheet_sets)} sets")
    print("  SCORING-SHEET.md   fill this in")
    print("  KEY-DO-NOT-OPEN.md do not open until scoring is complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
