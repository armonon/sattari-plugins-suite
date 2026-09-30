import json, sys, collections, numpy as np
# usage: PYTHONPATH=. python ak_decomp.py <tag> <dev|holdout> [base=r0]  (per-note accuracy by the key AutoKey actually used)
from csd_bench import tracks, section_notes, note_centre, crepe_cached, WORK
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
keys=json.load(open('csd_keys.json'))
tag=sys.argv[1]; split=sys.argv[2]; base=sys.argv[3] if len(sys.argv)>3 else 'r0'
st=collections.defaultdict(lambda:[0,0,0])  # n, ok_tag, ok_base
for stem in tracks(split):
    piece=stem.split('_')[1]; k=keys[piece]; kr=NAMES.index(k['root']); ks=0 if k['scale']=='major' else 1
    tr=json.load(open(WORK/f'{stem}_natural_{tag}.json')); fr=tr['frames']; lat=tr['reportedLatencySamples']
    ft=np.array([f['timeSec'] for f in fr])
    mo=crepe_cached(WORK/f'{stem}_natural_{tag}.wav',lat); mb=crepe_cached(WORK/f'{stem}_natural_{base}.wav',lat)
    mi=crepe_cached(WORK/f'{stem}.wav')
    t=np.arange(len(mo))*0.01; tb=np.arange(len(mb))*0.01; ti=np.arange(len(mi))*0.01
    for s,e,tgt in section_notes(stem):
        ci=note_centre(ti,mi,s,e); co=note_centre(t,mo,s,e); cb=note_centre(tb,mb,s,e)
        if not all(np.isfinite([ci,co,cb])) or abs(100*(ci-tgt))>48: continue
        sel=[fr[i] for i in np.where((ft>=s+0.08)&(ft<=e-0.05))[0]]
        if not sel: continue
        c=collections.Counter((f['usedRoot'],f['usedScale']) for f in sel).most_common(1)[0][0]
        cls='chromatic' if c[1]==2 else ('right' if c==(kr,ks) else ('rel/par' if c[1]!=ks or c[0]!=kr else 'wrong'))
        if c[1]!=2 and c!=(kr,ks): cls='wrong'
        x=st[cls]; x[0]+=1; x[1]+= abs(100*(co-tgt))<=10; x[2]+= abs(100*(cb-tgt))<=10
tot=[sum(v[i] for v in st.values()) for i in range(3)]
for k,v in sorted(st.items()): print(f'{k:10s} notes {v[0]:5d} ({100*v[0]/tot[0]:4.1f}%)  {tag} {100*v[1]/v[0]:5.1f}%  {base} {100*v[2]/v[0]:5.1f}%')
print(f'{"ALL":10s} notes {tot[0]:5d}          {tag} {100*tot[1]/tot[0]:5.1f}%  {base} {100*tot[2]/tot[0]:5.1f}%')
