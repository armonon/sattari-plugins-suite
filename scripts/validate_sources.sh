#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if command -v python3 >/dev/null 2>&1; then
  python3 - <<'PY'
from pathlib import Path
required = [
    'plugins/sub-conjurer/CMakeLists.txt',
    'plugins/stemdeck/CMakeLists.txt',
    'plugins/stemdeck/src/PluginHostManager.cpp',
    'plugins/stemdeck/src/PluginHostManager.h',
    'plugins/stemdeck/docs/stemdeck-sale-readiness-gates.md',
    'scripts/stemdeck_manual_validation_template.sh',
    'scripts/validate_stemdeck_app_bundle.sh',
    'plugins/side-chain-master/CMakeLists.txt',
    'plugins/royal-chain/CMakeLists.txt',
]
for p in required:
    if not Path(p).exists():
        raise SystemExit(f'missing {p}')

readme = Path('plugins/stemdeck/README.md').read_text()
if 'not sale-ready' not in readme.lower():
    raise SystemExit('StemDeck README must keep explicit not-sale-ready truth label')
if 'PluginHostManager' not in readme:
    raise SystemExit('StemDeck README must mention PluginHostManager scaffold')
if 'validate_stemdeck_app_bundle.sh' not in Path('scripts/package_stemdeck_app_alpha.sh').read_text():
    raise SystemExit('StemDeck app package script must include reusable app-bundle validation')
ui_source = Path('plugins/stemdeck/src/MainPluginEditor.cpp').read_text()
for needle in [
    'STANDALONE APP ALPHA QUICK START',
    'Load or drag a full mix',
    'Record WAV to capture the performed arrangement',
    'Not notarized or sale-ready',
]:
    if needle not in ui_source:
        raise SystemExit(f'StemDeck standalone onboarding rail missing {needle!r}')
print('source layout ok')
PY
fi

if command -v git >/dev/null 2>&1; then
  git diff --check
fi
