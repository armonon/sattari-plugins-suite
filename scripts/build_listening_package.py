#!/usr/bin/env python3
"""Build a BLINDED, loudness-matched Auto Pitch listening package from a verified candidate kit.

Run on the machine that holds the cleared vocals (lillypad). One command:

    python3 build_listening_package.py --out "$HOME/Desktop/vocal test/listening-cbd1ce4"

What it does, in order (any failure stops it before a single package file is written):

1. Verifies the candidate kit (made by scripts/make_candidate_kit.sh): every file's sha256 must match
   KIT-MANIFEST.json, no file may be missing or extra, and bin/AutoPitchBench must be the pinned
   candidate (cbd1ce4 unless --expected-bench-sha256 says otherwise). Mismatch = refuse to run.
2. Disk preflight: aborts below --min-free-gb (default 8, env BENCH_MIN_FREE_GB) or if the estimated
   work would take the disk under that floor.
3. Selects one excerpt (8-20 s, default 15 s) per clip from the corpus manifest, covering male,
   female, low register, high register, vibrato, sustained notes, transitions and difficult phrases
   from the manifest's tags / measured_characteristics. Missing categories are reported, never faked.
4. Renders every selected clip IN FULL with the kit's AutoPitchBench (so AutoKey sees the song from
   the start, as in a DAW), then cuts the same excerpt from each render after removing the plugin
   latency (calibrated per sample rate with a --disable-correction bypass render, and cross-checked
   against every render's reported latency).
5. Loudness-matches every item (ITU-R BS.1770-4 integrated loudness) and writes randomized labels.
   The answer key goes to a SEPARATE file next to the package (<out>-KEY.json); its sha256 is
   recorded in the package manifest, so a swapped or edited key is detectable (--verify).

Conditions per clip (labels A-D, or A-E when an Auto-Tune reference is present):
    dry          original source excerpt, latency-aligned, unprocessed
    ap_natural   Auto Pitch, Style Natural, manual key from the manifest, else C Chromatic
    ap_classic   Auto Pitch, Style Classic, same key
    ap_autokey   Auto Pitch, Style Classic (--autokey-style), --auto-key --manual-key-untouched
    autotune_pro owner-provided Auto-Tune Pro bounce (--autotune-dir), matched by clip id; else omitted
All Auto Pitch renders use the processor's factory defaults for everything else (amount 1.0,
speed 0.85, Do No Harm on, transition 0.5, formant preserve on, block 512); see RENDER_COMMON.

Standard library only (Python 3.9+), so it runs on a stock macOS python3 without numpy.

Other modes:
    --dry-run            verify the kit (if found) and list the selection; render nothing
    --verify <package>   re-hash a finished package against its manifest and its sealed key file
"""
from __future__ import annotations

import argparse
import array
import csv
import hashlib
import io
import itertools
import json
import math
import os
import random
import re
import secrets
import shutil
import subprocess
import sys
import tarfile
import tempfile
import wave
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent if HERE.name == "scripts" else None

# AutoPitchBench of commit cbd1ce4 (tag auto-pitch-rc-baseline-20260925), as fingerprinted in
# validation/auto-pitch/qualification/2026-09-25-cbd1ce4.md. A kit carrying any other binary is refused
# unless the operator names that binary's sha256 explicitly.
PINNED_BENCH_SHA256 = "92aa71a0591ce73ee7e1173608907bd5194857136e187b615d1c570af8c3ead7"

REAL_POLICIES = {"owned", "cleared", "public-domain"}
PACKAGE_FORMAT = 1
EXCERPT_MIN_S, EXCERPT_MAX_S = 8.0, 20.0
END_MARGIN_S = 0.25          # render tail lost to plugin latency; must exceed latency (checked)
FADE_MS = 10.0
PEAK_CEILING_DBFS = -1.0
IGNORED_NAMES = {".DS_Store"}

# Processor factory defaults (PluginProcessor::createParameterLayout). AutoPitchBench hard-codes
# doNoHarm=0, amount 0.82, speed 0.70; these arguments restore the defaults a fresh plugin instance has.
RENDER_COMMON = ["--block", "512", "--amount", "1", "--speed", "0.85", "--param", "doNoHarm=1"]

CONDITION_TEXT = {
    "dry": "Dry reference: original source excerpt, latency-aligned, no processing",
    "ap_natural": "Auto Pitch candidate, Style Natural, manual key (manifest) else C Chromatic",
    "ap_classic": "Auto Pitch candidate, Style Classic, manual key (manifest) else C Chromatic",
    "ap_autokey": "Auto Pitch candidate, AutoKey Follow on, manual key left at factory default",
    "autotune_pro": "Auto-Tune Pro reference render supplied by the owner",
}

# Category rules. Tags are the manifest's measurement-backed vocal_tags; thresholds are on
# measured_characteristics. A category is covered only by a clip that satisfies its rule.
LOW_F0_HZ, HIGH_F0_HZ, SUSTAIN_S = 180.0, 300.0, 3.0
CATEGORY_RULES = [
    ("male", "vocal_tags has male_vocal"),
    ("female", "vocal_tags has female_vocal"),
    ("low_register", f"f0_median_hz < {LOW_F0_HZ:.0f}"),
    ("high_register", f"f0_median_hz >= {HIGH_F0_HZ:.0f}"),
    ("vibrato", "vocal_tags has vibrato"),
    ("sustained_notes", f"longest_sustain_s >= {SUSTAIN_S:.0f}"),
    ("transitions", "vocal_tags has fast_note_transitions or melismatic"),
    ("difficult_phrases", "vocal_tags has rap, noisy_vocal or belting"),
]


def die(msg: str, code: int = 2):
    print(f"ERROR: {msg}", file=sys.stderr)
    raise SystemExit(code)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------------------------
# Kit verification
# --------------------------------------------------------------------------------------------
def safe_extract(tar_path: Path, dest: Path) -> Path:
    """Extract a kit tarball; only plain files/dirs under one top-level directory are accepted."""
    dest.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tar_path, "r:gz") as tf:
        members = tf.getmembers()
        tops = set()
        for m in members:
            p = Path(m.name)
            if p.is_absolute() or ".." in p.parts:
                die(f"kit tarball has an unsafe path: {m.name}")
            if not (m.isfile() or m.isdir()):
                die(f"kit tarball has a non-regular member (link/device): {m.name}")
            tops.add(p.parts[0])
        if len(tops) != 1:
            die(f"kit tarball must contain exactly one top-level directory, found {sorted(tops)}")
        for m in members:
            tf.extract(m, dest)
    return dest / tops.pop()


def verify_kit(kit: Path, expected_bench: str) -> dict:
    mpath = kit / "KIT-MANIFEST.json"
    if not mpath.is_file():
        die(f"not a candidate kit (no KIT-MANIFEST.json): {kit}")
    manifest = json.loads(mpath.read_text())
    listed = manifest.get("files", {})
    problems = []
    for rel, meta in sorted(listed.items()):
        p = kit / rel
        if p.is_symlink() or not p.is_file():
            problems.append(f"missing or not a regular file: {rel}")
        elif sha256_file(p) != meta["sha256"]:
            problems.append(f"sha256 MISMATCH: {rel}")
    for p in sorted(kit.rglob("*")):
        rel = p.relative_to(kit).as_posix()
        if p.name in IGNORED_NAMES or "__pycache__" in p.parts or p.is_dir():
            continue
        if rel != "KIT-MANIFEST.json" and rel not in listed:
            problems.append(f"unexpected extra file: {rel}")
    bench = listed.get("bin/AutoPitchBench", {}).get("sha256")
    if bench != expected_bench:
        problems.append(f"bin/AutoPitchBench is {bench}, expected pinned candidate {expected_bench}")
    if manifest.get("candidate", {}).get("bench_sha256") != bench:
        problems.append("KIT-MANIFEST candidate.bench_sha256 disagrees with the file list")
    if problems:
        print("KIT VERIFICATION FAILED - refusing to run:", file=sys.stderr)
        for pr in problems:
            print(f"  - {pr}", file=sys.stderr)
        raise SystemExit(3)
    # Quarantine (browser/AirDrop download) would make Gatekeeper block the unsigned binary.
    # Removing the xattr does not change file content, so the verified hashes still hold.
    subprocess.run(["xattr", "-dr", "com.apple.quarantine", str(kit)], capture_output=True)
    os.chmod(kit / "bin/AutoPitchBench", 0o755)
    return manifest


def resolve_kit(arg: str | None, work: Path | None) -> Path | None:
    if arg is None:
        return HERE if (HERE / "KIT-MANIFEST.json").is_file() else None
    p = Path(arg).expanduser().resolve()
    if p.is_dir():
        return p
    if p.is_file() and p.name.endswith((".tar.gz", ".tgz")):
        if work is None:
            die("internal: tarball kit needs a work directory")
        return safe_extract(p, work / "kit")
    die(f"--kit must be a kit directory or .tar.gz: {p}")


# --------------------------------------------------------------------------------------------
# WAV I/O (stdlib)
# --------------------------------------------------------------------------------------------
def read_wav_mono(path: Path):
    """Return (mono samples as list[float], sample_rate, channels). PCM 8/16/24/32 and float 32/64,
    including WAVE_FORMAT_EXTENSIBLE. Multichannel input is averaged to mono."""
    import struct
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        die(f"not a RIFF/WAVE file: {path}")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            tag, ch, sr, _, _, bits = struct.unpack_from("<HHIIHH", body, 0)
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack_from("<H", body, 24)[0]
            fmt = (tag, ch, sr, bits)
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        die(f"missing fmt/data chunk: {path}")
    tag, ch, sr, bits = fmt
    width = bits // 8
    raw = raw[: len(raw) - len(raw) % (width * ch)]
    if tag == 1 and width == 2:
        a, scale = array.array("h"), 1 / 32768.0
        a.frombytes(raw)
    elif tag == 1 and width == 3:
        n = len(raw) // 3
        buf = bytearray(4 * n)
        buf[1::4], buf[2::4], buf[3::4] = raw[0::3], raw[1::3], raw[2::3]
        a, scale = array.array("i"), 1 / 2147483648.0
        a.frombytes(bytes(buf))
    elif tag == 1 and width == 4:
        a, scale = array.array("i"), 1 / 2147483648.0
        a.frombytes(raw)
    elif tag == 1 and width == 1:
        a = [b - 128 for b in raw]
        scale = 1 / 128.0
    elif tag == 3 and width == 4:
        a, scale = array.array("f"), 1.0
        a.frombytes(raw)
    elif tag == 3 and width == 8:
        a, scale = array.array("d"), 1.0
        a.frombytes(raw)
    else:
        die(f"unsupported WAV format (tag {tag}, {bits} bit): {path}")
    if isinstance(a, array.array) and sys.byteorder != "little" and a.itemsize > 1:
        a.byteswap()
    if ch == 1:
        return [v * scale for v in a], sr, ch
    k = scale / ch
    return [sum(t) * k for t in zip(*(a[c::ch] for c in range(ch)))], sr, ch


def write_wav24_mono(path: Path, x, sr: int) -> None:
    ints = array.array("i", [(max(-8388608, min(8388607, math.floor(v * 8388608.0 + 0.5)))) << 8 for v in x])
    if sys.byteorder != "little":
        ints.byteswap()
    b = ints.tobytes()
    out = bytearray(3 * len(ints))
    out[0::3], out[1::3], out[2::3] = b[1::4], b[2::4], b[3::4]
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(sr)
        w.writeframes(bytes(out))


def write_wav16_mono(path: Path, x, sr: int) -> None:
    a = array.array("h", [max(-32768, min(32767, math.floor(v * 32768.0 + 0.5))) for v in x])
    if sys.byteorder != "little":
        a.byteswap()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(a.tobytes())


def wav_info(path: Path):
    """(duration_s, sr) without decoding; falls back to a full decode for formats wave rejects."""
    try:
        with wave.open(str(path), "rb") as w:
            return w.getnframes() / w.getframerate(), w.getframerate()
    except (wave.Error, EOFError):
        x, sr, _ = read_wav_mono(path)
        return len(x) / sr, sr


# --------------------------------------------------------------------------------------------
# Loudness: ITU-R BS.1770-4 integrated (K-weighting, 400 ms blocks, 75 % overlap, -70 LUFS absolute
# gate, -10 LU relative gate). Mono channel weight 1.0. Coefficients derived for any sample rate
# (libebur128 form); at 48 kHz they equal the tabulated BS.1770 values.
# --------------------------------------------------------------------------------------------
def k_weight_coeffs(sr: int):
    f0, g, q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    k = math.tan(math.pi * f0 / sr)
    vh = 10 ** (g / 20)
    vb = vh ** 0.4996667741545416
    a0 = 1 + k / q + k * k
    s1 = ((vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
          2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)
    f0, q = 38.13547087602444, 0.5003270373238773
    k = math.tan(math.pi * f0 / sr)
    a0 = 1 + k / q + k * k
    s2 = (1.0, -2.0, 1.0, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0)
    return s1, s2


def integrated_lufs(x, sr: int) -> float:
    (b0, b1, b2, a1, a2), (c0, c1, c2, d1, d2) = k_weight_coeffs(sr)
    x1 = x2 = y1 = y2 = z1 = z2 = 0.0
    sq = []
    ap = sq.append
    for xi in x:
        yi = b0 * xi + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, xi
        zi = c0 * yi + c1 * y1 + c2 * y2 - d1 * z1 - d2 * z2
        y2, y1 = y1, yi
        z2, z1 = z1, zi
        ap(zi * zi)
    csum = [0.0] + list(itertools.accumulate(sq))
    blk, hop = int(round(0.4 * sr)), int(round(0.1 * sr))
    if len(x) < blk:
        return float("-inf")
    z = [(csum[s + blk] - csum[s]) / blk for s in range(0, len(x) - blk + 1, hop)]
    lk = lambda p: -0.691 + 10 * math.log10(p) if p > 0 else float("-inf")
    z = [p for p in z if lk(p) > -70.0]
    if not z:
        return float("-inf")
    rel = lk(sum(z) / len(z)) - 10.0
    z = [p for p in z if lk(p) > rel]
    return lk(sum(z) / len(z))


def peak_dbfs(x) -> float:
    p = max((abs(v) for v in x), default=0.0)
    return 20 * math.log10(p) if p > 0 else float("-inf")


def envelope_lag_ms(ref, y, sr: int, max_ms: float = 300.0) -> float:
    """Lag of y against ref from 10 ms RMS envelopes (positive = y late). QC only."""
    hop = max(1, sr // 100)
    def env(s):
        n = len(s) // hop
        e = [math.sqrt(sum(v * v for v in s[i * hop:(i + 1) * hop]) / hop) for i in range(n)]
        m = sum(e) / max(1, len(e))
        return [v - m for v in e]
    a, b = env(ref), env(y)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    lim = int(max_ms / 10)
    best, best_lag = -1e300, 0
    for lag in range(-lim, lim + 1):
        if lag >= 0:
            c = sum(p * q for p, q in zip(a[: n - lag], b[lag:]))
        else:
            c = sum(p * q for p, q in zip(a[-lag:], b[: n + lag]))
        if c > best:
            best, best_lag = c, lag
    return best_lag * 10.0


# --------------------------------------------------------------------------------------------
# Corpus, categories, selection
# --------------------------------------------------------------------------------------------
NOTE_TO_SHARP = {"C": "C", "B#": "C", "C#": "C#", "DB": "C#", "D": "D", "D#": "D#", "EB": "D#",
                 "E": "E", "FB": "E", "F": "F", "E#": "F", "F#": "F#", "GB": "F#", "G": "G",
                 "G#": "G#", "AB": "G#", "A": "A", "A#": "A#", "BB": "A#", "B": "B", "CB": "B"}


def manual_key(clip: dict):
    """(root, scale, source) for the bench. manual_key > ground_truth.key > C chromatic."""
    text, source = clip.get("manual_key"), "manual_key"
    if not text and isinstance(clip.get("ground_truth"), dict):
        text, source = clip["ground_truth"].get("key"), "ground_truth.key"
    if not text:
        return "C", "chromatic", "none in manifest -> Chromatic"
    parts = str(text).replace("♯", "#").replace("♭", "b").split()
    root = NOTE_TO_SHARP.get(parts[0].upper()) if parts else None
    scale = parts[1].lower() if len(parts) > 1 else "major"
    if root is None or scale not in ("major", "minor", "chromatic"):
        die(f"{clip.get('clip_id')}: cannot parse key '{text}' (expected e.g. 'G# major')")
    return root, scale, f"{source} = {text}"


def categories_of(clip: dict) -> list[str]:
    tags = set(clip.get("vocal_tags", []))
    mc = clip.get("measured_characteristics", {}) or {}
    f0 = mc.get("f0_median_hz")
    sus = mc.get("longest_sustain_s")
    out = []
    if "male_vocal" in tags: out.append("male")
    if "female_vocal" in tags: out.append("female")
    if isinstance(f0, (int, float)) and f0 < LOW_F0_HZ: out.append("low_register")
    if isinstance(f0, (int, float)) and f0 >= HIGH_F0_HZ: out.append("high_register")
    if "vibrato" in tags: out.append("vibrato")
    if isinstance(sus, (int, float)) and sus >= SUSTAIN_S: out.append("sustained_notes")
    if tags & {"fast_note_transitions", "melismatic"}: out.append("transitions")
    if tags & {"rap", "noisy_vocal", "belting"}: out.append("difficult_phrases")
    return out


def select_clips(clips: list[dict], max_clips: int):
    """Greedy set cover of the categories (deterministic: ties broken by clip_id), then fill."""
    cats = {c["clip_id"]: categories_of(c) for c in clips}
    remaining = sorted(clips, key=lambda c: c["clip_id"])
    chosen, covered = [], set()
    while remaining and len(chosen) < max_clips:
        best = max(remaining, key=lambda c: (len(set(cats[c["clip_id"]]) - covered), -remaining.index(c)))
        if not set(cats[best["clip_id"]]) - covered:
            break
        chosen.append(best)
        covered |= set(cats[best["clip_id"]])
        remaining.remove(best)
    for c in remaining:                      # fill with the rest, alphabetical
        if len(chosen) >= max_clips:
            break
        chosen.append(c)
    coverage = {name: sorted(cid for cid in (c["clip_id"] for c in chosen) if name in cats[cid])
                for name, _ in CATEGORY_RULES}
    return chosen, coverage


def choose_window(x, sr: int, length_s: float):
    """Most active length_s window (50 ms frames above max(-50 dBFS, p95-30 dB)), boundaries snapped
    to the quietest nearby frame. Returns (t0, t1) seconds and a note."""
    usable = len(x) / sr - END_MARGIN_S
    if usable <= 1.0:
        die("clip shorter than 1.25 s")
    note = ""
    if usable < EXCERPT_MIN_S:
        return 0.0, round(usable, 2), f"clip shorter than {EXCERPT_MIN_S:.0f} s: full clip used"
    length_s = max(EXCERPT_MIN_S, min(EXCERPT_MAX_S, length_s, usable))
    fr = int(0.05 * sr)
    nfr = int(usable * sr) // fr
    csum = [0.0] + list(itertools.accumulate(v * v for v in x[: nfr * fr]))
    db = [10 * math.log10(max((csum[(i + 1) * fr] - csum[i * fr]) / fr, 1e-20)) for i in range(nfr)]
    ordered = sorted(db)
    thr = max(-50.0, ordered[int(0.95 * (len(ordered) - 1))] - 30.0)
    act = [0] + list(itertools.accumulate(1 if d > thr else 0 for d in db))
    wfr = int(round(length_s / 0.05))
    best_s, best_n = 0, -1
    for s in range(0, nfr - wfr + 1, 5):     # 0.25 s steps
        n = act[s + wfr] - act[s]
        if n > best_n:
            best_s, best_n = s, n
    def quietest(lo, hi):
        lo, hi = max(0, lo), min(nfr - 1, hi)
        return min(range(lo, hi + 1), key=lambda i: (db[i], i))
    s = quietest(best_s - 20, best_s + 10)
    e = quietest(best_s + wfr - 10, best_s + wfr + 20) + 1
    e = min(max(e, s + int(EXCERPT_MIN_S / 0.05)), s + int(EXCERPT_MAX_S / 0.05), nfr)
    return round(s * 0.05, 2), round(e * 0.05, 2), note or f"{best_n}/{wfr} active frames"


# --------------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------------
def bench_env(kit: Path, home: Path, session: str) -> dict:
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DYLD_", "SATTARI_"))}
    env["DYLD_LIBRARY_PATH"] = str(kit / "lib")   # the kit's onnxruntime, never a system/repo copy
    env["HOME"] = str(home)                        # nothing the bench writes lands in the real ~/Library
    env["SATTARI_SESSION_ID"] = session            # isolates the suite key bus from a running DAW
    return env


def run_bench(kit: Path, work: Path, src: Path, out_wav: Path, trace: Path, args: list, session: str,
              print_libs: bool = False):
    env = bench_env(kit, work / "home", session)
    if print_libs:
        env["DYLD_PRINT_LIBRARIES"] = "1"
    cmd = [str(kit / "bin/AutoPitchBench"), str(src), "--output", str(out_wav), "--trace", str(trace), *args]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0 or not out_wav.is_file() or not trace.is_file():
        die(f"render failed ({r.returncode}): {' '.join(cmd)}\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    lat = json.loads(trace.read_text()).get("reportedLatencySamples")
    return lat, r.stderr


def calibrate_latency(kit: Path, work: Path, sr: int) -> tuple[int, str]:
    """Bypass render of deterministic noise: output must equal input delayed by the reported latency."""
    rng = random.Random(1770)
    x = [0.0] * (sr // 4) + [rng.uniform(-0.25, 0.25) for _ in range(3 * sr)]
    src = work / f"calib-{sr}.wav"
    write_wav16_mono(src, x, sr)
    out, tr = work / f"calib-{sr}-out.wav", work / f"calib-{sr}.json"
    lat, err = run_bench(kit, work, src, out, tr, ["--disable-correction", "--block", "512"],
                         f"listening-calib-{sr}", print_libs=True)
    y, _, _ = read_wav_mono(out)
    x16, _, _ = read_wav_mono(src)
    if not isinstance(lat, int) or lat <= 0 or lat >= END_MARGIN_S * sr:
        die(f"implausible reported latency {lat} at {sr} Hz")
    worst = max(abs(a - b) for a, b in zip(x16[: len(x16) - lat], y[lat:]))
    if worst > 2.0 / 32768:
        die(f"latency calibration FAILED at {sr} Hz: bypass output != input delayed {lat} samples "
            f"(max err {worst:.2e})")
    onnx = [m.group(1) for m in (re.search(r"(/.*libonnxruntime[^/]*\.dylib)\s*$", ln) for ln in err.splitlines()) if m]
    if not onnx or not all(Path(p).resolve().is_relative_to((kit / "lib").resolve()) for p in onnx):
        die(f"onnxruntime was not loaded from the kit (dyld: {onnx or 'not reported'})")
    for p in (src, out, tr):
        p.unlink()
    return lat, "kit/lib/" + Path(onnx[0]).name


# --------------------------------------------------------------------------------------------
# Main build
# --------------------------------------------------------------------------------------------
def load_corpus(path: Path):
    m = json.loads(path.read_text())
    clips = []
    for c in m.get("clips", []):
        if c.get("cleared_for_internal_eval") is not True or c.get("permission_status") not in REAL_POLICIES:
            continue
        src = Path(str(c.get("source_path", "")))
        if not src.is_absolute():
            src = (path.parent / src).resolve()
        c = dict(c, _src=src)
        clips.append(c)
    return m, clips


def autotune_file(dirpath: Path | None, clip_id: str):
    if dirpath is None:
        return None, "no --autotune-dir given"
    exact = dirpath / f"{clip_id}.wav"
    if exact.is_file():
        return exact, "exact name"
    hits = sorted(p for p in dirpath.glob(f"{clip_id}*.wav") if p.is_file())
    if len(hits) == 1:
        return hits[0], "prefix match"
    if len(hits) > 1:
        die(f"--autotune-dir has several files for {clip_id}: {[h.name for h in hits]}")
    return None, "no file for this clip id"


def parse_windows(items: list[str]) -> dict:
    out = {}
    for it in items or []:
        try:
            cid, rng_ = it.split("=", 1)
            a, b = (float(v) for v in rng_.split(":"))
        except ValueError:
            die(f"--window expects clip_id=start:end seconds, got {it}")
        out[cid] = (a, b)
    return out


def require_free(path: Path, floor_gb: float, need_bytes: int = 0) -> float:
    probe = path
    while not probe.exists():
        probe = probe.parent
    free = shutil.disk_usage(probe).free
    if free / 2**30 < floor_gb or (free - need_bytes) / 2**30 < floor_gb:
        die(f"disk preflight: {free / 2**30:.1f} GB free on {probe}, need {need_bytes / 2**30:.2f} GB "
            f"and a {floor_gb:.0f} GB floor (--min-free-gb / BENCH_MIN_FREE_GB)")
    return free / 2**30


def build(args) -> int:
    out = Path(args.out).expanduser().resolve()
    key_path = out.parent / f"{out.name}-KEY.json"
    work = Path(args.work).expanduser().resolve() if args.work else out.parent / f"{out.name}-work"
    floor = float(os.environ.get("BENCH_MIN_FREE_GB", args.min_free_gb))

    if args.dry_run:
        work = Path(tempfile.mkdtemp(prefix="listening-dry-run-"))
    else:
        for p in (out, key_path):
            if p.exists() and (p.is_file() or any(p.iterdir())) and not args.force:
                die(f"refusing to overwrite {p} (a package must never change after scoring starts; "
                    "choose a new --out, or --force before any scoring)")
        require_free(out.parent, floor)
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
    (work / "home").mkdir(exist_ok=True)
    try:
        return _build(args, out, key_path, work, floor)
    finally:
        if args.dry_run or not args.keep_work:
            shutil.rmtree(work, ignore_errors=True)


def _build(args, out: Path, key_path: Path, work: Path, floor: float) -> int:
    kit = resolve_kit(args.kit, work)
    kit_manifest = None
    if kit is not None:
        kit_manifest = verify_kit(kit, args.expected_bench_sha256)
        print(f"kit verified: {kit}  (commit {kit_manifest['candidate']['commit'][:12]}, "
              f"{len(kit_manifest['files'])} files, AutoPitchBench {kit_manifest['candidate']['bench_sha256'][:16]})")
    elif not args.dry_run:
        die("no candidate kit: pass --kit <dir|tar.gz> or run the copy of this script inside the kit")
    else:
        print("kit: not given (dry run lists the selection only)")

    if args.manifest:
        manifest_path = Path(args.manifest).expanduser().resolve()
    elif kit is not None and (kit / "corpus/auto-pitch.json").is_file():
        manifest_path = kit / "corpus/auto-pitch.json"
    elif REPO is not None:
        manifest_path = REPO / "validation/corpus/auto-pitch.json"
    else:
        die("no corpus manifest: pass --manifest")
    corpus, clips = load_corpus(manifest_path)
    if not clips:
        die(f"no cleared clips in {manifest_path}")
    chosen, coverage = select_clips(clips, args.max_clips)
    gaps = [n for n, cl in coverage.items() if not cl]
    thin = [n for n, cl in coverage.items() if len(cl) == 1]
    windows_override = parse_windows(args.window)
    at_dir = Path(args.autotune_dir).expanduser().resolve() if args.autotune_dir else None
    if at_dir is not None and not at_dir.is_dir():
        die(f"--autotune-dir is not a directory: {at_dir}")

    print(f"corpus: {manifest_path}  ({len(clips)} cleared clips, {len(chosen)} selected)")
    plan, missing_src = [], []
    for c in chosen:
        cid = c["clip_id"]
        root, scale, ksrc = manual_key(c)
        at, at_why = autotune_file(at_dir, cid)
        entry = {"clip": c, "key": (root, scale, ksrc), "autotune": at, "autotune_note": at_why}
        if not c["_src"].is_file():
            missing_src.append(cid)
            entry["window"], entry["window_note"] = None, "SOURCE MISSING on this machine"
        else:
            x, sr, ch = read_wav_mono(c["_src"])
            entry.update(x=x, sr=sr, channels=ch)
            if cid in windows_override:
                a, b = windows_override[cid]
                if not (0 <= a < b <= len(x) / sr - END_MARGIN_S):
                    die(f"--window {cid}={a}:{b} is outside the usable clip (0-{len(x) / sr - END_MARGIN_S:.2f} s)")
                entry["window"], entry["window_note"] = (a, b), "operator --window"
            else:
                t0, t1, note = choose_window(x, sr, args.excerpt_seconds)
                entry["window"], entry["window_note"] = (t0, t1), note
        plan.append(entry)

    print("\nSELECTION (one excerpt per clip; set order and labels are randomized later)")
    for e in plan:
        c = e["clip"]
        w = e["window"]
        ws = f"{w[0]:6.2f}-{w[1]:6.2f} s" if w else "   (no audio)   "
        print(f"  {c['clip_id']:<24} {ws}  key: {e['key'][0]} {e['key'][1]:<9} "
              f"cats: {', '.join(categories_of(c)) or '-'}  [{e['window_note']}]")
        if at_dir is not None:
            print(f"  {'':<24} Auto-Tune ref: {e['autotune'].name if e['autotune'] else 'none'} ({e['autotune_note']})")
    print("\nCOVERAGE")
    for (name, rule) in CATEGORY_RULES:
        cl = coverage[name]
        print(f"  {name:<18} {', '.join(cl) if cl else 'MISSING - not in this corpus'}   (rule: {rule})")
    if gaps:
        print(f"  GAPS (reported, not faked): {', '.join(gaps)}")
    if thin:
        print(f"  thin (one clip only): {', '.join(thin)}")
    at_status = ("omitted: no --autotune-dir given" if at_dir is None else
                 f"{sum(1 for e in plan if e['autotune'])}/{len(plan)} clips have an owner-provided render")
    print(f"  Auto-Tune Pro slot: {at_status}")

    if args.dry_run:
        if missing_src:
            print(f"\nNOTE: sources missing here: {', '.join(missing_src)}")
        print("\ndry run: nothing rendered or written")
        return 0
    if missing_src:
        die(f"sources not found: {', '.join(missing_src)}")

    # --- disk estimate: 4 full renders (16-bit stereo) per clip + calibration + package ---------
    need = sum(len(e["x"]) * 4 * 4 for e in plan) + sum(int((e["window"][1] - e["window"][0]) * e["sr"]) * 3 * 5 for e in plan)
    free_gb = require_free(out.parent, floor, need)
    print(f"\ndisk: {free_gb:.1f} GB free, estimated need {need / 2**20:.0f} MB, floor {floor:.0f} GB")

    # --- latency calibration + kit load check (per sample rate) -----------------------------------
    latency, onnx_loaded = {}, None
    for sr in sorted({e["sr"] for e in plan}):
        latency[sr], onnx_loaded = calibrate_latency(kit, work, sr)
        print(f"latency @ {sr} Hz: {latency[sr]} samples (bypass render is the input delayed exactly by it); "
              f"onnxruntime loaded from {onnx_loaded}")

    seed = args.seed if args.seed is not None else secrets.randbelow(2**31)
    rng = random.Random(seed)
    order = sorted(plan, key=lambda e: e["clip"]["clip_id"])
    rng.shuffle(order)
    out.mkdir(parents=True, exist_ok=True)
    for p in out.iterdir():               # --force: start clean
        (shutil.rmtree if p.is_dir() else Path.unlink)(p)

    key_sets, files, csv_rows = [], {}, []
    fade = lambda n: [0.5 - 0.5 * math.cos(math.pi * i / n) for i in range(n)]
    for si, e in enumerate(order, start=1):
        c, sr, x = e["clip"], e["sr"], e["x"]
        cid, (root, scale, ksrc) = c["clip_id"], e["key"]
        lat = latency[sr]
        s0 = int(round(e["window"][0] * sr))
        n = int(round(e["window"][1] * sr)) - s0
        renders = {
            "ap_natural": ["--style", "Natural", "--key", root, "--scale", scale, *RENDER_COMMON],
            "ap_classic": ["--style", "Classic", "--key", root, "--scale", scale, *RENDER_COMMON],
            "ap_autokey": ["--style", args.autokey_style, "--auto-key", "--manual-key-untouched", *RENDER_COMMON],
        }
        items = {"dry": {"x": x[s0:s0 + n], "detail": {"source_sha256": sha256_file(c["_src"])}}}
        for cond, bargs in renders.items():
            rw, tr = work / f"{cid}-{cond}.wav", work / f"{cid}-{cond}.json"
            rlat, _ = run_bench(kit, work, c["_src"], rw, tr, bargs, f"listening-{cid}-{cond}"[:48])
            if rlat != lat:
                die(f"{cid} {cond}: reported latency {rlat} != calibrated {lat}")
            y, ysr, _ = read_wav_mono(rw)
            if ysr != sr or len(y) < s0 + lat + n:
                die(f"{cid} {cond}: render too short or wrong rate")
            seg = y[s0 + lat:s0 + lat + n]
            if any(v != v or abs(v) == float("inf") for v in seg):
                die(f"{cid} {cond}: non-finite samples in render")
            items[cond] = {"x": seg, "detail": {"bench_args": bargs, "render_sha256": sha256_file(rw),
                                                 "latency_removed_samples": lat}}
        if e["autotune"] is not None:
            y, ysr, _ = read_wav_mono(e["autotune"])
            if ysr != sr:
                print(f"  WARNING {cid}: Auto-Tune render is {ysr} Hz, source {sr} Hz - slot skipped (no resampler)")
                e["autotune_note"] = f"skipped: sample rate {ysr} != {sr}"
            else:
                off = 0
                if args.autotune_align == "auto":
                    off = int(round(envelope_lag_ms(x[s0:s0 + n], y[s0:s0 + n], sr) * sr / 1000))
                if s0 + off < 0 or len(y) < s0 + off + n:
                    print(f"  WARNING {cid}: Auto-Tune render does not cover the excerpt - slot skipped")
                    e["autotune_note"] = "skipped: render shorter than the excerpt"
                else:
                    items["autotune_pro"] = {"x": y[s0 + off:s0 + off + n],
                                             "detail": {"autotune_source_file": e["autotune"].name,
                                                        "file_sha256": sha256_file(e["autotune"]),
                                                        "offset_applied_samples": off}}

        # fades, loudness, one shared per-set safety reduction
        nf = min(int(FADE_MS / 1000 * sr), n // 4)
        ramp = fade(nf)
        for it in items.values():
            s = list(it["x"])
            for i in range(nf):
                s[i] *= ramp[i]
                s[-1 - i] *= ramp[i]
            it["x"] = s
            it["lufs_in"] = integrated_lufs(s, sr)
            if it["lufs_in"] == float("-inf"):
                die(f"{cid}: an item is silent (below the -70 LUFS gate); pick another --window")
            it["peak_in"] = peak_dbfs(s)
        reduction = min(0.0, min(PEAK_CEILING_DBFS - (it["peak_in"] + args.target_lufs - it["lufs_in"])
                                 for it in items.values()))
        conds = [k for k in CONDITION_TEXT if k in items]
        rng.shuffle(conds)
        set_id = f"S{si:02d}"
        set_key = {"set": set_id, "clip_id": cid, "window_s": list(e["window"]), "window_note": e["window_note"],
                   "manual_key": f"{root} {scale} ({ksrc})", "autotune_slot": e["autotune_note"],
                   "shared_peak_reduction_db": round(reduction, 3), "items": []}
        dry_x = items["dry"]["x"]
        for label, cond in zip("ABCDE", conds):
            it = items[cond]
            gain_db = args.target_lufs - it["lufs_in"] + reduction
            g = 10 ** (gain_db / 20)
            y = [v * g for v in it["x"]]
            name = f"{set_id}-{label}.wav"
            write_wav24_mono(out / name, y, sr)
            files[name] = sha256_file(out / name)
            lag = 0.0 if cond == "dry" else envelope_lag_ms(dry_x, it["x"], sr)
            if cond == "autotune_pro" and abs(lag) > 20.0 and args.autotune_align == "none":
                print(f"  WARNING {cid}: the Auto-Tune render looks offset by {lag:+.0f} ms against the dry "
                      "excerpt (check the bounce start, or rerun with --autotune-align auto)")
            set_key["items"].append({
                "label": label, "file": name, "condition": cond, "condition_text": CONDITION_TEXT[cond],
                "lufs_before": round(it["lufs_in"], 2), "gain_db": round(gain_db, 2),
                "lufs_after": round(integrated_lufs(y, sr), 2), "peak_dbfs_after": round(peak_dbfs(y), 2),
                "envelope_lag_vs_dry_ms": lag, **it["detail"]})
            csv_rows.append([set_id, label, name, "", "", "", "", "", ""])
        key_sets.append(set_key)
        print(f"  {set_id}: {len(conds)} items, loudness-matched to {args.target_lufs + reduction:.1f} LUFS")

    # --- sealed key (outside the package) -------------------------------------------------------
    key = {
        "WARNING": "ANSWER KEY - do not open until every score in SCORING-SHEET.csv is filled in.",
        "package_dir": out.name, "seed": seed,
        "candidate_commit": kit_manifest["candidate"]["commit"],
        "latency_samples_by_rate": {str(k): v for k, v in latency.items()},
        "sets": key_sets,
    }
    key_bytes = (json.dumps(key, indent=2) + "\n").encode()
    if key_path.exists():
        os.chmod(key_path, 0o644)
    key_path.write_bytes(key_bytes)
    os.chmod(key_path, 0o444)

    # --- scoring sheet + instructions -----------------------------------------------------------
    sheet = io.StringIO()
    wr = csv.writer(sheet, lineterminator="\n")
    wr.writerow(["set", "label", "file", "naturalness_1to5", "artifacts_1to5_5_is_none",
                 "tuning_accuracy_1to5", "rank_in_set_1_is_best", "defects_with_timestamps", "notes"])
    wr.writerows(sorted(csv_rows))
    (out / "SCORING-SHEET.csv").write_text(sheet.getvalue())
    files["SCORING-SHEET.csv"] = "(listener fills this in; not hashed)"
    counts = sorted({len(s["items"]) for s in key_sets})
    n_items = str(counts[0]) if len(counts) == 1 else f"{counts[0]}-{counts[-1]}"
    instructions = f"""AUTO PITCH BLIND LISTENING - {len(key_sets)} sets, {len(csv_rows)} files

Each set (S01, S02, ...) is one vocal excerpt in {n_items} versions labelled A, B, C, ...
One version is the untouched original; the others are tuned. Which is which is random per set
and is NOT the same letter from set to set. All versions are already loudness-matched: do not
change gain between files in a set.

1. Listen on the monitoring you mix on. Loop a set, switch between its letters freely.
2. For every file, fill one row of SCORING-SHEET.csv (Numbers/Excel/TextEdit):
     naturalness       1 = obviously processed/robotic ... 5 = sounds like the real singer
     artifacts         1 = severe (warble, crackle, metallic, glitches) ... 5 = none heard
     tuning accuracy   1 = clearly out of tune ... 5 = perfectly in tune
     rank in set       1 = the version you would use, 2 = next, ...
     defects           time + what, e.g. "0:04.2 warble on long 'oh'; 0:11 click"
3. Score every set before looking at anything else. Do NOT open the -KEY.json file next to
   this folder until the whole sheet is filled in; opening it early invalidates the session.
4. Hearing no difference is a valid result: score it as you hear it.

Send back: SCORING-SHEET.csv, PACKAGE-MANIFEST.json, the -KEY.json file (after scoring), and the
output of the --verify command. Never send or upload the audio.
"""
    (out / "LISTENER-INSTRUCTIONS.txt").write_text(instructions)
    files["LISTENER-INSTRUCTIONS.txt"] = sha256_file(out / "LISTENER-INSTRUCTIONS.txt")

    # --- package manifest (no mapping; records the key's sha256) --------------------------------
    manifest = {
        "package_format": PACKAGE_FORMAT,
        "truth_label": "internal validation only; vocal-derived audio - never upload, share or commit",
        "candidate": {"commit": kit_manifest["candidate"]["commit"],
                      "bench_sha256": kit_manifest["candidate"]["bench_sha256"],
                      "kit_manifest_sha256": sha256_file(kit / "KIT-MANIFEST.json"),
                      "onnxruntime_loaded_from": onnx_loaded},
        "builder_sha256": sha256_file(Path(__file__).resolve()),
        "corpus_manifest_sha256": sha256_file(manifest_path),
        "key_file": {"name": key_path.name, "location": "next to the package folder, NOT inside it",
                     "sha256": hashlib.sha256(key_bytes).hexdigest()},
        "blinding": {"labels": "A.. per set, randomized per set", "set_order": "randomized",
                     "seed": "recorded in the key file only"},
        "conditions_in_every_set": [CONDITION_TEXT[k] for k in ("dry", "ap_natural", "ap_classic", "ap_autokey")],
        "autotune_pro_slot": at_status,
        "render_settings": {"common_bench_args": RENDER_COMMON, "styles": ["Natural", "Classic"],
                            "autokey_style": args.autokey_style,
                            "key": "manifest manual_key / ground_truth.key, else C Chromatic",
                            "rendering": "full clip rendered, excerpt cut after removing the calibrated latency",
                            "latency_samples_by_rate": {str(k): v for k, v in latency.items()}},
        "loudness": {"method": "ITU-R BS.1770-4 integrated loudness (K-weighted, gated), mono; every item "
                               "gain-matched to the target, then ONE shared per-set reduction keeps every "
                               "sample peak <= -1 dBFS",
                     "target_lufs": args.target_lufs, "peak_ceiling_dbfs": PEAK_CEILING_DBFS,
                     "fade_ms": FADE_MS, "output": "mono 24-bit PCM WAV at the source sample rate"},
        "selection": {"excerpt_seconds": args.excerpt_seconds, "bounds_s": [EXCERPT_MIN_S, EXCERPT_MAX_S],
                      "rule": "most active window, boundaries snapped to the quietest nearby 50 ms frame",
                      "clips": sorted(e["clip"]["clip_id"] for e in plan)},
        "coverage": {"rules": dict(CATEGORY_RULES), "clips": coverage, "gaps": gaps, "thin": thin},
        "files": {k: v for k, v in sorted(files.items()) if k != "SCORING-SHEET.csv"},
    }
    (out / "PACKAGE-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")

    print(f"\npackage : {out}")
    print(f"key     : {key_path}   (sealed; sha256 recorded in PACKAGE-MANIFEST.json)")
    print(f"work dir: {'kept (contains UNBLINDED renders): ' + str(work) if args.keep_work else 'removed'}")
    if gaps:
        print(f"coverage gaps: {', '.join(gaps)}")
    return 0


def verify_package(pkg: Path, key_arg: str | None) -> int:
    pkg = pkg.expanduser().resolve()
    m = json.loads((pkg / "PACKAGE-MANIFEST.json").read_text())
    bad = []
    for name, digest in m["files"].items():
        p = pkg / name
        if not p.is_file() or sha256_file(p) != digest:
            bad.append(f"changed or missing: {name}")
    listed = set(m["files"]) | {"PACKAGE-MANIFEST.json", "SCORING-SHEET.csv"}
    bad += [f"unexpected file: {p.name}" for p in sorted(pkg.iterdir())
            if p.name not in listed and p.name not in IGNORED_NAMES]
    key_path = Path(key_arg).expanduser().resolve() if key_arg else pkg.parent / m["key_file"]["name"]
    if not key_path.is_file():
        bad.append(f"key file not found: {key_path}")
    elif sha256_file(key_path) != m["key_file"]["sha256"]:
        bad.append(f"KEY FILE sha256 does not match the manifest: {key_path}")
    filled = total = 0
    sheet = pkg / "SCORING-SHEET.csv"
    if sheet.is_file():
        for row in csv.DictReader(sheet.read_text().splitlines()):
            total += 1
            filled += all(row.get(k, "").strip() for k in
                          ("naturalness_1to5", "artifacts_1to5_5_is_none", "tuning_accuracy_1to5"))
    print(f"package: {pkg}\ncandidate: {m['candidate']['commit']}  bench {m['candidate']['bench_sha256'][:16]}")
    print(f"audio files: {sum(1 for k in m['files'] if k.endswith('.wav'))}, scores filled: {filled}/{total}")
    if bad:
        print("VERIFY: FAIL")
        for b in bad:
            print(f"  - {b}")
        return 4
    print("VERIFY: PASS (audio, instructions and sealed key match the manifest)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="package directory to create (key goes to <out>-KEY.json beside it)")
    ap.add_argument("--kit", help="candidate kit directory or .tar.gz (default: the kit this script sits in)")
    ap.add_argument("--manifest", help="corpus manifest (default: kit corpus/auto-pitch.json, else repo copy)")
    ap.add_argument("--seed", type=int, help="blinding seed; same seed + inputs = identical package. "
                                             "Default: random, stored only in the key file")
    ap.add_argument("--autotune-dir", help="owner-provided Auto-Tune Pro bounces, <clip_id>.wav, full clip from 0 s")
    ap.add_argument("--autotune-align", choices=["none", "auto"], default="none",
                    help="none: bounces are assumed time-aligned (DAW bounce with PDC). auto: shift by envelope lag")
    ap.add_argument("--autokey-style", default="Classic", choices=["Classic", "Natural"])
    ap.add_argument("--excerpt-seconds", type=float, default=15.0)
    ap.add_argument("--window", action="append", metavar="CLIP=START:END", help="override an excerpt window")
    ap.add_argument("--max-clips", type=int, default=12)
    ap.add_argument("--target-lufs", type=float, default=-20.0)
    ap.add_argument("--expected-bench-sha256", default=PINNED_BENCH_SHA256,
                    help="sha256 the kit's AutoPitchBench must have (default: the cbd1ce4 candidate)")
    ap.add_argument("--min-free-gb", type=float, default=8.0)
    ap.add_argument("--work", help="scratch directory for full renders (default: <out>-work, removed after)")
    ap.add_argument("--keep-work", action="store_true", help="keep the UNBLINDED renders in the work dir")
    ap.add_argument("--dry-run", action="store_true", help="verify the kit and list the selection only")
    ap.add_argument("--force", action="store_true", help="replace an existing package (never after scoring began)")
    ap.add_argument("--verify", metavar="PACKAGE", help="verify a finished package against its manifest and key")
    ap.add_argument("--key", help="key file for --verify (default: <package>-KEY.json beside it)")
    args = ap.parse_args()
    if sys.version_info < (3, 9):
        die("python 3.9+ required")
    if args.verify:
        return verify_package(Path(args.verify), args.key)
    if not args.out:
        if args.dry_run:
            args.out = str(Path.cwd() / "listening-dry-run")
        else:
            die("--out is required")
    if not (EXCERPT_MIN_S <= args.excerpt_seconds <= EXCERPT_MAX_S):
        die(f"--excerpt-seconds must be within {EXCERPT_MIN_S:.0f}-{EXCERPT_MAX_S:.0f}")
    return build(args)


if __name__ == "__main__":
    raise SystemExit(main())
