#!/usr/bin/env bash
# Prove every manifest alpha payload is locally usable without producing a package.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="${SATTARI_HUB_MANIFEST:-$repo_root/apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json}"
build_tree="${SATTARI_BUILD_TREE:-build/all}"
autopitch_build_dir="${SATTARI_AUTOPITCH_BUILD_DIR:-$repo_root/build/auto-pitch-neural}"

resolve_source() {
  local relative_path="$1"
  if [[ "$relative_path" == build/all/plugins/auto-pitch/* ]]; then
    printf '%s/%s\n' "$autopitch_build_dir" "${relative_path#build/all/plugins/auto-pitch/}"
  else
    printf '%s/%s\n' "$repo_root" "${relative_path/build\/all/$build_tree}"
  fi
}

validate_bundle() {
  local bundle="$1"
  [[ -d "$bundle" ]] || { echo "missing alpha bundle: $bundle" >&2; return 1; }
  [[ -f "$bundle/Contents/Info.plist" ]] || { echo "missing bundle Info.plist: $bundle" >&2; return 1; }
  local executable
  executable="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f -perm -111 -print -quit 2>/dev/null)"
  [[ -n "$executable" ]] || { echo "missing bundle executable: $bundle" >&2; return 1; }
}

copied=0
while IFS= read -r relative_path; do
  [[ -n "$relative_path" ]] || continue
  source_path="$(resolve_source "$relative_path")"
  validate_bundle "$source_path"
  python3 "$repo_root/scripts/check_hub_bundle_version.py" "$manifest" "$relative_path" "$source_path"
  if [[ "$relative_path" == build/all/plugins/auto-pitch/* ]]; then
    "$repo_root/scripts/verify_auto_pitch_bundle.sh" "$source_path"
  fi
  copied=$((copied + 1))
done < <(python3 - <<'PY' "$manifest"
import json, sys, os
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
seen = set()
for product in manifest["products"]:
    if product.get("shippingProfile") != "alpha" or (product.get("portfolioStatus", "active") not in ("active", "specialist") and os.environ.get("SATTARI_INCLUDE_LEGACY") != "1"):
        continue
    for bundle in product["bundles"]:
        path = bundle["relativeSourcePath"]
        if path not in seen:
            seen.add(path)
            print(path)
PY
)

expected="$(python3 - <<'PY' "$manifest"
import json, sys, os
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
print(len({bundle["relativeSourcePath"]
           for product in manifest["products"]
           if product.get("shippingProfile") == "alpha" and (product.get("portfolioStatus", "active") in ("active", "specialist") or os.environ.get("SATTARI_INCLUDE_LEGACY") == "1")
           for bundle in product["bundles"]}))
PY
)"
[[ "$copied" -eq "$expected" ]] || {
  echo "alpha payload count mismatch: expected $expected, validated $copied" >&2
  exit 1
}
echo "Hub alpha payload proof passed: $copied bundles"
