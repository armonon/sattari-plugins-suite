import json, subprocess, collections
from pathlib import Path
from score_key import parse, rel, OUT, NAMES
HERE=Path(__file__).parent; B=HERE.parents[1]/'build/auto-pitch-neural/AutoPitchBench'
man=json.load(open(OUT/'manifest.json')); cats=collections.Counter(); bykind=collections.defaultdict(list); res=[]
for m in man:
    subprocess.run([str(B),str(OUT/'wav'/m['file']),'--output','/tmp/ak.wav','--trace','/tmp/ak.json','--auto-key','--key','C','--scale','major','--block','512'],capture_output=True)
    fr=json.load(open('/tmp/ak.json'))['frames']; last=fr[-1]
    p=(last['detectedRoot']%12, last.get('detectedScale',0)==1)
    c=rel(p,parse(m['label'])); cats[c]+=1; bykind[m['kind']].append(c=='exact')
    res.append({**m,'pred':f"{NAMES[p[0]]} {'minor' if p[1] else 'major'}",'cat':c})
json.dump(res,open(OUT/'score_autopitch_autokey.json','w'),indent=1)
n=len(res); print('Auto Pitch AutoKey:', cats['exact'],'/',n, f"= {100*cats['exact']/n:.1f}% exact |", dict(cats))
for k,v in bykind.items(): print(f'   {k}: {sum(v)}/{len(v)}')
