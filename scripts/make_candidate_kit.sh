#!/usr/bin/env bash
# Package the EXACT Auto Pitch release-candidate render binary into a portable, hash-manifested kit
# for scripts/build_listening_package.py on another Mac (the owner's "lillypad").
#
#   scripts/make_candidate_kit.sh                    # candidate = tag auto-pitch-rc-baseline-20260925
#   scripts/make_candidate_kit.sh --rev <rev> --build-dir <dir> --out <dir>
#
# Output: dist/auto-pitch-candidate-<short-sha>.tar.gz (+ .sha256 sidecar). dist/ is gitignored.
#
# Read-only on the build tree: the binary is COPIED, never relinked, rebuilt or re-signed, so the kit's
# AutoPitchBench is byte-identical to the fingerprinted candidate (sha256 checked against the
# qualification report, and the original is re-hashed afterwards to prove it was not touched).
# Portability: its only non-system dependency is @rpath/libonnxruntime.1.22.0.dylib, whose LC_RPATH is
# an absolute path into this repo. The kit ships that dylib in lib/ and the builder runs the binary with
# DYLD_LIBRARY_PATH=<kit>/lib (dyld searches it before LC_RPATH), which keeps the binary unmodified.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rev="auto-pitch-rc-baseline-20260925"
build_dir="$repo/build/auto-pitch-neural"
out_dir="$repo/dist"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --rev) rev="$2"; shift 2 ;;
    --build-dir) build_dir="$(cd "$2" && pwd)"; shift 2 ;;
    --out) mkdir -p "$2"; out_dir="$(cd "$2" && pwd)"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
sha() { shasum -a 256 "$1" | awk '{print $1}'; }

commit="$(git -C "$repo" rev-parse "$rev^{commit}")" || die "cannot resolve $rev"
short="${commit:0:7}"
bench="$build_dir/AutoPitchBench"
[[ -f "$bench" ]] || die "missing $bench"

# Disk preflight (same floor as validation/accuracy_20260923/diskguard.py).
free_gb=$(df -g "$repo" | awk 'NR==2{print $4}')
(( free_gb >= ${BENCH_MIN_FREE_GB:-8} )) || die "only ${free_gb} GB free (< ${BENCH_MIN_FREE_GB:-8} GB floor)"

# The binary must be the one the qualification report fingerprinted for this commit.
qual="$(ls "$repo"/validation/auto-pitch/qualification/*-"$short".md 2>/dev/null | head -1 || true)"
[[ -n "$qual" ]] || die "no qualification report validation/auto-pitch/qualification/*-$short.md for $commit"
want16="$(awk -F'`' '/^\| AutoPitchBench \|/{print $2; exit}' "$qual")"
bench_sha_before="$(sha "$bench")"
[[ -n "$want16" && "${bench_sha_before:0:16}" == "$want16" ]] \
  || die "AutoPitchBench sha256 ${bench_sha_before:0:16} != fingerprint '$want16' in ${qual#$repo/}"

# Resolve every non-system dylib (only @rpath/libonnxruntime is expected).
rpaths=()
while IFS= read -r rp; do rpaths+=("$rp"); done < <(otool -l "$bench" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | sort -u)
deps=()
while IFS= read -r d; do deps+=("$d"); done < <(otool -L "$bench" | awk '/^\t/ && $1 !~ /^\/(System|usr\/lib)\// {print $1}' | sort -u)
libs=()
for d in ${deps[@]+"${deps[@]}"}; do
  case "$d" in
    @rpath/*)
      leaf="${d#@rpath/}"; found=""
      for rp in ${rpaths[@]+"${rpaths[@]}"}; do [[ -f "$rp/$leaf" ]] && { found="$rp/$leaf"; break; }; done
      [[ -n "$found" ]] || die "cannot resolve $d via LC_RPATH ${rpaths[*]:-none}"
      libs+=("$found")
      # The bundled dylib must itself need nothing outside the system.
      extra="$(otool -L "$found" | awk '/^\t/ && $1 !~ /^\/(System|usr\/lib)\// && $1 != "'"$d"'" {print $1}' | sort -u)"
      [[ -z "$extra" ]] || die "$leaf has further non-system deps: $extra"
      ;;
    *) die "unexpected non-system dependency $d (only @rpath/libonnxruntime is handled)" ;;
  esac
done

stage_root="$(mktemp -d "${TMPDIR:-/tmp}/apkit.XXXXXX")"
trap 'rm -rf "$stage_root"' EXIT
name="auto-pitch-candidate-$short"
kit="$stage_root/$name"
mkdir -p "$kit/bin" "$kit/lib" "$kit/corpus" "$kit/provenance"
cp "$bench" "$kit/bin/AutoPitchBench"
for l in ${libs[@]+"${libs[@]}"}; do cp "$l" "$kit/lib/$(basename "$l")"; done
chmod 755 "$kit/bin/AutoPitchBench" "$kit"/lib/*
cp "$repo/scripts/build_listening_package.py" "$kit/build_listening_package.py"
cp "$repo/validation/corpus/auto-pitch.json" "$kit/corpus/auto-pitch.json"
cp "$qual" "$kit/provenance/$(basename "$qual")"

[[ "$(sha "$kit/bin/AutoPitchBench")" == "$bench_sha_before" ]] || die "copy of AutoPitchBench differs"
# Apple silicon requires a valid signature (the arm64 slice is linker-signed ad hoc); Intel does not,
# and the x86_64 slice is unsigned as built. Nothing is re-signed: that would change the hash.
codesign --verify -a arm64 "$kit/bin/AutoPitchBench" || die "arm64 slice of the copied binary fails codesign --verify"

# Smoke: the copy must load the KIT's onnxruntime (not the repo's) and render a tone.
python3 - "$stage_root/tone.wav" <<'PY'
import math, struct, sys, wave
sr = 48000
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<h", int(9000 * math.sin(2 * math.pi * 223.0 * i / sr))) for i in range(sr)))
PY
mkdir -p "$stage_root/home"
smoke="$(env -u DYLD_FALLBACK_LIBRARY_PATH HOME="$stage_root/home" SATTARI_SESSION_ID=kit-smoke \
  DYLD_LIBRARY_PATH="$kit/lib" DYLD_PRINT_LIBRARIES=1 \
  "$kit/bin/AutoPitchBench" "$stage_root/tone.wav" --smoke 2>&1)" || die "kit smoke render failed: $smoke"
loaded="$(grep -o '/.*libonnxruntime[^/]*\.dylib' <<<"$smoke" | head -1)"
[[ "$(cd "$(dirname "$loaded")" && pwd -P)" == "$(cd "$kit/lib" && pwd -P)" ]] \
  || die "smoke loaded onnxruntime from '$loaded', not the kit"
rm -f "$stage_root/tone.wav"

minos="$(otool -l "$bench" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')"
archs="$(lipo -archs "$bench")"
head_commit="$(git -C "$repo" rev-parse HEAD)"
dirty_scripts="$(git -C "$repo" status --porcelain -- scripts/build_listening_package.py validation/corpus/auto-pitch.json | wc -l | tr -d ' ')"

python3 - "$kit" "$commit" "$rev" "$bench_sha_before" "${qual#$repo/}" "$minos" "$archs" "$head_commit" "$dirty_scripts" <<'PY'
import hashlib, json, os, sys, datetime
kit, commit, rev, bench_sha, qual, minos, archs, head, dirty = sys.argv[1:]
files = {}
for root, _, names in os.walk(kit):
    for n in sorted(names):
        p = os.path.join(root, n)
        rel = os.path.relpath(p, kit)
        files[rel] = {"sha256": hashlib.sha256(open(p, "rb").read()).hexdigest(), "bytes": os.path.getsize(p)}
m = {
    "kit_format": 1,
    "candidate": {"commit": commit, "rev": rev, "bench_sha256": bench_sha, "qualification_report": qual},
    "made_from": {"repo_head": head,
                  "builder_and_corpus_uncommitted_changes": int(dirty) > 0,
                  "created_utc": datetime.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"},
    "runtime": {"run": "DYLD_LIBRARY_PATH=<kit>/lib <kit>/bin/AutoPitchBench ...",
                "why": "binary kept byte-identical to the candidate; its LC_RPATH points into the build repo",
                "archs": archs, "min_macos": minos},
    "files": dict(sorted(files.items())),
}
open(os.path.join(kit, "KIT-MANIFEST.json"), "w").write(json.dumps(m, indent=2) + "\n")
PY

mkdir -p "$out_dir"
tarball="$out_dir/$name.tar.gz"
COPYFILE_DISABLE=1 tar -czf "$tarball" -C "$stage_root" "$name"
(cd "$out_dir" && shasum -a 256 "$name.tar.gz" > "$name.tar.gz.sha256")

[[ "$(sha "$bench")" == "$bench_sha_before" ]] || die "ORIGINAL $bench changed during packaging"

echo "candidate : $commit ($rev)"
echo "binary    : AutoPitchBench sha256 $bench_sha_before (matches ${qual#$repo/})"
echo "deps      : ${libs[*]:-none} -> kit lib/ ; archs $archs ; min macOS $minos"
echo "smoke     : OK, onnxruntime loaded from the kit"
echo "kit       : $tarball ($(du -h "$tarball" | awk '{print $1}'))"
echo "sha256    : $(awk '{print $1}' "$out_dir/$name.tar.gz.sha256")"
echo "manifest  : KIT-MANIFEST.json sha256 $(sha "$kit/KIT-MANIFEST.json")"
