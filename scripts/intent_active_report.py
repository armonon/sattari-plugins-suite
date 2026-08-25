#!/usr/bin/env python3
"""
Intent Engine v2 — ACTIVE AUDIO REPORT
Consumes _active.json (C++ v2_active engine, real audio path) +
_v2.json (shadow traces with ground-truth labels).
Produces INTENT_ENGINE_V2_ACTIVE_REPORT.md.
"""

import json
import math
import os
import glob
import sys
from pathlib import Path

REPO       = Path(__file__).resolve().parent.parent
TRACES_DIR = REPO / "vocalbench-results" / "intent-bench-v2" / "traces"
OUT_DIR    = REPO / "vocalbench-results" / "intent-bench-v2"

# ---------------------------------------------------------------------------
# Role constants (must match Phase 2 bench)
# ---------------------------------------------------------------------------
class Role:
    STABLE_TARGET     = "STABLE_TARGET"
    TRANSITION        = "TRANSITION"
    PASSING_TONE      = "PASSING_TONE"
    SCOOP             = "SCOOP"
    SLIDE             = "SLIDE"
    VIBRATO_EXCURSION = "VIBRATO_EXCURSION"
    WRONG_NOTE        = "WRONG_NOTE"
    RELEASE           = "RELEASE"

TRANSITION_ROLES = {Role.SLIDE, Role.SCOOP, Role.RELEASE}
PASSING_ROLES    = {Role.PASSING_TONE, Role.VIBRATO_EXCURSION}


# ---------------------------------------------------------------------------
# Load + join one case
# ---------------------------------------------------------------------------
def load_case(case_id):
    active_path = TRACES_DIR / f"{case_id}_active.json"
    shadow_path = TRACES_DIR / f"{case_id}_v2.json"

    with open(active_path) as f:
        active = json.load(f)
    with open(shadow_path) as f:
        shadow = json.load(f)

    af = active["frames"]
    sf = shadow["frames"]

    if len(af) != len(sf):
        print(f"  WARNING: {case_id} frame count mismatch ({len(af)} vs {len(sf)})")
        n = min(len(af), len(sf))
        af = af[:n]; sf = sf[:n]

    joined = []
    for a, s in zip(af, sf):
        row = {
            "blockIndex":       a["blockIndex"],
            "timeSec":          a["timeSec"],
            "voiced":           a["voiced"],
            "detectedMidi":     a.get("detectedMidi", -1),
            "confidence":       a.get("confidence", 0),
            "intentTarget":     a.get("intentTarget", -1),
            "intentConf":       a.get("intentConf", 0),
            "intentReasonName": a.get("intentReasonName", ""),
            "intentFallback":   a.get("intentFallback", True),
            "prodTarget":       a.get("prodTarget", -1),
            # actual driven target: v2 when not fallback, else prod
            "activeTarget":     a.get("intentTarget", -1) if not a.get("intentFallback", True) else a.get("prodTarget", -1),
            # ground truth from shadow trace
            "gtNoteRole":         s.get("gtNoteRole"),
            "gtIntendedDest":     s.get("gtIntendedDest"),
            "gtShouldCorrectNow": s.get("gtShouldCorrectNow"),
            "gtPerformedNote":    s.get("gtPerformedNote"),
            # python shadow v2 target for cross-check
            "shadowV2Target":     s.get("shadowV2Target"),
        }
        joined.append(row)

    # infer category from case_id
    if case_id.startswith("ADV_"):
        cat = _infer_adv_category(case_id)
    else:
        cat = _infer_base_category(case_id)

    meta = shadow.get("id", case_id)
    return {"id": case_id, "category": cat, "frames": joined}


def _infer_adv_category(cid):
    cid_u = cid.upper()
    if "BC_" in cid_u and ("+" in cid or "-" in cid):  return "adv_boundary"
    if "VIB" in cid_u:                                  return "adv_vib_boundary"
    if "CHROM" in cid_u:                                return "adv_chromatic_pass"
    if "SLIDE" in cid_u or "SCOOP" in cid_u or "FALL" in cid_u or "SEMI" in cid_u or "WHOLE" in cid_u or "RUN" in cid_u:
        return "adv_slide"
    return "adv_other"

def _infer_base_category(cid):
    cid_u = cid.upper()
    if cid_u.startswith("BC_"):         return "boundary"
    if "CHROM" in cid_u:                return "chromatic_passing"
    if "FALL" in cid_u:                 return "fall"
    if "OUT_OF_SCALE" in cid_u:         return "out_of_scale"
    if "REPEAT" in cid_u:               return "repeated_note"
    if cid_u.startswith("RUN_"):        return "run"
    if "SCOOP" in cid_u:               return "scoop"
    if "SEMI" in cid_u:                return "semitone_slide"
    if "SHORT" in cid_u:               return "short_note"
    if "VIB" in cid_u:                 return "vibrato_boundary"
    if "WHOLE" in cid_u:               return "whole_tone_slide"
    if "WRONG" in cid_u:              return "wrong_note"
    if "SLIDE" in cid_u:              return "adv_slide"
    return "other"


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------
def compute_metrics(frames):
    voiced  = [f for f in frames if f["voiced"] and f["gtNoteRole"]]
    stable  = [f for f in voiced if f["gtNoteRole"] == Role.STABLE_TARGET]
    transit = [f for f in voiced if f["gtNoteRole"] in TRANSITION_ROLES]
    passing = [f for f in voiced if f["gtNoteRole"] in PASSING_ROLES]
    driving = [f for f in voiced if not f["intentFallback"]]

    def acc(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key) == f.get("gtIntendedDest")) / len(frs)

    def pres(frs):
        # passing-tone preservation: target != performed note (don't commit to passing note)
        if not frs: return None
        return sum(1 for f in frs
                   if f.get("activeTarget") != round(f.get("gtPerformedNote") or 0)) / len(frs)

    def false_target(frs):
        should = [f for f in frs if f.get("gtShouldCorrectNow")]
        if not should: return None
        return sum(1 for f in should if f.get("activeTarget") != f.get("gtIntendedDest")) / len(should)

    def chatter(frs, key):
        if len(frs) < 2: return 0.0
        return sum(1 for i in range(1, len(frs)) if frs[i].get(key) != frs[i-1].get(key)) / len(frs)

    def octave_err(frs, key):
        return sum(1 for f in frs if abs((f.get(key) or 0) - (f.get("gtIntendedDest") or 0)) >= 12)

    driving_rate = len(driving) / len(voiced) if voiced else 0.0

    reason_dist = {}
    for f in voiced:
        r = f.get("intentReasonName") or "?"
        reason_dist[r] = reason_dist.get(r, 0) + 1

    return {
        "n_voiced":          len(voiced),
        "n_stable":          len(stable),
        "n_transit":         len(transit),
        "n_passing":         len(passing),
        "driving_rate":      driving_rate,
        "fallback_rate":     1.0 - driving_rate,
        # active (v2 driving or prod fallback)
        "active_stable_acc": acc(stable,  "activeTarget"),
        "active_dest_acc":   acc(transit, "activeTarget"),
        "active_pass_pres":  pres(passing),
        "active_false_tgt":  false_target(voiced),
        "active_chatter":    chatter(voiced, "activeTarget"),
        "active_oct_err":    octave_err(voiced, "activeTarget"),
        # production baseline (prodTarget in same run)
        "prod_stable_acc":   acc(stable,  "prodTarget"),
        "prod_dest_acc":     acc(transit, "prodTarget"),
        "prod_pass_pres":    pres_prod(passing),
        "prod_false_tgt":    false_target_prod(voiced),
        "prod_chatter":      chatter(voiced, "prodTarget"),
        "prod_oct_err":      octave_err(voiced, "prodTarget"),
        "reason_dist":       reason_dist,
    }

def pres_prod(frs):
    if not frs: return None
    return sum(1 for f in frs if f.get("prodTarget") != round(f.get("gtPerformedNote") or 0)) / len(frs)

def false_target_prod(frs):
    should = [f for f in frs if f.get("gtShouldCorrectNow")]
    if not should: return None
    return sum(1 for f in should if f.get("prodTarget") != f.get("gtIntendedDest")) / len(should)


# ---------------------------------------------------------------------------
# Aggregate
# ---------------------------------------------------------------------------
def aggregate(results):
    all_voiced, cats = [], {}
    for r in results:
        voiced = [f for f in r["frames"] if f["voiced"] and f["gtNoteRole"]]
        all_voiced.extend(voiced)
        cats.setdefault(r["category"], []).append(r)

    stable  = [f for f in all_voiced if f["gtNoteRole"] == Role.STABLE_TARGET]
    transit = [f for f in all_voiced if f["gtNoteRole"] in TRANSITION_ROLES]
    passing = [f for f in all_voiced if f["gtNoteRole"] in PASSING_ROLES]
    driving = [f for f in all_voiced if not f["intentFallback"]]

    def acc(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key) == f.get("gtIntendedDest")) / len(frs)

    def false_tgt(frs, key):
        should = [f for f in frs if f.get("gtShouldCorrectNow")]
        if not should: return 0.0
        return sum(1 for f in should if f.get(key) != f.get("gtIntendedDest")) / len(should)

    def cht(frs, key):
        if len(frs) < 2: return 0.0
        return sum(1 for i in range(1,len(frs)) if frs[i].get(key) != frs[i-1].get(key)) / len(frs)

    def pass_pres_k(frs, key):
        if not frs: return None
        return sum(1 for f in frs if f.get(key) != round(f.get("gtPerformedNote") or 0)) / len(frs)

    def oct_err(frs, key):
        return sum(1 for f in frs if abs((f.get(key) or 0)-(f.get("gtIntendedDest") or 0))>=12)

    driving_rate = len(driving) / len(all_voiced) if all_voiced else 0.0

    # Aggregate reason distribution
    reason_agg = {}
    for f in all_voiced:
        r = f.get("intentReasonName") or "?"
        reason_agg[r] = reason_agg.get(r, 0) + 1

    m = {
        "total_voiced":        len(all_voiced),
        "total_driving":       len(driving),
        "driving_rate":        driving_rate,
        # active
        "active_stable_acc":   acc(stable,  "activeTarget"),
        "active_dest_acc":     acc(transit, "activeTarget"),
        "active_pass_pres":    pass_pres_k(passing, "activeTarget"),
        "active_false_tgt":    false_tgt(all_voiced, "activeTarget"),
        "active_chatter":      cht(all_voiced, "activeTarget"),
        "active_oct_err":      oct_err(all_voiced, "activeTarget"),
        # production
        "prod_stable_acc":     acc(stable,  "prodTarget"),
        "prod_dest_acc":       acc(transit, "prodTarget"),
        "prod_pass_pres":      pass_pres_k(passing, "prodTarget"),
        "prod_false_tgt":      false_tgt(all_voiced, "prodTarget"),
        "prod_chatter":        cht(all_voiced, "prodTarget"),
        "prod_oct_err":        oct_err(all_voiced, "prodTarget"),
        "reason_agg":          reason_agg,
        "cats":                cats,
    }
    return m


# ---------------------------------------------------------------------------
# Gate
# ---------------------------------------------------------------------------
def gate_check(m):
    def fmt(v): return f"{v:.1%}" if v is not None else "—"

    stable_ok  = (m["active_stable_acc"] or 0) >= (m["prod_stable_acc"] or 0) - 0.02
    dest_ok    = (m["active_dest_acc"]   or 0) >= (m["prod_dest_acc"]   or 0)
    pass_ok    = (m["active_pass_pres"]  or 0) >= (m["prod_pass_pres"]  or 0) - 0.02
    ft_ok      = (m["active_false_tgt"]  or 0) <= (m["prod_false_tgt"]  or 1.0) + 0.02
    chatter_ok = (m["active_chatter"]    or 0) <= (m["prod_chatter"]    or 0) * 1.20
    octave_ok  = m["active_oct_err"] == 0
    driving_ok = m["driving_rate"] >= 0.50   # v2 must drive at least 50% of voiced frames

    # Improvement is meaningful if any key metric improves by ≥1pp
    meaningful = (
        ((m["active_stable_acc"] or 0) - (m["prod_stable_acc"] or 0)) > 0.01
        or ((m["active_dest_acc"] or 0)   - (m["prod_dest_acc"]   or 0)) > 0.01
        or ((m["prod_false_tgt"]  or 0)   - (m["active_false_tgt"] or 0)) > 0.01
    )

    go = stable_ok and dest_ok and pass_ok and ft_ok and chatter_ok and octave_ok and driving_ok

    if go and meaningful:
        verdict = "GO — ACTIVE AUDIO CONFIRMED"
    elif go:
        verdict = "HOLD — PARITY (no regression, but improvement not confirmed)"
    else:
        issues = []
        if not stable_ok:  issues.append(f"stable-target accuracy regressed (prod={fmt(m['prod_stable_acc'])} active={fmt(m['active_stable_acc'])})")
        if not dest_ok:    issues.append(f"destination accuracy regressed (prod={fmt(m['prod_dest_acc'])} active={fmt(m['active_dest_acc'])})")
        if not pass_ok:    issues.append(f"passing-tone preservation regressed (prod={fmt(m['prod_pass_pres'])} active={fmt(m['active_pass_pres'])})")
        if not ft_ok:      issues.append(f"false-target rate increased (prod={fmt(m['prod_false_tgt'])} active={fmt(m['active_false_tgt'])})")
        if not chatter_ok: issues.append(f"chatter exceeded production×1.20 (prod={fmt(m['prod_chatter'])} active={fmt(m['active_chatter'])})")
        if not octave_ok:  issues.append(f"octave errors: {m['active_oct_err']}")
        if not driving_ok: issues.append(f"v2 driving rate too low ({m['driving_rate']:.1%})")
        verdict = "FAIL — REGRESSIONS\n" + "\n".join(f"  - {i}" for i in issues)

    return verdict


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
def write_report(results, m, verdict):
    def fmt(v): return f"{v:.1%}" if v is not None else "—"
    def fmtN(v): return f"{v}" if v is not None else "—"

    lines = [
        "# Intent Engine v2 — ACTIVE AUDIO REPORT",
        "",
        "**Phase:** ACTIVE AUDIO EXPERIMENT (C++ v2_active mode drives real audio path)",
        f"**Corpus:** {len(results)} cases | **Voiced frames:** {m['total_voiced']:,} | **v2 driving:** {m['total_driving']:,} ({m['driving_rate']:.1%})",
        "",
        "---",
        "",
        "## Verdict",
        "",
        f"```",
        verdict,
        "```",
        "",
        "---",
        "",
        "## Aggregate Metrics",
        "",
        "| Metric | Production | v2 Active | Delta |",
        "|--------|-----------|-----------|-------|",
    ]

    rows = [
        ("Stable-target accuracy",      "prod_stable_acc",  "active_stable_acc"),
        ("Destination accuracy",         "prod_dest_acc",    "active_dest_acc"),
        ("Passing-tone preservation",    "prod_pass_pres",   "active_pass_pres"),
        ("False-target rate (lower=better)", "prod_false_tgt", "active_false_tgt"),
        ("Chatter rate (lower=better)",  "prod_chatter",     "active_chatter"),
    ]
    for label, pk, ak in rows:
        pv = m[pk]; av = m[ak]
        if pv is not None and av is not None:
            delta = av - pv
            sign  = "+" if delta >= 0 else ""
            delta_s = f"{sign}{delta:.1%}"
        else:
            delta_s = "—"
        lines.append(f"| {label} | {fmt(pv)} | {fmt(av)} | {delta_s} |")

    lines += [
        f"| Octave errors | {m['prod_oct_err']} | {m['active_oct_err']} | — |",
        "",
        "---",
        "",
        "## v2 Driving Breakdown",
        "",
        f"- **v2 driving rate:** {m['driving_rate']:.1%} ({m['total_driving']:,} / {m['total_voiced']:,} voiced frames)",
        f"- **Fallback rate:** {1-m['driving_rate']:.1%} (fell back to production target)",
        "",
        "### C++ Reason Code Distribution (all voiced frames)",
        "",
        "| Reason | Frames | % |",
        "|--------|--------|---|",
    ]
    for rc, cnt in sorted(m["reason_agg"].items(), key=lambda x: -x[1]):
        lines.append(f"| {rc} | {cnt:,} | {cnt / m['total_voiced']:.1%} |")

    lines += [
        "",
        "---",
        "",
        "## By Category",
        "",
        "| Category | N | Prod Stab | Active Stab | Prod Dest | Active Dest | Active Pass | Active FT | Drive% |",
        "|----------|---|-----------|-------------|-----------|-------------|-------------|-----------|--------|",
    ]

    for cat, rs in sorted(m["cats"].items()):
        all_f  = []
        for r in rs: all_f.extend([f for f in r["frames"] if f["voiced"] and f["gtNoteRole"]])
        stab   = [f for f in all_f if f["gtNoteRole"] == Role.STABLE_TARGET]
        trans  = [f for f in all_f if f["gtNoteRole"] in TRANSITION_ROLES]
        passf  = [f for f in all_f if f["gtNoteRole"] in PASSING_ROLES]
        drv    = [f for f in all_f if not f["intentFallback"]]

        def a(frs, k):
            if not frs: return None
            return sum(1 for f in frs if f.get(k) == f.get("gtIntendedDest")) / len(frs)
        def pp(frs, k):
            if not frs: return None
            return sum(1 for f in frs if f.get(k) != round(f.get("gtPerformedNote") or 0)) / len(frs)
        def ft(frs, k):
            sh = [f for f in frs if f.get("gtShouldCorrectNow")]
            if not sh: return None
            return sum(1 for f in sh if f.get(k) != f.get("gtIntendedDest")) / len(sh)

        drive_pct = len(drv) / len(all_f) if all_f else 0.0
        lines.append(
            f"| {cat} | {len(rs)} "
            f"| {fmt(a(stab,'prodTarget'))} | {fmt(a(stab,'activeTarget'))} "
            f"| {fmt(a(trans,'prodTarget'))} | {fmt(a(trans,'activeTarget'))} "
            f"| {fmt(pp(passf,'activeTarget'))} "
            f"| {fmt(ft(all_f,'activeTarget'))} "
            f"| {drive_pct:.0%} |"
        )

    # Worst regressions
    deltas = []
    for r in results:
        m_r = r.get("metrics", {})
        ps  = m_r.get("prod_stable_acc")
        as_ = m_r.get("active_stable_acc")
        if ps is not None and as_ is not None:
            deltas.append((r["id"], r["category"], ps, as_, as_ - ps))
    deltas.sort(key=lambda x: x[4])  # worst first

    lines += [
        "",
        "---",
        "",
        "## Worst Cases (by Stable-Target Delta)",
        "",
        "| Case | Category | Prod Stab | Active Stab | Delta |",
        "|------|----------|-----------|-------------|-------|",
    ]
    for cid, cat, ps, as_, d in deltas[:10]:
        sign = "+" if d >= 0 else ""
        lines.append(f"| {cid} | {cat} | {fmt(ps)} | {fmt(as_)} | {sign}{d:.1%} |")

    lines += [
        "",
        "---",
        "",
        "## Architecture Note",
        "",
        "In V2_ACTIVE mode, the C++ intent engine drives `targetMidi` when `intentConf ≥ 0.68`",
        "(high-confidence) or when `intentConf ≥ 0.50` with STABILITY_ANCHOR / VIBRATO_HOLD /",
        "PASSING_TONE reason. Below that threshold, the engine falls back to the production target.",
        "Strategy G correction dynamics (transitionBoost, hysteresis) remain frozen.",
        "",
        "**Active target = intentTarget when !intentFallback, else prodTarget.**",
    ]

    path = OUT_DIR / "INTENT_ENGINE_V2_ACTIVE_REPORT.md"
    path.write_text("\n".join(lines))
    print(f"\nReport written: {path}")
    return path


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    active_files = sorted(TRACES_DIR.glob("*_active.json"))
    if not active_files:
        print("ERROR: no *_active.json files found in", TRACES_DIR); sys.exit(1)

    case_ids = [f.name.replace("_active.json", "") for f in active_files]
    print(f"Intent Engine v2 — ACTIVE AUDIO REPORT")
    print(f"{'='*52}")
    print(f"Found {len(case_ids)} active traces\n")

    results = []
    for i, cid in enumerate(case_ids):
        print(f"  [{i+1:3d}/{len(case_ids)}] {cid} ... ", end="", flush=True)
        try:
            r = load_case(cid)
        except Exception as e:
            print(f"ERROR: {e}"); continue

        m = compute_metrics(r["frames"])
        r["metrics"] = m

        voiced = [f for f in r["frames"] if f["voiced"] and f["gtNoteRole"]]
        driving = [f for f in voiced if not f["intentFallback"]]
        drive_pct = len(driving)/len(voiced) if voiced else 0

        ps = m.get("prod_stable_acc"); as_ = m.get("active_stable_acc")
        delta = (as_ or 0) - (ps or 0)
        sign = "+" if delta >= 0 else ""
        ps_s  = f"{ps:.0%}" if ps  is not None else "—"
        as_s  = f"{as_:.0%}" if as_ is not None else "—"
        print(f"stab prod={ps_s} active={as_s} ({sign}{delta:.0%})  drive={drive_pct:.0%}")

        results.append(r)

        if (i+1) % 10 == 0:
            # Intermediate aggregate
            m_tmp = aggregate(results)
            print(f"\n  --- Progress [{i+1}/{len(case_ids)}] ---")
            print(f"  Voiced so far:  {m_tmp['total_voiced']:,}")
            print(f"  Stable acc:     prod={m_tmp['prod_stable_acc']:.1%}  active={m_tmp['active_stable_acc']:.1%}")
            print(f"  Dest acc:       prod={m_tmp['prod_dest_acc']:.1%}  active={m_tmp['active_dest_acc']:.1%}" if m_tmp['prod_dest_acc'] else "  Dest acc:  —")
            print(f"  Driving rate:   {m_tmp['driving_rate']:.1%}")
            print()

    print(f"\n{'='*52}")
    print(f"Aggregating {len(results)} cases...")
    m = aggregate(results)
    verdict = gate_check(m)

    def fmt(v): return f"{v:.1%}" if v is not None else "—"
    print(f"\nAGGREGATE RESULTS")
    print(f"  Voiced frames:         {m['total_voiced']:,}")
    print(f"  v2 driving rate:       {m['driving_rate']:.1%}")
    print(f"  Stable-target acc:     prod={fmt(m['prod_stable_acc'])}  active={fmt(m['active_stable_acc'])}")
    print(f"  Destination acc:       prod={fmt(m['prod_dest_acc'])}  active={fmt(m['active_dest_acc'])}")
    print(f"  Passing-tone pres:     prod={fmt(m['prod_pass_pres'])}  active={fmt(m['active_pass_pres'])}")
    print(f"  False-target rate:     prod={fmt(m['prod_false_tgt'])}  active={fmt(m['active_false_tgt'])}")
    print(f"  Chatter rate:          prod={fmt(m['prod_chatter'])}  active={fmt(m['active_chatter'])}")
    print(f"  Octave errors:         prod={m['prod_oct_err']}  active={m['active_oct_err']}")
    print(f"\n{verdict}")

    write_report(results, m, verdict)


if __name__ == "__main__":
    main()
