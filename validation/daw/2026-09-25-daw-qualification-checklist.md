# DAW qualification: Auto Pitch + Sattari Key (Logic Pro, Ableton Live, FL Studio)

Candidate: **c63d63c** (2026-09-29 release fix on top of cbd1ce4: 44.1 kHz vibrato and latency
reporting, see `validation/auto-pitch/2026-09-29-44k1-fix-review.md`). cbd1ce4 is superseded, so rows
filled against it do not count. Results count only for the candidate binaries, so the setup below is
done before any row.

## Setup (once per machine)

1. Quit every DAW.
2. Run `scripts/install_candidate.sh c63d63c`.
   - It installs the packaged candidate from `dist/candidate-c63d63c/` (build it with
     `AP_BUILD=build/auto-pitch-rc2 scripts/package_candidate.sh c63d63c`): self-contained bundles,
     which is what a user gets.
   - It verifies every installed bundle is byte-identical to the package, runs auval, and prints
     four sha256 lines. Paste them here:

   ```
   (paste)
   ```

3. Vocal material: a dry lead vocal of 2–4 minutes, preferably one of the cleared stems, plus one
   song-length stereo mix for Sattari Key. Note the files used.
4. Record the host versions: Logic ______, Live ______, FL ______. Also record macOS ______ and the
   audio interface ______.

Mark each cell **P** (pass), **F** (fail, with a note), or **–** (not applicable). A single F blocks
release until it is explained. Add a timestamp or the project name to every note.

## Auto Pitch

Plugin format: Logic uses AU; Live and FL use VST3. Also run Live with AU once, in row 1.

| # | Test | Pass when | Logic | Live | FL |
|---|---|---|---|---|---|
| 1 | Insert / load on a vocal track | appears under Sattari; UI opens; no error or crash | | | |
| 2 | Mono track | processes, no silence, no channel duplication artefacts | | | |
| 3 | Stereo track | L and R both processed; the image is not collapsed | | | |
| 4 | Latency reporting | a sustained vocal printed through the plugin lines up with the dry track after bounce (within ~1 ms); host shows 3173 samples at 48 kHz (3038 at 44.1 kHz). Also listen to hard consonants and plosives: the engine smears sharp attacks about 7-10 ms later than the vowel body (measured 2026-09-25). Note whether that is audible against the dry track or the beat | | | |
| 5 | Host bypass on/off during playback, 10 times | no click, no old audio replayed, bypassed = dry in time | | | |
| 6 | Enable Correction on/off during playback, 10 times | no click; off = dry | | | |
| 7 | Manual key (e.g. D major, then Chromatic) | notes snap to the chosen scale; the status line shows it | | | |
| 8 | Auto Key on / off | on: shows "Chromatic until a key is detected", then a key; off: back to the manual key; no jump or click at the toggle | | | |
| 9 | Preset switching (Natural → Harmony · Pop Stack → Natural → Classic) | names display correctly (no "Â·"); nothing carries over between presets, e.g. harmony voices or pan | | | |
| 10 | Automation: write Amount, Speed, Key, Enable Correction and Formant; play back | automation follows; no clicks beyond the effect itself | | | |
| 11 | Project save, quit host, reopen | every setting restored, including key, Auto Key, harmony and pitch edits; the plugin does not re-open in a different state | | | |
| 12 | State recall repeated 3 times: save, reopen, delete one pitch-edit note, save, reopen | deleted notes stay deleted; the project file does not keep growing | | | |
| 13 | Playback start/stop and locate/loop | no stuck notes, bursts or stale audio after a locate | | | |
| 14 | Bounce / export offline, twice | both bounces are bit-identical (null them against each other), also with Humanize > 0 | | | |
| 15 | Realtime bounce, if the host offers it | matches the offline bounce (within the Neural Pitch caveat, if on) | | | |
| 16 | Multiple instances (4 vocal tracks) | independent settings; no cross-talk; CPU scales roughly linearly | | | |
| 17 | Sample-rate change, 44.1 → 48 → 96 kHz, where the host allows | reloads correctly; latency re-reported (3038 at 44.1, 3173 at 48, 6346 at 96 kHz); still in tune; at 44.1 kHz a wide vibrato is not widened | | | |
| 18 | Buffer size change, 64 / 256 / 1024 | no dropouts at 256+; 64 may be CPU-limited, so record CPU | | | |
| 19 | Remove plugin, re-insert, undo the removal | no crash; the undo restores the state | | | |
| 20 | Long playback: loop the 3-minute song for 30 minutes or more | no drift, no rising CPU, no crash; note CPU at start and end | | | |
| 22 | Transpose to Key −5 st on a drum loop, printed next to the dry loop | KNOWN OPEN DEFECT (predates the fix): the transposed audio sits off the grid by about 1.5 ms per semitone. Record how audible it is; not a pass/fail row until the owner decides the fix | | | |
| 21 | CPU observation: host CPU meter with 1 instance and with 4; and 1 instance host-bypassed | record the numbers. Bypass keeps the engine running (full CPU while bypassed) by design | | | |

## Sattari Key

| # | Test | Pass when | Logic | Live | FL |
|---|---|---|---|---|---|
| K1 | Insert on the stereo mix | UI opens; shows a key and its confidence after a few seconds of playback | | | |
| K2 | Correct key on the reference song | matches the known key; record the key shown and the truth | | | |
| K3 | Auto Pitch "Follow Song Key" with Sattari Key on the mix | Auto Pitch shows Song Key as the source and follows it; when Sattari Key is removed it falls back cleanly | | | |
| K4 | Save, reopen | the lock/dynamic setting is restored; no stale key shown for a different song | | | |
| K5 | Bypass / remove / re-insert | no crash, no stale bus key | | | |
| K6 | Sample-rate and buffer changes | still detects; no crash | | | |

## Host-specific notes to capture

- **Logic:** AU validation passes in the Plug-in Manager. Test with Low Latency Mode on and off:
  Logic may bypass plugins with high latency. Check that the track delay compensation display
  agrees with 3173 samples at 48 kHz.
- **Live:** test in both Arrangement and Session. Try the "Reduced latency when monitoring" setting.
  Freeze and flatten a track, then compare with the bounce.
- **FL:** test with a mixer insert and with the wrapper's "use fixed size buffers" off and on.
  Check the PDC display, and render with "HQ for all plugins".

## What to send back

- This file with every cell filled in, plus the four sha256 lines from the setup.
- The bounces from rows 14 and 15 (or the null-test result), and any crash reports from
  `~/Library/Logs/DiagnosticReports`.
- The CPU numbers from rows 20 and 21.
