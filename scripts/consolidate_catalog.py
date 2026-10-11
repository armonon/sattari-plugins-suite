#!/usr/bin/env python3
"""Apply the portfolio consolidation without changing binary identities or lifecycle."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def consolidate(catalog: dict) -> dict:
    groups = [
        dict(id="modular", name="Sattari Modular", verb="builds", accent="amber", tier="paid",
             tagline="Native effects and layered instruments in one chain platform, with VST3 hosting.", primary="modular", kind="flagship", page="modular.html"),
        dict(id="royal-chain", name="Sattari Royal Chain", verb="finishes", accent="mint", tier="paid",
             tagline="A focused vocal finishing chain with tuning, dynamics, tone and ambience.", primary="royal-chain", kind="flagship"),
        dict(id="auto-pitch", name="Sattari Auto Pitch", verb="tunes", accent="mint", tier="paid",
             tagline="Precision vocal tuning, captured-note editing and harmony.", primary="auto-pitch", kind="flagship"),
        dict(id="mix", name="Sattari Mix", verb="balances", accent="blue", tier="paid",
             tagline="Linked-track analysis and gain, EQ and dynamics proposals with compare and undo.", primary="mix", kind="flagship"),
        dict(id="tools", name="Sattari Tools", verb="shapes", accent="amber", tier="bundle",
             tagline="Arpeggiation, low-end generation, MIDI mapping, ducking and timing alignment.", primary="sattari-arp", kind="tools"),
        dict(id="compass", name="Sattari Compass", verb="measures", accent="blue", tier="free",
             tagline="Session context, key detection, tuning displays and analysis utilities.", primary="brain", kind="foundation"),
    ]
    collections = {
        "vocal": ["clean", "gate", "deess", "eq", "comp", "heat", "double", "face", "glass-choir", "space"],
        "create": ["echo", "heat", "pulse", "riser", "shimmer", "space", "warble", "warp", "vocoder", "eightoheight"],
        "instruments": ["chop", "granulator", "rhythm-genie"],
        "mix": ["clean", "eq", "comp", "gate", "peak", "multiband", "imager", "transient"],
    }
    by_id = {p["id"]: p for p in catalog["products"]}
    for pid, name, target, role in [
        ("modular", "Sattari Modular FX", "SattariModularFX", "primary"),
        ("modular-instrument", "Sattari Modular Instrument", "SattariModularInstrument", "variant"),
    ]:
        by_id.setdefault(pid, dict(id=pid, name=name, category="Modular rack", accent="amber",
                                  lifecycle="alpha", role="instrument" if role == "variant" else "rack",
                                  shippingProfile="alpha", webVisible=True))
        by_id[pid].update(sourceDirectory="modular", buildTarget=target,
                         formats=["AU", "VST3", "Standalone"], binaryVersion="0.2.0")

    assignments = {s["primary"]: (s["id"], "primary", "active") for s in groups}
    assignments["modular-instrument"] = ("modular", "variant", "active")
    for pid in ("sub-conjurer", "sattari-arp", "side-chain-master", "align", "scaler"):
        assignments.setdefault(pid, ("tools", "component", "active"))
    for pid in ("song-key", "tuner", "scope", "meter"):
        assignments[pid] = ("compass", "component", "active")
    for pid in ("vocal", "create", "stack"):
        assignments[pid] = ("modular", "legacy", "legacy")
    assignments["vocal-master"] = ("royal-chain", "legacy", "legacy")
    assignments["bus"] = ("mix", "legacy", "legacy")
    assignments["harmony"] = ("auto-pitch", "component", "migration-pending")
    assignments["maqam"] = ("auto-pitch", "component", "specialist")
    for pid in ("voxkey", "voxsynth", "morph"):
        assignments[pid] = ("modular", "component", "held")
    module_ids = {pid for members in collections.values() for pid in members}
    for pid in module_ids:
        assignments[pid] = ("modular", "component", "module")
    for pid, product in by_id.items():
        product["collections"] = [cid for cid, members in collections.items() if pid in members]
        if pid == "stemdeck":
            product.update(portfolioStatus="separate", suite=None, suiteRole=None)
            continue
        group, role, status = assignments[pid]
        product.update(suite=group, suiteRole=role, portfolioStatus=status,
                       webVisible=status in ("active", "specialist"))
        if status in ("legacy", "module", "migration-pending", "held"):
            product["destination"] = group
    catalog.update(schemaVersion=2, suite=groups, products=list(by_id.values()),
                   collections=[dict(id=cid, name=f"Sattari {cid.title()} Collection",
                                     destination="modular", members=members)
                                for cid, members in collections.items()],
                   consolidation=dict(date="2026-10-02", preserveLegacyIdentities=True,
                                      publicReleaseApproved=False))
    return catalog


if __name__ == "__main__":
    path = ROOT / "config/products.json"
    path.write_text(json.dumps(consolidate(json.loads(path.read_text())), indent=2) + "\n")
