#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
hub_root="$repo_root/apps/sattari-hub"

swift build --package-path "$hub_root" -c release
SATTARI_REPO_ROOT="$repo_root" swift run --package-path "$hub_root" -c release SattariHub --contract-check

python3 - <<'PY' "$hub_root/Sources/SattariHub/Resources/sattari-products.json"
import json
import sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
assert manifest["schemaVersion"] == 1
assert manifest["suiteName"] == "Sattari Audio Suite"
products = manifest["products"]
assert len(products) >= 7
ids = {p["id"] for p in products}
for required in ["auto-pitch", "voxkey", "stemdeck", "sub-conjurer", "side-chain-master", "royal-chain", "sattari-arp"]:
    assert required in ids, required
for product in products:
    assert product["bundles"], product["id"]
    assert product.get("accent"), f"{product['id']} missing brand accent"
    for bundle in product["bundles"]:
        assert bundle["kind"] in {"AU", "VST3", "Standalone"}
        assert bundle["relativeSourcePath"]
        assert bundle["installSubpath"]
print(f"manifest contract ok: products={len(products)} bundles={sum(len(p['bundles']) for p in products)}")
PY

# Identity (id/name/accent) must match the C++ brand registry — never let them drift.
python3 "$repo_root/scripts/check_brand_registry_sync.py"

git -C "$repo_root" diff --check -- apps/sattari-hub scripts/validate_sattari_hub.sh scripts/package_sattari_hub_alpha.sh core/include/sattari_audio_core/SattariBranding.h
