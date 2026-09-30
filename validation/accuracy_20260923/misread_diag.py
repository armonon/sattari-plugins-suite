import json, numpy as np, sys
from pitchref import SR
tag=sys.argv[1]; REF=json.load(open('voices/reference_notes.json'))
rows=[]
for vid,r in sorted(REF.items()):
    if int(vid[1:3])%2==0: continue
    tr=json.load(open(f'work/{vid}_{tag}.json')); B=tr['blockSize']; mi=np.load(f'work/{vid}_detuned.wav.crepe.npy'); mo=np.load(f'work/{vid}_{tag}.wav.crepe.npy')
    fr=tr['frames']; tb=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for (a,b,t,med) in r['notes']:
        s,e=a+8,b-5; ci=mi[s:e]; ci=ci[np.isfinite(ci)]; co=mo[s:e]; co=co[np.isfinite(co)]
        if e-s<3 or len(ci)<3 or len(co)<3: continue
        inc=100*(np.median(ci)-t); out=100*(np.median(co)-t)
        if abs(inc)>45: continue
        sel=[f for f,tt in zip(fr,tb) if s*0.01<=tt<=e*0.01 and f['voiced']]
        if not sel: continue
        dets=np.array([100*(f['detectedMidi']-f['correctionCents']/100-t) for f in sel])
        if np.mean(np.abs(dets)>600)>0.2: continue
        dp=np.median(dets)
        rows.append((vid, t, inc, dp, out, (b-a)*10))
R=np.array([(x[2],x[3],x[4],x[1]) for x in rows])
mis=np.abs(R[:,0]-R[:,1])>8
print('notes',len(R),'misread>8c',int(mis.sum()),'| median (detected - CREPE) on misreads',round(float(np.median(R[mis,1]-R[mis,0])),1),'| overall median bias',round(float(np.median(R[:,1]-R[:,0])),1))
print('among misreads: output error vs (CREPE-detected) correlation', round(float(np.corrcoef(R[mis,2], R[mis,0]-R[mis,1])[0,1]),2))
for x in [r for r,m in zip(rows,mis) if m][:10]: print(f"  {x[0]} midi {x[1]} len {x[5]}ms: CREPE in {x[2]:+.1f} detected {x[3]:+.1f} out {x[4]:+.1f}")
