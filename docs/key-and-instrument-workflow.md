# Song-key context, instruments and beat matching

## Click a DAW clip and analyse it

Yes, when the DAW exposes that audio through ARA. Select one audio region linked
to the plugin's ARA document, then press **Scan DAW clip**. A normal AU/VST3 insert
cannot browse arbitrary arrangement selections. The current integration reads
the entire selected, trimmed, unwarped clip, before host effects. It does not
limit the scan to the playhead's current moment. A short selected clip cannot
provide the harmonic context of the rest of the arrangement.

See [arrangement-clip-analysis.md](arrangement-clip-analysis.md) for the host,
source-processing and persistence limitations. The user's DAW/version and real
host qualification are still required; this is not a newly installed release.

## Hear more of the song before choosing a key

- File and arrangement scans already analyse the entire supplied source before
  publishing a completed result.
- Normal **Listen live** now requires **15 seconds** of new, non-silent audio
  actually analysed. **Song context** adjusts this from **5 to 60 seconds**.
  The UI shows progress and does not enable a premature detected-key lock.
- The running song estimator accumulates context beyond that minimum. Repeated
  snapshots of the same data and overlapping FFT samples no longer count as
  additional song evidence. Silence does not satisfy the minimum.
- **Dynamic Key** is a different task: tracking intentional changes during a
  performance. Its existing shorter evidence horizon remains separate. It does
  not wait for the whole-song context control.
- **Re-scan** resets the context. A manually chosen key is an explicit decision,
  so it does not need to wait for automatic detection.

More context reduces premature decisions; it is not proof of a correct musical
key. A sustained chord, modal music, relative major/minor ambiguity or a genuine
key change can still need review. For exact, comprehensive source coverage,
prefer a complete file/clip scan to a live worker's sampled view of the stream.

## Two different kinds of tuning

### Single-note instruments

Use **Instrument - Bass**, **Instrument - Mid** or **Instrument - High**. These
select the existing instrument detector ranges and print correction path without
vocal-formant preservation or generated harmonies. Choose the target key or
enable **Follow Song** to receive Sattari Key's published key. These workflows
are for monophonic sources such as bass lines, leads or isolated melodic stems.

The separate **Poly Scale Lock** shortcut now preserves the selected key source
instead of disabling Follow Song and enabling input AutoKey. Its tooltip makes
the per-peak/shared-harmonics limitation explicit, and it is disabled while
whole-mix transpose takes precedence.

For Poly's supported controls, measured-delay contract, harmonic grouping and
remaining limitations, see [Polyphonic instrumental correction](polyphonic-tuner.md).

### A complete beat, loop or chordal instrument

Use **Beat / Sample Transpose**, then **Match Key** in the Advanced drawer.
This uses the existing whole-mix Rubber Band path, not monophonic correction or
the experimental per-peak polyphonic retuner.

1. Analyse the **reference song** in Sattari Key; review and **Lock session** to
   publish the target. Use one reference publisher.
2. In Auto Pitch, scan/drop the **original beat being moved**. Its completed,
   confident scan supplies the source key. Do not scan the reference here unless
   that is also the material being shifted.
3. Enable **Follow Song**, choose **Beat / Sample Transpose**, and click
   **Match Key**. Without Follow Song, an explicit manual major/minor key can be
   the destination; AutoKey and map following must be off to avoid an ambiguous
   source-versus-target choice.
4. Match Key sets a fixed nearest semitone interval. Example: C minor source to
   D minor target is **+2 semitones**. The interval survives project recall;
   rescanning is needed to perform a new match after reopening.
5. Click Match Key again after changing the target or replacing the source.
   This is not an automatically moving target-follow transpose. The semitone
   slider remains available for a deliberate manual shift.

The reference and source are separate facts. If Sattari Key scans the same beat
and declares its existing key as the destination, a zero-semitone shift is
correct. Lock the reference before shifting; do not dynamically derive the
target from that same track's already-transposed output.

Safety checks reject unfinished/unconfident sources, missing/expired session
targets, chromatic/custom destinations and major/minor mismatches. Failure leaves
the previous shift unchanged. Turning transpose on does not silently run a match.

A fixed shift preserves chord intervals and duration, but shifts drum timbre too.
The existing shifter's transient handling and stereo behavior still need listening
on representative material; these changes do not add a new beat-optimized DSP engine.
It cannot change C minor to D major, repair individual clashing chord tones, or
leave drums untouched while moving the melody. Those operations need isolated
stems, note editing or a substantially different separation engine. A drum-only
loop may have no meaningful major/minor key at all.

## Verification

Local validation on **2026-09-16**: all **12** tests in the command below passed
on Apple Silicon (11.51 seconds total). Both plugin shared-code libraries built
for arm64 and x86_64; Intel runtime testing and actual DAW qualification remain
pending. The minimum-size Key editor and Auto Pitch's Advanced drawer were
rendered from the real UI and visually checked.

The C-major beat fixture's three tones moved to D major, with measured steady
pitch errors below 1.2 cents. Harmonic-rich monophonic 110/220/880-Hz target
fixtures started 35 cents sharp and measured below 1 cent residual after settling.
These numbers apply only to those synthetic fixtures and analysis settings;
they do not establish accuracy or perceived quality on arbitrary instruments.

`SongKeyLiveContextSmoke` exercises silence, the 15-second gate, repeated
snapshots, lock safety, rescan and saved context settings. `AutoPitchSourceMatchSmoke`
uses the real source-analysis pipeline, checks target routing and rejection
cases, and measures rendered chord transposition plus three synthetic instrument
registers. It also selects the new presets through the real editor and verifies
that Poly Scale Lock retains the suite-key setting. These are engineering checks,
not listening scores or an accuracy
benchmark on arbitrary songs.

Build shared code and test tools only (no installation/signing):

```sh
cmake --build build-dynamic-key --target SongKeyRender AutoPitchCoreContract \
  SongKeyARAContract AutoPitchARAContract --parallel 4
SATTARI_SESSION_ID=key-source-tests ctest --test-dir build-dynamic-key \
  -R '^(SongKeyLiveContextSmoke|AutoPitchSourceMatchSmoke|SongKeyRenderSmoke|SongKeyARAContract|AutoPitchARAContract|SongKeyArrangementClipSmoke|AutoPitchArrangementClipSmoke|SongKeyFileAnalysisSmoke|SongKeyDynamicFollowSmoke|SongKeyLiveTempoSmoke|AutoPitchCoreContract|SattariCrossPluginBusContract)$' \
  --output-on-failure
```

The pitch-shifting algorithms and reported latency are unchanged in this update;
the changes concern analysis context, source/target routing, commands and presets.

Primary references: [Celemony ARA overview](https://www.celemony.com/en/10-years-of-ara),
[Rubber Band technical description](https://www.breakfastquay.com/rubberband/technical.html).
