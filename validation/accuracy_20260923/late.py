import json, numpy as np
from synth_pitch import OUT, SR
tag='hyst_cm'; man=json.load(open(OUT/'manifest.json')); rows=[]
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); fr=tr['frames']; B=tr['blockSize']
    tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for i,n in enumerate(m['notes']):
        t0=n['start']/SR; t1=n['end']/SR; off=n['offset_c']
        idx=np.where((tc>=t0)&(tc<=t1))[0]
        rq=np.array([fr[j]['correctionCents'] if fr[j]['voiced'] else np.nan for j in idx]); tg=[fr[j]['detectedMidi'] for j in idx]
        ok=np.abs(rq+off)<25; rt=None
        for j in range(len(ok)-2):
            if ok[j] and ok[j+1] and ok[j+2]: rt=tc[idx[j]]-t0; break
        prev=m['notes'][i-1] if i else None
        legato= prev is not None and prev['end']==n['start']
        step= (n['target']+n['offset_c']/100) - (prev['target']+prev['offset_c']/100) if prev else None
        rows.append((rt, legato, step, off, m['voice'], [x for x in rq[:14]]))
late=[r for r in rows if r[0] is None or r[0]>0.1]
print('late notes', len(late), 'of', len(rows), '| legato among late', sum(r[1] for r in late), '| legato overall', sum(r[1] for r in rows))
import collections
print('late by |step| semitones:', collections.Counter(round(abs(r[2])) if r[2] is not None else 'first' for r in late))
print('all by |step|:', collections.Counter(round(abs(r[2])) if r[2] is not None else 'first' for r in rows))
for r in late[:4]: print(' ', r[4], 'legato' if r[1] else 'rest', 'step', None if r[2] is None else round(r[2],2), 'off', r[3], 'req first frames', [None if not np.isfinite(x) else round(x) for x in r[5]])
