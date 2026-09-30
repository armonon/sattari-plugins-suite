import json, sys, numpy as np
from synth_pitch import OUT, SR
tag=sys.argv[1]; man=json.load(open(OUT/'manifest.json'))
bins=np.arange(0,400,20); tgt_ok={b:[] for b in bins}; err={b:[] for b in bins}
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); fr=tr['frames']; B=tr['blockSize']
    tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    mo=np.load(OUT/f"{m['name']}_{tag}.wav.crepe.npy")
    for i,n in enumerate(m['notes']):
        t0=n['start']/SR; t1=n['end']/SR
        for b in bins:
            t=t0+b/1000
            if t>t1-0.06: break
            j=np.searchsorted(tc,t)
            if j<len(fr) and fr[j]['voiced']:
                # held target = detected pitch + request (cents)
                tmidi=(fr[j]['detectedMidi']) # rounded detected pitch, not target
            k=int(t*100)
            if k<len(mo) and np.isfinite(mo[k]): err[b].append(abs(100*(mo[k]-n['target'])))
print('ms after onset : median |output - target| (c) , % frames within 10c')
for b in bins:
    e=np.array(err[b]); 
    if len(e): print(f'{b:4d} ms: {np.median(e):5.1f}c  {100*np.mean(e<=10):5.1f}%  (n={len(e)})')
