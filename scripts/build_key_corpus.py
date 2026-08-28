#!/usr/bin/env python3
# Build the COMBINED key-detection corpus: GiantSteps (EDM, 0=C string labels) +
# GTZAN tonal tracks (Lerch 0=A ints -> 0=C strings). Emits validation/combined/ with
# wav/ symlinks + labels.csv in the "<file.wav>,<Root major|minor>" format that
# SattariKeyDatasetEval consumes. Run SattariKeyDatasetEval on it to dump the HPCP
# features (chroma_cov.csv / chroma_seq.bin), then scripts/train_keynet.py to (re)train
# keynet.onnx. Diverse data is what makes the key model robust beyond EDM: cross-genre
# exact accuracy jumps 36% -> 52% for a small EDM cost (see key-model memory).
#
# Prereqs: scripts/fetch_giantsteps.sh (>=1 track) and scripts/fetch_gtzan_keys.sh.
import glob, os, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
gsdir, gtdir = f"{ROOT}/validation/giantsteps", f"{ROOT}/validation/gtzan"
out = f"{ROOT}/validation/combined"; wav = f"{out}/wav"
os.makedirs(wav, exist_ok=True)
for f in glob.glob(f"{wav}/*"): os.remove(f)

lines = []
# GiantSteps: reuse existing labels + symlink wavs
ngs = 0
for ln in open(f"{gsdir}/labels.csv").read().strip().splitlines():
    fn = ln.split(",")[0]; src = os.path.abspath(f"{gsdir}/wav/{fn}")
    if os.path.exists(src):
        os.symlink(src, f"{wav}/{fn}"); lines.append(ln); ngs += 1

# GTZAN: Lerch int (0=A) -> "Root major|minor" (0=C note names), skip -1 (no key)
NOTE = ['A','A#','B','C','C#','D','D#','E','F','F#','G','G#']
labs = {}
for f in glob.glob(f"{gtdir}/keys/**/*.lerch.txt", recursive=True):
    try: labs[os.path.basename(f).replace(".lerch.txt","")] = int(open(f).read().strip())
    except ValueError: pass
ngt = 0
for src in glob.glob(f"{gtdir}/genres/**/*.wav", recursive=True):
    stem = os.path.basename(src).replace(".wav","")
    v = labs.get(stem, -1)
    if v < 0: continue
    fn = f"{stem}.wav"
    os.symlink(os.path.abspath(src), f"{wav}/{fn}")
    lines.append(f"{fn},{NOTE[v % 12]} {'minor' if v >= 12 else 'major'}"); ngt += 1

open(f"{out}/labels.csv", "w").write("\n".join(lines) + "\n")
print(f"combined corpus: {len(lines)} tracks (giantsteps {ngs} + gtzan {ngt}) -> {out}")
print(f"next: build/<tree>/core/SattariKeyDatasetEval {out}   then   scripts/train_keynet.py {out}/chroma_cov.csv keynet.onnx")
