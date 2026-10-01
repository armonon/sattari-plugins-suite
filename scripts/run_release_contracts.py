#!/usr/bin/env python3
"""Run the complete CTest inventory serially with a private bus ID per test.

Never move/delete the user's session bus. Use CTest (rather than globbing built
binaries) so missing executables and argument-based regression tests cannot be
silently omitted from a green release gate. --build refreshes the explicit test
target; it does not build format wrappers, install plug-ins, or sign bundles.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import uuid


def run(build: Path, build_first: bool = False, jobs: int = 2) -> int:
    if build_first:
        result = subprocess.run(["cmake", "--build", str(build), "--parallel", str(jobs),
                                 "--target", "SattariReleaseContracts"])
        if result.returncode != 0:
            print("FAIL: qualification build failed; refusing to test stale executables", flush=True)
            return 1
    inventory = subprocess.run(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        capture_output=True, text=True, check=True)
    tests = json.loads(inventory.stdout).get("tests", [])
    excluded = [test for test in tests if "stemdeck" in
                (test["name"] + " ".join(test.get("command", []))).lower()]
    for test in excluded:
        print(f"SKIP: {test['name']} (StemDeck is outside this release)", flush=True)
    tests = [test for test in tests if test not in excluded]
    if not tests:
        print("FAIL: CTest inventory is empty; nothing was validated", flush=True)
        return 1
    failed = []
    for test in tests:
        name = test["name"]
        env = dict(os.environ, SATTARI_SESSION_ID="gate-" + uuid.uuid4().hex)
        result = subprocess.run(
            ["ctest", "--test-dir", str(build), "--output-on-failure",
             "--timeout", "300", "--no-tests=error", "-R", "^" + re.escape(name) + "$"], env=env)
        if result.returncode != 0:
            failed.append(name)
    print(f"Release contracts: {len(tests) - len(failed)}/{len(tests)} passed", flush=True)
    if failed:
        print("FAILED: " + ", ".join(failed), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--build", action="store_true", dest="build_first",
                        help="Build the test-only SattariReleaseContracts target first")
    parser.add_argument("--jobs", type=int, default=2, help="Build parallelism (default: 2)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        raise SystemExit(run(args.build.resolve(), args.build_first, args.jobs))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Release contracts failed: {error}", flush=True)
        raise SystemExit(1)
