# Auto Pitch benchmark WAV replacement

Selectively integrates the benchmark-file fix from historical commit `c63d63c0a1633986579f1e21a3536058a36e190d`. Repeating `--output` now replaces the existing WAV instead of appending another RIFF document. An unreplaceable existing path returns failure rather than silently appending.

The actual `AutoPitchBench` target built successfully with the current arm64 Auto Pitch configuration. Two renders to the same path each contain exactly 144,000 frames at 48 kHz, one RIFF document, identical file sizes and identical SHA-256. A nonempty output directory is refused with exit 1, preserving its sentinel. Binary hash and render hashes are in qualification.json. No DSP changes or historical golden fingerprints are imported by this port. The resampling/FIFO changes from that original commit still require a separate port and qualification.
