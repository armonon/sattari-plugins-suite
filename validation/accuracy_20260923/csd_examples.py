import json, csv, shutil, numpy as np
from pathlib import Path
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
D=Path('csd_onset'); notes=json.load(open(D/'notes_r0_dev.json')); ex=D/'examples'; ex.mkdir(exist_ok=True)
fails=[n for n in notes if not n['pass']]
picks={}
def pick(name, pred, k=2):
    c=[n for n in fails if pred(n)][:k]; picks[name]=c
pick('clean_attack', lambda n: n['context']=='attack_after_rest' and not n['scoop'] and (n['vibrato_c'] or 0)<15)
pick('soft_attack', lambda n: n['context']=='attack_after_rest' and n['category'] and 'late' in n['category'])
pick('scoop', lambda n: n['scoop'])
pick('slide_large_jump', lambda n: n['context']=='large_jump')
pick('vibrato', lambda n: (n['vibrato_c'] or 0)>=25)
pick('repeated_same_note', lambda n: n['context']=='repeated_note')
pick('semitone_transition', lambda n: n['context']=='semitone_step')
pick('fast_run', lambda n: n['dur']<0.3)
for name, lst in picks.items():
    for n in lst:
        src=D/'traces'/f"{n['stem']}_n{n['note']:03d}.csv"; dst=ex/f"{name}__{n['stem']}_n{n['note']:03d}"
        shutil.copy(src, str(dst)+'.csv')
        rows=list(csv.DictReader(open(src)))
        f=lambda k: np.array([float(r[k]) if r[k] not in ('',None) else np.nan for r in rows])
        t=f('ms_from_onset'); gt=f('gt_note')
        plt.figure(figsize=(8,3.2))
        plt.plot(t, 100*(f('input_pitch')-gt), label='singer (input)', color='#888')
        plt.plot(t, 100*(f('output_pitch')-gt), label='Auto Pitch output', color='#d33')
        plt.plot(t, 100*(f('selected_target')-gt), label='selected target', color='#36c', lw=2, drawstyle='steps-post')
        plt.plot(t, 100*(f('detected_pitch')-gt), label='Auto Pitch detected', color='#3a3', lw=0.8, ls='--')
        plt.axhline(0,color='k',lw=0.5); plt.axhspan(-10,10,color='g',alpha=0.08); plt.ylim(-160,160)
        plt.title(f"{name}: {n['stem']} note {n['note']} | category: {n['category']} | out {n['out_c']}c"); plt.xlabel('ms from annotated onset'); plt.ylabel('cents vs intended note')
        plt.legend(fontsize=7, loc='upper right'); plt.tight_layout(); plt.savefig(str(dst)+'.png', dpi=110); plt.close()
    print(name, len(lst))
