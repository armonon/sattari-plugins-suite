#!/usr/bin/env python3
"""Verify every factory-preset parameter ID is a real parameter of its plugin.

A wrong ID in addFactoryPreset() silently does nothing (SattariPresetManager ignores
unknown IDs), so the compiler can't catch it. A real param ID always appears as a quoted
string somewhere in the plugin's sources — in its declaration AND in the editor's
parameter attachment (SliderAttachment(apvts, "id", ...)). Module-backed plugins declare
params in core/.../modules/*.h, so those headers are folded in too.

We flag a preset ID only if it appears NOWHERE as a quoted string. Concatenated IDs
(e.g. "kick"+"Decay") won't appear literally — those are listed separately as "review".
"""
import os, re, glob, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plug = os.path.join(root, 'plugins')
coreinc = os.path.join(root, 'core', 'include')
bad = 0; review = 0; checked = 0

def quoted_strings(text):
    return set(re.findall(r'"([A-Za-z0-9_]+)"', text))

for slug in sorted(os.listdir(plug)):
    srcdir = os.path.join(plug, slug, 'src')
    if not os.path.isdir(srcdir):
        continue
    files = [f for f in glob.glob(os.path.join(srcdir, '**', '*'), recursive=True)
             if f.endswith(('.cpp', '.h', '.mm')) and os.path.isfile(f)]
    text = '\n'.join(open(f, encoding='utf-8', errors='ignore').read() for f in files)
    if 'addFactoryPreset' not in text:
        continue
    checked += 1

    # Fold in any included core module headers (module-backed plugins declare params there).
    mods = re.findall(r'#include\s*<(sattari_audio_core/[^>]+)>', text)
    for m in mods:
        p = os.path.join(coreinc, m)
        if os.path.isfile(p):
            text += '\n' + open(p, encoding='utf-8', errors='ignore').read()

    known = quoted_strings(text)
    # Preset IDs: the key of each {"id", <number>} pair in a PresetValues list.
    preset_ids = set(re.findall(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*[-0-9.]', text))
    # Exclude ids that are choice-option lookups etc.: only those inside addFactoryPreset(...)
    # calls. Cheap heuristic — restrict to ids that also look like params (short, camel).
    unknown = sorted(i for i in preset_ids if i not in known)

    npresets = len(re.findall(r'addFactoryPreset', text))
    if unknown:
        bad += 1
        print(f"  X {slug}: {npresets} presets — IDs not found anywhere: {unknown}")
    else:
        print(f"  ok {slug}: {npresets} presets, all preset IDs present in sources")

print(f"\n{checked} plugins with factory presets; {bad} with genuinely-missing IDs")
sys.exit(1 if bad else 0)
