import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from causal_ceiling import hyst_targets
keys=json.load(open('csd_keys.json')); N=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
MAJ=[0,2,4,5,7,9,11]; MIN=[0,2,3,5,7,8,10]
def snap(v, root, sc, thr):
    n=int(round(v))
    if (n-root)%12 in sc: return n
    if thr>0 and abs(v-n)*100<=thr: return n            # clearly on an accidental: accept it
    cands=[n-1,n+1]; cands=[c for c in cands if (c-root)%12 in sc]
    return min(cands,key=lambda c:abs(v-c)) if cands else n
def targets(m, root, sc, thr, margin=0.5, hold=0.12, hop=0.01):
    out=np.full(len(m),np.nan); held=-1; away=0.0; silent=0.0
    for i,v in enumerate(m):
        if not np.isfinite(v):
            silent+=hop
            if silent>0.15: held=-1
            continue
        silent=0.0; near=snap(v,root,sc,thr)
        if held<0 or near==held: held=near; away=0.0
        else:
            away+=hop
            if abs(v-held)-abs(v-near)>margin or away>=hold: held=near; away=0.0
        out[i]=held
    return out
split=sys.argv[1] if len(sys.argv)>1 else 'dev'
for thr in [0,15,20,25,30]:
    r={'in':[0,0],'out':[0,0]}
    for stem in tracks(split):
        k=keys[stem.split('_')[1]]; root=N.index(k['root']); sc=MAJ if k['scale']=='major' else MIN
        mi=crepe_cached(WORK/f'{stem}.wav'); ti=np.arange(len(mi))*0.01; ts=targets(mi,root,sc,thr)
        for s,e,tg in section_notes(stem):
            ci=note_centre(ti,mi,s,e)
            if not np.isfinite(ci) or abs(100*(ci-tg))>48: continue
            c=note_centre(ti,ts.astype(float),s,e)
            if np.isfinite(c):
                g='in' if (tg-root)%12 in sc else 'out'; r[g][0]+=1; r[g][1]+=abs(100*(c-tg))<=10
    tot=r['in'][0]+r['out'][0]; ok=r['in'][1]+r['out'][1]
    print(f"accidental threshold {thr:2d}c: all {100*ok/tot:5.1f}% | in-key {100*r['in'][1]/r['in'][0]:5.1f}% | out-of-key {100*r['out'][1]/r['out'][0]:5.1f}%")
