import json, sys, numpy as np
from pitchref import load, f0, SR
vid=sys.argv[1]; tag=sys.argv[2] if len(sys.argv)>2 else 'classic'
tr=json.load(open(f'work/{vid}_{tag}.json')); lat=tr['reportedLatencySamples']; B=tr['blockSize']
mi,_=f0(load(f'work/{vid}_detuned.wav')); mo,_=f0(load(f'work/{vid}_{tag}.wav')[lat:])
det_err=[]; applied_vs_needed=[]; out_vs_expected=[]
for fr in tr['frames']:
    if not fr['voiced'] or fr['confidence']<0.8: continue
    t_end=fr['timeSec']+B/SR
    k=int(round((t_end-1024/SR)/0.01))          # detector frame centre
    kb=int(round((fr['timeSec']+B/2/SR)/0.01))    # block centre (where the shift is applied)
    if k<1 or kb>=len(mi) or kb>=len(mo) or not np.isfinite(mi[k]) or not np.isfinite(mi[kb]) or not np.isfinite(mo[kb]): continue
    plug=fr['detectedMidi']-fr['correctionCents']/100.0
    det_err.append(100*(plug-mi[k]))
    tgt=round(mi[kb]); needed=100*(tgt-mi[kb])
    realized=100*(mo[kb]-mi[kb])
    applied_vs_needed.append((fr['correctionCents'], needed, realized))
a=np.array(applied_vs_needed); d=np.array(det_err)
print(vid, 'detector minus CREPE (at detector frame): median', round(float(np.median(d)),1), 'c, |med|', round(float(np.median(np.abs(d))),1), 'c')
print('  requested correction vs needed at block: |med diff|', round(float(np.median(np.abs(a[:,0]-a[:,1]))),1), 'c')
print('  realized shift vs requested:              |med diff|', round(float(np.median(np.abs(a[:,2]-a[:,0]))),1), 'c')
print('  realized shift vs needed:                 |med diff|', round(float(np.median(np.abs(a[:,2]-a[:,1]))),1), 'c')
