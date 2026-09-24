import json, subprocess, numpy as np
from pathlib import Path
from pitchref import load, f0, SR
REF=json.load(open('voices/reference_notes.json')); within=[]
for vid,r in sorted(REF.items()):
    if int(vid[1:3])%2==1: continue
    mi=np.load(f'work/{vid}_detuned.wav.crepe.npy')
    tgt=np.full(len(mi),np.nan)
    for (a,b,tg,med) in r['notes']: tgt[a:b]=tg
    lines=[]; last=None
    for k in range(len(mi)):
        s=(tgt[k]-mi[k]) if np.isfinite(tgt[k]) and np.isfinite(mi[k]) else 0.0
        s=round(float(s),3)
        if s!=last: lines.append(f"{int(k*0.01*SR)} {s}"); last=s
    Path(f'work/{vid}_flatmap.txt').write_text("\n".join(lines)+"\n")
    out=f'work/{vid}_flatoracle.wav'
    subprocess.run(['rubberband','-3','-F','-q','--pitchmap',f'work/{vid}_flatmap.txt',f'work/{vid}_detuned.wav',out],check=True,capture_output=True)
    mo,_=f0(load(out))
    for (a,b,tg,med) in r['notes']:
        ci=mi[a+8:b-5]; ci=ci[np.isfinite(ci)]
        if len(ci)<3 or abs(100*(np.median(ci)-tg))>45: continue
        for v in mo[a:b-5]:
            if np.isfinite(v): within.append(abs(100*(v-tg))<=10)
print(f'PERFECT HARD-TUNE (per-frame flatten): frames within 10c {100*np.mean(within):.1f}%')
