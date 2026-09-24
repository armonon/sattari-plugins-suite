import json, subprocess, collections, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from score_key import parse, rel, OUT, NAMES
HERE=Path(__file__).parent; B=HERE.parents[1]/'build/auto-pitch-neural/AutoPitchBench'
prof, subset = sys.argv[1], sys.argv[2]
TUNE={'baritone','tenor'}
man=[m for m in json.load(open(OUT/'manifest.json')) if subset=='all' or (subset=='tune')==(m['voice'] in TUNE)]
def run(m):
    tr=f"/tmp/akp_{prof}_{m['file']}.json"
    args=[str(B),str(OUT/'wav'/m['file']),'--output','/tmp/akp.wav','--trace',tr,'--auto-key','--key','C','--scale','major','--block','512']
    if prof!='default': args+=['--autokey-profile',prof]
    subprocess.run(args,capture_output=True)
    last=json.load(open(tr))['frames'][-1]; return m,(last['detectedRoot']%12,last.get('detectedScale',0)==1)
cats=collections.Counter(); bykind=collections.defaultdict(list)
with ThreadPoolExecutor(10) as ex:
    for m,p in ex.map(run, man):
        c=rel(p,parse(m['label'])); cats[c]+=1; bykind[m['kind']].append(c=='exact')
n=sum(cats.values()); print(f'profile {prof:8} {subset:7}: {cats["exact"]}/{n} = {100*cats["exact"]/n:5.1f}% |', ' '.join(f'{k}:{sum(v)}/{len(v)}' for k,v in bykind.items()), '|', dict(cats))
