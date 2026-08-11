---
type: host-compatibility-matrix
product: auto-pitch
candidate_revision: 7fef5442
date: 2026-08-10
gate: 4
truth_label: internal validation only; nothing here is a shipping claim
status: NOT STARTED
---

# Auto Pitch — Gate 4 host matrix

Frozen candidate **7fef5442**. If the plugin is rebuilt mid-matrix, every result
above that point is void and the matrix restarts. That is the whole point of
freezing.

Only claim a host that was actually tested. A blank box is a blank box, not a
pass.

---

## STEP 0 — Clean the plugin folders FIRST

**This is not optional and it is not housekeeping.** Stale bundles are installed
and they will appear in every DAW's plugin list next to the real one:

| path | modified | identity |
|---|---|---|
| `~/Library/Audio/Plug-Ins/Components/AutoPitch.component` | 2026-04-28 | `aufx OGTA OGT_` |
| `~/Library/Audio/Plug-Ins/VST3/AutoPitch.vst3` | 2026-04-28 | — |
| `~/Library/Audio/Plug-Ins/VST3/AutoPitch.component` | 2026-04-10 | **a .component inside the VST3 folder** |
| `~/Library/Audio/Plug-Ins/Components/Sattari Auto Pitch.component` | 2026-08-10 | `aufx Ap01 Satr` ← the candidate |
| `~/Library/Audio/Plug-Ins/VST3/Sattari Auto Pitch.vst3` | 2026-08-10 | ← the candidate |

The April bundles carry a **different** identity (`OGTA`/`OGT_`), so they are not
an identity collision — they are a different product or a much older build. The
hazard is simpler and worse: a DAW list showing both "AutoPitch" and "Sattari
Auto Pitch" invites testing the wrong one for four hours. The `.component` sitting
in the VST3 directory is also simply in the wrong place and some hosts log a scan
error on it.

**These have NOT been deleted.** They may be another product. Confirm what
`OGTA`/`OGT_` is before removing anything.

- [ ] Confirmed what the April `AutoPitch` bundles are
- [ ] Moved or removed them (or explicitly decided to keep and can tell them apart)
- [ ] `killall -9 AudioComponentRegistrar` and rescanned in each host
- [ ] Each host's list shows exactly ONE Sattari Auto Pitch per format

---

## Hosts

Checked on this machine:

| host | present | format under test |
|---|---|---|
| **Logic Pro** (`Logic Pro Creator Studio.app`, v12.0, native arm64) | yes | AU |
| **Ableton Live 12 Suite** | yes | VST3 |
| **FL Studio 2026** | yes | VST3 |
| **REAPER** | **NOT INSTALLED** | — |

The plan named Logic / Ableton / REAPER. REAPER is absent and FL Studio is
present instead. **Install REAPER anyway** — it is free to evaluate, and it is
the most valuable third host precisely because it is the most permissive: it
will run configurations the other two refuse, including odd buffer sizes and
sample rates, which is exactly what the documented operating envelope needs
poked at. FL Studio is a useful fourth, not a substitute.

- [ ] REAPER installed, or its absence recorded as a matrix limitation

---

## The two Auto-Pitch-specific tests that matter most

Everything below the line is standard host hygiene. These two are where this
plugin specifically is likely to fail, and both are easy to get wrong.

### A. Latency compensation — the null test

Auto Pitch reports latency (2048 samples at 48 kHz on the Studio path, measured
2080, 0.67 ms delta). If the host's delay compensation is not honoured, the
tuned vocal sits ~43 ms late against the rest of the session and nobody notices
until the mix feels wrong.

Do this, per host, and do not accept "it sounds fine":

1. Two identical tracks of the same vocal file, sample-aligned.
2. Auto Pitch on track 2 only, **Amount = 0** (or Enable Correction off) so it
   is a pure pass-through that still reports its latency.
3. Invert polarity on track 2. Solo both.
4. **PASS: silence, or below -60 dBFS.** Anything audible means the host is not
   compensating and the number is how far out it is.
5. Repeat with Amount = 100 and confirm the ALIGNMENT is still correct even
   though the audio now differs — check the transient positions, not the null.

- [ ] Logic  null: ______ dBFS
- [ ] Ableton null: ______ dBFS
- [ ] REAPER  null: ______ dBFS
- [ ] FL      null: ______ dBFS

### B. The AutoKey confidence gate — new behaviour in this candidate

As of `8c2ce30`, Auto Pitch **withholds** a detected key from the session bus
when the detector's margin is below 0.10. On a solo vocal the margin is
routinely 0.01-0.07, so the correct behaviour is now **no key published at all**.

This will look like a regression to anyone who does not know. Verify it is
working AS DESIGNED:

1. One Auto Pitch on a solo vocal, AutoKey ON.
2. A second Sattari plugin that follows song key (Harmony or Maqam), Follow Song
   Key ON.
3. **PASS: the follower does NOT adopt a key, and keeps its own.**
4. Auto Pitch's own Key Detection tab should show a low confidence percentage
   rather than 0% (the tab used to display the pitch tracker's confidence, which
   was the wrong quantity entirely — fixed in this candidate).
5. Repeat on a full instrumental where the margin should clear the gate, and
   confirm the key DOES propagate.

- [ ] Logic    solo-vocal withheld ___  instrumental propagated ___
- [ ] Ableton  solo-vocal withheld ___  instrumental propagated ___
- [ ] REAPER   solo-vocal withheld ___  instrumental propagated ___

---

## Per-host matrix

Repeat the whole block for each host. `n/a` is a valid answer where a host has
no such feature — say so rather than leaving it blank.

### Scan and instantiate
- [ ] Cold scan completes with no error or timeout
- [ ] Instantiates on a mono track
- [ ] Instantiates on a stereo track
- [ ] Reported latency visible in the host and matches ~2048 samples @ 48 kHz
- [ ] Remove and re-insert 5x, no crash, no leak in the host's plugin list

### Automation
All 21 registered parameters: `amount speed style scale keyRoot scaleMode
autoKey followSongKey neuralPitch liveEngine enableCorrection doNoHarm
formantPreserve vibrato classicMode vocalRange throat humanize transition
outputGain traceConfidence`

- [ ] Every parameter appears in the host's automation list
- [ ] Writing automation on `amount` is read back correctly
- [ ] Writing automation on `style` (discrete) lands on exact values, no drift
- [ ] Fast automation ramps produce no zipper noise or dropout
- [ ] Automation survives save/reload

### State
- [ ] Save project, close, reopen — every parameter restored
- [ ] Graph object edits restored (split/join/delete, 64-step undo history)
- [ ] Key Map restored
- [ ] Duplicate the track/instance — the copy carries the same state, and the
      two instances do not interfere
- [ ] Load a preset, save project, reopen — preset state persists
- [ ] Open a project saved with the PREVIOUS build, confirm no state loss

### Sample rate and buffer — verify the documented envelope

The envelope is **44.1/48 kHz at 128 samples or above**. Measured CPU p95
against block budget: 96 kHz at <=128 samples FAILS (0.4-0.8x headroom), and 64
samples is marginal at every rate. This is documented, not a surprise. What the
host matrix must establish is that it **fails gracefully** — dropouts, not
crashes, not silence, not corruption.

| rate | buffer | expected | Logic | Ableton | REAPER | FL |
|---|---|---|---|---|---|---|
| 44.1k | 128 | clean | | | | |
| 44.1k | 512 | clean | | | | |
| 48k | 128 | clean | | | | |
| 48k | 256 | clean | | | | |
| 48k | 512 | clean | | | | |
| 48k | 1024 | clean | | | | |
| 48k | 64 | marginal | | | | |
| 96k | 512 | marginal | | | | |
| 96k | 128 | **overrun expected** | | | | |
| 96k | 64 | **overrun expected** | | | | |

- [ ] Change sample rate with the plugin loaded and playing — no crash
- [ ] Change buffer size with the plugin loaded and playing — no crash
- [ ] At the failing cells: dropouts only. No crash, no NaN, no stuck silence.

### Transport
- [ ] Tempo change during playback
- [ ] Tempo automation across a section boundary
- [ ] Transport jump (locate) mid-note — no stuck pitch, no burst
- [ ] Loop across the loop point repeatedly — no drift, no click at the seam
- [ ] Start playback from mid-note rather than from a phrase start

### Render
- [ ] Offline bounce completes
- [ ] Realtime bounce completes (where the host offers it)
- [ ] **Offline and realtime bounces of the same region match** — null them
      against each other; PASS is below -60 dBFS. Studio modes use a
      non-realtime path, so this is where a divergence would hide.
- [ ] Freeze / flatten the track, confirm the frozen audio matches the live path
- [ ] Bounce is sample-aligned with the session (this is test A again, offline)

### Bypass
- [ ] Host bypass produces the dry signal
- [ ] Host bypass is still LATENCY COMPENSATED (a bypass that changes alignment
      is a common and nasty bug)
- [ ] Plugin's own enable/disable behaves the same
- [ ] Toggling bypass during playback produces no click

### Editor
- [ ] Open and close the editor 20x during playback — no crash, no leak
- [ ] Editor reflects automation moving in real time
- [ ] Editor reopens with the correct state after project reload
- [ ] Resize / scale, and on an external monitor at a different DPI

### Multiple instances
- [ ] 8 instances, all processing — note host CPU: ______
- [ ] 16 instances — note host CPU: ______
- [ ] 16 instances with automation running on all of them
- [ ] The session still saves and reopens with 16 instances
- [ ] No crosstalk: instance 1's key/settings do not leak into instance 2
      (relevant because the session bus is shared — only ONE instance should be
      publishing)

---

## Verdict

| host | scan | latency | automation | state | rates | transport | render | bypass | editor | multi | VERDICT |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Logic | | | | | | | | | | | |
| Ableton | | | | | | | | | | | |
| REAPER | | | | | | | | | | | |
| FL Studio | | | | | | | | | | | |

    GATE 4:  PASS / BLOCKED / NOT VERIFIED

Blockers found:

```

```

Notes and anything surprising:

```

```
