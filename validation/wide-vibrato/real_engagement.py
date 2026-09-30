import json, os, subprocess, sys, numpy as np
sys.argv=[sys.argv[0]]; sys.path.insert(0,'.')
import pitch_metrics as pm
from concurrent.futures import ThreadPoolExecutor
B='/Users/agentsattari/code/sattari-worktrees/rnd-next-gen/build/auto-pitch-rnd/AutoPitchBench'
items=pm.leadvocal_items('dev')[0::6]
def render(it, tag, extra):
    src=pm.WORK/f"{it['id']}.wav"; tr=f"/tmp/ve_{it['id']}_{tag}.json"; out=f"/tmp/ve_{it['id']}_{tag}.wav"
    subprocess.run([B,str(src),'--output',out,'--trace',tr,'--key','C','--scale','chromatic','--amount','1','--speed','1','--block','512','--style','Classic',*extra],check=True,capture_output=True)
    os.remove(out); return tr
jobs=[(it,t,e) for it in items for t,e in (('on',['--vibrato-target']),('off',[]))]
with ThreadPoolExecutor(8) as ex: trs=list(ex.map(lambda j: render(*j), jobs))
T={(j[0]['id'],j[1]):json.load(open(t))['frames'] for j,t in zip(jobs,trs)}
stats={'notes':0,'active':[], 'engaged':[], 'chg_on':[], 'chg_off':[]}
for it in items:
    mi=pm.ref(pm.WORK/f"{it['id']}.wav"); ti=np.arange(len(mi))*0.01
    fon,foff=T[(it['id'],'on')],T[(it['id'],'off')]
    ft=np.array([f['timeSec'] for f in fon])
    for s,e,tgt in it['notes']:
        ci=pm.core(ti,mi,s,e)
        if ci is None: continue
        d,r=pm.vibrato(ci)
        if not (d>40 and 4<=r<=8): continue
        sel=np.where((ft>=s+0.08)&(ft<=e-0.05))[0]
        if len(sel)<5: continue
        stats['notes']+=1
        stats['active'].append(np.mean([fon[i]['vibActive'] for i in sel])); stats['engaged'].append(np.mean([fon[i]['vibEngaged'] for i in sel]))
        for tag,fr in (('on',fon),('off',foff)):
            tg=[round(fr[i]['targetMidi']) for i in sel if fr[i]['voiced']]
            stats['chg_'+tag].append(sum(1 for a,b in zip(tg,tg[1:]) if a!=b))
n=stats['notes']; lens=None
print('wide-vibrato notes',n)
print('frames with vibrato detected (median/mean) %.2f/%.2f; engaged %.2f/%.2f'%(np.median(stats['active']),np.mean(stats['active']),np.median(stats['engaged']),np.mean(stats['engaged'])))
for t in ('off','on'):
    c=np.array(stats['chg_'+t]); print(t,'notes with target changes %.1f%%, mean changes/note %.2f'%(100*np.mean(c>0),c.mean()))
