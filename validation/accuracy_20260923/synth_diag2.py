import json, sys, numpy as np
from synth_pitch import OUT, core_centres, SR
tag=sys.argv[1]; man=json.load(open(OUT/'manifest.json'))
cat={'unvoiced_in_plugin':0,'settles_late(2nd half ok)':0,'wrong_throughout':0}; total=0; fails=0
for m in man:
    tr=json.load(open(OUT/f"{m['name']}_{tag}.json")); fr=tr['frames']; B=tr['blockSize']
    tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr]); vo=np.array([f['voiced'] for f in fr])
    mo=np.load(OUT/f"{m['name']}_{tag}.wav.crepe.npy")
    for n in m['notes']:
        a=int((n['start']/SR+0.10)*100); b=int((n['end']/SR-0.06)*100)
        seg=mo[a:b]; ok=np.isfinite(seg)
        if ok.sum()<5: continue
        total+=1; c=100*(np.mean(seg[ok])-n['target'])
        if abs(c)<=10: continue
        fails+=1
        sel=(tc>=a/100)&(tc<=b/100)
        if sel.any() and vo[sel].mean()<0.5: cat['unvoiced_in_plugin']+=1; continue
        h=len(seg)//2; s2=seg[h:]; s2=s2[np.isfinite(s2)]
        if len(s2)>=3 and abs(100*(np.mean(s2)-n['target']))<=10: cat['settles_late(2nd half ok)']+=1
        else: cat['wrong_throughout']+=1
print(tag, 'fails', fails, 'of', total, cat)
