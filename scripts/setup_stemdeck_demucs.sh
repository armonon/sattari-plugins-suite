#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-/opt/homebrew/bin/python3.12}"
VENV_DIR="$REPO_DIR/.venv/stemdeck-demucs"
CONFIG_DIR="$HOME/Library/Application Support/Sattari/StemDeck/StemProvider"
CONFIG_FILE="$CONFIG_DIR/separator-command.txt"

if [[ ! -x "$PYTHON_BIN" ]]; then
  echo "Python not found at $PYTHON_BIN. Install Homebrew python@3.12 or set PYTHON_BIN." >&2
  exit 2
fi

# Homebrew Python's pyexpat can resolve against the system expat on some macOS
# setups. Prefer Homebrew expat when present so venv/pip/Demucs work reliably.
if [[ -d /opt/homebrew/opt/expat/lib ]]; then
  export DYLD_LIBRARY_PATH="/opt/homebrew/opt/expat/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi

"$PYTHON_BIN" -m venv "$VENV_DIR"
"$VENV_DIR/bin/python" -m pip install --upgrade pip wheel
"$VENV_DIR/bin/python" -m pip install 'setuptools<82' demucs torchcodec

mkdir -p "$CONFIG_DIR"
printf '%s\n' "$REPO_DIR/scripts/stemdeck_separate_demucs_venv.sh" > "$CONFIG_FILE"

"$VENV_DIR/bin/python" - <<'PY'
import demucs, torch, torchaudio, torchcodec
print('StemDeck Demucs environment ready')
print('demucs', demucs.__version__)
print('torch', torch.__version__)
print('torchaudio', torchaudio.__version__)
PY

echo "Wrote separator command: $CONFIG_FILE"
