#!/usr/bin/env python3
"""Run the complete CTest inventory serially with a private bus ID per test.

Never move/delete the user's session bus. Use CTest (rather than globbing built
binaries) so missing executables and argument-based regression tests cannot be
silently omitted from a green release gate. --build refreshes the explicit test
target; it does not build format wrappers, install plug-ins, or sign bundles.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid


def fingerprint(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(build: Path, build_first: bool = False, jobs: int = 2,
        config: str | None = None, report: Path | None = None) -> int:
    evidence = {"schemaVersion": 1, "startedUtc": datetime.now(timezone.utc).isoformat(),
                "buildDirectory": str(build), "configuration": config,
                "buildFirst": build_first, "results": [], "passed": False}
    def finish(code):
        evidence["completedUtc"] = datetime.now(timezone.utc).isoformat()
        evidence["passed"] = code == 0
        if report is not None:
            report.parent.mkdir(parents=True, exist_ok=True)
            temporary = report.with_suffix(report.suffix + ".partial")
            temporary.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
            temporary.replace(report)
        return code

    if report is not None:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    configuration = ["-C", config] if config else []
    if build_first:
        command = ["cmake", "--build", str(build), "--parallel", str(jobs),
                   "--target", "SattariReleaseContracts"]
        if config:
            command += ["--config", config]
        result = subprocess.run(command)
        evidence["buildExitCode"] = result.returncode
        if result.returncode != 0:
            print("FAIL: qualification build failed; refusing to test stale executables", flush=True)
            return finish(1)
    inventory = subprocess.run(
        ["ctest", "--test-dir", str(build), *configuration, "--show-only=json-v1"],
        capture_output=True, text=True, check=True)
    tests = json.loads(inventory.stdout).get("tests", [])
    excluded = [test for test in tests if "stemdeck" in
                (test["name"] + " ".join(test.get("command", []))).lower()]
    for test in excluded:
        print(f"SKIP: {test['name']} (StemDeck is outside this release)", flush=True)
    evidence["excludedTests"] = [test["name"] for test in excluded]
    tests = [test for test in tests if test not in excluded]
    if not tests:
        print("FAIL: CTest inventory is empty; nothing was validated", flush=True)
        return finish(1)
    failed = []
    for test in tests:
        name = test["name"]
        command = test.get("command", [])
        executable = Path(command[0]) if command else None
        before = fingerprint(executable) if executable and executable.is_file() else None
        env = dict(os.environ, SATTARI_SESSION_ID="gate-" + uuid.uuid4().hex)
        result = subprocess.run(
            ["ctest", "--test-dir", str(build), *configuration, "--output-on-failure",
             "--timeout", "300", "--no-tests=error", "-R", "^" + re.escape(name) + "$"], env=env)
        after = fingerprint(executable) if executable and executable.is_file() else None
        changed = before != after
        if changed:
            print(f"FAIL: executable changed during testing: {name}", flush=True)
        evidence["results"].append({"name": name, "command": command,
                                    "exitCode": result.returncode if not changed else 1,
                                    "executableSha256": before})
        if result.returncode != 0 or changed:
            failed.append(name)
    print(f"Release contracts: {len(tests) - len(failed)}/{len(tests)} passed", flush=True)
    if failed:
        print("FAILED: " + ", ".join(failed), flush=True)
    return finish(1 if failed else 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--build", action="store_true", dest="build_first",
                        help="Build the test-only SattariReleaseContracts target first")
    parser.add_argument("--jobs", type=int, default=2, help="Build parallelism (default: 2)")
    parser.add_argument("--config", help="Multi-configuration build configuration (e.g. Release)")
    parser.add_argument("--report", type=Path, help="Write machine-readable results and executable hashes")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        raise SystemExit(run(args.build.resolve(), args.build_first, args.jobs, args.config, args.report))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Release contracts failed: {error}", flush=True)
        raise SystemExit(1)
