#!/usr/bin/env python3
"""Lightweight Sattari Audio suite gate inventory.

This intentionally checks structure, not product quality. Gate movement still requires
machine validation, export artifacts, and human listening/visual reports.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN_ROOT = ROOT / "plugins"
REQUIRED = [
    ROOT / "core" / "include" / "sattari_audio_core" / "SattariAudioCore.h",
    ROOT / "validation" / "templates" / "listening-test-report.md",
    ROOT / "validation" / "templates" / "product-validation-report.md",
    ROOT / "validation" / "schemas" / "render_manifest.schema.json",
    ROOT / "validation" / "schemas" / "auto_pitch_corpus_manifest.schema.json",
]


def main() -> int:
    missing = [str(p.relative_to(ROOT)) for p in REQUIRED if not p.exists()]
    plugin_dirs = sorted(p for p in PLUGIN_ROOT.iterdir() if p.is_dir())
    root_cmake = (ROOT / "CMakeLists.txt").read_text()

    missing_cmake = [
        str((plugin / "CMakeLists.txt").relative_to(ROOT))
        for plugin in plugin_dirs
        if not (plugin / "CMakeLists.txt").is_file()
    ]
    unregistered = [
        plugin.name
        for plugin in plugin_dirs
        if f"add_subdirectory(plugins/{plugin.name})" not in root_cmake
    ]

    codes: dict[str, list[str]] = {}
    missing_code: list[str] = []
    invalid_code: dict[str, str] = {}
    code_pattern = re.compile(r"\bPLUGIN_CODE\s+(\S+)")
    set_pattern = re.compile(r"\bset\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z0-9]{4})\s*\)")
    for plugin in plugin_dirs:
        cmake_file = plugin / "CMakeLists.txt"
        if not cmake_file.is_file():
            continue
        cmake_text = cmake_file.read_text()
        variables: dict[str, list[str]] = {}
        for var, value in set_pattern.findall(cmake_text):
            variables.setdefault(var, []).append(value)

        plugin_codes: list[str] = []
        for token in code_pattern.findall(cmake_text):
            var_match = re.fullmatch(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", token)
            if var_match is not None:
                plugin_codes.extend(variables.get(var_match.group(1), []))
            else:
                plugin_codes.append(token)

        if not plugin_codes:
            missing_code.append(plugin.name)
            continue
        for code in plugin_codes:
            if re.fullmatch(r"[A-Za-z0-9]{4}", code) is None:
                invalid_code[plugin.name] = code
            codes.setdefault(code, []).append(plugin.name)

    duplicate_codes = {
        code: slugs for code, slugs in sorted(codes.items()) if len(slugs) > 1
    }
    failures = bool(
        missing
        or missing_cmake
        or unregistered
        or missing_code
        or invalid_code
        or duplicate_codes
    )

    report = {
        "suite": "sattari-audio",
        "truth_label": "suite structure check only; not sale-ready evidence",
        "required_files_checked": [str(p.relative_to(ROOT)) for p in REQUIRED],
        "missing": missing,
        "plugins": {
            "count": len(plugin_dirs),
            "slugs": [plugin.name for plugin in plugin_dirs],
            "missing_cmake": missing_cmake,
            "unregistered_in_root": unregistered,
            "missing_plugin_code": missing_code,
            "invalid_plugin_codes": invalid_code,
            "duplicate_plugin_codes": duplicate_codes,
        },
        "pass": not failures,
    }
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
