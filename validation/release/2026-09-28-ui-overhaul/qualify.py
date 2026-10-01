#!/usr/bin/env python3
"""Fresh, bounded qualification of the five UI candidate builds, not a release claim."""

import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BUILD = ROOT / "build/mix-automix"
PRODUCTS = [
    ("sattari-arp", "SattariArp", "Sattari Arp", "aumi", "Arp1", "0.1.1"),
    ("sub-conjurer", "SubConjurer", "Sub Conjurer", "aumf", "Sc01", "0.1.1"),
    ("mix", "SattariMix", "Sattari Mix", "aumf", "Mx01", "0.2.1"),
    ("vocal", "SattariVocal", "Sattari Vocal", "aumf", "Vcl1", "0.1.1"),
    ("create", "SattariCreate", "Sattari Create", "aumf", "Cr01", "0.1.1"),
]
results = []
previous = {}
if "--retry-failed" in sys.argv:
    previous = {result["name"]: result for result in json.loads((HERE / "results.json").read_text())}


def run(name, command, timeout=300):
    if name in previous and previous[name]["exit_code"] == 0:
        results.append(previous[name])
        return
    start = time.monotonic()
    with (HERE / f"{name}.log").open("w") as log:
        try:
            result = subprocess.run(
                [str(arg) for arg in command], cwd=ROOT, stdout=log,
                stderr=subprocess.STDOUT, timeout=timeout,
                env={**os.environ, "SATTARI_SESSION_ID": f"ui-qualification-{os.getpid()}-{name}"},
                check=False,
            )
            code = result.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            log.write(f"\nERROR: {error}\n")
            code = -1
    results.append({"name": name, "exit_code": code, "seconds": round(time.monotonic() - start, 2),
                    "command": [str(arg) for arg in command]})
    (HERE / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"{'PASS' if code == 0 else 'FAIL'} {name}", flush=True)


for slug, target, name, au_type, code, version in PRODUCTS:
    ui = BUILD / slug / f"{target}UIContract"
    run(f"{slug}-ui", [ui, HERE / "screenshots"])
    run(f"{slug}-ui-intel", ["arch", "-x86_64", ui])

for slug, target in [
    ("sattari-arp", "SattariArpTimingContract"),
    ("sub-conjurer", "SubConjurerContract"),
    ("mix", "SattariMixProductContract"),
    ("mix", "SattariMixAutoMixContract"),
    ("mix", "SattariMixRackRegression"),
    ("vocal", "SattariVocalProductContract"),
    ("create", "SattariCreateProductContract"),
]:
    binary = BUILD / slug / target if target != "SattariMixRackRegression" else BUILD / target
    args = [HERE / "screenshots"] if target == "SattariMixAutoMixContract" else []
    run(target, [binary, *args])

for slug, target, name, au_type, code, version in PRODUCTS:
    artifacts = BUILD / slug / f"{target}_artefacts/Release"
    for format_name, suffix in [("AU", "component"), ("VST3", "vst3"), ("Standalone", "app")]:
        if slug == "sattari-arp" and format_name == "Standalone":
            continue
        bundle = artifacts / format_name / f"{name}.{suffix}"
        run(f"{slug}-{format_name}-universal", ["lipo", bundle / "Contents/MacOS" / name, "-verify_arch", "arm64", "x86_64"])
        run(f"{slug}-{format_name}-version", ["/usr/libexec/PlistBuddy", "-c", "Print :CFBundleShortVersionString", bundle / "Contents/Info.plist"])
        observed = (HERE / f"{slug}-{format_name}-version.log").read_text().strip()
        if observed != version:
            results[-1]["exit_code"] = 1
            print(f"FAIL expected {version}, got {observed}", flush=True)
    run(f"{slug}-machine", ["bash", ROOT / "scripts/validate_plugin.sh", artifacts, au_type, code, "Satr"], timeout=600)

(HERE / "results.json").write_text(json.dumps(results, indent=2) + "\n")
failed = sum(result["exit_code"] != 0 for result in results)
print(f"{len(results) - failed}/{len(results)} qualification commands passed; manual release gates remain.")
raise SystemExit(bool(failed))
