"""Coverage report of the lead-vocal corpus: counts and minutes per split x sex x register x genre,
vibrato / note-length / ornament buckets, and the gaps against the required coverage list.
Reads metadata through corpus.metadata_all() (no audio paths; does not open the holdout).
Writes coverage.json and rewrites the block between the coverage markers in README.md.

usage: python3 validation/leadvocal/coverage.py
"""
import collections, json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from corpus import metadata_all, header, HERE

SPLITS = ['dev', 'validation', 'holdout-sealed']
KEPT = set(SPLITS)


def sex_of(it):
    if it.get('sex'):
        return it['sex']
    return f"{it['sex_inferred']}*" if it.get('sex_inferred') else '?'


def vib(it):
    return (it.get('vibrato') or {}).get('bucket', 'unknown')


def notelen(it):
    ns = it.get('note_stats')
    if not ns:
        return 'no notes'
    m = ns['median_len_s']
    return '<0.25 s' if m < 0.25 else ('0.25-0.5 s' if m <= 0.5 else '>0.5 s')


def orn(it):
    o = it.get('ornaments') or {}
    tags = []
    if o.get('scoop_frac') is not None and o['scoop_notes_tested'] >= 30 and o['scoop_frac'] >= 0.25: tags.append('scoops')
    if o.get('portamento_frac') is not None and o['transitions_tested'] >= 10 and o['portamento_frac'] >= 0.5: tags.append('portamento')
    return tags


def table(rows, keyf, title, splits=SPLITS):
    cells = collections.defaultdict(lambda: [0, 0.0])
    for it in rows:
        for k in (keyf(it) if isinstance(keyf(it), list) else [keyf(it)]):
            c = cells[(k, it['split'])]; c[0] += 1; c[1] += it['duration_s'] / 60
    keys = sorted({k for k, _ in cells}, key=str)
    out = [f'**{title}** (items / minutes)', '', '| ' + ' | '.join([''] + splits + ['total']) + ' |',
           '|' + '---|' * (len(splits) + 2)]
    for k in keys:
        tot = [sum(cells[(k, s)][0] for s in splits), sum(cells[(k, s)][1] for s in splits)]
        out.append('| ' + ' | '.join([str(k)] + [f'{cells[(k, s)][0]} / {cells[(k, s)][1]:.1f}' if cells[(k, s)][0] else '-'
                                                   for s in splits] + [f'{tot[0]} / {tot[1]:.1f}']) + ' |')
    return '\n'.join(out) + '\n', {str(k): {s: cells[(k, s)] for s in splits} for k in keys}


def main():
    allit = metadata_all(); h = header()
    kept = [i for i in allit if i['split'] in KEPT]
    prim = [i for i in kept if i['set'] == 'primary']
    md, js = [], {}

    # split x source
    t, j = table(kept, lambda i: f"{i['set']}:{i['source']}", 'Split x source (kept items; all sets)'); md.append(t); js['split_source'] = j
    ex = collections.Counter((i['source'], i['split']) for i in allit if i['split'] not in KEPT)
    md.append('Excluded (never used, so no singer or song crosses splits): ' +
              ', '.join(f'{s} {sp} {n}' for (s, sp), n in sorted(ex.items())) + '\n')
    sing = {sp: len({i['singer_id'] for i in prim if i['split'] == sp}) for sp in SPLITS}
    song = {sp: len({i['song_id'] for i in prim if i['split'] == sp}) for sp in SPLITS}
    md.append(f'Primary singers per split: {sing}; songs per split: {song}\n')
    js['singers'] = sing; js['songs'] = song

    t, j = table(prim, lambda i: f'{sex_of(i)} {i.get("register") or "?"}', 'Primary: sex x register (* = inferred sex, ? = unknown)'); md.append(t); js['sex_register'] = j
    t, j = table(prim, lambda i: f"{i['source']}: {i.get('genre') or 'unknown'}", 'Primary: genre (by source)'); md.append(t); js['genre'] = j
    t, j = table(prim, vib, 'Primary: vibrato bucket (extent peak-to-peak: narrow < 50 c <= medium < 100 c <= wide)'); md.append(t); js['vibrato'] = j
    t, j = table(prim, notelen, 'Primary: median note length'); md.append(t); js['note_length'] = j
    t, j = table(prim, lambda i: i.get('pace') or ['no notes'], 'Primary: pace tags (fast: median note < 0.25 s; slow: > 0.5 s; sustained: >= 25% of note time in notes >= 1 s)'); md.append(t); js['pace'] = j
    t, j = table(prim, lambda i: orn(i) or ['none / not measurable'], 'Primary: ornaments (scoops: >= 25% of notes start >= 80 c low; portamento: >= 50% of legato leaps >= 2 st glide >= 80 ms)'); md.append(t); js['ornaments'] = j
    t, j = table(prim, lambda i: 'label' if i['source'] == 'al' else ('ambiguous (margin < 0.05)' if i.get('key_ambiguous') else 'clear'),
                 'Primary: key truth quality'); md.append(t); js['key_quality'] = j
    t, j = table(prim, lambda i: f"{i['sample_rate']} Hz" + (' (lossy AAC)' if i.get('codec') else ''), 'Primary: sample rate'); md.append(t); js['sample_rate'] = j

    al = [i for i in prim if i['source'] == 'al']
    n20 = [i for i in allit if i['source'] == 'n20emv2']
    isf = [i['label_in_scale_frac'] for i in al if i.get('label_in_scale_frac') is not None]
    md.append(f"Apple Loops key labels (unverified): the f0 Temperley fit names the same key for "
              f"{sum(bool(i.get('label_agrees_with_f0_fit')) for i in al)}/{len(al)} primary items (short phrases underdetermine "
              f"a key, so this is weak evidence either way); voiced time inside the labelled scale: median "
              f"{sorted(isf)[len(isf) // 2]:.2f}, {sum(x < 0.8 for x in isf)} items below 0.80 (suspect label or strongly chromatic phrase).\n")
    grp = {i['arrangement_group']['id']: i['arrangement_group']['singers'] for i in n20}
    md.append(f"N20EM key truth: {len(grp)} song+arrangement groups ({sum(1 for v in grp.values() if v >= 2)} pooled over >= 2 singers, "
              f"covering {sum(1 for i in n20 if i['arrangement_group']['singers'] >= 2)}/{len(n20)} items); the single-performance fit "
              f"agrees with the pooled key for {sum(i['arrangement_group']['item_fit_agrees_with_pooled'] for i in n20)} items; "
              f"key_ambiguous {sum(bool(i['key_ambiguous']) for i in n20)}; modulation confirmed across singers: "
              f"{sum(bool(i.get('modulation_confirmed')) for i in n20)} items.\n")

    t, j = table(prim, lambda i: f"{sex_of(i)} {i.get('register') or '?'} | {i.get('genre') or 'unknown'}",
                 'Primary: sex x register x genre')
    md.append('<details><summary>split x sex x register x genre (full cross-table)</summary>\n\n' + t + '\n</details>\n'); js['sex_register_genre'] = j

    # gaps vs required list
    def cnt(pred, rows=prim):
        r = [i for i in rows if pred(i)]
        return {sp: sum(1 for i in r if i['split'] == sp) for sp in SPLITS}, sum(i['duration_s'] for i in r) / 60

    def status(pred, derivable=True, note=''):
        if not derivable:
            return ('unknown (not derivable from these sources)', None, 0.0, note)
        c, m = cnt(pred); n = sum(c.values())
        st = 'missing' if n == 0 else ('thin' if n < 5 or m < 10 or c['holdout-sealed'] == 0 else 'covered')
        return (st, c, round(m, 1), note)

    sx = lambda i: i.get('sex') or i.get('sex_inferred')
    desc = lambda d: (lambda i: d in (i.get('apple_descriptors') or []))
    G = lambda *g: (lambda i: (i.get('genre') or '') in g)
    req = {
        'male low': status(lambda i: sx(i) == 'M' and i.get('register') == 'low'),
        'male mid': status(lambda i: sx(i) == 'M' and i.get('register') == 'mid'),
        'male high': status(lambda i: sx(i) == 'M' and i.get('register') == 'high'),
        'female low': status(lambda i: sx(i) == 'F' and i.get('register') == 'low'),
        'female mid': status(lambda i: sx(i) == 'F' and i.get('register') == 'mid'),
        'female high': status(lambda i: sx(i) == 'F' and i.get('register') == 'high'),
        'breathy': status(None, False, 'no source labels breathiness; needs annotation or an HNR measure'),
        'bright': status(None, False, 'Apple has no "Bright" descriptor on these items'),
        'dark': status(desc('Dark'), True, 'Apple descriptor label only'),
        'powerful': status(desc('Intense'), True, 'proxy: Apple descriptor "Intense"'),
        'inexperienced': status(lambda i: i['source'] in ('n20emv2', 'vocadito'), True, 'dataset-level: N20EM untrained-to-amateur, vocadito mixed'),
        'sustained': status(lambda i: 'sustained' in (i.get('pace') or [])),
        'fast': status(lambda i: 'fast' in (i.get('pace') or [])),
        'slow': status(lambda i: 'slow' in (i.get('pace') or [])),
        'vibrato wide': status(lambda i: vib(i) == 'wide'),
        'vibrato narrow': status(lambda i: vib(i) == 'narrow'),
        'scoops': status(lambda i: 'scoops' in orn(i), True, 'heuristic from notes + f0'),
        'slides / portamento': status(lambda i: 'portamento' in orn(i), True, 'heuristic from notes + f0'),
        'hard / soft consonants': status(None, False, 'needs phonetic annotation'),
        'falsetto / chest / mixed': status(None, False, 'register mechanism is not labelled anywhere'),
        'genre pop': status(G('pop')), 'genre R&B': status(G('r&b', 'urban (apple label: r&b/hip-hop)')),
        'genre rock': status(G('rock')), 'genre electronic': status(G('electronic')), 'genre indie': status(G('indie')),
        'genre rap-sung': status(G('hip-hop'), True, 'proxy: melodic Apple Loops labelled Hip Hop; no rap-sung lead'),
        'genre jazz': status(G('jazz')), 'genre acoustic': status(desc('Acoustic'), True, 'proxy: Apple descriptor "Acoustic" (instrumentation tag, not genre)'),
        'genre musical theatre': status(G('musical-theatre')),
        'has bleed': status(lambda i: i.get('has_bleed')),
        'full-band 44.1/48 kHz whole songs': status(lambda i: not i.get('sr_limited') and i['duration_s'] >= 120),
    }
    md.append('**Required coverage vs what the corpus holds** (primary set; counts dev / validation / holdout-sealed; '
              'thin = < 5 items, < 10 min, or none sealed)\n')
    md.append('| requirement | status | dev / val / holdout | minutes | note |\n|---|---|---|---|---|')
    for k, (st, c, m, note) in req.items():
        md.append(f"| {k} | {st} | {'/'.join(str(c[s]) for s in SPLITS) if c else '-'} | {m if c else '-'} | {note} |")
    js['requirements'] = {k: {'status': st, 'counts': c, 'minutes': m, 'note': note} for k, (st, c, m, note) in req.items()}
    js['built'] = h['built']
    text = '\n'.join(md) + '\n'
    json.dump(js, open(HERE / 'coverage.json', 'w'), indent=1)
    readme = HERE / 'README.md'
    if readme.exists():
        r = readme.read_text()
        r = re.sub(r'(<!-- coverage:start -->\n).*?(<!-- coverage:end -->)', lambda m: m.group(1) + text + m.group(2), r, flags=re.S)
        readme.write_text(r)
    print(text)


if __name__ == '__main__':
    main()
