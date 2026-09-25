"""Free-space preflight for the benchmarks. Renders are ~26 MB each and a CSD tag writes 48 of them;
on 2026-09-25 csd_work reached 36 GB and the disk fell to 568 MB free with Logic running.
Every benchmark calls require_free() before rendering and aborts cleanly below the floor.
BENCH_MIN_FREE_GB overrides the default floor (8 GB)."""
import os, shutil, sys

def require_free(gb=None, path='/'):
    floor = float(os.environ.get('BENCH_MIN_FREE_GB', gb if gb is not None else 8))
    free = shutil.disk_usage(path).free / 2**30
    if free < floor:
        sys.exit(f'ABORT: {free:.1f} GB free on {path} < {floor:.0f} GB floor (BENCH_MIN_FREE_GB). Prune renders first.')
    return free
