# Sattari suite flow

The suite is sold and tested as six musical jobs, not as a menu of 48 unrelated
components. Every product must still work alone; Compass adds shared context rather
than becoming a hard dependency.

## The default session

1. Put **Compass** on the master or a dedicated analysis bus. Confirm key, tempo,
   tuning, section, groove and energy. Compass owns the short authority lease.
2. Use **Auto Pitch** on sources that need correction. Start in Live mode while
   tracking, move to Studio or the graph only when the take needs deeper editing.
3. Build playable material in **Instruments**: Stack for layers, Rhythm Genie for
   drums, Chop/Granulator for samples, and the standalone voice/MIDI tools where
   their host type requires a separate plugin.
4. Finish vocals in **Vocal**. The `Vocal Studio` preset is the complete path:
   Clean → Gate → De-Ess → EQ → Comp → Heat → Double → Space. Auto Pitch,
   Harmony, Maqam and Morph stay standalone because their editing/latency models
   do not fit a lightweight rack slot.
5. Shape channels and buses in **Mix** with the shared module DSP. The standalone
   utility and analysis plugins remain available for focused work and metering.
6. Add movement in **Create**. Key- and tempo-aware modules follow only the context
   dimensions the user enables; no module should change timbre merely because
   Compass is present.

## Multi-project sessions

Set `SATTARI_SESSION_ID` before launching a DAW to isolate its Session Bus files.
For example:

```sh
SATTARI_SESSION_ID=album-a open -a "Logic Pro"
```

With no id, the suite intentionally retains the legacy global bus filenames. The
transport is file-backed and polled off the audio thread; DSP consumers receive a
coherent lock-free snapshot.

## Product-quality contract

Each of the six products must clear the same ladder before a public build:

- deterministic DSP, state, oversized-block and rack/transport contracts;
- AU and VST3 compilation plus format validation on Apple silicon and Intel;
- automation, latency, bypass, multi-instance and project-reopen sessions in the
  supported DAWs;
- real-voice and real-instrument listening corpora at multiple sample rates and
  buffer sizes;
- signed and notarized installer, clean-machine install/uninstall, and rollback;
- versioned evidence for every model, benchmark and public performance claim.

AAX, ARA host qualification and multi-track graph editing are separate release
programs. They should not block the core six-product beta, and should not be
advertised until their SDK, host and interaction evidence exists.
