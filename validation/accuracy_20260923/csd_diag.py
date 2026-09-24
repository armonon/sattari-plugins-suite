import json, sys, numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached
tag=sys.argv[1] if len(sys.argv)>1 else 'base'; mode='natural'; rows=[]
for stem in tracks('tune'):
    tr=json.load(open(WORK/f'{stem}_{mode}_{tag}.json')); lat=tr['reportedLatencySamples']
    mi=crepe_cached(WORK/f'{stem}.wav'); mo=crepe_cached(WORK/f'{stem}_{mode}_{tag}.wav', lat)
    ti=np.arange(len(mi))*0.01; to=np.arange(len(mo))*0.01
    fr=tr['frames']; tc=np.array([f['timeSec'] for f in fr]); voiced=np.array([f['voiced'] for f in fr])
    for s,e,tg in section_notes(stem):
        ci=note_centre(ti,mi,s,e); co=note_centre(to,mo,s,e)
        if not(np.isfinite(ci) and np.isfinite(co)) or abs(100*(ci-tg))>48: continue
        sel=(tc>=s+0.08)&(tc<=e-0.05); vfrac=voiced[sel].mean() if sel.any() else 0
        # second-half-only centre
        h=(s+e)/2; co2=note_centre(to,mo,h-0.08,e)
        rows.append((100*(ci-tg),100*(co-tg),e-s,vfrac,100*(co2-tg) if np.isfinite(co2) else np.nan))
R=np.array(rows); fail=np.abs(R[:,1])>10
print('notes',len(R),'fail',int(fail.sum()), f'({100*fail.mean():.1f}%)')
print(' plugin mostly UNVOICED on the note (<50% of frames):', int((fail&(R[:,3]<0.5)).sum()))
under=fail&(np.sign(R[:,1])==np.sign(R[:,0]))&(np.abs(R[:,1])<np.abs(R[:,0]))
print(' under-corrected (moved toward target, stopped short):', int(under.sum()), '| residual/input median', round(float(np.median(R[under,1]/R[under,0])),2))
print(' made worse or overshot:', int((fail&~under&(R[:,3]>=0.5)).sum()))
late=fail&(np.abs(R[:,4])<=10)
print(' 2nd half of note is fine (slow settle):', int(late.sum()))
for lo,hi in [(0,0.4),(0.4,0.8),(0.8,99)]:
    s=(R[:,2]>=lo)&(R[:,2]<hi); print(f'  note length {lo}-{hi}s: fail {100*fail[s].mean():.0f}% (n={s.sum()})')
