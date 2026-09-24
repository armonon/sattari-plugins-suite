import json, subprocess, sys, collections
from pathlib import Path
HERE=Path(__file__).parent; OUT=HERE/'synth_key'; SK=HERE.parents[1]/'build-live-integration/plugins/song-key/SongKeyRender'
NAMES=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]; FLAT={"Db":"C#","Eb":"D#","Gb":"F#","Ab":"G#","Bb":"A#"}
def parse(s):
    r,m=s.strip().split()[:2]; r=FLAT.get(r,r); return NAMES.index(r), m.lower().startswith('min')
def rel(p,t):
    (pr,pm),(tr,tm)=p,t
    if p==t: return 'exact'
    if pm==tm and (pr-tr)%12 in (5,7): return 'fifth'
    if pm!=tm and ((not tm and (pr-tr)%12==9) or (tm and (pr-tr)%12==3)): return 'relative'
    if pm!=tm and pr==tr: return 'parallel'
    return 'other'
if __name__ == '__main__':   # imported by score_autokey_rt.py for parse/rel only
    man=json.load(open(OUT/'manifest.json')); res=[]; cats=collections.Counter(); bykind=collections.defaultdict(list)
    for m in man:
        out=subprocess.run([str(SK),'--analyse-file',str(OUT/'wav'/m['file'])],capture_output=True,text=True).stdout.strip().splitlines()
        line=[l for l in out if '|' in l][-1] if any('|' in l for l in out) else ''
        try: p=parse(line.split('|')[0])
        except Exception: p=None
        c=rel(p,parse(m['label'])) if p else 'no_result'
        cats[c]+=1; bykind[m['kind']].append(c=='exact'); res.append({**m,'pred':line.split('|')[0].strip(),'cat':c})
    json.dump(res,open(OUT/'score_songkey_file.json','w'),indent=1)
    n=len(res); print('Sattari Key (file analysis):', cats['exact'],'/',n, f"= {100*cats['exact']/n:.1f}% exact |", dict(cats))
    for k,v in bykind.items(): print(f'   {k}: {sum(v)}/{len(v)}')
