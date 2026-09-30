"""Note score v2: median over the note's STEADY frames only (local 50 ms pitch std < 8 c),
so natural scoops/glides inside a note don't count against the tuner. Needs >= 5 steady frames."""
import json, sys, numpy as np
from pathlib import Path
from pitchref import load, f0
def steady_median(m, a, b):
    seg = m[a+8:b-5]
    if len(seg) < 5: return None
    ok = []
    for i in range(len(seg)):
        w = seg[max(0,i-2):i+3]; w = w[np.isfinite(w)]
        if np.isfinite(seg[i]) and len(w) >= 4 and np.std(w)*100 < 8: ok.append(seg[i])
    return float(np.median(ok)) if len(ok) >= 5 else None
def score(get_out, subset='tune', tol=10.0):
    REF=json.load(open('voices/reference_notes.json')); tot=ok=0; errs=[]
    for vid,r in sorted(REF.items()):
        k=int(vid[1:3])
        if (subset=='tune' and k%2==0) or (subset=='holdout' and k%2==1): continue
        mi=np.load(f'work/{vid}_detuned.wav.crepe.npy'); mo=get_out(vid)
        for (a,b,t,med) in r['notes']:
            si=mi[a+8:b-5]; si=si[np.isfinite(si)]
            if len(si)<3 or abs(100*(np.median(si)-t))>45: continue
            s=steady_median(mo,a,b)
            if s is None: continue
            e=100*(s-t); tot+=1; ok+=abs(e)<=tol; errs.append(e)
    return ok, tot, errs
def cached(path, lat=0):
    c=Path(str(path)+'.crepe.npy')
    if c.exists(): return np.load(c)
    m,_=f0(load(path)[lat:]); np.save(c,m); return m
if __name__=='__main__':
    subset=sys.argv[1]
    for tag in sys.argv[2:]:
        if tag=='oracle': g=lambda v: cached(f'work/{v}_oracle.wav')
        elif tag=='original': g=lambda v: cached(f'voices/{v}.wav')
        else:
            def g(v, tag=tag):
                lat=json.load(open(f'work/{v}_{tag}.json'))['reportedLatencySamples']; return cached(f'work/{v}_{tag}.wav', lat)
        ok,tot,e=score(g, subset); a=np.abs(e)
        print(f'{tag:24} {ok}/{tot} = {100*ok/tot:5.1f}% within 10c | within 5c {100*np.mean(a<=5):5.1f}% | median {np.median(a):4.1f}c | p95 {np.percentile(a,95):5.1f}c')
