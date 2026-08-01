#!/usr/bin/env python3
"""Fail if an installed .component is older than the source it was built from.

WHY THIS EXISTS
---------------
The release gate reported GREEN over two separate crashes that were live in
source at the time: a MIDI-learn constructor that segfaulted five plugins on
instantiation, and an instrument-rack buffer overflow on oversized blocks. In
both cases the bundles in ~/Library predated the source change, so auval
faithfully validated binaries nobody was shipping.

A gate that tests yesterday's build is worse than no gate, because it converts
"untested" into "passed".

SCOPE — why this is per-plugin and not a single global timestamp
----------------------------------------------------------------
Comparing every component against the newest file anywhere in the repo is
correct but useless: editing one plugin marks all 49 stale, so the check becomes
noise and gets ignored, which is how the original bug survived. Each component
is therefore compared against the sources it can actually be built from:

    plugins/<slug>/          its own code
    core/include, core/src   the shared code every plugin compiles in

A core change still invalidates everything, which is right — that is exactly the
shape of the MIDI-learn bug, a shared header that broke five products at once.

Usage:  check_component_freshness.py [--verbose]
Exit 0 when every installed Sattari component is at least as new as its sources.
"""
from __future__ import annotations

import json
import plistlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = Path.home() / "Library/Audio/Plug-Ins/Components"
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".mm", ".c"}


def newest_mtime(*roots: Path) -> float:
    newest = 0.0
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                newest = max(newest, path.stat().st_mtime)
    return newest


def product_slugs() -> dict[str, str]:
    """Map a product's display name to its plugins/<slug> directory name."""
    catalog = json.loads((ROOT / "config/products.json").read_text())
    return {p["name"]: p["id"] for p in catalog["products"]}


def component_mtime(bundle: Path) -> float | None:
    macos = bundle / "Contents/MacOS"
    if not macos.is_dir():
        return None
    binaries = [p for p in macos.iterdir() if p.is_file()]
    return min(p.stat().st_mtime for p in binaries) if binaries else None


def identity(bundle: Path) -> tuple[str, str, str] | None:
    """(type, subtype, manufacturer) — the AU's real identity, or None."""
    plist = bundle / "Contents/Info.plist"
    if not plist.is_file():
        return None
    try:
        with plist.open("rb") as handle:
            entry = plistlib.load(handle)["AudioComponents"][0]
        return (entry["type"], entry["subtype"], entry["manufacturer"])
    except Exception:
        return None


def is_ours(bundle: Path) -> bool:
    ident = identity(bundle)
    return ident is not None and ident[2] == "Satr"


def main() -> int:
    verbose = "--verbose" in sys.argv

    if not COMPONENTS.is_dir():
        print("no Components directory - nothing installed to check")
        return 0

    core_mtime = newest_mtime(ROOT / "core/include", ROOT / "core/src")
    slugs = product_slugs()
    plugin_mtimes: dict[str, float] = {}

    stale: list[tuple[str, float, float]] = []
    unknown: list[str] = []
    by_identity: dict[tuple[str, str, str], list[str]] = {}
    checked = 0

    for bundle in sorted(COMPONENTS.glob("*.component")):
        if not is_ours(bundle):
            continue
        name = bundle.stem
        built = component_mtime(bundle)
        if built is None:
            continue
        checked += 1
        by_identity.setdefault(identity(bundle), []).append(name)

        slug = slugs.get(name)
        if slug is None:
            # Not in the catalog: fall back to core only, and say so rather than
            # silently skipping — an unlisted product is its own problem.
            unknown.append(name)
            source = core_mtime
        else:
            if slug not in plugin_mtimes:
                plugin_mtimes[slug] = newest_mtime(ROOT / "plugins" / slug)
            source = max(core_mtime, plugin_mtimes[slug])

        if built < source:
            stale.append((name, built, source))
        elif verbose:
            print(f"  ok    {name}")

    # Two bundles claiming one AU identity is worse than a stale one: macOS keys
    # the AudioComponent registry on (type, subtype, manufacturer), so WHICH of
    # them a host loads is undefined. A rename that leaves the old bundle behind
    # produces exactly this, and the loser is usually the older, buggier binary.
    duplicates = {ident: names for ident, names in by_identity.items() if len(names) > 1}
    if duplicates:
        print(f"component freshness FAILED: {len(duplicates)} duplicated AU identity")
        for (typ, sub, manu), names in sorted(duplicates.items()):
            print(f"  {typ}/{sub}/{manu} claimed by {len(names)}: {', '.join(sorted(names))}")
        print("\nA host will load one of these at random. Remove the stale bundle(s)")
        print("from ~/Library/Audio/Plug-Ins/Components before shipping.")
        return 1

    if unknown:
        print(f"note: {len(unknown)} component(s) not in products.json, "
              f"compared against core only: {', '.join(sorted(unknown))}")

    if stale:
        import datetime

        def when(ts: float) -> str:
            return datetime.datetime.fromtimestamp(ts).strftime("%b %d %H:%M")

        print(f"component freshness FAILED: {len(stale)} of {checked} stale")
        for name, built, source in stale:
            print(f"  {name:<28} built {when(built)}, source {when(source)}")
        print("\nauval would validate binaries that do not match this working tree.")
        print("Rebuild them (JUCE copies AU targets to ~/Library on build).")
        return 1

    print(f"component freshness ok: {checked} installed, none older than their source")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
