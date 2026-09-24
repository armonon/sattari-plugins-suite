import json, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
SR=48000; stats={'fail':[], 'pass':[]}
for stem in tracks('tune'):
    tr=json.load(open(WORK/f'{stem}_natural_base.json')); B=tr['blockSize']; lat=tr['reportedLatencySamples']
    mi=crepe_cached(WORK/f'{stem}.wav'); mo=crepe_cached(WORK/f'{stem}_natural_base.wav', lat)
    fr=tr['frames']; tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for s,e,tg in section_notes(stem):
        ci=note_centre(np.arange(len(mi))*0.01,mi,s,e); co=note_centre(np.arange(len(mo))*0.01,mo,s,e)
        if not(np.isfinite(ci) and np.isfinite(co)) or abs(100*(ci-tg))>48: continue
        d=[]; tgts=[]
        for f,t in zip(fr,tc):
            if s+0.08<=t<=e-0.05 and f['voiced']:
                k=int(round(t*100))
                if 0<=k<len(mi) and np.isfinite(mi[k]):
                    target=round(f['detectedMidi']+f['correctionCents']/100); det=target-f['correctionCents']/100
                    d.append(100*(det-mi[k])); tgts.append(target)
        if len(d)<5: continue
        d=np.array(d); key='fail' if abs(100*(co-tg))>10 else 'pass'
        stats[key].append((np.median(d), np.percentile(np.abs(d),50), np.mean(np.array(tgts)!=tg)))
for k in ['pass','fail']:
    a=np.array(stats[k]); print(f"{k}: detector-CREPE per-note median offset |{np.median(np.abs(a[:,0])):.1f}|c | per-frame |err| {np.median(a[:,1]):.1f}c | frames targeting the WRONG note {100*np.mean(a[:,2]):.1f}%  (n={len(a)})")
