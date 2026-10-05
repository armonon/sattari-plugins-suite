#!/usr/bin/env python3
"""Check a selected payload's binary version against its Hub manifest entry."""
import argparse
import json
from pathlib import Path
import plistlib


def verify(manifest: dict, relative_path: str, source: Path) -> None:
    entries = [b for p in manifest["products"] for b in p["bundles"] if b["relativeSourcePath"] == relative_path]
    if len(entries) != 1:
        raise ValueError(f"Expected exactly one manifest entry for {relative_path}")
    expected = entries[0].get("expectedVersion")
    if expected is None:
        return
    with (source / "Contents/Info.plist").open("rb") as stream:
        actual = plistlib.load(stream).get("CFBundleShortVersionString")
    if actual != expected:
        raise ValueError(f"Stale payload {source}: expected {expected}, found {actual!r}; rebuild before packaging")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("relative_path")
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    try:
        verify(json.loads(args.manifest.read_text()), args.relative_path, args.source)
    except (ValueError, OSError, plistlib.InvalidFileException) as error:
        parser.exit(1, str(error) + "\n")
