"""Analytic target-choice score (key mode, production hold) for detector variants built from the
production detector's own readings (trace), causal only. Output assumed == chosen target."""
import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from key_ceiling import targets, keys, N, MAJ, MIN
from det_diag import det_series
split=sys.argv[1] if len(sys.argv)>1 else 'dev'
def causal_median(x, k):
    out=np.full(len(x),np.nan)
    for i in range(len(x)):
        w=x[max(0,i-k+1):i+1]; w=w[np.isfinite(w)]
        if np.isfinite(x[i]) and len(w): out[i]=np.median(w)
    return out
def causal_ema(x, a):
    out=np.full(len(x),np.nan); e=None
    for i,v in enumerate(x):
        if not np.isfinite(v): e=None; continue
        e=v if e is None or abs(v-e)>0.75 else e+a*(v-e); out[i]=e
    return out
def outlier_reject(x, thr=0.35, k=5):
    """replace a reading that jumps > thr semitones from the causal median of the last k readings (unless it persists)"""
    out=x.copy(); run=0
    for i in range(k,len(x)):
        w=x[i-k:i]; w=w[np.isfinite(w)]
        if np.isfinite(x[i]) and len(w)>=3:
            med=np.median(w)
            if abs(x[i]-med)>thr and run<3: out[i]=med; run+=1
            else: run=0
    return out
variants={'D0 production detector':lambda d:d,
          'D1 causal median 3 blocks (~32 ms)':lambda d:causal_median(d,3),
          'D2 causal median 5 blocks (~53 ms)':lambda d:causal_median(d,5),
          'D3 causal EMA a=0.5':lambda d:causal_ema(d,0.5),
          'D4 causal EMA a=0.3':lambda d:causal_ema(d,0.3),
          'D5 outlier rejection (>35 c jumps)':lambda d:outlier_reject(d),
          'reference (perfect detector)':None}
res={k:[0,0] for k in variants}
for stem in tracks(split):
    k=keys[stem.split('_')[1]]; root=N.index(k['root']); sc=MAJ if k['scale']=='major' else MIN
    ref=crepe_cached(WORK/f'{stem}.wav'); tr_=np.arange(len(ref))*0.01
    tc,det=det_series(stem)
    # production reads once per 512-sample block (~10.7 ms); keep that cadence, then sample to 10 ms grid
    for name,f in variants.items():
        if f is None: series=ref
        else:
            dd=f(det); idx=np.clip(np.searchsorted(tc,tr_)-1,0,len(tc)-1); series=dd[idx]
        ts=targets(series,root,sc,0).astype(float)
        for s,e,tg in section_notes(stem):
            ci=note_centre(tr_,ref,s,e)
            if not np.isfinite(ci) or abs(100*(ci-tg))>48: continue
            c=note_centre(tr_,ts,s,e)
            if np.isfinite(c): res[name][0]+=1; res[name][1]+=abs(100*(c-tg))<=10
for name,(n,ok) in res.items(): print(f'{name:38} {100*ok/n:5.1f}%  (n={n})')
