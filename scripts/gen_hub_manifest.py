#!/usr/bin/env python3
"""Regenerate the Hub installer manifest from the C++ brand registry.

One manifest product per plugin in SattariBranding.h products(), with identity
(id, name, category, accent) taken from the registry and deployment data (target,
product name, bundle paths) taken from each plugin's CMakeLists. Keeps
`sattari-products.json` in exact sync with the registry so
scripts/check_brand_registry_sync.py passes.

Run after adding/removing a plugin or changing its name/accent.
"""
from __future__ import annotations
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "core" / "include" / "sattari_audio_core" / "SattariBranding.h"
MANIFEST = ROOT / "apps" / "sattari-hub" / "Sources" / "SattariHub" / "Resources" / "sattari-products.json"

ENTRY = re.compile(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]{8}|k[A-Za-z0-9_]+)\s*\}')
CONST = re.compile(r'constexpr\s+std::uint32_t\s+(k[A-Za-z0-9_]+)\s*=\s*0x([0-9a-fA-F]{8})')
TARGET = re.compile(r'TARGET\s+([A-Za-z0-9_]+)')
PRODUCT = re.compile(r'PRODUCT_NAME\s+"([^"]+)"')

def accent_hex6(token, symbols):
    argb = token[2:] if token.startswith("0x") else symbols.get(token, "")
    return ("#" + argb[-6:].lower()) if argb else ""

def main() -> int:
    text = HEADER.read_text()
    symbols = dict(CONST.findall(text))
    old = json.loads(MANIFEST.read_text())
    top = {k: old[k] for k in ("schemaVersion", "suiteName", "suiteVersion", "truthLabel",
                               "defaultUpdateChannel", "updateFeedURL") if k in old}
    version = old.get("suiteVersion", "0.2.0-alpha")

    products = []
    for pid, name, category, accent in ENTRY.findall(text):
        cml = ROOT / "plugins" / pid / "CMakeLists.txt"
        if not cml.exists():
            print(f"  skip {pid}: no plugins/{pid}/CMakeLists.txt", file=sys.stderr); continue
        c = cml.read_text()
        tm, pm = TARGET.search(c), PRODUCT.search(c)
        if not tm or not pm:
            print(f"  skip {pid}: no TARGET/PRODUCT_NAME", file=sys.stderr); continue
        target, product = tm.group(1), pm.group(1)
        base = f"build/all/plugins/{pid}/{target}_artefacts/Release"
        bundles = [
            {"kind": "AU",         "installSubpath": f"Library/Audio/Plug-Ins/Components/{product}.component", "relativeSourcePath": f"{base}/AU/{product}.component"},
            {"kind": "VST3",       "installSubpath": f"Library/Audio/Plug-Ins/VST3/{product}.vst3",            "relativeSourcePath": f"{base}/VST3/{product}.vst3"},
            {"kind": "Standalone", "installSubpath": f"Applications/{product}.app",                            "relativeSourcePath": f"{base}/Standalone/{product}.app"},
        ]
        products.append({
            "id": pid, "name": name, "category": category,
            "accent": accent_hex6(accent, symbols),
            "description": f"{name} — {category}.",
            "formats": ["AU", "VST3"], "status": "alpha", "version": version,
            "modules": [{"name": name, "slug": pid}],
            "bundles": bundles,
        })

    out = {**top, "products": products}
    MANIFEST.write_text(json.dumps(out, indent=2) + "\n")
    print(f"wrote {len(products)} products -> {MANIFEST.relative_to(ROOT)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
