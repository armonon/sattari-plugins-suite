# Sattari Audio Suite — internal alpha quick start

Truth label: **controlled internal alpha; not public, notarized, or sale-ready**.
The suite is six customer-facing products backed by 48 implementation plugins.
All six primary products are in the current installable alpha; Arp and Sub
Conjurer remain included as qualified component plug-ins. The suite is GPLv3
under the current JUCE/Rubber Band dependency terms;
see `LICENSING.md` before making a commercial distribution decision.

## Install the current alpha

1. Build the six primary products and run their machine validation gates.
2. Build the internal package with `scripts/package_suite_installer.sh`.
3. Install the package, then open **Sattari Hub**.
4. The Hub shows six products. Expand **Components** to see lifecycle and format
   details; only alpha-qualified components expose an Install action.
5. Restart the target DAW after installing AU or VST3 bundles.

The Hub is the authority for what is installable. A visible experimental
component is not a claim that the component is finished or downloadable.

## 1. Sattari Compass — knows the session

Compass is the session authority: it detects major/minor key, conservative
major/minor chord evidence, reference tuning, energy and musical onsets; reads
host tempo; and publishes that evidence together with the groove, maqam,
section, time-signature and explicit overrides the user approves.

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
- For a set that genuinely modulates, place **Sattari Key** on the music/master
  feed, choose **Listen live**, enable **Dynamic follow**, and set a 1–5 second
  scan window. Leave Auto Pitch's **Follow Song Key** on for the singer. Three
  seconds is the recommended balance; one second reacts fastest but has less
  harmonic evidence. A strong new key moves after one complete window, a
  marginal new key must repeat in two windows, and silence or ambiguity retains
  the last trusted key. The suite reader adds up to roughly 0.4 seconds after an
  accepted change; this does not add audio latency to the vocal path.
- Start with **Natural** for expressive correction or **Pitch Center** for a
  clean, stronger pull. Use **Hard Tune** only when the snap is intentional.
- For monitored performance, enable **LIVE + 2MS** for the measured 2.00 ms
  Extreme path. Disable 2MS for the more exact 11.90 ms Ultra Quality renderer,
  or choose Range for the 70 Hz floor at 21.60 ms.
- Factory workflows are deterministic: changing workflow clears incompatible
  MIDI, graph, transpose, polyphonic and generated-vibrato modes instead of
  silently inheriting them from the previous choice.
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

Stack is the primary product shell and is now an **internal alpha**. Its layer
contract and AU/VST3 host gates pass; musical source-loading and full-project
listening remain required. The other Instruments components are experimental.

## 4. Sattari Vocal — finishes the voice

Vocal is an eight-slot finishing rack covering cleanup, tuning handoff,
dynamics, tone, doubling, harmony, character, and space. Royal Chain, Harmony,
Vocal Master, Double, De-Ess, Face, and Glass Choir are component technologies
behind that rack.

Current state: **internal alpha**. Its machine gates pass, but the next proof is
an owned vocal corpus spanning
clean, breathy, raspy, low, high, vibrato, consonant-heavy, and noisy material.
The whole rack and all ten factory workflows now pass the executable product
contract; **Dry Lead** is the focused no-space/no-width signature starting point.

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

The Mix rack is an **internal alpha**; most component technologies remain
experimental outside the rack and must earn broader exposure through listening tests.
The whole rack and all eight workflows now pass the executable product contract;
**Low-End Control** is the focused translation workflow to evaluate first.

## 6. Sattari Create — transforms in key and time

Create is the musical-effects rack: Morph, Warp, Space, Vocoder, Shimmer, Echo,
Heat, Riser, Pulse, 808, and Warble. Its product thesis is that creative effects
follow approved key, tempo, section, and groove context from Compass.

Current state: **internal alpha**. The direction is differentiated, but it still
needs musical A/B evidence and finished-project testing before public release.
The whole rack and all nine workflows now pass the executable product contract;
**Compass Motion** is the first explicit suite-native tempo-following signature.

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
