"""Loader for the lead-vocal corpus (validation/leadvocal/manifest.json). All dev tooling reads the
corpus through items(); nothing opens manifest.json directly.

Sealing: the holdout-sealed split is for milestone validation only and is never tuned on.
items('holdout-sealed') raises unless allow_holdout=True AND the environment variable
LEADVOCAL_MILESTONE holds a non-empty reason. Every opening appends
(UTC timestamp, reason, caller) to validation/leadvocal/holdout-access.log, which is committed.

    from corpus import items
    dev = items('dev')                                  # primary + secondary dev items
    val = items('validation', source='n20emv2')
    LEADVOCAL_MILESTONE="KE2 milestone 1" python my_eval.py   # with items('holdout-sealed', allow_holdout=True)
"""
import datetime, getpass, inspect, json, os, socket, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'manifest.json'
ACCESS_LOG = HERE / 'holdout-access.log'
SPLITS = ('dev', 'validation', 'holdout-sealed', 'excluded-crossover', 'excluded-prior-tuning')
AL_WORK = Path(os.environ.get('LEADVOCAL_AL_WORK', HERE.parent / 'accuracy_20260923/al_work'))


class HoldoutSealed(PermissionError):
    pass


def _caller():
    for fr in inspect.stack()[2:]:
        if Path(fr.filename).resolve() != Path(__file__).resolve():
            return f'{fr.filename}:{fr.lineno}:{fr.function} argv={" ".join(sys.argv)}'
    return f'argv={" ".join(sys.argv)}'


def _log_open(reason, n):
    line = '\t'.join([datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
                      reason.replace('\t', ' ').replace('\n', ' '), _caller(),
                      f'{getpass.getuser()}@{socket.gethostname()}', f'items={n}'])
    with open(ACCESS_LOG, 'a') as f:
        f.write(line + '\n')


def header():
    return json.load(open(MANIFEST))['header']


def _resolve(it):
    it = dict(it)
    p = it.get('path')
    if p:
        p = p.replace('$AL_WORK', str(AL_WORK))
        it['path'] = str(Path(os.path.expanduser(p)))
    if it.get('accomp_path'):
        it['accomp_path'] = os.path.expanduser(it['accomp_path'])
    return it


def items(split, allow_holdout=False, source=None, set=None):
    """Items of one split. split: dev | validation | holdout-sealed (alias: holdout) |
    excluded-crossover | excluded-prior-tuning. source: n20emv2 | vocadito | al | csd (optional).
    set: primary | secondary (optional). Paths are returned expanded."""
    if split == 'holdout':
        split = 'holdout-sealed'
    if split not in SPLITS:
        raise ValueError(f'unknown split {split!r}; one of {SPLITS}')
    if split == 'holdout-sealed':
        reason = os.environ.get('LEADVOCAL_MILESTONE', '').strip()
        if not allow_holdout or not reason:
            raise HoldoutSealed('holdout-sealed is sealed: pass allow_holdout=True AND set LEADVOCAL_MILESTONE to the '
                                'milestone reason. Every opening is logged to validation/leadvocal/holdout-access.log.')
    out = [_resolve(it) for it in json.load(open(MANIFEST))['items']
           if it['split'] == split and (source is None or it['source'] == source) and (set is None or it['set'] == set)]
    if split == 'holdout-sealed':
        _log_open(reason, len(out))
    return out


PATH_FIELDS = ('path', 'accomp_path', 'source_paths')


def metadata_all():
    """Every item of every split with its audio path fields removed: for coverage accounting
    and licence audits only (counts, minutes, tags). It cannot be used to render or score
    audio, so it does not open the holdout and is not logged."""
    return [{k: v for k, v in it.items() if k not in PATH_FIELDS} for it in json.load(open(MANIFEST))['items']]


if __name__ == '__main__':
    import collections
    h = header()
    print(h['name'], '| built', h['built'])
    for s in ('dev', 'validation'):
        its = items(s)
        print(f'{s:11s} {len(its):4d} items {sum(i["duration_s"] for i in its) / 60:7.1f} min',
              dict(collections.Counter(i['source'] for i in its)))
    print('holdout-sealed: sealed (counts in manifest header only)')
