import json, sys, numpy as np
from pitchref import load, f0
REF=json.load(open('voices/reference_notes.json'))
for tag in sys.argv[1:]:
    within=[]; onset=[]
    for vid,r in sorted(REF.items()):
        if int(vid[1:3])%2==1: continue
        t=json.load(open(f'work/{vid}_{tag}.json')); lat=t['reportedLatencySamples']+int(t.get('prototypeLookahead',0))
        mo,_=f0(load(f'work/{vid}_{tag}.wav')[lat:]); mi=np.load(f'work/{vid}_detuned.wav.crepe.npy')
        for (a,b,tg,med) in r['notes']:
            ci=mi[a+8:b-5]; ci=ci[np.isfinite(ci)]
            if len(ci)<3 or abs(100*(np.median(ci)-tg))>45: continue
            seg=mo[a:b-5]
            for k,v in enumerate(seg):
                if np.isfinite(v):
                    ok=abs(100*(v-tg))<=10; within.append(ok)
                    if k<15: onset.append(ok)
    print(f'{tag:10} frames within 10c: all {100*np.mean(within):.1f}% | first 150 ms of notes {100*np.mean(onset):.1f}%')
