#!/usr/bin/env python3
"""Validate every generated product surface against config/products.json."""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "config" / "products.json"
BRAND_INC = ROOT / "core/include/sattari_audio_core/SattariProductCatalog.generated.inc"
MANIFEST = ROOT / "apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json"
WEB = ROOT / "apps/sattari-web/product-catalog.js"
WEB_DETAILS = ROOT / "apps/sattari-web/plugins-data.js"
CI = ROOT / "config/ci-matrix.generated.json"

BRAND_ENTRY = re.compile(
    r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(k\w+)\s*\},'
)
ACCENTS = {
    "mint": "kAccent",
    "violet": "kAccentViolet",
    "amber": "kAccentAmber",
    "blue": "kAccentBlue",
}


def main() -> int:
    catalog = json.loads(CATALOG.read_text())
    products = catalog["products"]
    by_id = {p["id"]: p for p in products}
    errors: list[str] = []

    if len(products) != 49 or len(by_id) != 49:
        errors.append("catalog must contain 49 unique products")

    generated_brand = {
        pid: {"name": name, "category": category, "accent": accent}
        for pid, name, category, accent in BRAND_ENTRY.findall(BRAND_INC.read_text())
    }
    if set(generated_brand) != set(by_id):
        errors.append("generated C++ branding ids differ from catalog")
    for pid in set(generated_brand) & set(by_id):
        expected = by_id[pid]
        actual = generated_brand[pid]
        if actual != {
            "name": expected["name"],
            "category": expected["category"],
            "accent": ACCENTS[expected["accent"]],
        }:
            errors.append(f"{pid}: generated C++ branding drift")

    manifest = json.loads(MANIFEST.read_text())
    manifest_by_id = {p["id"]: p for p in manifest["products"]}
    expected_hub_ids = {
        p["id"] for p in products if p["lifecycle"] != "excluded"
    }
    if set(manifest_by_id) != expected_hub_ids:
        errors.append("Hub ids differ from non-excluded catalog ids")
    for pid in set(manifest_by_id) & expected_hub_ids:
        source = by_id[pid]
        built = manifest_by_id[pid]
        for key in ("name", "category", "lifecycle", "shippingProfile"):
            if built.get(key) != source.get(key):
                errors.append(f"{pid}: Hub {key} drift")
        if len(built.get("bundles", [])) != 3:
            errors.append(f"{pid}: Hub must declare AU, VST3 and Standalone")

    web_text = WEB.read_text()
    prefix = "window.SATTARI_PRODUCT_CATALOG = "
    try:
        web_data = json.loads(web_text.split(prefix, 1)[1].rsplit(";", 1)[0])
    except (IndexError, json.JSONDecodeError):
        web_data = []
        errors.append("could not parse generated web catalog")
    if {p["id"] for p in web_data} != set(by_id):
        errors.append("web lifecycle catalog ids differ from product catalog")
    detailed_ids = set(re.findall(r'\bid:\s*"([^"]+)"', WEB_DETAILS.read_text()))
    visible_ids = {p["id"] for p in products if p["webVisible"]}
    if detailed_ids & visible_ids != visible_ids:
        errors.append(
            "website detail data missing visible ids: "
            + str(sorted(visible_ids - detailed_ids))
        )

    ci = json.loads(CI.read_text())["include"]
    expected_ci = {p["id"] for p in products if "ci" in p}
    if {p["plugin"] for p in ci} != expected_ci:
        errors.append("generated CI matrix differs from catalog ci entries")
    if "stemdeck" in {p["plugin"] for p in ci}:
        errors.append("StemDeck entered the non-StemDeck CI matrix")

    if errors:
        print("product catalog sync FAILED:")
        for error in errors:
            print("  -", error)
        return 1

    print(
        "product catalog sync ok: "
        f"catalog={len(products)} hub={len(manifest_by_id)} "
        f"web={sum(bool(p['webVisible']) for p in products)} ci={len(ci)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
