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
assert len({p["id"] for p in products}) == len(products)
suites = manifest["suite"]
assert len(suites) == 6
assert {s["id"] for s in suites} == {
    "compass", "auto-pitch", "modular", "royal-chain", "mix", "tools"
}
ids = {p["id"] for p in products}
for required in ["auto-pitch", "voxkey", "brain", "sub-conjurer", "side-chain-master", "royal-chain", "sattari-arp"]:
    assert required in ids, required
assert "stemdeck" not in ids
for product in products:
    assert product["bundles"], product["id"]
    assert product.get("accent"), f"{product['id']} missing brand accent"
    for bundle in product["bundles"]:
        assert bundle["kind"] in {"AU", "VST3", "Standalone"}
        assert bundle["relativeSourcePath"]
        assert bundle["installSubpath"]
    if product.get("updateArchiveURL"):
        assert len(product.get("updateArchiveSHA256", "")) == 64
        assert product.get("updateArchiveBytes", 0) > 0
assert {p["id"] for p in products if p.get("shippingProfile") == "alpha"} == {
    "auto-pitch", "brain", "vocal", "mix", "create", "stack",
    "sattari-arp", "sub-conjurer", "modular", "modular-instrument"
}
for suite in suites:
    members = [p for p in products if p.get("suite") == suite["id"]]
    assert members, f"{suite['id']} has no components"
    primaries = [p for p in members if p.get("suiteRole") == "primary"]
    assert len(primaries) == 1, f"{suite['id']} must have one primary"
    assert primaries[0]["id"] == suite["primary"], f"{suite['id']} primary mismatch"
print(f"manifest contract ok: suites={len(suites)} products={len(products)} bundles={sum(len(p['bundles']) for p in products)}")
PY

# Identity, lifecycle, Hub, web and CI metadata all come from the catalog.
python3 "$repo_root/scripts/check_brand_registry_sync.py"
"$repo_root/scripts/validate_hub_alpha_payloads.sh"

git -C "$repo_root" diff --check -- apps/sattari-hub apps/sattari-web config scripts/validate_sattari_hub.sh scripts/validate_hub_alpha_payloads.sh scripts/package_sattari_hub_alpha.sh core/include/sattari_audio_core/SattariBranding.h core/include/sattari_audio_core/SattariProductCatalog.generated.inc
