# Sattari Audio Suite — Quick Start (one-pager per plugin)

These are the smallest installs and first-pattern wirings for each of the nine
plugins. For deeper guides see each `plugins/<slug>/README.md`. The whole suite
is GPLv3 (see `LICENSING.md`).

## Install (the whole suite)

1. Run `scripts/package_suite_installer.sh` once to produce
   `build/installer/Sattari-Audio-Suite.pkg` (~86 MB; bundles all 9 plugins).
2. Double-click the `.pkg`. `Sattari Hub.app` lands in `/Applications`.
3. Open the Hub → **Install All**. AU, VST3, and Standalone bundles are copied
   into `~/Library/Audio/Plug-Ins/{Components,VST3}/` and `/Applications/`.
4. Restart your DAW (Logic / Ableton / FL / Reaper / Live).

---

## 1. Sattari Auto Pitch — vocal & instrument tuning
**Where:** insert on a mono or stereo audio track with a vocal or monophonic
instrument.
**First 30 seconds:**
- Header → pick **Sound Source** (Alto/Tenor, Bass Voice, or one of the three
  Instrument bands).
- BASIC → use the large key control to set **Key & Scale** (e.g. A Minor),
  auto-detect this track, or follow Sattari Key from the song bus.
- **Retune Speed** dialed for the feel (high = "hard tune", low = natural).
- For per-section keys: PRO tab → **Scan Track…** → pick the song's audio file.
  Enable **Use Key Map** to follow the timeline at playback.
**Tabs:** BASIC = performance controls; PRO = detector, MIDI/reference,
formant/vibrato and key-map controls; EDIT = capture note objects, drag to
retarget, or right-click to bypass a note.
**A/B compare:** A | B | x in the header.

## 2. Sattari Align — time alignment
**Where:** insert on the track to shift (the "target"). Route the reference
into the plugin's Guide sidechain input.
**First 30 seconds:**
- **Search Range:** how far the cross-correlator looks (start at 100 ms).
- **Sensitivity / Smoothing:** for jitter control.
- Watch the big offset readout — when "Guide: active" lights up, the lag will
  start tracking. Hit **Lock** to print the aligned take.
**Limits:** v1 = bulk-shift only. Realtime can only delay the target to catch
up to the guide; if target is earlier than guide, swap which signal you route
as Guide.

## 3. Sattari Royal Chain — vocal channel chain
**Where:** insert on a vocal track. AU/VST3/Standalone.
**First 30 seconds:**
- Top row: **Vocal Style** preset (Modern Rap / R&B / Pop / Persian-World Pop /
  Indie / Podcast / Background / Hook / Demo Rescue / Live).
- **Vocal Crown Compass** (Portal-style): drag the 5 module nodes radially —
  TUNE, WIDTH, CONTROL, FINISH, SMOOTH. Centre = 0, outer ring = 100. Bright
  pulse around a node = you hit the rail.
- **Sync Key from Auto Pitch** toggle (new): when on, the Tune module's
  key/scale follows whatever Auto Pitch is currently detecting on its bus.
- Macro row at the bottom: Clean / Tune / Control / Tone / Air / Smooth /
  Glow / Width / Space / Royal.

## 4. Sattari Bus — channel strip
**Where:** insert on any bus / aux / master.
**First 30 seconds:**
- **Mode:** Clean / Glue / Console / Tape.
- **Drive** at 0 = bit-transparent. Turn up for harmonic colour with 4×
  oversampling (anti-aliased).
- **6 factory presets**: Mix Glue / Drum Bus / Master Polish / Parallel
  Crush / Tape Warm / Wide & Open.

## 5. Side Chain Master — sidechain ducker
**Where:** insert on the channel to duck (the bass / pad / etc.). Route the
trigger into the Sidechain input.
**First 30 seconds:**
- **Use External Sidechain** on, or stay on the internal-detector fallback.
- **Tempo sync duck** for the classic EDM pump — no sidechain needed; the
  curve locks to host PPQ.
- **Lookahead:** up to 10 ms; host PDC compensates.
- Watch the **GR history graph** at the bottom.

## 6. Sub Conjurer — sub-bass synth
**Where:** insert on kick or bass.
**First 30 seconds:**
- **Mode:** Clean / Power / Deep / 808 / Perc-to-Sub.
- **Tuning:** Follow Pitch (tracks the source), Lock Note (fixed), MIDI Note
  (incoming MIDI sets the pitch).
- **Sub Amount** controls how loud the synthesized sub sits under the source.
- 10 genre presets: Clean 808 / Heavy Trap / Bass Guitar Weight / Kick
  Reinforcer / Club Low-End / Warm R&B / Subtle Mix Weight / Deep Synth /
  Tight Low-End / Cinematic Sub.

## 7. Sattari VoxKey — voice → MIDI
**Where:** insert on a vocal track. AU/VST3/Standalone.
**First 30 seconds:**
- Dry vocal passes through unchanged.
- MIDI output drives a synth on another track. In Ableton: VoxKey on the
  vocal track, MIDI track receives `MIDI From: VoxKey` with `Monitor: In`.
- **Mode:** Melody / Bass Follow / Harmony / Drum Mouth / Pad-Soul Layer.
- **Key & Scale:** snaps detected notes to the chosen scale.
- **Bend Range** (semitones) matches the receiving synth's pitch-bend range.
- **Glide / Legato**: overlap new notes so a mono synth glides instead of
  retriggering its amp envelope.

## 8. Sattari Arp — MIDI arpeggiator
**Where:** insert as a MIDI effect / MIDI plugin **before** the synth in a
MIDI chain. AU MIDI FX in Logic; "MIDI Effect Rack → MIDI Effects" in Ableton.
**First 30 seconds:**
- Hold a chord. The arp outputs the notes.
- **Pattern:** Up / Down / Up-Down / Down-Up / As Played / Random / Chord Pulse.
- **Rate:** 1/4 to 1/32 (with triplets).
- **Scale Root + Scale** (8 scales) quantize the output to a key.
- **Host Sync** on for transport panic + loop-reset.
- Sample-accurate timing — contract test asserts 0-sample spacing error.

## 9. Sattari StemDeck — 4-deck DJ remix
**Where:** Standalone macOS app (no AU). VST3 also available.
**First 30 seconds:**
- Open the Standalone.
- Drag a full mix onto Deck A, B, C, or D.
- Mix with per-deck fader, EQ, filter, and the A/C ⇄ B/D crossfaders.
- **AI Split** runs Demucs (configure with `scripts/setup_stemdeck_demucs.sh`).
- **Record WAV** captures the performance.
- Truth label: deepest DSP work is on the separate `stemdeck` worktree.

---

## Cross-plugin "Send to Tune"

Place a Sattari Auto Pitch on any vocal track. Place a Sattari Royal Chain on
any vocal track. Toggle Royal Chain's **Sync Key from Auto Pitch** on. Royal
Chain's Tune module now follows Auto Pitch's live detected key — one detector
steers the project. (Backed by a JSON file under
`~/Library/Application Support/Sattari Audio/cross-plugin/`.)

## Tester feedback

Until a dedicated channel is set up, report issues on the project GitHub or
via email to the address in `README.md`.
