import json, sys, numpy as np
from pitchref import load, f0, SR
res={}
for vid in sys.argv[1:]:
    tr=json.load(open(f'work/{vid}_classic.json')); lat=tr['reportedLatencySamples']; B=tr['blockSize']
    mi,_=f0(load(f'work/{vid}_detuned.wav')); mo,_=f0(load(f'work/{vid}_classic.wav')[lat:])
    n=min(len(mi),len(mo)); shift=mo[:n]-mi[:n]   # realized shift per 10ms frame (output already latency-compensated)
    req=np.full(n,np.nan)
    for fr in tr['frames']:
        kb=int(round((fr['timeSec']+B/2/SR)/0.01))
        if 0<=kb<n: req[kb]=fr['correctionCents']/100.0 if fr['voiced'] else np.nan
    # forward-fill request to 10ms grid
    last=np.nan
    for i in range(n):
        if np.isfinite(req[i]): last=req[i]
        else: req[i]=last
    for L in range(-40,41,2):
        a=req[max(0,-L):n-max(0,L)]; b=shift[max(0,L):n-max(0,-L)] if L>=0 else shift[:n+L]
        if L<0: a=req[-L:n]; b=shift[:n+L]
        else: a=req[:n-L]; b=shift[L:n]
        m=np.isfinite(a)&np.isfinite(b)
        res.setdefault(L,[]).append(np.median(np.abs(a[m]-b[m]))*100)
best=min(res,key=lambda L: np.mean(res[L]))
for L in [-30,-20,-10,-6,-4,-2,0,2,4,6,10,20,30]:
    print(f"lag {L*10:+4d} ms: mean |realized-requested| {np.mean(res[L]):5.1f}c")
print('best lag', best*10, 'ms (positive = effect appears later than the block that requested it)')
