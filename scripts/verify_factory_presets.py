#!/usr/bin/env python3
"""Check independent literal evidence for inline factory-preset IDs.

This is a source heuristic, not a runtime parameter-registry proof. Preset calls
and numeric key/value entries cannot provide their own supporting evidence.
Generated parameter IDs and indirect preset arguments require review.
"""
import argparse
import json
from pathlib import Path
import re

PAIR = re.compile(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*[-+0-9.]')
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')


def without_comments(text):
    return TOKEN.sub(lambda m: ' ' * len(m[0]) if m[0].startswith(('/',)) else m[0], text)


def preset_calls(text):
    """Balanced parentheses, ignoring quoted string/character punctuation."""
    masked = TOKEN.sub(lambda m: ' ' * len(m[0]), text)
    calls = []
    for match in re.finditer(r'(?:\.|->)\s*addFactoryPreset\s*\(', masked):
        depth = 1
        end = match.end()
        while end < len(masked) and depth:
            depth += (masked[end] == '(') - (masked[end] == ')')
            end += 1
        if depth == 0:
            calls.append((match.start(), end, text[match.end():end - 1]))
    return calls


def analyze(text):
    text = without_comments(text)
    calls = preset_calls(text)
    independent = list(text)
    ids = set()
    indirect = 0
    for start, end, body in calls:
        keys = set(PAIR.findall(body))
        ids.update(keys)
        # Nonliteral keys or values may evade the deliberately narrow matcher.
        if not keys or re.search(r'\{\s*[^\s{"}][^,{}]*,', body):
            indirect += 1
        independent[start:end] = ' ' * (end - start)
    independent = ''.join(independent)
    # A named PresetValues list must not count as independent ID evidence either.
    independent = PAIR.sub(
        lambda m: m[0] if re.search(r'ParameterID\s*$', independent[max(0, m.start() - 40):m.start()]) else '{ ',
        independent)
    known = set(re.findall(r'"([A-Za-z0-9_]+)"', independent))
    generated = bool(re.search(r'\bParameterID\s*[({]\s*[^"\s]|\b(?:prefix|pfx)\s*\+', independent))
    missing = sorted(ids - known)
    status = 'review' if generated or indirect else 'missing-literal-evidence' if missing else 'literal-evidence'
    if not calls:
        status = 'not-applicable'
    return {'status': status, 'preset_calls': len(calls), 'preset_ids': sorted(ids),
            'missing_literal_ids': missing, 'generated_parameter_ids': generated,
            'indirect_or_unparsed_calls': indirect,
            'limit': 'Independent quoted-string evidence does not establish that an ID is registered at runtime.'}


def inspect(root):
    results = []
    for plugin in sorted((root / 'plugins').iterdir()):
        src = plugin / 'src'
        if not src.is_dir():
            continue
        files = sorted(p for p in src.rglob('*') if p.suffix in ('.cpp', '.h', '.mm') and p.is_file())
        text = '\n'.join(p.read_text(encoding='utf-8', errors='replace') for p in files)
        for module in sorted(set(re.findall(r'#include\s*<(sattari_audio_core/[^>]+)>', text))):
            header = root / 'core/include' / module
            if header.is_file():
                text += '\n' + header.read_text(encoding='utf-8', errors='replace')
        results.append({'plugin': plugin.name, **analyze(text)})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    results = inspect(args.root)
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        for row in results:
            print(f"{row['status']} {row['plugin']}: {row['preset_calls']} calls; unresolved IDs {row['missing_literal_ids']}")
    return 1 if any(r['status'] == 'missing-literal-evidence' for r in results) else 2 if any(r['status'] == 'review' for r in results) else 0


if __name__ == '__main__':
    raise SystemExit(main())
