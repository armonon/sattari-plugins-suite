import json, sys, numpy as np
from synth_pitch import OUT, SR
tag=sys.argv[1]; man=json.load(open(OUT/'manifest.json')); req_t=[]; out_t=[]
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); fr=tr['frames']; B=tr['blockSize']
    tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    tgtc=np.array([(f['detectedMidi']*0+0) for f in fr])
    mo=np.load(OUT/f"{m['name']}_{tag}.wav.crepe.npy")
    for n in m['notes']:
        t0=n['start']/SR; t1=n['end']/SR; off=n['offset_c']
        # requested correction "right" when it is within 12c of -offset (the vibrato makes it wobble; use 3-frame median)
        idx=np.where((tc>=t0)&(tc<=t1))[0]
        rq=np.array([fr[i]['correctionCents'] if fr[i]['voiced'] else np.nan for i in idx])
        # true instantaneous deviation is unknown per frame here; use target stability instead: requested target note == n.target
        # approximate: time until |req + off| < 25 for 3 consecutive frames
        ok=np.abs(rq+off)<25; rt=np.nan
        for j in range(len(ok)-2):
            if ok[j] and ok[j+1] and ok[j+2]: rt=tc[idx[j]]-t0; break
        req_t.append(rt)
        k0=int(t0*100); k1=int(t1*100); seg=mo[k0:k1]
        ot=np.nan
        for j in range(len(seg)-4):
            w=seg[j:j+5]
            if np.all(np.isfinite(w)) and abs(100*(np.mean(w)-n['target']))<10: ot=j/100; break
        out_t.append(ot)
r=np.array(req_t); o=np.array(out_t)
print(tag, 'requested-correction settles: median', round(np.nanmedian(r)*1000), 'ms, p90', round(np.nanpercentile(r,90)*1000), 'ms | never', int(np.isnan(r).sum()))
print(tag, 'output within 10c:           median', round(np.nanmedian(o)*1000), 'ms, p90', round(np.nanpercentile(o,90)*1000), 'ms | never', int(np.isnan(o).sum()))
