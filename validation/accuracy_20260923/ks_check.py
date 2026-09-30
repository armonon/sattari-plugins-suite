import json, numpy as np
from synth_key import melody, VOICE_RANGE, NAMES
MAJ=np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88]); MIN=np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
def ks(hist):
    best=None
    for r in range(12):
        for mi,prof in ((False,MAJ),(True,MIN)):
            c=np.corrcoef(hist,np.roll(prof,r))[0,1]
            if best is None or c>best[0]: best=(c,r,mi)
    return best
res=json.load(open('synth_key/score_songkey_file.json')); amb=0; fails=[r for r in res if r['kind']=='acappella' and r['cat']!='exact']
for key_i in range(24):
    root,minor=key_i%12,key_i>=12
    for vi,voice in enumerate(VOICE_RANGE):
        if vi%2: continue
        rng=np.random.default_rng(key_i*10+vi); notes,_=melody(root,minor,voice,rng)
        h=np.zeros(12)
        for c,d,_,_ in notes: h[int(round(c))%12]+=d
        c,r,mi=ks(h); name=f"{NAMES[root].replace('#','s')}_{'min' if minor else 'maj'}_{voice}_acappella.wav"
        ok=(r==root and mi==minor)
        f=[x for x in fails if x['file']==name]
        if f: print(f"{name:32} label {NAMES[root]} {'minor' if minor else 'major':5} | SongKey {f[0]['pred']:10} | KS-on-notes {NAMES[r]} {'minor' if mi else 'major'} {'(agrees with label)' if ok else '(AMBIGUOUS melody)'}")
        amb+= not ok
print('a cappella melodies where KS on the exact notes disagrees with the label:', amb, 'of 48')
