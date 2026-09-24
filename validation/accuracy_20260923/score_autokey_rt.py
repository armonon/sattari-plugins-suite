"""Auto Pitch AutoKey at realtime pace (as in a DAW), 12 clips in parallel."""
import json, subprocess, collections, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from score_key import parse, rel, OUT, NAMES
HERE=Path(__file__).parent; B=HERE.parents[1]/'build/auto-pitch-neural/AutoPitchBench'
tag=sys.argv[1] if len(sys.argv)>1 else 'rt'
man=json.load(open(OUT/'manifest.json'))
def run(m):
    tr=f"/tmp/akrt_{m['file']}.json"
    subprocess.run([str(B),str(OUT/'wav'/m['file']),'--output',f'/tmp/akrt_{m["file"]}','--trace',tr,'--auto-key','--key','C','--scale','major','--block','512','--realtime'],capture_output=True)
    last=json.load(open(tr))['frames'][-1]; return m,(last['detectedRoot']%12,last.get('detectedScale',0)==1)
cats=collections.Counter(); bykind=collections.defaultdict(list); res=[]
with ThreadPoolExecutor(12) as ex:
    for m,p in ex.map(run, man):
        c=rel(p,parse(m['label'])); cats[c]+=1; bykind[m['kind']].append(c=='exact'); res.append({**m,'pred':f"{NAMES[p[0]]} {'minor' if p[1] else 'major'}",'cat':c})
json.dump(res,open(OUT/f'score_autopitch_autokey_{tag}.json','w'),indent=1)
n=len(res); print(f'Auto Pitch AutoKey (realtime, {tag}):', cats['exact'],'/',n, f"= {100*cats['exact']/n:.1f}% exact |", dict(cats))
for k,v in bykind.items(): print(f'   {k}: {sum(v)}/{len(v)}')
