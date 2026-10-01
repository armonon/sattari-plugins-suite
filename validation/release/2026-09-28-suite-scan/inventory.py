#!/usr/bin/env python3
"""Read-only catalog, contract and installed-AU audit; not a release gate."""
import collections
import datetime
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def command(args):
    try:
        result = subprocess.run(args, capture_output=True, text=True, timeout=30)
        return {"exit": result.returncode, "output": (result.stdout + result.stderr).strip()}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"exit": -1, "output": str(error)}


catalog = json.loads((ROOT / "config/products.json").read_text())
ctest = command(["ctest", "--test-dir", str(ROOT / "build/local"), "--show-only=json-v1"])
tests = json.loads(ctest["output"])["tests"] if ctest["exit"] == 0 else []
products = []
for product in catalog["products"]:
    slug = product["id"]
    folder = ROOT / "plugins" / slug
    cmake = (folder / "CMakeLists.txt").read_text()
    clean_cmake = re.sub(r"#.*", "", cmake)
    version = re.search(r"project\([^)]*VERSION\s+([^\s)]+)", clean_cmake)
    code = re.search(r"PLUGIN_CODE\s+(\w+)", clean_cmake)
    executables = re.findall(r"add_executable\(\s*([^\s)]+)", clean_cmake)
    registered = [test["name"] for test in tests
                  if f"/plugins/{slug}/" in " ".join(test.get("command", []))]
    products.append({
        "id": slug, "name": product["name"], "lifecycle": product["lifecycle"],
        "suite": product.get("suite"), "version": version.group(1) if version else None,
        "code": code.group(1) if code else None, "executables": executables,
        "test_sources": [str(path.relative_to(ROOT)) for path in sorted((folder / "tests").glob("*"))
                         if path.is_file()],
        "cached_registered_cases": registered,
        "registry_ci": product.get("ci", {}),
    })

installed = []
components = Path.home() / "Library/Audio/Plug-Ins/Components"
for bundle in sorted(components.glob("*.component")):
    try:
        with (bundle / "Contents/Info.plist").open("rb") as handle:
            plist = plistlib.load(handle)
        entries = [entry for entry in plist.get("AudioComponents", [])
                   if entry.get("manufacturer") == "Satr"]
        if not entries:
            continue
        binary = bundle / "Contents/MacOS" / plist["CFBundleExecutable"]
        installed.append({
            "path": str(bundle), "version": plist.get("CFBundleShortVersionString"),
            "identities": [{key: entry.get(key) for key in ("type", "subtype", "manufacturer")}
                           for entry in entries],
            "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "architectures": command(["lipo", "-archs", str(binary)]),
            "signature_verify": command(["codesign", "--verify", "--deep", "--strict", str(bundle)]),
            "signature_details": command(["codesign", "-dv", "--verbose=2", str(bundle)]),
        })
    except (OSError, KeyError, plistlib.InvalidFileException) as error:
        installed.append({"path": str(bundle), "error": str(error)})

identities = collections.defaultdict(list)
for bundle in installed:
    for identity in bundle.get("identities", []):
        identities["/".join(identity.values())].append(bundle["path"])

print(json.dumps({
    "date": datetime.date.today().isoformat(),
    "scope": "Catalog/source inventory and installed AU inspection; no fresh suite build or listening approval",
    "catalog_count": len(products), "lifecycle_counts": dict(collections.Counter(p["lifecycle"] for p in products)),
    "cached_ctest_count": len(tests), "products": products, "installed_au": installed,
    "duplicate_au_identities": {key: paths for key, paths in identities.items() if len(paths) > 1},
}, indent=2))
