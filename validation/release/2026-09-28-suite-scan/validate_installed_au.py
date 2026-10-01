#!/usr/bin/env python3
"""Validate installed AU identities without building or installing any plugin."""
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

HERE = Path(__file__).resolve().parent
inventory = json.loads((HERE / "inventory.json").read_text())
logs = HERE / "auval"
logs.mkdir(exist_ok=True)
results = []
for bundle in inventory["installed_au"]:
    for identity in bundle.get("identities", []):
        args = [identity[key] for key in ("type", "subtype", "manufacturer")]
        started = time.monotonic()
        try:
            run = subprocess.run(["auval", "-v", *args], capture_output=True, text=True,
                                 timeout=90, env=dict(os.environ, SATTARI_SESSION_ID="audit-" + uuid.uuid4().hex))
            output = run.stdout + run.stderr
            code = run.returncode
        except subprocess.TimeoutExpired as error:
            output = "TIMEOUT after 90 seconds\n" + str(error.stdout or "") + str(error.stderr or "")
            code = -1
        log = logs / ("-".join(args) + ".log")
        log.write_text(output)
        passed = code == 0 and "AU VALIDATION SUCCEEDED" in output
        result = {"bundle": bundle["path"], "version": bundle["version"],
                  "binary_sha256": bundle["sha256"], "identity": identity,
                  "exit": code, "pass": passed, "seconds": round(time.monotonic() - started, 2),
                  "log": str(log.relative_to(HERE))}
        results.append(result)
        (HERE / "auval-results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"{'PASS' if passed else 'FAIL'} {'/'.join(args)} {Path(bundle['path']).name}", flush=True)
print(f"Installed AU validation: {sum(row['pass'] for row in results)}/{len(results)} passed", flush=True)
raise SystemExit(0 if results and all(row["pass"] for row in results) else 1)
