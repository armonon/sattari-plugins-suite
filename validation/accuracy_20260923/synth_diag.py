import json, sys, numpy as np
from synth_pitch import OUT, core_centres, SR
tag=sys.argv[1]; man=json.load(open(OUT/'manifest.json')); rows=[]
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); lat=tr['reportedLatencySamples']
    mo=np.load(OUT/f"{m['name']}_{tag}.wav.crepe.npy")
    cs=core_centres(mo,m['notes'])
    for i,(n,c) in enumerate(zip(m['notes'],cs)):
        if not np.isfinite(c): continue
        legato = i>0 and m['notes'][i-1]['end']==n['start']
        rows.append(((n['end']-n['start'])/SR, abs(n['offset_c']), 100*(c-n['target']), n['offset_c'], legato, m['voice']))
R=np.array([(r[0],r[1],r[2],r[3],r[4]) for r in rows],dtype=float)
fail=np.abs(R[:,2])>10
print('fail',int(fail.sum()),'of',len(R))
for lo,hi in [(0,0.5),(0.5,0.7),(0.7,1.0)]:
    s=(R[:,0]>=lo)&(R[:,0]<hi); print(f'  dur {lo}-{hi}s: fail {100*fail[s].mean():.0f}% (n={s.sum()})')
for lo,hi in [(20,30),(30,38),(38,46)]:
    s=(R[:,1]>=lo)&(R[:,1]<hi); print(f'  |offset| {lo}-{hi}c: fail {100*fail[s].mean():.0f}%')
print('  legato fail', f"{100*fail[R[:,4]==1].mean():.0f}%", '| after rest', f"{100*fail[R[:,4]==0].mean():.0f}%")
same=np.sign(R[fail,2])==np.sign(R[fail,3]); print('  failures under-corrected (same sign as offset):', int(same.sum()), 'of', int(fail.sum()), '| median residual/offset on fails', round(float(np.median(R[fail,2]/R[fail,3])),2))
