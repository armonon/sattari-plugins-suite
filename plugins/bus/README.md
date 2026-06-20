# Sattari Bus

Truth label: **internal plugin build** (source + DSP scaffold). Builds AU/VST3/Standalone via the
shared packaging path; not DAW-listened or sale-ready yet.

A bus / aux channel strip in the spirit of a Logic bus, but with far more processing on the
channel itself. Stereo in/out, one instance per bus.

## Controls

- **Input Trim** — gain into the strip.
- **HPF / LPF** — one-pole high-pass and low-pass to top-and-tail the bus.
- **Low / Mid / High** — 3-band tone (low shelf @ 120 Hz, bell @ 1 kHz, high shelf @ 8 kHz).
- **Glue compressor** — Threshold, Ratio, Attack, Release, Makeup. Stereo-linked, soft.
- **Drive** — normalised soft saturation for bus "glue"/colour.
- **Width** — mid/side stereo width (0 = mono, 1 = unity, 2 = wide).
- **Pan** — equal-power balance.
- **Mix** — parallel blend with the untouched input (parallel compression / New York style).
- **Output / Ceiling** — output gain and a soft output ceiling.
- **Mute / Bypass**, plus four **character modes** (Clean, Glue, Console, Tape) that nudge the
  comp + drive feel.

## Build

```bash
cmake -S plugins/bus -B build/bus -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/bus --config Release -j 4
./scripts/validate_plugin.sh build/bus/SattariBus_artefacts/Release aufx Bus1 Satr
```

## Current limitations

- DSP is a first pass: one-pole HPF/LPF (gentle 6 dB/oct), fixed EQ band frequencies, and a
  simple soft-knee glue compressor. The drive stage is now **4x oversampled** (only the
  nonlinearity), with the parallel-mix dry latency-compensated and the oversampler latency
  reported to the host. Needs a real-material listening pass before any sale-ready claim.
