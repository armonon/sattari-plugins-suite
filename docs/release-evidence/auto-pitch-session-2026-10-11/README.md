# Older session reliability fixes integrated

Selectively ports the session and factory-workflow parts of OpenClaw's older
`cbd1ce4` commit into current Auto Pitch, preserving its newer note editing,
harmony parameters, waveform/timeline serialization and live pitch engine.

Legacy sessions with duplicated sections now restore their newest MIDI mappings
and deleted-note state. Saves keep one KeyMap/PitchEdits/MidiLearn section and
strip extension trees from parameter state after loading. Humanize keeps a
per-instance saved seed and restarts its walk at prepare/reset, so repeated
bounces and reopening the same saved session produce matching non-silent audio.
Factory workflows restore every per-voice harmony default, including pan,
level and delay, rather than inheriting the previous workflow's placements.

The full arm64 AutoPitchCoreContract passes with optional Signalsmith linked,
ONNX off and Rubber Band selected. New regressions reproduce affected legacy
state, check six bounded round trips, compare repeated/reopened render samples,
and exercise actual factory menu changes. Existing pitch, harmony, bypass and
noise checks pass. Source and test-binary hashes are attached.

This does not port the original correction/host-bypass crossfades or the newer
44.1kHz engine/priming repair. Those still require integration and qualification.
Windows, supported host/listening acceptance, package approval and licensing
remain separate distribution gates. No native installer has been published.
