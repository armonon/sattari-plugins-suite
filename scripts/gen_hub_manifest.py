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
FORMATS = re.compile(r"^\s*FORMATS\s+([A-Za-z0-9_ ]+)\s*$", re.MULTILINE)
ACCENTS = {
    "mint": "kAccent",
    "violet": "kAccentViolet",
    "amber": "kAccentAmber",
    "blue": "kAccentBlue",
    "crimson": "kAccentCrimson",
}


def write_if_changed(path: Path, contents: str) -> None:
    if not path.exists() or path.read_text() != contents:
        path.write_text(contents)


def deployment(product: dict) -> tuple[str, str, list[dict]]:
    directory = product.get("sourceDirectory", product["id"])
    cml = ROOT / "plugins" / directory / "CMakeLists.txt"
    if not cml.exists():
        raise ValueError(f"{product['id']}: missing {cml.relative_to(ROOT)}")
    text = cml.read_text()
    target_match = TARGET.search(text)
    product_match = PRODUCT.search(text)
    if not target_match or not product_match:
        raise ValueError(f"{product['id']}: missing TARGET or PRODUCT_NAME")
    target, deployed_name = target_match.group(1), product_match.group(1)
    if directory == "modular":
        target = product.get("buildTarget", "")
        if target not in ("SattariModularFX", "SattariModularInstrument") or "foreach(kind IN ITEMS FX Instrument)" not in text:
            raise ValueError(f"{product['id']}: unrecognised Modular variant")
        deployed_name = deployed_name.replace("${kind}", target.removeprefix("SattariModular"))
        versions = set(re.findall(r"\bVERSION\s+(\d+\.\d+\.\d+)", text))
        if versions != {product.get("binaryVersion")}:
            raise ValueError(f"{product['id']}: expected binary version differs from CMake")
    if deployed_name != product["name"]:
        raise ValueError(
            f"{product['id']}: catalog name {product['name']!r} "
            f"!= CMake PRODUCT_NAME {deployed_name!r}"
        )
    base = f"build/all/plugins/{directory}/{target}_artefacts/Release"
    format_match = FORMATS.search(text)
    formats = format_match.group(1).split() if format_match else ["AU", "VST3", "Standalone"]
    unknown_formats = set(formats) - {"AU", "VST3", "Standalone"}
    if unknown_formats:
        raise ValueError(f"{product['id']}: unsupported formats {sorted(unknown_formats)}")
    bundle_by_format = {
        "AU": {
            "kind": "AU",
            "installSubpath": f"Library/Audio/Plug-Ins/Components/{deployed_name}.component",
            "relativeSourcePath": f"{base}/AU/{deployed_name}.component",
        },
        "VST3": {
            "kind": "VST3",
            "installSubpath": f"Library/Audio/Plug-Ins/VST3/{deployed_name}.vst3",
            "relativeSourcePath": f"{base}/VST3/{deployed_name}.vst3",
        },
        "Standalone": {
            "kind": "Standalone",
            "installSubpath": f"Applications/{deployed_name}.app",
            "relativeSourcePath": f"{base}/Standalone/{deployed_name}.app",
        },
    }
    bundles = [bundle_by_format[plugin_format] for plugin_format in formats]
    if product.get("binaryVersion"):
        for bundle in bundles:
            bundle["expectedVersion"] = product["binaryVersion"]
    return target, deployed_name, bundles


def validate(catalog: dict) -> list[dict]:
    if catalog.get("schemaVersion") != 2:
        raise ValueError("catalog schemaVersion must be 2")
    products = catalog.get("products", [])
    ids = [p.get("id") for p in products]
    if not products or len(set(ids)) != len(ids):
        raise ValueError("catalog must contain unique product ids")
    directories = {p.get("sourceDirectory", p["id"]) for p in products}
    discovered = {p.parent.name for p in (ROOT / "plugins").glob("*/CMakeLists.txt")}
    if directories != discovered:
        raise ValueError(f"catalog source coverage differs: missing={sorted(discovered - directories)} unknown={sorted(directories - discovered)}")
    required = {
        "id", "name", "category", "accent", "lifecycle", "role",
        "shippingProfile", "webVisible", "suite", "suiteRole", "portfolioStatus", "collections",
    }
    for product in products:
        missing = required - product.keys()
        if missing:
            raise ValueError(f"{product.get('id')}: missing {sorted(missing)}")
        if product["accent"] not in ACCENTS:
            raise ValueError(f"{product['id']}: unknown accent {product['accent']}")
        deployment(product)
    modular = [p for p in products if p.get("sourceDirectory", p["id"]) == "modular"]
    if {(p["id"], p.get("buildTarget")) for p in modular} != {
        ("modular", "SattariModularFX"), ("modular-instrument", "SattariModularInstrument")
    }:
        raise ValueError("catalog must include both distinct Modular deployment variants")
    alpha = {p["id"] for p in products if p["shippingProfile"] == "alpha"}
    if alpha != {
        "auto-pitch", "brain", "vocal", "mix", "create", "stack",
        "sattari-arp", "sub-conjurer", "modular", "modular-instrument",
    }:
        raise ValueError(f"alpha profile drifted: {sorted(alpha)}")
    stem = next(p for p in products if p["id"] == "stemdeck")
    if stem["lifecycle"] != "excluded" or stem["webVisible"]:
        raise ValueError("stemdeck must remain excluded from this non-StemDeck program")

    suite = catalog.get("suite", [])
    suite_ids = [s["id"] for s in suite]
    if set(suite_ids) != {"modular", "royal-chain", "auto-pitch", "mix", "tools", "compass"} or len(set(suite_ids)) != len(suite_ids):
        raise ValueError("suite must define the four flagships, Tools and Compass")
    for entry in suite:
        missing = {"id", "name", "verb", "accent", "tier", "tagline", "primary"} - entry.keys()
        if missing:
            raise ValueError(f"suite {entry.get('id')}: missing {sorted(missing)}")
        if entry["accent"] not in ACCENTS:
            raise ValueError(f"suite {entry['id']}: unknown accent {entry['accent']}")

    by_suite: dict[str, list[dict]] = {s: [] for s in suite_ids}
    for product in products:
        group = product["suite"]
        if group is None:
            if product["lifecycle"] != "excluded":
                raise ValueError(f"{product['id']}: no suite, but not excluded")
            if product["suiteRole"] is not None:
                raise ValueError(f"{product['id']}: suiteRole set without a suite")
            continue
        if group not in by_suite:
            raise ValueError(f"{product['id']}: unknown suite {group!r}")
        if product["suiteRole"] not in ("primary", "variant", "component", "legacy"):
            raise ValueError(f"{product['id']}: invalid suiteRole")
        if product.get("portfolioStatus") not in ("active", "module", "legacy", "held", "specialist", "migration-pending"):
            raise ValueError(f"{product['id']}: invalid portfolioStatus")
        if product["webVisible"] != (product["portfolioStatus"] in ("active", "specialist")):
            raise ValueError(f"{product['id']}: storefront visibility differs from portfolio decision")
        by_suite[group].append(product)

    # Exactly one primary per suite product, and it must be the one the suite
    # entry names — the headline binary a user launches to "be in" that product.
    for entry in suite:
        members = by_suite[entry["id"]]
        if not members:
            raise ValueError(f"suite {entry['id']}: no plugins assigned")
        primaries = [p["id"] for p in members if p["suiteRole"] == "primary"]
        if primaries != [entry["primary"]]:
            raise ValueError(
                f"suite {entry['id']}: primary must be exactly [{entry['primary']}], got {primaries}")
    collections = catalog.get("collections", [])
    collection_ids = [c["id"] for c in collections]
    if set(collection_ids) != {"vocal", "create", "instruments", "mix"} or len(collection_ids) != 4:
        raise ValueError("catalog must contain the four distinct Modular collections")
    for collection in collections:
        if collection["destination"] != "modular" or not collection["members"]:
            raise ValueError("invalid collection destination or empty members")
        if len(set(collection["members"])) != len(collection["members"]):
            raise ValueError(f"{collection['id']}: duplicate collection member")
        for pid in collection["members"]:
            if pid not in ids or collection["id"] not in next(p for p in products if p["id"] == pid)["collections"]:
                raise ValueError(f"{collection['id']}: collection membership drift for {pid}")
    for product in products:
        expected = [c["id"] for c in collections if product["id"] in c["members"]]
        if product["collections"] != expected:
            raise ValueError(f"{product['id']}: reverse collection membership drift")
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
                "blue": "#68a7ff",
                "crimson": "#e8384f",
            }[product["accent"]],
            "description": f"{product['name']} — {product['category']}.",
            "formats": [bundle["kind"] for bundle in bundles],
            "status": product["lifecycle"],
            "lifecycle": product["lifecycle"],
            "shippingProfile": product["shippingProfile"],
            "version": catalog["suiteVersion"],
            "suite": product["suite"],
            "suiteRole": product["suiteRole"],
            "portfolioStatus": product["portfolioStatus"],
            "collections": product["collections"],
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
        "suite": catalog["suite"],
        "collections": catalog["collections"],
        "products": manifest_products,
    }
    write_if_changed(MANIFEST, json.dumps(manifest, indent=2) + "\n")

    brand_lines = [
        "// Generated by scripts/gen_hub_manifest.py from config/products.json.",
        "// Do not edit by hand.",
    ]
    for product in products:
        brand_lines.append(
            f'{{ "{product["id"]}", "{product["name"]}", '
            f'"{product["category"]}", {ACCENTS[product["accent"]]} }},'
        )
    write_if_changed(BRAND_INC, "\n".join(brand_lines) + "\n")

    web = [
        {
            "id": p["id"],
            "name": p["name"],
            "category": p["category"],
            "page": ("modular.html#instrument" if p["id"] == "modular-instrument" else "modular.html") if p.get("sourceDirectory") == "modular" else "plugin.html?id=" + p["id"],
            "lifecycle": p["lifecycle"],
            "role": p["role"],
            "shippingProfile": p["shippingProfile"],
            "webVisible": p["webVisible"],
            "suite": p["suite"],
            "suiteRole": p["suiteRole"],
            "portfolioStatus": p["portfolioStatus"],
            "collections": p["collections"],
        }
        for p in products
    ]
    suite_web = [
        dict(s, count=sum(1 for p in products
                          if p["suite"] == s["id"] and p["portfolioStatus"] not in ("held", "legacy", "migration-pending")))
        for s in catalog["suite"]
    ]
    write_if_changed(WEB_CATALOG,
        "// Generated by scripts/gen_hub_manifest.py. Do not edit.\n"
        "window.SATTARI_PRODUCT_CATALOG = "
        + json.dumps(web, separators=(",", ":"))
        + ";\n"
        "window.SATTARI_SUITE = "
        + json.dumps(suite_web, separators=(",", ":"))
        + ";\n"
        "window.SATTARI_COLLECTIONS = "
        + json.dumps(catalog["collections"], separators=(",", ":"))
        + ";\n"
    )

    ci = []
    for product in products:
        if "ci" not in product:
            continue
        entry = {"plugin": product["id"]}
        entry.update(product["ci"])
        ci.append(entry)
    write_if_changed(CI_MATRIX, json.dumps({"include": ci}, indent=2) + "\n")

    grouped = ", ".join(
        f"{s['id']}:{sum(1 for p in products if p['suite'] == s['id'])}"
        for s in catalog["suite"])
    print(
        f"generated branding={len(products)} hub={len(manifest_products)} "
        f"web={sum(1 for p in products if p['webVisible'])} ci={len(ci)}"
    )
    print(f"  suite ({len(catalog['suite'])} products) -> {grouped}")


if __name__ == "__main__":
    generate()
