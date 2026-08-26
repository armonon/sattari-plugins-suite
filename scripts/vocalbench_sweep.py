#!/usr/bin/env python3
"""Phase 15 — Natural mode correction-strength coefficient sweep.

Tests candidate coefficients for styleCorrectionStrength (Natural mode):
  Baseline: amount * 0.52
  Candidates: 0.60, 0.70, 0.80, 0.90, 1.00

For each candidate:
  1. Edits PluginProcessor.cpp to change the coefficient
  2. Incremental cmake --build (only recompiles PluginProcessor.cpp)
  3. Runs VocalBench stable suite (39 cases)
  4. Records: mean error, p90, p95, max, efficiency, pass rate, regression test status
  5. Restores original coefficient

Usage:
    python3 scripts/vocalbench_sweep.py \\
        [--bench build/plugins/auto-pitch/AutoPitchBench] \\
        [--output vocalbench-sweep-results]
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT  = SCRIPT_DIR.parent
PROCESSOR  = REPO_ROOT / "plugins" / "auto-pitch" / "src" / "PluginProcessor.cpp"
BUILD_DIR  = REPO_ROOT / "build"

CANDIDATES = [
    ("baseline", 0.52),
    ("A",        0.60),
    ("B",        0.70),
    ("C",        0.80),
    ("D",        0.90),
    ("E",        1.00),
]

COEFF_PATTERN = re.compile(
    r'(if \(style == "Natural"\)\s*\n\s*return amount \* )(\d+\.\d+f)(;)')


def read_original_coeff() -> float:
    src = PROCESSOR.read_text()
    m = COEFF_PATTERN.search(src)
    if not m:
        raise RuntimeError("Could not find Natural strength coefficient in source")
    return float(m.group(2).rstrip("f"))


def patch_coeff(new_coeff: float) -> None:
    src = PROCESSOR.read_text()
    new_src = COEFF_PATTERN.sub(
        rf'\g<1>{new_coeff:.2f}f\g<3>', src)
    # When replacing with the same value (baseline re-run), no change is needed.
    PROCESSOR.write_text(new_src)


def rebuild() -> tuple[bool, str]:
    result = subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "--target", "AutoPitchBench"],
        capture_output=True, text=True, timeout=120,
        cwd=str(REPO_ROOT))
    ok = result.returncode == 0
    return ok, (result.stdout + result.stderr).strip()


def run_bench_suite(bench: Path, output_dir: Path,
                    bench_args: dict) -> dict:
    """Run VocalBench stable suite; return parsed summary from results.json."""
    cmd = [
        sys.executable,
        str(SCRIPT_DIR / "vocalbench_run.py"),
        "--bench", str(bench),
        "--suite", "stable",
        "--output", str(output_dir),
        "--key",    bench_args["key"],
        "--scale",  bench_args["scale"],
        "--style",  str(bench_args["style"]),
        "--amount", str(bench_args["amount"]),
        "--speed",  str(bench_args["speed"]),
        "--block",  str(bench_args["block"]),
        "--skip-regression",
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if proc.returncode not in (0, 1):
        return {"error": f"exit {proc.returncode}", "output": proc.stderr}

    rj = output_dir / "results.json"
    if not rj.exists():
        return {"error": "results.json not written"}
    try:
        return json.loads(rj.read_text())
    except Exception as e:
        return {"error": str(e)}


def run_regression(build_dir: Path) -> dict:
    ok = True
    for cmd_args, name in [
        (["AutoPitchCoreContract"], "CoreContract"),
        (["AutoPitchRender", "--smoke"], "Render --smoke"),
        (["AutoPitchRender"], "Render null-test"),
    ]:
        p = subprocess.run(
            [str(build_dir / "plugins" / "auto-pitch" / cmd_args[0])] + cmd_args[1:],
            capture_output=True, text=True, timeout=60)
        if p.returncode != 0:
            ok = False
            print(f"  ❌ {name}: exit {p.returncode}")
        else:
            print(f"  ✅ {name}")
    return {"passed": ok}


def extract_summary(results: dict) -> dict:
    s = results.get("summary", {})
    ae = s.get("absOutputErrorStats", {})
    ef = s.get("correctionEfficiencyStats", {})
    return {
        "passRate":       f"{s.get('passed',0)}/{s.get('totalCases',0)}",
        "meanAbsErr":     ae.get("mean"),
        "medianAbsErr":   ae.get("median"),
        "p90AbsErr":      ae.get("p90"),
        "p95AbsErr":      ae.get("p95"),
        "maxAbsErr":      ae.get("max"),
        "meanEfficiency": ef.get("mean"),
        "octaveErrors":   s.get("octaveErrors", 0),
        "crashes":        s.get("crashes", 0),
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Phase 15 — Natural mode coefficient sweep")
    ap.add_argument("--bench",  default="")
    ap.add_argument("--output", default="vocalbench-sweep-results")
    ap.add_argument("--key",    default="A")
    ap.add_argument("--scale",  default="minor")
    ap.add_argument("--style",  default=0, type=int)
    ap.add_argument("--amount", default=1.0, type=float)
    ap.add_argument("--speed",  default=0.9, type=float)
    ap.add_argument("--block",  default=512, type=int)
    args = ap.parse_args()

    bench_path = Path(args.bench) if args.bench else None
    if bench_path is None or not bench_path.exists():
        for p in [
            REPO_ROOT / "build" / "plugins" / "auto-pitch" / "AutoPitchBench",
        ]:
            if p.exists() and os.access(p, os.X_OK):
                bench_path = p
                break
    if bench_path is None:
        print("ERROR: AutoPitchBench not found", file=sys.stderr)
        return 1

    bench_args = {
        "key": args.key, "scale": args.scale, "style": args.style,
        "amount": args.amount, "speed": args.speed, "block": args.block,
    }

    output_dir = REPO_ROOT / args.output
    output_dir.mkdir(parents=True, exist_ok=True)

    # Verify we can find and parse the current coefficient
    original_coeff = read_original_coeff()
    print(f"Original Natural coefficient: {original_coeff:.2f}")
    print(f"Candidates: {[c[1] for c in CANDIDATES]}")
    print()

    results: list[dict] = []

    for variant_name, coeff in CANDIDATES:
        print(f"{'='*60}")
        print(f"Variant {variant_name}: coefficient = {coeff:.2f}")

        # Patch the source
        patch_coeff(coeff)

        # Rebuild
        print("  Building...")
        ok, build_log = rebuild()
        if not ok:
            print(f"  BUILD FAILED:\n{build_log[:500]}")
            results.append({"variant": variant_name, "coeff": coeff,
                            "buildFailed": True})
            patch_coeff(original_coeff)
            continue
        print("  Build ok")

        # Run regression
        print("  Regression gate...")
        reg = run_regression(BUILD_DIR)
        if not reg["passed"]:
            print("  REGRESSION GATE FAILED — recording but continuing")

        # Run stable suite
        variant_dir = output_dir / f"variant_{variant_name}_{coeff:.2f}"
        print(f"  Running stable suite → {variant_dir.name}")
        suite_results = run_bench_suite(bench_path, variant_dir, bench_args)

        if "error" in suite_results:
            print(f"  Suite error: {suite_results['error']}")
            results.append({"variant": variant_name, "coeff": coeff,
                            "suiteError": suite_results["error"]})
        else:
            s = extract_summary(suite_results)
            results.append({
                "variant":            variant_name,
                "coeff":              coeff,
                "predictedStrength":  round(coeff * args.amount, 4),
                "buildFailed":        False,
                "regressionPassed":   reg["passed"],
                **s,
            })
            print(f"  pass={s['passRate']}  "
                  f"mean={s['meanAbsErr']}c  "
                  f"p90={s['p90AbsErr']}c  "
                  f"max={s['maxAbsErr']}c  "
                  f"eff={s['meanEfficiency']}")

    # Restore original coefficient
    patch_coeff(original_coeff)
    print(f"\nRestored coefficient to {original_coeff:.2f}")

    # Write sweep results
    sweep_doc = {
        "hypothesis": "Raising Natural mode styleCorrectionStrength coefficient improves residual",
        "benchSettings": bench_args,
        "variants": results,
    }
    rj = output_dir / "sweep_results.json"
    rj.write_text(json.dumps(sweep_doc, indent=2) + "\n")
    print(f"\nwrote: {rj}")

    # Generate comparison table
    lines: list[str] = []
    lines.append("# Phase 15 — Natural Mode Correction Strength Sweep\n")
    lines.append(f"Key: {args.key} {args.scale}  Amount: {args.amount}  Speed: {args.speed}\n")
    lines.append("| Variant | Coeff | Strength | Pass | Mean Err | p90 | p95 | Max | Eff | Regression |")
    lines.append("|---------|-------|----------|------|----------|-----|-----|-----|-----|------------|")
    for r in results:
        if r.get("buildFailed"):
            lines.append(f"| {r['variant']} | {r['coeff']:.2f} | — | BUILD FAIL | — | — | — | — | — | — |")
            continue
        strength = round(r.get("predictedStrength", 0) * 100, 0)
        reg = "✅" if r.get("regressionPassed") else "❌"
        lines.append(
            f"| {r['variant']} | {r['coeff']:.2f} | {strength:.0f}% | "
            f"{r.get('passRate','—')} | "
            f"{r.get('meanAbsErr','—')}c | "
            f"{r.get('p90AbsErr','—')}c | "
            f"{r.get('p95AbsErr','—')}c | "
            f"{r.get('maxAbsErr','—')}c | "
            f"{r.get('meanEfficiency','—')} | "
            f"{reg} |")

    lines.append("")
    lines.append("## Interpretation\n")
    if results and not results[0].get("buildFailed"):
        baseline = results[0]
        b_mean = baseline.get("meanAbsErr", 0) or 0
        for r in results[1:]:
            if r.get("buildFailed"):
                continue
            r_mean = r.get("meanAbsErr", 0) or 0
            delta = r_mean - b_mean
            sign  = "improved" if delta < 0 else "degraded"
            lines.append(f"- Variant {r['variant']} (coeff={r['coeff']:.2f}): "
                         f"mean error {sign} by {abs(delta):.2f}c vs baseline")

    report_md = output_dir / "sweep_report.md"
    report_md.write_text("\n".join(lines) + "\n")
    print(f"wrote: {report_md}")

    # Determine winner
    good = [r for r in results
            if not r.get("buildFailed") and r.get("regressionPassed")]
    if not good:
        print("\nNo viable candidates found.")
        return 1

    winner = min(good, key=lambda r: r.get("meanAbsErr") or 999)
    print(f"\n{'='*60}")
    print(f"Lowest mean error: Variant {winner['variant']} (coeff={winner['coeff']:.2f})")
    print(f"  mean={winner.get('meanAbsErr')}c  "
          f"p90={winner.get('p90AbsErr')}c  "
          f"eff={winner.get('meanEfficiency')}")
    print(f"{'='*60}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
