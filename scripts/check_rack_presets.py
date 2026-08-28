#!/usr/bin/env python3
"""Verify every rack factory preset references a real module and a real parameter.

Why this exists: SattariPresetManager::applyValues() SKIPS parameter ids it does
not recognise. A typo'd id therefore produces a preset that loads cleanly, sounds
wrong, and reports no error anywhere — the worst possible failure mode. Module ids
have the same problem in the other direction (a bad id resolves to slot "Empty").

This walks the rack hosts' addChainPreset() calls and checks both halves against
the module headers that actually define the parameters.

Run:  python3 scripts/check_rack_presets.py     (exit 1 on any bad reference)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODULE_DIR = os.path.join(ROOT, "core", "include", "sattari_audio_core", "modules")
HOSTS = [
    os.path.join(ROOT, "plugins", "mix", "src", "PluginProcessor.cpp"),
    os.path.join(ROOT, "plugins", "create", "src", "PluginProcessor.cpp"),
    os.path.join(ROOT, "plugins", "vocal", "src", "PluginProcessor.cpp"),
]


def module_parameters():
    """moduleId -> set of parameter ids, parsed from each <X>Module.h."""
    mods = {}
    for fn in sorted(os.listdir(MODULE_DIR)):
        if not fn.endswith("Module.h"):
            continue
        src = open(os.path.join(MODULE_DIR, fn)).read()
        m = re.search(r'moduleId\(\)\s*const noexcept override\s*\{\s*return "([^"]+)"', src)
        if not m:
            continue
        params = set()
        # ParameterID (prefix + "x")  /  ParameterID { prefix + "x" }
        params |= set(re.findall(r'ParameterID\s*[\({]\s*prefix\s*\+\s*"([A-Za-z0-9]+)"', src))
        # Local float/bool lambda forms: f ("x", ...), b ("x", ...).
        # Require the comma so a generated id such as f ("thr" + n, ...) is
        # not misreported as a literal parameter named merely "thr".
        params |= set(re.findall(r'\b[fb]\s*\(\s*"([A-Za-z0-9]+)"\s*,', src))

        # Expand loop-generated band ids such as "thr" + n. The declaration's
        # kBands value is the authoritative range used by addParameters().
        band_count = re.search(r'static\s+constexpr\s+int\s+kBands\s*=\s*(\d+)', src)
        if band_count:
            generated = set(re.findall(
                r'\b[fb]\s*\(\s*"([A-Za-z0-9]+)"\s*\+\s*n\s*,', src))
            for base in generated:
                params.update(f"{base}{index}" for index in range(1, int(band_count.group(1)) + 1))
        params |= set(re.findall(r'prefix\s*\+\s*"([A-Za-z0-9]+)"\s*,\s*1\s*\)', src))
        mods[m.group(1)] = params
    return mods


def main():
    mods = module_parameters()
    problems, checked = [], 0

    for host in HOSTS:
        if not os.path.exists(host):
            continue
        src = open(host).read()
        rel = os.path.relpath(host, ROOT)

        for chain in re.findall(r'addChainPreset\s*\(\s*"[^"]+",\s*\{([^}]*)\}', src):
            for mid in re.findall(r'"([a-z]+)"', chain):
                checked += 1
                if mid not in mods:
                    problems.append(f"{rel}: chain names unknown module '{mid}'")

        for mid, pid in re.findall(r'\{"([a-z]+)\.([A-Za-z0-9]+)"', src):
            checked += 1
            if mid not in mods:
                problems.append(f"{rel}: unknown module '{mid}'")
            elif pid not in mods[mid]:
                problems.append(
                    f"{rel}: module '{mid}' has no parameter '{pid}' "
                    f"(has: {', '.join(sorted(mods[mid]))})"
                )

    print(f"rack presets: checked {checked} references across {len(mods)} modules")
    for p in problems:
        print("  BAD:", p)
    if problems:
        print(f"rack preset check FAILED: {len(problems)} bad reference(s)")
        return 1
    print("rack preset check ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
