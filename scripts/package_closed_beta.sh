#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="0.2.0-alpha"
suite_pkg="${SATTARI_SUITE_PKG:-$repo_root/build/release-candidate/Sattari-Audio-Suite.pkg}"
auto_archive="${SATTARI_AUTOPITCH_TESTER_ARCHIVE:-}"
out_root="${1:-$repo_root/build/closed-beta}"

if [[ -z "$auto_archive" ]]; then
  auto_archive="$(find "$repo_root/build/tester-packages" -maxdepth 1 -type f \
    -name 'sattari-auto-pitch-neural-tester-v*.tar.gz' -print 2>/dev/null \
    | sort | tail -1)"
fi

[[ -f "$suite_pkg" ]] || { echo "missing suite installer: $suite_pkg" >&2; exit 2; }
[[ -n "$auto_archive" && -f "$auto_archive" ]] || {
  echo "missing Auto Pitch tester archive; run scripts/package_auto_pitch_neural_tester.sh" >&2
  exit 2
}

package_name="Sattari-Audio-Closed-Beta-v$version"
stage="$(mktemp -d "$out_root/.closed-beta.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
package="$stage/$package_name"
mkdir -p "$package/Installers" "$package/Validation"

ditto "$suite_pkg" "$package/Installers/$(basename "$suite_pkg")"
ditto "$auto_archive" "$package/Installers/$(basename "$auto_archive")"
cp "$repo_root/validation/closed-beta/README.md" "$package/README.md"
cp "$repo_root/validation/daw/"suite-daw-validation-*.md "$package/Validation/"
cp "$repo_root/validation/auto-pitch/2026-08-09-objective-benchmark.md" "$package/Validation/"
cp "$repo_root/validation/auto-pitch/corpus-manifest.example.json" "$package/Validation/"
cp "$repo_root/scripts/qualify_auto_pitch_corpus.py" "$package/Validation/"

(
  cd "$package"
  find Installers Validation -type f -print0 | sort -z \
    | xargs -0 shasum -a 256 > SHA256SUMS.txt
)

mkdir -p "$out_root"
final_dir="$out_root/$package_name"
archive="$out_root/$package_name.tar.gz"
[[ ! -e "$final_dir" && ! -e "$archive" ]] || {
  echo "closed-beta output already exists: $final_dir or $archive" >&2
  exit 2
}
ditto "$package" "$final_dir"
tar -czf "$archive" -C "$stage" "$package_name"

echo "$final_dir"
echo "$archive"
