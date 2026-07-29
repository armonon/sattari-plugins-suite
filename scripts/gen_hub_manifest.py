#!/usr/bin/env python3
"""Generate product-facing suite metadata from config/products.json.

The catalog is authoritative for identity, lifecycle and release profile. This
generator keeps four consumers deterministic:

* the C++ branding registry initializer;
* the Hub install/update manifest;
* the website lifecycle/visibility catalog;
* the focused macOS CI matrix.

Run after editing config/products.json or a plugin's deployment CMake.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "config" / "products.json"
MANIFEST = ROOT / "apps" / "sattari-hub" / "Sources" / "SattariHub" / "Resources" / "sattari-products.json"
BRAND_INC = ROOT / "core" / "include" / "sattari_audio_core" / "SattariProductCatalog.generated.inc"
WEB_CATALOG = ROOT / "apps" / "sattari-web" / "product-catalog.js"
CI_MATRIX = ROOT / "config" / "ci-matrix.generated.json"

TARGET = re.compile(r"TARGET\s+([A-Za-z0-9_]+)")
PRODUCT = re.compile(r'PRODUCT_NAME\s+"([^"]+)"')
ACCENTS = {
    "mint": "kAccent",
    "violet": "kAccentViolet",
    "amber": "kAccentAmber",
}


def deployment(product: dict) -> tuple[str, str, list[dict]]:
    cml = ROOT / "plugins" / product["id"] / "CMakeLists.txt"
    if not cml.exists():
        raise ValueError(f"{product['id']}: missing {cml.relative_to(ROOT)}")
    text = cml.read_text()
    target_match = TARGET.search(text)
    product_match = PRODUCT.search(text)
    if not target_match or not product_match:
        raise ValueError(f"{product['id']}: missing TARGET or PRODUCT_NAME")
    target, deployed_name = target_match.group(1), product_match.group(1)
    if deployed_name != product["name"]:
        raise ValueError(
            f"{product['id']}: catalog name {product['name']!r} "
            f"!= CMake PRODUCT_NAME {deployed_name!r}"
        )
    base = f"build/all/plugins/{product['id']}/{target}_artefacts/Release"
    bundles = [
        {
            "kind": "AU",
            "installSubpath": f"Library/Audio/Plug-Ins/Components/{deployed_name}.component",
            "relativeSourcePath": f"{base}/AU/{deployed_name}.component",
        },
        {
            "kind": "VST3",
            "installSubpath": f"Library/Audio/Plug-Ins/VST3/{deployed_name}.vst3",
            "relativeSourcePath": f"{base}/VST3/{deployed_name}.vst3",
        },
        {
            "kind": "Standalone",
            "installSubpath": f"Applications/{deployed_name}.app",
            "relativeSourcePath": f"{base}/Standalone/{deployed_name}.app",
        },
    ]
    return target, deployed_name, bundles


def validate(catalog: dict) -> list[dict]:
    if catalog.get("schemaVersion") != 1:
        raise ValueError("catalog schemaVersion must be 1")
    products = catalog.get("products", [])
    ids = [p.get("id") for p in products]
    if len(products) != 49 or len(set(ids)) != len(ids):
        raise ValueError("catalog must contain 49 unique product ids")
    required = {
        "id", "name", "category", "accent", "lifecycle", "role",
        "shippingProfile", "webVisible",
    }
    for product in products:
        missing = required - product.keys()
        if missing:
            raise ValueError(f"{product.get('id')}: missing {sorted(missing)}")
        if product["accent"] not in ACCENTS:
            raise ValueError(f"{product['id']}: unknown accent {product['accent']}")
        deployment(product)
    alpha = {p["id"] for p in products if p["shippingProfile"] == "alpha"}
    if alpha != {"brain", "sattari-arp", "sub-conjurer"}:
        raise ValueError(f"alpha profile drifted: {sorted(alpha)}")
    stem = next(p for p in products if p["id"] == "stemdeck")
    if stem["lifecycle"] != "excluded" or stem["webVisible"]:
        raise ValueError("stemdeck must remain excluded from this non-StemDeck program")
    return products


def generate() -> None:
    catalog = json.loads(CATALOG.read_text())
    products = validate(catalog)

    old_manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    old_by_id = {p["id"]: p for p in old_manifest.get("products", [])}

    manifest_products = []
    for product in products:
        if product["lifecycle"] == "excluded":
            continue
        _, _, bundles = deployment(product)
        old = old_by_id.get(product["id"], {})
        generated = {
            "id": product["id"],
            "name": product["name"],
            "category": product["category"],
            "accent": {
                "mint": "#62f5c8",
                "violet": "#9b7bff",
                "amber": "#f5a24b",
            }[product["accent"]],
            "description": f"{product['name']} — {product['category']}.",
            "formats": ["AU", "VST3", "Standalone"],
            "status": product["lifecycle"],
            "lifecycle": product["lifecycle"],
            "shippingProfile": product["shippingProfile"],
            "version": catalog["suiteVersion"],
            "bundles": bundles,
        }
        for key in ("updateArchiveURL", "updateArchiveSHA256", "updateArchiveBytes"):
            if old.get(key) is not None:
                generated[key] = old[key]
        manifest_products.append(generated)

    manifest = {
        "schemaVersion": 1,
        "suiteName": "Sattari Audio Suite",
        "suiteVersion": catalog["suiteVersion"],
        "truthLabel": catalog["truthLabel"],
        "defaultUpdateChannel": catalog["defaultUpdateChannel"],
        "updateFeedURL": (
            old_manifest.get("updateFeedURL")
            if old_manifest.get("updateFeedSHA256") else None
        ),
        "updateFeedSHA256": old_manifest.get("updateFeedSHA256"),
        "products": manifest_products,
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n")

    brand_lines = [
        "// Generated by scripts/gen_hub_manifest.py from config/products.json.",
        "// Do not edit by hand.",
    ]
    for product in products:
        brand_lines.append(
            f'{{ "{product["id"]}", "{product["name"]}", '
            f'"{product["category"]}", {ACCENTS[product["accent"]]} }},'
        )
    BRAND_INC.write_text("\n".join(brand_lines) + "\n")

    web = [
        {
            "id": p["id"],
            "lifecycle": p["lifecycle"],
            "role": p["role"],
            "shippingProfile": p["shippingProfile"],
            "webVisible": p["webVisible"],
        }
        for p in products
    ]
    WEB_CATALOG.write_text(
        "// Generated by scripts/gen_hub_manifest.py. Do not edit.\n"
        "window.SATTARI_PRODUCT_CATALOG = "
        + json.dumps(web, separators=(",", ":"))
        + ";\n"
    )

    ci = []
    for product in products:
        if "ci" not in product:
            continue
        entry = {"plugin": product["id"]}
        entry.update(product["ci"])
        ci.append(entry)
    CI_MATRIX.write_text(json.dumps({"include": ci}, indent=2) + "\n")

    print(
        f"generated branding={len(products)} hub={len(manifest_products)} "
        f"web={sum(1 for p in products if p['webVisible'])} ci={len(ci)}"
    )


if __name__ == "__main__":
    generate()
