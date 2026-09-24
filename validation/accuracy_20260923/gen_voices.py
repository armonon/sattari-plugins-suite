"""Generate dry a cappella singing clips with ElevenLabs Music for accuracy testing.
Key is read from Keychain (service com.sattari.elevenlabs.api-key); it is never written anywhere."""
import json, os, subprocess, sys, time, urllib.request, urllib.error
from pathlib import Path
HERE = Path(__file__).parent; OUT = HERE / "voices"; OUT.mkdir(exist_ok=True)
API = "https://api.elevenlabs.io/v1/music?output_format=mp3_48000_192"
def key():
    return subprocess.check_output(["security","find-generic-password","-a",os.environ.get("USER","agentsattari"),"-s","com.sattari.elevenlabs.api-key","-w"],text=True).strip()
NEG = ["instruments","drums","piano","guitar","bass guitar","synth","beat","reverb","echo","autotune","choir","harmony","backing vocals","spoken word","rap"]
VOICES = [
 ("v01_bass_male_gospel","deep bass male voice","slow gospel hymn","D","major"),
 ("v02_baritone_male_soul","warm baritone male voice","soul ballad","F","minor"),
 ("v03_tenor_male_pop","bright tenor male voice","upbeat pop","A","major"),
 ("v04_countertenor_folk","light countertenor male voice","gentle folk song","E","minor"),
 ("v05_contralto_female_jazz","dark contralto female voice","smoky jazz ballad","C","minor"),
 ("v06_alto_female_rnb","smooth alto female voice","R&B slow jam with melismas","Bb","major"),
 ("v07_mezzo_female_theatre","strong mezzo-soprano female voice","musical theatre","G","major"),
 ("v08_soprano_female_classical","clear soprano female voice","classical art song","Eb","major"),
 ("v09_baritone_male_country","twangy baritone male voice","country ballad","G","major"),
 ("v10_tenor_male_rock","gritty tenor male voice","rock anthem sung","E","minor"),
 ("v11_alto_female_lullaby","soft breathy alto female voice","lullaby","F","major"),
 ("v12_soprano_female_pop","airy soprano female voice","dreamy pop","B","minor"),
 ("v13_bass_male_folk","low bass male voice","sea shanty","A","minor"),
 ("v14_tenor_male_rnb","falsetto-heavy tenor male voice","neo-soul","Db","major"),
 ("v15_mezzo_female_soul","powerful mezzo female voice","gospel soul with vibrato","C","major"),
 ("v16_child_like_female","young bright female voice","simple children's song","D","major"),
 ("v17_baritone_male_jazz","velvet crooner baritone male voice","jazz standard","Ab","major"),
 ("v18_soprano_female_opera","operatic soprano female voice with wide vibrato","aria","F#","minor"),
 ("v19_alto_female_blues","raspy alto female voice","slow blues","G","minor"),
 ("v20_tenor_male_kpop","clean light tenor male voice","K-pop ballad","E","major"),
 ("v21_mezzo_female_folk","plain unornamented mezzo female voice","Irish folk air","D","minor"),
 ("v22_baritone_male_pop","conversational baritone male voice","indie pop","C","major"),
 ("v23_soprano_female_rnb","agile soprano female voice","R&B runs and riffs","A","minor"),
 ("v24_tenor_male_musical","legit tenor male voice","musical theatre ballad","B","major"),
]
LYRICS = ["[Verse]\nHold the line and let it ring\nEvery note a quiet thing\nCarry me home on the evening wind",
          "[Verse]\nWalking slow beneath the rain\nCalling out your name again\nOne more light across the plain",
          "[Verse]\nSing it soft and sing it true\nAll the morning comes to you\nGolden sky in shades of blue"]
def gen(i, vid, voice, style, k, mode, dur_ms=16000):
    mp3 = OUT / f"{vid}.mp3"; wav = OUT / f"{vid}.wav"
    if wav.exists(): return "exists"
    payload = {"composition_plan": {"chunks": [{"text": LYRICS[i % 3], "duration_ms": dur_ms,
        "positive_styles": ["a cappella", "solo " + voice, style, f"in {k} {mode}", "dry close-mic studio vocal", "clear sustained melody"],
        "negative_styles": NEG, "context_adherence": "high"}]},
        "model_id": "music_v2_5", "seed": 23000 + i, "store_for_inpainting": False, "sign_with_c2pa": False}
    req = urllib.request.Request(API, data=json.dumps(payload).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "xi-api-key": key()})
    try:
        with urllib.request.urlopen(req, timeout=240) as r: mp3.write_bytes(r.read())
    except urllib.error.HTTPError as e:
        return f"HTTP {e.code}: {e.read()[:200]!r}"
    subprocess.run(["ffmpeg","-loglevel","error","-y","-i",str(mp3),"-ac","1","-ar","48000",str(wav)], check=True)
    meta = OUT / "prompts.jsonl"
    with meta.open("a") as f: f.write(json.dumps({"id": vid, "voice": voice, "style": style, "prompted_key": f"{k} {mode}", "seed": 23000 + i}) + "\n")
    return "ok"
if __name__ == "__main__":
    sel = sys.argv[1:] or [v[0] for v in VOICES]
    for i, v in enumerate(VOICES):
        if v[0] in sel:
            t = time.time(); print(v[0], gen(i, *v), f"{time.time()-t:.0f}s", flush=True)
