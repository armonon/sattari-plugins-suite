#!/usr/bin/env python3
"""Assert the C++ brand registry and the Hub manifest never drift.

Identity for the Sattari suite (id, display name, accent colour) lives in
`core/include/sattari_audio_core/SattariBranding.h`. The Hub installer manifest
`apps/sattari-hub/Sources/SattariHub/Resources/sattari-products.json` carries the
same identity plus deployment data (versions, bundles, URLs).

This check fails loudly if the two disagree on the set of product ids, on any
display name, or on any accent colour — so the plugin UI, the Hub cards, and the
installer can never silently diverge.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "core" / "include" / "sattari_audio_core" / "SattariBranding.h"
MANIFEST = ROOT / "apps" / "sattari-hub" / "Sources" / "SattariHub" / "Resources" / "sattari-products.json"

# Matches: { "id", "Display Name", "Category", 0xAARRGGBB },
# The accent field is either a literal 0xAARRGGBB or a named palette constant
# (e.g. kAccent) — the suite now uses the symbol for every product, so resolve it.
ENTRY = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]{8}|k[A-Za-z0-9_]+)\s*\}'
)
# Matches: inline constexpr std::uint32_t kAccent = 0xff62f5c8;
CONST = re.compile(r'constexpr\s+std::uint32_t\s+(k[A-Za-z0-9_]+)\s*=\s*0x([0-9a-fA-F]{8})')


def _accent_to_hex6(token: str, symbols: dict[str, str]) -> str:
    argb = token[2:] if token.startswith("0x") else symbols.get(token, "")
    return ("#" + argb[-6:].lower()) if argb else ""


def parse_registry() -> dict[str, dict[str, str]]:
    text = HEADER.read_text()
    symbols = {name: argb for name, argb in CONST.findall(text)}
    out: dict[str, dict[str, str]] = {}
    for pid, name, _category, accent in ENTRY.findall(text):
        out[pid] = {"name": name, "accent": _accent_to_hex6(accent, symbols)}
    return out


def parse_manifest() -> dict[str, dict[str, str]]:
    data = json.loads(MANIFEST.read_text())
    out: dict[str, dict[str, str]] = {}
    for prod in data["products"]:
        out[prod["id"]] = {
            "name": prod["name"],
            "accent": str(prod.get("accent", "")).lower(),
        }
    return out


def main() -> int:
    registry = parse_registry()
    manifest = parse_manifest()

    if not registry:
        print("brand sync FAILED: could not parse any entries from SattariBranding.h")
        return 1

    errors: list[str] = []

    reg_ids, man_ids = set(registry), set(manifest)
    if reg_ids != man_ids:
        only_reg = sorted(reg_ids - man_ids)
        only_man = sorted(man_ids - reg_ids)
        if only_reg:
            errors.append(f"ids only in registry (missing from Hub manifest): {only_reg}")
        if only_man:
            errors.append(f"ids only in Hub manifest (missing from registry): {only_man}")

    for pid in sorted(reg_ids & man_ids):
        r, m = registry[pid], manifest[pid]
        if r["name"] != m["name"]:
            errors.append(f"{pid}: name mismatch — registry {r['name']!r} vs manifest {m['name']!r}")
        if r["accent"] != m["accent"]:
            errors.append(f"{pid}: accent mismatch — registry {r['accent']} vs manifest {m['accent']!r}")

    if errors:
        print("brand sync FAILED:")
        for e in errors:
            print("  -", e)
        return 1

    print(f"brand sync ok: {len(registry)} products consistent across registry + Hub manifest")
    for pid in sorted(registry):
        print(f"  {pid:18} {registry[pid]['name']:22} {registry[pid]['accent']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
