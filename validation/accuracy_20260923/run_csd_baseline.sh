#!/bin/bash
cd "$(dirname "$0")"
export PYTHONWARNINGS=ignore
PY=~/.venvs/sattari-audio/bin/python
for mode in natural detuned; do
  $PY csd_bench.py score base $mode tune --style Classic 2>/dev/null | grep SUMMARY
  $PY csd_bench.py oracle $mode tune 2>/dev/null | grep SUMMARY
done
echo DONE
