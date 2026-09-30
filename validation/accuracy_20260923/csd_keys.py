import mido, collections, numpy as np, json
from csd_bench import DS
N=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
MAJ=np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88]); MIN=np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
keys={}
for p in ['ER','LI','ND']:
    h=np.zeros(12)
    for s in ['soprano','alto','tenor','bass']:
        mid=mido.MidiFile(DS/f'CSD_{p}_{s}_midi.mid'); tpb=mid.ticks_per_beat
        for tr in mid.tracks:
            on={}; t=0
            for m in tr:
                t+=m.time
                if m.type=='note_on' and m.velocity>0: on[m.note]=t
                elif m.type in('note_off','note_on') and m.note in on: h[m.note%12]+=t-on.pop(m.note)
    sc=sorted(((np.corrcoef(h,np.roll(pr,r))[0,1],r,mi) for r in range(12) for mi,pr in ((False,MAJ),(True,MIN))),reverse=True)
    keys[p]={'root':N[sc[0][1]],'scale':'minor' if sc[0][2] else 'major','margin':round(sc[0][0]-sc[1][0],3),'runner_up':f"{N[sc[1][1]]} {'minor' if sc[1][2] else 'major'}"}
json.dump(keys,open('csd_keys.json','w'),indent=1); print(json.dumps(keys))
