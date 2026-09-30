import numpy as np
from csd_bench import tracks, section_notes, note_centre, WORK, crepe_cached, singer_f0
from key_ceiling import targets, keys, N, MAJ, MIN
from det_diag import det_series
for split in ['dev','holdout']:
    res={'production detector':[0,0],'manual f0 (independent perfect detector)':[0,0],'CREPE reference':[0,0]}
    for stem in tracks(split):
        k=keys[stem.split('_')[1]]; root=N.index(k['root']); sc=MAJ if k['scale']=='major' else MIN
        ref=crepe_cached(WORK/f'{stem}.wav'); tr_=np.arange(len(ref))*0.01
        t,mf=singer_f0(stem); man=np.interp(tr_, t, np.nan_to_num(mf, nan=-1)); man[man<0]=np.nan
        tc,det=det_series(stem); idx=np.clip(np.searchsorted(tc,tr_)-1,0,len(tc)-1); prod=det[idx]
        for name,series in [('production detector',prod),('manual f0 (independent perfect detector)',man),('CREPE reference',ref)]:
            ts=targets(series,root,sc,0).astype(float)
            for s,e,tg in section_notes(stem):
                ci=note_centre(tr_,ref,s,e)
                if not np.isfinite(ci) or abs(100*(ci-tg))>48: continue
                c=note_centre(tr_,ts,s,e)
                if np.isfinite(c): res[name][0]+=1; res[name][1]+=abs(100*(c-tg))<=10
    for name,(n,ok) in res.items(): print(f'{split:7} {name:42} {100*ok/n:5.1f}%')
