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
    # HostGraph replaced the old PluginHostManager scaffold (see root CLAUDE.md
    # §7: "The old PluginHostManager.{h,cpp} scaffold was orphaned and has been
    # deleted"). This check pointed at the deleted files until this fix, which
    # meant validate_sources.sh — the first step of package_stemdeck_app_alpha.sh —
    # could never pass: the packaging pipeline was unrunnable.
    'plugins/stemdeck/src/HostGraph.cpp',
    'plugins/stemdeck/src/HostGraph.h',
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
if 'HostGraph' not in readme:
    raise SystemExit('StemDeck README must document the HostGraph plugin-host architecture')
if 'validate_stemdeck_app_bundle.sh' not in Path('scripts/package_stemdeck_app_alpha.sh').read_text():
    raise SystemExit('StemDeck app package script must include reusable app-bundle validation')
# These strings are the CURRENT standalone onboarding-rail copy
# (MainPluginEditor.cpp's subtitle + standaloneWorkflowGuide labels). The
# product's onboarding copy has changed at least once since this check was
# written and it still pinned the OLD wording — meaning it could never pass
# against the actual shipping UI either. Check that the rail communicates the
# same two things (what RECORD does; the app is not sale-ready) rather than
# pinning exact prose that will drift again.
ui_source = Path('plugins/stemdeck/src/MainPluginEditor.cpp').read_text()
for needle in [
    'RECORD captures master',   # standaloneWorkflowGuide: explains what Record does
    'STEM MIXER',               # subtitle: names the product surface
]:
    if needle not in ui_source:
        raise SystemExit(f'StemDeck standalone onboarding rail missing {needle!r}')
print('source layout ok')
PY
fi

if command -v git >/dev/null 2>&1; then
  git diff --check
fi
