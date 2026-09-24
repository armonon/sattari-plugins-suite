import json, subprocess, numpy as np
from pathlib import Path
from pitchref import load, f0, SR
REF=json.load(open('voices/reference_notes.json')); tot=0; ok=0; ok_orig=0; tot_orig=0
for vid,r in sorted(REF.items()):
    if int(vid[1:3])%2==0: continue
    notes=r['notes']; mi=np.load(f'work/{vid}_detuned.wav.crepe.npy')
    lines=[]
    for (a,b,t,med) in notes:
        seg=mi[a+8:b-5]; seg=seg[np.isfinite(seg)]
        corr=-(np.median(seg)-t) if len(seg)>=3 else 0.0     # exact per-note correction (semitones)
        lines.append(f"{int(a*0.01*SR)} {corr:.4f}")
    Path(f'work/{vid}_oracle_map.txt').write_text("\n".join(lines)+"\n")
    out=f'work/{vid}_oracle.wav'
    subprocess.run(['rubberband','-3','-F','-q','--pitchmap',f'work/{vid}_oracle_map.txt',f'work/{vid}_detuned.wav',out],check=True,capture_output=True)
    mo,_=f0(load(out)); ms=np.load(f'work/{vid}_detuned.wav.crepe.npy')
    morig,_=f0(load(f'voices/{vid}.wav'))
    for (a,b,t,med) in notes:
        si=mi[a+8:b-5]; si=si[np.isfinite(si)]
        if len(si)<3 or abs(100*(np.median(si)-t))>45: continue
        so=mo[a+8:b-5]; so=so[np.isfinite(so)]; sg=morig[a+8:b-5]; sg=sg[np.isfinite(sg)]
        if len(so)>=3: tot+=1; ok+= abs(100*(np.median(so)-t))<=10
        if len(sg)>=3: tot_orig+=1; ok_orig+= abs(100*(np.median(sg)-t))<=10
print(f'ORACLE per-note correction through rubberband: {ok}/{tot} = {100*ok/tot:.1f}% within 10c')
print(f'ORIGINAL AI singer (no detune): {ok_orig}/{tot_orig} = {100*ok_orig/tot_orig:.1f}% within 10c')
