import json, sys, numpy as np, collections
sys.argv=[sys.argv[0]]; sys.path.insert(0,'.')
import pitch_metrics as pm
items=pm.leadvocal_items('dev')[0::6]
TR={it['id']:json.load(open(f"/tmp/ve_{it['id']}_off.json"))['frames'] for it in items}
REF={it['id']:pm.ref(pm.WORK/f"{it['id']}.wav") for it in items}
SEGS=[]; LEG=[]
for it in items:
    fr=TR[it['id']]; ft=np.array([f['timeSec'] for f in fr]); mi=REF[it['id']]; ti=np.arange(len(mi))*0.01; notes=it['notes']
    for k,(s,e,t) in enumerate(notes):
        sel=(ti>=s)&(ti<=e); seg=mi[sel]; tt=ti[sel]
        if len(seg)>=40 and np.all(np.isfinite(seg)):
            med=np.median(seg); ok=np.abs(seg-med)<=1.0; best=(0,0); run=0
            for j,v in enumerate(ok):
                run=run+1 if v else 0
                if run>best[1]-best[0]: best=(j-run+1,j+1)
            a,b=best
            if b-a>=40:
                core=seg[a:b]; d,r=pm.vibrato(core)
                cat='wide' if (d>40 and 4<=r<=8) else ('mid' if (d>15 and 4<=r<=8) else 'other')
                SEGS.append((it['id'],cat,tt[a]+0.03,tt[b-1]-0.03))
        prev=notes[k-1] if k else None
        ci=pm.core(ti,mi,s,e)
        if prev and ci is not None and s-prev[1]<0.06 and t!=prev[2]:
            LEG.append((it['id'],s,int(round(np.nanmean(ci)))))
def score(sim, P):
    TG={i:sim(TR[i],P) for i in TR}
    fl=collections.defaultdict(list)
    for i,cat,a,b in SEGS:
        ft=np.array([f['timeSec'] for f in TR[i]]); g=TG[i][(ft>=a)&(ft<=b)]; g=g[g>=0]
        if len(g)>=10: fl[cat].append(np.sum(g[1:]!=g[:-1])>0)
    dl=[]
    for i,s,n in LEG:
        ft=np.array([f['timeSec'] for f in TR[i]]); idx=np.where((ft>=s-0.05)&(ft<=s+0.4))[0]
        hit=[ft[j]-s for j in idx if TG[i][j]==n]; dl.append(1000*hit[0] if hit else 400)
    return ' '.join(f"{c} {100*np.mean(v):3.0f}%" for c,v in sorted(fl.items()))+f" | legato delay med {np.median(dl):3.0f} p90 {np.percentile(dl,90):3.0f} ms"
def cur(fr,P):
    held=-1; away=0; silent=0; out=[]; pt=None
    for f in fr:
        dt=0.0107 if pt is None else f['timeSec']-pt; pt=f['timeSec']
        if not f['voiced']:
            silent+=dt
            if silent>0.15: held=-1
            out.append(-1); continue
        silent=0; x=f['inputMidi']; n=int(round(x))
        if held<0 or n==held: held=n; away=0; out.append(held); continue
        m=abs(x-held)-abs(x-n); away+=dt
        if m>P['margin'] or away>=P['after']: held=n; away=0
        out.append(held)
    return np.array(out)
def lock(fr,P):
    # note-lock: hold while the pitch stays inside held +/- band; leave by (a) leap, (b) fast median landed, (c) slow mean past midpoint+margin
    held=-1; silent=0; out=[]; hist=[]; pt=None
    for f in fr:
        dt=0.0107 if pt is None else f['timeSec']-pt; pt=f['timeSec']
        if not f['voiced']:
            silent+=dt
            if silent>0.15: held=-1; hist=[]
            out.append(-1); continue
        silent=0; x=f['inputMidi']; t=f['timeSec']; hist.append((t,x)); hist=[h for h in hist if t-h[0]<=P['slow']]
        if held<0: held=int(round(x)); out.append(held); continue
        fast=np.median([h[1] for h in hist if t-h[0]<=P['fast']]); slow=np.mean([h[1] for h in hist])
        full=(t-hist[0][0])>=0.8*P['slow']
        if abs(x-held)>P['leap']: held=int(round(x))
        elif abs(fast-held)>=P['fm'] and abs(fast-round(fast))<=P['land']: held=int(round(fast))
        elif full and abs(slow-held)>=0.5+P['sm']: held=int(round(slow))
        out.append(held)
    return np.array(out)
print('current      ', score(cur, dict(margin=0.5, after=0.12)))
for fm,land in ((0.75,0.3),(0.8,0.25),(0.85,0.2)):
    for fast in (0.05,0.08):
        for slow,sm in ((0.25,0.15),(0.35,0.2)):
            P=dict(leap=1.5,fm=fm,land=land,fast=fast,slow=slow,sm=sm)
            print(f"lock fast {fast*1000:.0f}ms>={fm} land<={land} slow {slow*1000:.0f}+{sm}", score(lock,P))
