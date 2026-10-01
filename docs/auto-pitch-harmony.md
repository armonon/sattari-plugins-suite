# Auto Pitch harmony

The harmony engine renders up to four vocal voices from a monophonic singer.
It is not a multitrack vocal separator or the polyphonic instrumental tuner.
Whole-mix Transpose and active Poly tuning take precedence over vocal harmony.

## Choosing notes

- Intelligent uses chord evidence when available, otherwise the selected key/scale.
  Auto Key's global key alone is not a chord progression.
- Interval uses scale degrees; in Chromatic each degree is a semitone.
- MIDI uses up to four held notes. Targets outside the renderer's one-octave
  range are octave-folded, preserving their pitch class instead of clipping to
  a different note. The final range is relative to the original vocal, including
  after large lead-note edits. This is not unrestricted multi-octave rendering.

Each voice has enable/solo, role, interval/octave, level, pan, delay, detune and
vibrato. Global controls add glide, pitch/timing humanization, filtering and a
feed-forward stereo ensemble. Voice Only auditions the generated parts.
Lead Correction Off and Amount zero retain the selected Live tracker, so they
can still generate harmony without tuning the lead. With harmony disabled, the
lead continues to use the latency-aligned dry reference.
Humanization updates on the sample clock every 180 ms, independently of host
buffer size. Parameter changes and source-detector updates still arrive through
the host/processor; renderer block-invariance tests do not imply that every DAW
automation schedule or detector configuration produces identical output.

## Editing H1–H4

Capture lead notes, enable the desired harmony voices, then use Edit:

- Drag an H1–H4 region vertically to set that voice's pitch without moving the lead.
- Shift-click a harmony region to mute it; Shift-clicking a muted region returns
  it to automatic generation. Undo restores the previous explicit pitch.
- Right-click it to restore automatic generation.
- Hold Alt to select a harmony overlapping the lead.

The edits participate in undo/redo, project recall and lead-note splits. Timing
follows the parent lead note; these are independent pitch/mute overrides, not
independently timed clips or stem exports. Targets remain subject to the vocal
renderer range above. The harmony readout comes from the audio renderer and
includes the active overrides; it is a target display, not a pitch measurement
of every instant of glide or vibrato.

## Per-voice formants

The Formant slider shifts each voice's spectral envelope from −12 to +12
semitones without intentionally changing its fundamental pitch. It is available
only in Studio when the existing lead-engine alignment has enough room for the
1024-sample formant stage. The stage consumes that alignment budget rather than
adding reported latency. Live disables the control and retains its saved value
for a return to Studio. No low-latency formant capability is claimed.

The new parameters are appended to the parameter list to preserve existing
automation indices. Older projects without these parameters load neutral
formants, including when loaded into an already edited instance.

## Qualification boundary

Contracts cover target selection, four-voice rendering, deterministic reset,
callback-size-independent modulation, formant direction/fundamental preservation,
editor gestures, audio/readout agreement and state recall. The local corpus
renderer exercises cleared real vocals without uploading them. Finite samples,
no clipping and audible output are safety checks, not a naturalness score.

Human listening, target-DAW automation/transport/state checks, and a matched
commercial comparison remain necessary before a release-quality claim. New
candidate changes do not inherit the qualification of an older binary.
