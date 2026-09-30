import json, numpy as np
from pitchref import SR
REF=json.load(open('voices/reference_notes.json')); tag='tune_classicmode'; shown=0
for vid,r in sorted(REF.items()):
    if int(vid[1:3])%2==0: continue
    tr=json.load(open(f'work/{vid}_{tag}.json')); B=tr['blockSize']; mi=np.load(f'work/{vid}_detuned.wav.crepe.npy')
    fr=tr['frames']; tb=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for (a,b,t,med) in r['notes']:
        s,e=a+8,b-5
        ci=mi[s:e]; ci=ci[np.isfinite(ci)]
        if e-s<3 or len(ci)<3: continue
        inc=100*(np.median(ci)-t)
        if abs(inc)>45: continue
        sel=[f for f,tt in zip(fr,tb) if s*0.01<=tt<=e*0.01 and f['voiced']]
        if not sel: continue
        dets=np.array([100*(f['detectedMidi']-f['correctionCents']/100-t) for f in sel]); rqs=np.array([f['correctionCents'] for f in sel])
        if np.mean(np.abs(dets)>600)>0.2: continue
        dp=np.median(dets); rq=np.median(rqs)
        if abs(dp-inc)<=8 and abs(rq+dp)>8 and shown<8:
            shown+=1
            tg=[f['detectedMidi'] for f in sel]; reasons=sorted(set(f['intentReasonName'] for f in sel))
            print(f"{vid} note t={t}: in {inc:+.1f} det {dp:+.1f} req {rq:+.1f} | frame targets {sorted(set(tg))} | per-frame req spread {np.percentile(rqs,10):+.0f}..{np.percentile(rqs,90):+.0f} | det spread {np.percentile(dets,10):+.0f}..{np.percentile(dets,90):+.0f}")
