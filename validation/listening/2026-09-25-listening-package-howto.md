# Auto Pitch blind listening package: how to build it on lillypad (candidate cbd1ce4)

LISTENING STATUS: NOT VERIFIED. This document sets up the session. It contains no judgments.

## What you receive

`auto-pitch-candidate-cbd1ce4.tar.gz` and its `.sha256` file, made by `scripts/make_candidate_kit.sh`
from tag `auto-pitch-rc-baseline-20260925` (commit `cbd1ce4c8735`). The tarball contains:

| path | what |
|---|---|
| `bin/AutoPitchBench` | the candidate render binary, byte-identical to the one fingerprinted in `validation/auto-pitch/qualification/2026-09-25-cbd1ce4.md` (sha256 `92aa71a0591ce73e...`) |
| `lib/libonnxruntime.1.22.0.dylib` | its only non-system dependency |
| `build_listening_package.py` | the package builder (Python 3.9+, standard library only, no numpy needed) |
| `corpus/auto-pitch.json` | the corpus manifest; it points at `/Users/lillypad/Desktop/vocal test/wav/*.wav` |
| `provenance/2026-09-25-cbd1ce4.md` | the qualification report the binary hash comes from |
| `KIT-MANIFEST.json` | commit, and sha256 and size of every file above |

The builder refuses to run if any file's hash differs from `KIT-MANIFEST.json`, if a file is missing
or extra, or if `bin/AutoPitchBench` is not the pinned cbd1ce4 binary. The binary is unmodified. It is
run with `DYLD_LIBRARY_PATH=<kit>/lib`, so it loads the bundled onnxruntime. The builder checks the
dyld load path on every run.

Requirements: macOS 13.3 or later, Intel or Apple silicon, python3 (`xcode-select --install` provides
it), and at least 8 GB free disk. The work itself needs about 0.2 GB.

## Commands (Terminal, on lillypad)

```sh
cd ~/Downloads                                   # wherever the tarball landed
shasum -a 256 -c auto-pitch-candidate-cbd1ce4.tar.gz.sha256   # must print: OK
tar xzf auto-pitch-candidate-cbd1ce4.tar.gz
KIT=~/Downloads/auto-pitch-candidate-cbd1ce4

# 1. See what will be selected (renders nothing):
python3 "$KIT/build_listening_package.py" --dry-run

# 2. Build the package (about 2-3 minutes for the six corpus clips):
python3 "$KIT/build_listening_package.py" --out "$HOME/Desktop/vocal test/listening-cbd1ce4"

# 2b. Optional: include Auto-Tune Pro reference renders (see below):
python3 "$KIT/build_listening_package.py" --out "$HOME/Desktop/vocal test/listening-cbd1ce4-atp" \
        --autotune-dir "$HOME/Desktop/vocal test/autotune-pro"

# 3. After scoring, check that nothing changed:
python3 "$KIT/build_listening_package.py" --verify "$HOME/Desktop/vocal test/listening-cbd1ce4"
```

Step 2 creates two things:

- the folder `listening-cbd1ce4/`: `S01-A.wav ...`, `SCORING-SHEET.csv`, `LISTENER-INSTRUCTIONS.txt`
  and `PACKAGE-MANIFEST.json`;
- the answer key `listening-cbd1ce4-KEY.json`, NEXT TO the folder, not inside it. Do not open it
  until every score is filled in.

The builder will not overwrite an existing package. Every rebuild needs a new `--out`.

## What is in each set

Each set is one excerpt of one corpus clip. Its versions are labelled A-D, or A-E when an Auto-Tune
reference is present. Labels are shuffled per set and so is the set order.

| condition | how it is made |
|---|---|
| dry | the original source excerpt, unprocessed, time-aligned with the renders |
| Auto Pitch Natural | Style Natural, key from the manifest (`manual_key`, else `ground_truth.key`; only dickey-prairie has one, G# major), otherwise C Chromatic |
| Auto Pitch Classic | Style Classic, same key |
| Auto Pitch + AutoKey | Style Classic, `--auto-key --manual-key-untouched` (the style used in the AutoKey accuracy evidence; `--autokey-style Natural` changes it) |
| Auto-Tune Pro | only if `--autotune-dir` holds a file for that clip; otherwise the slot is left out and the manifest says so |

All Auto Pitch renders use the plugin's factory defaults for everything else: amount 1.0,
speed 0.85, Do No Harm on, transition 0.5, formant preserve on, block 512. The bench's own
defaults differ, so they are overridden explicitly. Factory presets such as "Natural" or
"Hard Tune" are not used; only the style changes.

- **Full-clip rendering.** Each clip is rendered in full, so AutoKey hears the song from the start
  as it would in a DAW. The excerpt is then cut from the render after removing the plugin
  latency.
- **Latency calibration.** Latency is calibrated with a bypass render of noise, which must equal
  the input delayed exactly by the reported latency (2885 samples). Every render's reported
  latency is checked against that value.
- **Loudness matching.** Every file is set to -20 LUFS integrated (ITU-R BS.1770-4, K-weighted,
  gated). If any file would then peak above -1 dBFS, the whole set is turned down by one shared
  amount. Output is mono 24-bit WAV with 10 ms fades.
- **Excerpt selection.** One excerpt per clip, 15 s (always between 8 and 20 s): the most active
  window, with its edges moved to the quietest nearby point. Clips shorter than 8 s are used
  whole. To set a window yourself: `--window slay-demons=30:45`.
- **Seed.** Without `--seed`, a random seed is drawn and stored only in the key file. The same seed
  and the same inputs always produce identical files.

## Auto-Tune Pro references (optional)

In your DAW, bounce each clip through Auto-Tune Pro:

- the whole clip, starting at 0 s;
- 48 kHz (the corpus rate);
- any WAV format;
- named `<clip_id>.wav`, for example `slay-demons.wav`. A single file starting with the clip id
  also matches.

The builder assumes the bounce is time-aligned, which is true when the DAW compensates plugin
delay. It warns if a bounce looks offset by more than 20 ms. `--autotune-align auto` shifts it by
the measured offset instead. Record the Auto-Tune settings you used in the notes column.

## Coverage of the current corpus

The dry run showed every requested category covered by the six clips. Vibrato is thin, with a
single clip (vs-vibrato-f5). The other categories have two clips each; difficult phrases has four:

| category | clips |
|---|---|
| male | vs-belt-m6, vs-melisma-m3 |
| female | dickey-prairie, vs-vibrato-f5 |
| low register (f0 < 180 Hz) | slay-demons, vs-melisma-m3 |
| high register (f0 >= 300 Hz) | dickey-prairie, vs-vibrato-f5 |
| vibrato | vs-vibrato-f5 |
| sustained notes (>= 3 s) | vs-belt-m6, vs-vibrato-f5 |
| transitions | slay-demons, vs-melisma-m3 |
| difficult phrases (rap, noisy or belting) | dickey-prairie, guidos-pizza, slay-demons, vs-belt-m6 |

Known limits of this corpus:

- There is no female low-register clip and no male high-register clip.
- Three clips are VocalSet exercises, not songs.
- guidos-pizza is only in the difficult-phrases category.

## What to send back

1. `SCORING-SHEET.csv`, filled in. Each row takes naturalness, artifacts, tuning accuracy (1-5
   each), a rank within the set, and defects with timestamps.
2. `PACKAGE-MANIFEST.json`.
3. `listening-cbd1ce4-KEY.json`, only after scoring is complete.
4. The output of the `--verify` command, which should end in `VERIFY: PASS`.

Never send, upload or commit the audio: `public_release_allowed` is false for the owner's clips.
The results then go into a report here (`validation/templates/listening-test-report.md`).
