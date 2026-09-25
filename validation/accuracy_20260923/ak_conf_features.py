import json, collections, numpy as np, sys
sys.path.insert(0,'.')
from veto_proto import pcs, NAMES
from csd_bench import tracks
keys=json.load(open('csd_keys.json'))
def load(split):
    X=[]
    for st in tracks(split):
        k=keys[st.split('_')[1]]; truth=(NAMES.index(k['root']),0 if k['scale']=='major' else 1)
        fr=json.load(open(f'csd_work/{st}_natural_akfix_chrom.json'))['frames']
        t=np.array([f['timeSec'] for f in fr]); since=0.0; prev=None; start=0.0
        for i,f in enumerate(fr):
            d=(f['detectedRoot'],f['detectedScale'])
            if d!=prev: prev=d; start=f['timeSec']
            if i%5 or not f['voiced'] or d[1] not in (0,1): continue
            X.append((f['keyConf'], f['timeSec']-start, pcs(*d)==pcs(*truth), st.split('_')[2]))
    return X
for split in ('dev','holdout'):
    X=load(split); c=np.array([x[0] for x in X]); p=np.array([x[1] for x in X]); ok=np.array([x[2] for x in X]); n=len(X)
    print(split,'frames',n,'raw right',round(100*ok.mean(),1))
    print('  margin only   : ' + ' | '.join(f'g{g:.2f} R{100*np.mean((c>=g)&ok):4.1f} W{100*np.mean((c>=g)&~ok):4.1f}' for g in (0.05,0.08,0.10,0.12)))
    for P in (5,10,20):
        print(f'  persist>={P:2d}s  : ' + ' | '.join(f'g{g:.2f} R{100*np.mean((c>=g)&(p>=P)&ok):4.1f} W{100*np.mean((c>=g)&(p>=P)&~ok):4.1f}' for g in (0.0,0.03,0.05,0.08)))
