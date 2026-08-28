# Performance evidence

"Seems faster" is not evidence. Record baseline and candidate together.

Collect where applicable: median, p95, p99, max, allocation count, memory,
startup, per-instance CPU, multi-instance CPU — plus the sample rate and block
size each figure was taken at, since neither is meaningful without them.

A measurable regression must be disclosed, not omitted.

Open items carried over from `validation/auto-pitch/2026-08-09-objective-benchmark.md`:

- Studio lock realtime CPU — RTF 0.607, p95 19.873 ms  (warning)
- Studio + Neural realtime CPU — RTF 0.632, p95 18.631 ms  (warning)
- Both need re-measuring across 44.1/48/96 kHz and 64/128/256/512 samples.

Naming: `YYYY-MM-DD-<product>-<scenario>.md`
