# Arrangement clip analysis — Key and Auto Pitch

## What this adds

Both editors have a **Scan DAW clip** action. In an ARA-enabled instance, the
action requests the DAW's current selection, reads the selected region's trimmed
source audio in memory, and analyses it on a background worker. No temporary
audio export, file import, playback pass, or external upload is used.

The selection must contain exactly one audio region linked to that ARA document.
The plugin cannot enumerate arbitrary audio clips in a host that does not expose
them. A conventional AU/VST3 insert is not equivalent to an ARA extension.

This is a source-code integration awaiting qualification in the user's DAW,
not a claim of universal host compatibility or a newly installed release.

For the longer live-analysis window and reference-key-to-beat workflow, see
[Song context, instruments and beat matching](key-and-instrument-workflow.md).

## Producer workflow

1. Load the ARA-enabled plugin using the DAW's ARA/region-extension workflow.
   The exact setup depends on the DAW, format, and version.
2. Select one linked audio clip in the arrangement.
3. Open the plugin and click **Scan DAW clip**. The clip name identifies the
   analysed source. Review the key, alternative estimate, confidence margin,
   tempo, and tuning offset before applying a result.
4. In **Sattari Key**, enable **Publish to Suite** if necessary and use
   **Lock session** to hold the reviewed result. Auto Pitch's **Follow Song**
   action uses the existing suite-key bus. Use one publishing Key instance per
   session, not competing publishers on several tracks.
5. Alternatively, scan directly in **Auto Pitch**. Its KEY workspace also shows
   the existing waveform, chord and key-region analysis. AutoKey can use the
   overall verdict; **Follow Song Map** uses the region map. An analysed clip's
   map is offset to its arrangement position and is inactive outside that clip.
   Low-confidence results still respect the existing trust gates.
6. Rescan after trimming, moving, replacing, or otherwise editing the clip.
   Relevant clip/source edits and clip deletion automatically revoke the old
   result. Key unlocks it and withdraws its publication; Auto Pitch clears the
   clip map and detected-key latch. Analysis is a snapshot, not a continuously
   reanalysed arrangement document.

Scanning provides key/tempo/chord analysis; it does not automatically transfer
the vocal into Auto Pitch's note editor or time-correction engine. Existing
note capture and edited-playback workflows remain separate.

## Deliberate limits

- **ARA access required.** Empty/multiple selections, unavailable audio, and
  ordinary non-ARA inserts show a limitation message. There is no fallback that
  silently scans another track.
- **Unwarped source audio, before track effects.** Time-stretch flags or differing
  source/playback durations are rejected. Host clip gain, fades, clip pitch
  processing, inserts, automation, and the final mix are not rendered into this
  analysis. Use live listening to analyse what the DAW actually plays after
  processing.
- **No MIDI clips or synthesized-instrument rendering.** The host must expose
  readable audio samples.
- **No saved ARA analysis cache yet.** Reopening a session requires rescanning;
  source readers and computed song maps are not persisted as ARA archive data.
- **ARA playback still needs host qualification.** The shared raw renderer now
  supports band-limited sample-rate conversion and mono/stereo adaptation. Native
  rate, sample-aligned playback has an exact-copy path. This is not a warp engine
  or a renderer for host effects. Real-time reads never wait for disk; a cold
  seek can underrun until read-ahead catches up. Offline rendering may wait for
  the read-ahead cache. Check playback, seeking and export in the actual DAW.
  Analysis always requests the source's native rate and channel count.

## Implementation and checks

- `SattariArrangementClip.h`: selection validation, native-rate JUCE ARA region
  reader, clip name, timeline origin and document-thread change/deletion watch.
- `SattariARADocument.h`: shared ARA factory/raw playback implementation; refreshes
  invalidated source caches after host edits and handles oversized audio blocks.
- `SattariClipPlayback.h`: preallocated random-access windowed-sinc conversion,
  with source reads clamped to the selected trim. Seeks and block-size changes
  do not carry a stale streaming phase into another position.
- Key now has conditional ARA processor/editor integration. It reuses the same
  whole-source analysis as file analysis, including the suite-key publisher.
- Auto Pitch accepts a reader-backed source, offsets key-map/harmony lookups to
  arrangement time, and refreshes its UI when a different clip has the same
  duration and region count.
- Source-reader ownership stays on the document/message thread; the worker
  borrows it until joined. Read failures abort the scan without publishing a
  partial verdict. JUCE invalidates region readers on relevant host edits; a
  separate validity flag also covers edits after reading but before publication.
- Key/Auto Pitch publications carry an optional per-instance ownership token.
  Revoking one instance's result cannot delete a newer frame from another
  instance of the same product. Existing bus readers remain compatible. This
  protects withdrawal; it does not arbitrate competing same-priority publishers.

The synthetic reader smoke tests use a 12-second C-major/120-BPM region trimmed
from a 36-second source whose surrounding audio is in F-sharp major. They check
trim boundaries, results, worker cancellation/failure, and reader ownership.
Auto Pitch additionally checks map activation at a 45-second arrangement offset.
Key additionally checks publication of the clip's reviewed key and tempo.

`SongKeyARAContract` and `AutoPitchARAContract` use each actual production factory
with an in-process ARA host to exercise selection, region reading, timeline
metadata, both real processor/editor bindings, completed-result revocation,
cancellation during host edits, clip deletion and per-instance bus isolation.
They also exercise the production buffered playback role at 44.1/48/96 kHz,
mono/stereo, oversized blocks, stopped transport, source-cache replacement and
restoration of temporarily disabled host sample access.
It is not a substitute for Logic/REAPER/etc. testing.

The converter contract covers 36 source/output sample-rate and channel cases,
variable block sizes, read failure, trim boundaries, exact native-rate copy and
high-frequency alias rejection. These synthetic checks concern raw clip
playback, not the sound of pitch correction or musical key-detection accuracy.

Local validation on 2026-09-12: both ARA-enabled shared-code libraries compiled
for macOS arm64/x86_64; all nine tests listed below passed on Apple Silicon
(7.67 seconds total). Intel execution and real-DAW qualification remain pending.
Minimum-size Key and
Auto Pitch editor renders and the real Key editor showing the ARA fixture result
were visually checked. The 120-BPM fixture was estimated at approximately
118.6 BPM (within the existing 2-BPM contract tolerance), not exactly 120 BPM.
These are integration checks, not a new key-accuracy benchmark or listening score.
The 36-case converter test measured maximum sample error `3.96281e-7` for its
1-kHz fixtures, zero difference across tested block partitions, and alias RMS
`1.18554e-7` for a 30-kHz tone downsampled from 96 to 44.1 kHz. These values are
specific to those fixtures, not a general audio-quality rating.

Build and run without packaging or installing plugins:

```sh
cmake --build build-dynamic-key --target SongKeyRender AutoPitchCoreContract \
  SongKeyARAContract AutoPitchARAContract SattariCrossPluginBusContract --parallel 4
SATTARI_SESSION_ID=arrangement-tests ctest --test-dir build-dynamic-key \
  -R '^(SongKeyARAContract|AutoPitchARAContract|SongKeyArrangementClipSmoke|AutoPitchArrangementClipSmoke|SongKeyFileAnalysisSmoke|SongKeyDynamicFollowSmoke|SongKeyLiveTempoSmoke|AutoPitchCoreContract|SattariCrossPluginBusContract)$' \
  --output-on-failure
```

The ARA contracts are available when the corresponding
`SATTARI_SONGKEY_ENABLE_ARA` / `SATTARI_AUTOPITCH_ENABLE_ARA` option is `ON` and the
SDK is present. Reader analysis smoke tests also build without ARA.

No AU/VST3 installation, signing, notarization, release, or subjective listening
approval is implied by these checks. Pitch-correction DSP was not changed by
this feature.

API references: [JUCE ARA integration](https://github.com/juce-framework/JUCE/blob/master/docs/ARA.md)
and [JUCE ARAPlaybackRegionReader](https://docs.juce.com/master/classjuce_1_1ARAPlaybackRegionReader.html).
