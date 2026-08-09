# Sattari Audio Suite — internal alpha quick start

Truth label: **controlled internal alpha; not public, notarized, or sale-ready**.
The suite is six customer-facing products backed by 48 implementation plugins.
Only Compass, Auto Pitch, Arp, and Sub Conjurer are in the current installable
alpha. The suite is GPLv3 under the current JUCE/Rubber Band dependency terms;
see `LICENSING.md` before making a commercial distribution decision.

## Install the current alpha

1. Build the four shipping plugins and run their machine validation gates.
2. Build the internal package with `scripts/package_suite_installer.sh`.
3. Install the package, then open **Sattari Hub**.
4. The Hub shows six products. Expand **Components** to see lifecycle and format
   details; only alpha-qualified components expose an Install action.
5. Restart the target DAW after installing AU or VST3 bundles.

The Hub is the authority for what is installable. A visible experimental
component is not a claim that the component is finished or downloadable.

## 1. Sattari Compass — knows the session

Compass is the session authority: it detects major/minor key and reference
tuning, reads host tempo, and publishes that evidence together with the chord,
groove, maqam, section, and energy context the user explicitly approves.

- Insert Compass early in the session and feed it representative program audio.
- Confirm the detected musical context rather than treating detection as
  infallible.
- Let following products consume the approved session context.
- Sattari Key, Tuner, and Scope are components of this product direction, not
  separate top-level products.

Current state: **alpha**. Machine contracts pass; manual host and musical-corpus
validation remain release gates.

## 2. Sattari Auto Pitch — tunes the performance

Auto Pitch provides realtime monophonic correction, graph editing, note-object
controls, formant/vibrato controls, MIDI/reference control, and key-map support.
Sattari Maqam extends the direction into microtonal tuning.

- Insert on an isolated vocal or monophonic instrument.
- Set the voice/instrument range before tuning.
- Select or follow the correct key and scale.
- Start with slower correction and preserved formants; increase retune strength
  only after confirming tracking stability.
- Use Edit mode for deliberate note-level repair instead of forcing difficult
  phrases through aggressive realtime correction.

Current state: **alpha**. Synthetic accuracy and safety tests pass, but a scored,
owned real-vocal corpus and manual DAW listening matrix are still mandatory.

## 3. Sattari Instruments — makes musical material

Instruments is the playable/generative family: Stack, Arp, Chop, Granulator,
Rhythm Genie, Scaler, VoxKey, and VoxSynth.

Current installable component: **Sattari Arp (alpha)**.

- Place Arp before an instrument in a MIDI chain.
- Hold a chord, select the pattern and rhythmic rate, then enable host sync.
- Use root and scale quantization when the pattern must follow Compass context.
- Validate MIDI routing and loop-reset behavior in the target DAW before using
  it in a live or critical session.

Stack is the intended primary product shell, but remains a **preview**. The
other Instruments components are experimental.

## 4. Sattari Vocal — finishes the voice

Vocal is an eight-slot finishing rack covering cleanup, tuning handoff,
dynamics, tone, doubling, harmony, character, and space. Royal Chain, Harmony,
Vocal Master, Double, De-Ess, Face, and Glass Choir are component technologies
behind that rack.

Current state: **preview**. Do not treat the rack or its experimental components
as qualified deliverables. The next proof is an owned vocal corpus spanning
clean, breathy, raspy, low, high, vibrato, consonant-heavy, and noisy material.

## 5. Sattari Mix — controls dynamics and tone

Mix is the steady, context-aware production rack: EQ, compression, cleanup,
transients, limiting, imaging, metering, bus tone, alignment, gating,
multiband control, sidechain control, and low-end synthesis.

Current installable component: **Sub Conjurer (alpha)**.

- Insert Sub Conjurer on kick or bass material.
- Choose pitch follow, fixed note, or MIDI control.
- Add sub conservatively and monitor below 100 Hz on suitable playback.
- Check phase, translation, silence gating, and pitch tracking on the actual
  source before committing the result.

The Mix rack is still a **preview**; most component technologies are
experimental and must earn their place through listening tests.

## 6. Sattari Create — transforms in key and time

Create is the musical-effects rack: Morph, Warp, Space, Vocoder, Shimmer, Echo,
Heat, Riser, Pulse, 808, and Warble. Its product thesis is that creative effects
follow approved key, tempo, section, and groove context from Compass.

Current state: **preview**. The direction is differentiated, but it needs a
small set of unmistakable signature workflows, stronger macro interaction, and
musical A/B evidence before it becomes a product rather than a collection.

## Required tester record

For every alpha session, record:

- exact plugin build and source commit;
- DAW/version, plugin format, macOS version, and Apple Silicon/Intel path;
- source type and whether the audio is owned or cleared for testing;
- crash, CPU, latency, artifact, timing, recall, and automation results;
- a short musical judgment: better, neutral, or worse, with the reason.

Report reproducible issues through the project tracker with the smallest source
clip and session needed to demonstrate the problem. Do not upload uncleared
commercial recordings.
