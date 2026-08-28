#!/usr/bin/env python3
"""Fail if an installed .component does not match what this tree just built.

WHY THIS EXISTS
---------------
The release gate reported GREEN over two crashes that were live in source: a
MIDI-learn constructor that segfaulted five plugins on instantiation, and an
instrument-rack buffer overflow. In both cases the bundles in ~/Library predated
the source, so auval faithfully validated binaries nobody was shipping.

A gate that tests yesterday's build is worse than no gate: it turns "untested"
into "passed".

WHAT THIS CHECKS, AND WHAT IT DELIBERATELY DOES NOT
---------------------------------------------------
It compares each INSTALLED bundle against the artefact the BUILD TREE produced.
It does NOT try to decide whether that artefact is itself up to date.

The first version did try, by comparing every component against the newest file
anywhere in core/. That is wrong, and the gate proved it: editing
SattariInstrumentRack.h marked Harmony stale, but Harmony does not include that
header, so ninja correctly did not rebuild it. Six false failures on the first
real run — precisely the cry-wolf outcome that gets a check ignored.

Timestamps cannot answer "is this current"; only the dependency graph can, and
ninja already owns it. So the division of labour is:

    ninja      — is the build tree current?   (the gate's build phase)
    this file  — did the install actually happen, and is the result unambiguous?

That is strictly more accurate AND has no false positives.

Usage:  check_component_freshness.py [build-tree] [--verbose]
Exit 0 when every installed Sattari component matches its built artefact and no
two bundles claim the same plug-in identity.
"""
from __future__ import annotations

import json
import plistlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = Path.home() / "Library/Audio/Plug-Ins/Components"


def build_config(tree: Path) -> str:
    """The config ninja is actually producing (Debug/Release/...)."""
    cache = tree / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                value = line.split("=", 1)[1].strip()
                if value:
                    return value
    return "Debug"


def built_artefacts(tree: Path, config: str) -> dict[str, Path]:
    """Map 'Sattari X.component' -> the artefact this tree built for it."""
    found: dict[str, Path] = {}
    for path in tree.glob(f"plugins/*/*_artefacts/{config}/AU/*.component"):
        found[path.name] = path
    return found


def newest_binary_mtime(bundle: Path) -> float | None:
    macos = bundle / "Contents/MacOS"
    if not macos.is_dir():
        return None
    binaries = [p for p in macos.iterdir() if p.is_file()]
    return max(p.stat().st_mtime for p in binaries) if binaries else None


def identity(bundle: Path) -> tuple[str, str, str] | None:
    """(type, subtype, manufacturer) — the AU's real identity, not its name."""
    plist = bundle / "Contents/Info.plist"
    if not plist.is_file():
        return None
    try:
        with plist.open("rb") as handle:
            entry = plistlib.load(handle)["AudioComponents"][0]
        return (entry["type"], entry["subtype"], entry["manufacturer"])
    except Exception:
        return None


def excluded_products() -> set[str]:
    catalog = json.loads((ROOT / "config/products.json").read_text())
    return {p["name"] for p in catalog["products"] if p.get("lifecycle") == "excluded"}


def when(ts: float) -> str:
    import datetime
    return datetime.datetime.fromtimestamp(ts).strftime("%b %d %H:%M")


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "--verbose" in sys.argv
    tree = ROOT / (args[0] if args else "build/local")

    if not COMPONENTS.is_dir():
        print("no Components directory - nothing installed to check")
        return 0
    if not (tree / "CMakeCache.txt").is_file():
        print(f"no CMake tree at {tree.relative_to(ROOT)} - cannot compare installs")
        return 1

    config = build_config(tree)
    artefacts = built_artefacts(tree, config)
    excluded = excluded_products()

    stale: list[tuple[str, float, float]] = []
    unbuilt: list[str] = []
    skipped: list[str] = []
    by_identity: dict[tuple[str, str, str], list[str]] = {}
    checked = 0

    for bundle in sorted(COMPONENTS.glob("*.component")):
        ident = identity(bundle)
        if ident is None or ident[2] != "Satr":
            continue
        name = bundle.stem
        installed = newest_binary_mtime(bundle)
        if installed is None:
            continue
        checked += 1
        by_identity.setdefault(ident, []).append(name)

        artefact = artefacts.get(bundle.name)
        if artefact is None:
            # Nothing in this tree can produce it. Legitimate for an excluded
            # product; otherwise auval is about to test a binary we did not build.
            (skipped if name in excluded else unbuilt).append(name)
            continue

        built = newest_binary_mtime(artefact)
        # Whole-second granularity, and NOT as a fudge factor. The install copy
        # preserves the mtime but truncates its fractional part (installed reads
        # .000000000 where the artefact reads .580219991), so a correct install
        # always compares 0-1s "older" than what it came from. Comparing floors
        # is the exact comparison; a real stale install is seconds-to-days out.
        if built is not None and int(installed) < int(built):
            stale.append((name, installed, built))
        elif verbose:
            print(f"  ok    {name}")

    # Two bundles claiming one identity is worse than a stale one: macOS keys the
    # AudioComponent registry on (type, subtype, manufacturer), so which one a host
    # loads is undefined. A rename that leaves the old bundle behind does exactly
    # this — Brain -> Compass did, and the loser was the older, crashing binary.
    duplicates = {i: n for i, n in by_identity.items() if len(n) > 1}
    if duplicates:
        print(f"component freshness FAILED: {len(duplicates)} duplicated AU identity")
        for (typ, sub, manu), names in sorted(duplicates.items()):
            print(f"  {typ}/{sub}/{manu} claimed by {len(names)}: {', '.join(sorted(names))}")
        print("\nA host will load one of these at random. Remove the stale bundle(s).")
        return 1

    if skipped:
        print(f"note: skipped {len(skipped)} excluded product(s): {', '.join(sorted(skipped))}")

    if stale or unbuilt:
        print(f"component freshness FAILED ({config} tree)")
        for name, installed, built in stale:
            print(f"  {name:<28} installed {when(installed)}, built {when(built)} - copy did not happen")
        for name in unbuilt:
            print(f"  {name:<28} installed, but this tree builds no artefact for it")
        print("\nauval would validate binaries that do not match this build tree.")
        return 1

    print(f"component freshness ok: {checked} installed, all match the {config} build")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
