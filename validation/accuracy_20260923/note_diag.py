import json, sys, numpy as np
from pitchref import load, f0, SR
vid, tag = sys.argv[1], sys.argv[2]
ref=json.load(open('voices/reference_notes.json'))[vid]['notes']; det=json.load(open(f'work/{vid}_detune.json'))
tr=json.load(open(f'work/{vid}_{tag}.json')); lat=tr['reportedLatencySamples']; B=tr['blockSize']
mi=np.load(f'work/{vid}_detuned.wav.crepe.npy'); mo,_=f0(load(f'work/{vid}_{tag}.wav')[lat:])
fr=tr['frames']; tb=np.array([f['timeSec']+B/SR-1024/SR for f in fr])  # detector frame centre
print(f"{'note':>4} {'len':>4} {'target':>6} {'in':>6} {'detect':>6} {'req':>6} {'out':>6}  (cents rel. target; req = requested correction)")
for i,(a,b,t,med) in enumerate(ref):
    s,e=a+8,b-5
    if e-s<3: continue
    ci=mi[s:e]; co=mo[s:e]
    sel=[f for f,tt in zip(fr,tb) if s*0.01<=tt<=e*0.01 and f['voiced']]
    if not sel: continue
    dp=np.median([f['detectedMidi']-f['correctionCents']/100 for f in sel]); rq=np.median([f['correctionCents'] for f in sel])
    out=100*(np.nanmedian(co)-t)
    flag='FAIL' if abs(out)>10 else ''
    print(f"{i:4d} {(b-a)*10:4d} {t:6d} {100*(np.nanmedian(ci)-t):6.1f} {100*(dp-t):6.1f} {rq:6.1f} {out:6.1f} {flag}")
