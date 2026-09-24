import json, sys, numpy as np
from synth_pitch import OUT, core_centres, SR
tag=sys.argv[1]; man=json.load(open(OUT/'manifest.json')); shown=0
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); fr=tr['frames']; B=tr['blockSize']
    mo=np.load(OUT/f"{m['name']}_{tag}.wav.crepe.npy"); mi=np.load(OUT/f"{m['name']}.wav.crepe.npy")
    cs=core_centres(mo,m['notes'])
    for i,(n,c) in enumerate(zip(m['notes'],cs)):
        if not np.isfinite(c) or abs(100*(c-n['target']))<=15 or shown>=2: continue
        shown+=1; t0=n['start']/SR; t1=n['end']/SR
        print(f"{m['name']} note {i}: target {n['target']} true offset {n['offset_c']:+.1f}c, out centre {100*(c-n['target']):+.1f}c, dur {t1-t0:.2f}s")
        for f in fr:
            tc=f['timeSec']+B/SR-1024/SR
            if t0<=tc<=t1 and int((tc-t0)*1000)%60<12:
                k=int(tc*100)
                det=f['detectedMidi']-f['correctionCents']/100-n['target'] if f['voiced'] else np.nan
                print(f"   t+{(tc-t0)*1000:4.0f}ms in(CREPE) {100*(mi[k]-n['target']):+6.1f}  det {100*det:+6.1f}  tgt {f['detectedMidi']}  req {f['correctionCents']:+6.1f}  out {100*(mo[k]-n['target']):+6.1f}  conf {f['confidence']:.2f}")
    if shown>=2: break
