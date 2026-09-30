import json, sys, numpy as np
from pathlib import Path
from pitchref import load, f0, SR
tag=sys.argv[1]; subset=sys.argv[2] if len(sys.argv)>2 else 'tune'
REF=json.load(open('voices/reference_notes.json')); cats={}; total=0; passed=0
for vid,r in sorted(REF.items()):
    k=int(vid[1:3])
    if subset=='tune' and k%2==0 or subset=='holdout' and k%2==1: continue
    tr=json.load(open(f'work/{vid}_{tag}.json')); lat=tr['reportedLatencySamples']; B=tr['blockSize']
    mi=np.load(f'work/{vid}_detuned.wav.crepe.npy')
    cache=Path(f'work/{vid}_{tag}.wav.crepe.npy')
    if cache.exists(): mo=np.load(cache)
    else: mo,_=f0(load(f'work/{vid}_{tag}.wav')[lat:]); np.save(cache,mo)
    fr=tr['frames']; tb=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for (a,b,t,med) in r['notes']:
        s,e=a+8,b-5
        if e-s<3: continue
        ci=mi[s:e]; ci=ci[np.isfinite(ci)]; co=mo[s:e]; co=co[np.isfinite(co)]
        if len(ci)<3 or len(co)<3: continue
        inc=100*(np.median(ci)-t)
        if abs(inc)>45: cats['ambiguous_input(excluded)']=cats.get('ambiguous_input(excluded)',0)+1; continue
        total+=1; out=100*(np.median(co)-t)
        if abs(out)<=10: passed+=1; continue
        sel=[f for f,tt in zip(fr,tb) if s*0.01<=tt<=e*0.01 and f['voiced']]
        if not sel: cats['unvoiced_in_plugin']=cats.get('unvoiced_in_plugin',0)+1; continue
        dets=np.array([100*(f['detectedMidi']-f['correctionCents']/100-t) for f in sel])
        octv=np.mean(np.abs(dets)>600)
        dp=np.median(dets); rq=np.median([f['correctionCents'] for f in sel])
        if octv>0.2: c='octave_error_in_detector'
        elif abs(dp-inc)>8: c='detector_misread(>8c)'
        elif abs(rq+dp)>8: c='target/requested_mismatch'
        else: c='shifter_under_delivery'
        cats[c]=cats.get(c,0)+1
print(tag, subset, 'scored notes', total, 'pass', passed, f'({100*passed/total:.1f}%)'); print(json.dumps(cats, indent=0))
