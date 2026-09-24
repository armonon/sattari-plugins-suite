import json, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
SR=48000; res={'fail':[], 'pass':[]}; reqres={'fail':[], 'pass':[]}
for stem in tracks('tune'):
    tr=json.load(open(WORK/f'{stem}_natural_base.json')); B=tr['blockSize']; lat=tr['reportedLatencySamples']
    mi=crepe_cached(WORK/f'{stem}.wav'); mo=crepe_cached(WORK/f'{stem}_natural_base.wav', lat)
    ti=np.arange(len(mi))*0.01; to=ti[:len(mo)] if len(mo)<=len(ti) else np.arange(len(mo))*0.01
    fr=tr['frames']; tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    for s,e,tg in section_notes(stem):
        ci=note_centre(ti,mi,s,e); co=note_centre(np.arange(len(mo))*0.01,mo,s,e)
        if not(np.isfinite(ci) and np.isfinite(co)) or abs(100*(ci-tg))>48: continue
        sel=[f for f,t in zip(fr,tc) if s+0.08<=t<=e-0.05 and f['voiced']]
        if len(sel)<5: continue
        # plugin's own reading of the pitch = detected note - requested correction (target + ...); use target frames
        det=np.median([f['detectedMidi'] - 0 for f in sel])  # rounded detection
        rq=np.median([f['correctionCents'] for f in sel])
        k='fail' if abs(100*(co-tg))>10 else 'pass'
        # needed correction = tg - ci ; requested rq
        reqres[k].append(rq - 100*(tg-ci))
for k in ['pass','fail']:
    d=np.array(reqres[k]); print(f'{k}: requested minus needed correction: median {np.median(d):+.1f}c, |median| {np.median(np.abs(d)):.1f}c, n={len(d)}')
