import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from key_ceiling import snap, targets, keys, N, MAJ, MIN
def ema_key(m, root, sc, tau, hop=0.01):
    out=np.full(len(m),np.nan); e=None; away=0.0; held=-1; silent=1.0
    for i,v in enumerate(m):
        if not np.isfinite(v): silent+=hop; continue
        if e is None or silent>0.04: e=v; held=-1
        silent=0.0
        if abs(v-e)>0.75:
            away+=hop
            if away>=0.04: e=v; away=0.0; held=-1
            else: out[i]=snap(v,root,sc,0); continue
        else: away=0.0; e+=(1-np.exp(-hop/tau))*(v-e)
        c=snap(e,root,sc,0)
        if held<0 or (c!=held and abs(e-held)-abs(e-c)>0.2): held=c
        out[i]=held
    return out
split=sys.argv[1] if len(sys.argv)>1 else 'dev'
for name,f in [('key + production hold',lambda m,r,s: targets(m,r,s,0)),('key + 100 ms average',lambda m,r,s: ema_key(m,r,s,0.10)),('key + 50 ms average',lambda m,r,s: ema_key(m,r,s,0.05))]:
    ok=tot=0
    for stem in tracks(split):
        k=keys[stem.split('_')[1]]; root=N.index(k['root']); sc=MAJ if k['scale']=='major' else MIN
        mi=crepe_cached(WORK/f'{stem}.wav'); ti=np.arange(len(mi))*0.01; ts=f(mi,root,sc)
        for s,e,tg in section_notes(stem):
            ci=note_centre(ti,mi,s,e)
            if not np.isfinite(ci) or abs(100*(ci-tg))>48: continue
            c=note_centre(ti,ts.astype(float),s,e)
            if np.isfinite(c): tot+=1; ok+=abs(100*(c-tg))<=10
    print(f'{name:24} ceiling {100*ok/tot:5.1f}%')
