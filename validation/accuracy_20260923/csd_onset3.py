import json, sys, numpy as np, collections
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
from csd_onset import trace_frames
tag, split = sys.argv[1], sys.argv[2]
off=collections.Counter(); det_vs_crepe=[]; det_near=0; n=0; hold=0; conf_o=[]; conf_ok=[]
for stem in tracks(split):
    lat, B, t0, voiced, target, detected, corr, conf, reason = trace_frames(tag, stem)
    mi = crepe_cached(WORK / f"{stem}.wav")
    notes = section_notes(stem)
    for ni, (s, e, tg) in enumerate(notes):
        prev = notes[ni-1][2] if ni else None
        for k in range(int(np.ceil(s*100)), int((e-0.05)*100)):
            t=k*0.01; j=int(np.searchsorted(t0,t,side='right')-1)
            if j<0 or k>=len(mi) or not voiced[j] or not np.isfinite(mi[k]): continue
            nearest=round(mi[k])
            if target[j]!=tg and target[j]!=nearest and target[j]!=prev:
                n+=1; off[int(target[j]-tg)]+=1; det_vs_crepe.append(100*(detected[j]-mi[k]))
                det_near += round(detected[j])==target[j]; hold += round(detected[j])!=target[j]; conf_o.append(conf[j])
            elif target[j]==tg: conf_ok.append(conf[j])
d=np.array(det_vs_crepe)
print('wrong-target "other" frames:', n)
print(' target - gt (semitones):', off.most_common(6))
print(' plugin detected pitch vs CREPE input: median', round(float(np.median(d)),1),'c | |median|', round(float(np.median(np.abs(d))),1), 'c | >50c apart:', f"{100*np.mean(np.abs(d)>50):.0f}%")
print(' target == plugin detected nearest note (plugin followed its own reading):', f'{100*det_near/n:.0f}%', '| target held away from its reading (hysteresis hold):', f'{100*hold/n:.0f}%')
print(' confidence: other-wrong frames median', round(float(np.median(conf_o)),2), '| right-target frames median', round(float(np.median(conf_ok)),2))
