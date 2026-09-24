"""Production detector (from trace) vs reference pitch (CREPE-tiny, seeded) on real singers."""
import json, sys, numpy as np, collections
from csd_bench import tracks, section_notes, WORK, crepe_cached
SR=48000
def det_series(stem, tag='r0key'):
    tr=json.load(open(WORK/f'{stem}_natural_{tag}.json')); B=tr['blockSize']; fr=tr['frames']
    tc=np.array([f['timeSec']+B/SR-1024/SR for f in fr])
    v=np.array([f['voiced'] for f in fr]); corr=np.array([f['correctionCents'] for f in fr]); d=np.array([f['detectedMidi'] for f in fr])
    tgt=np.round(d+corr/100); det=np.where(v, tgt-corr/100, np.nan)
    return tc, det
if __name__ == '__main__':
    tag=sys.argv[1] if len(sys.argv)>1 else 'r0key'; split=sys.argv[2] if len(sys.argv)>2 else 'dev'
    errs=collections.defaultdict(list); lagscore=collections.defaultdict(list)
    for stem in tracks(split):
        ref=crepe_cached(WORK/f'{stem}.wav'); tr_=np.arange(len(ref))*0.01
        tc,det=det_series(stem, tag)
        # detector sampled on the 10 ms reference grid
        idx=np.clip(np.searchsorted(tc,tr_)-1,0,len(tc)-1); ds=det[idx]
        onset=np.zeros(len(ref),bool)
        for s,e,tg in section_notes(stem): onset[int(s*100):int(min(e,s+0.15)*100)]=True
        m=np.isfinite(ref)&np.isfinite(ds); e=100*(ds-ref)
        sec=stem.split('_')[2]
        errs['all'].append(e[m]); errs['onset'].append(e[m&onset]); errs['steady'].append(e[m&~onset]); errs[sec].append(e[m])
        for L in range(-8,9):
            a=np.roll(ds,-L); mm=np.isfinite(ref)&np.isfinite(a)
            lagscore[L].append(np.median(np.abs(100*(a[mm]-ref[mm]))))
    print(f"{'set':8} {'bias':>6} {'|err| med':>9} {'p90':>6} {'>25c':>6} {'>50c':>6} {'n':>7}")
    for k,v in errs.items():
        x=np.concatenate(v); a=np.abs(x)
        print(f"{k:8} {np.median(x):+6.1f} {np.median(a):9.1f} {np.percentile(a,90):6.1f} {100*np.mean(a>25):5.1f}% {100*np.mean(a>50):5.1f}% {len(x):7d}")
    best=min(lagscore,key=lambda L: np.mean(lagscore[L]))
    print('median |err| by time shift of detector (10 ms steps; + = detector reading later audio):', {L*10: round(float(np.mean(v)),1) for L,v in lagscore.items() if L%2==0}, '| best', best*10,'ms')
