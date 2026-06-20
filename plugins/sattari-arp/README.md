# Sattari Arp v0.1

A real MIDI-effect arpeggiator plugin intended to sit before any synthesizer in a DAW MIDI/audio-instrument chain.

## Formats

- AU MIDI FX/component
- VST3 MIDI effect

## Controls

- Rate: 1/4, 1/8, 1/8 triplet, 1/16, 1/16 triplet, 1/32
- Pattern: Up, Down, Up-Down, Down-Up, As Played, Random, Chord Pulse
- Octaves: 1-4 octave expansion
- Gate, Swing, Velocity mode, Fixed Velocity, Humanize, Transpose
- Latch held notes and optional Input Thru

## Build

```bash
cmake -S plugins/sattari-arp -B build/sattari-arp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/sattari-arp --config Release
```

JUCE copies the AU/VST3 to the user plugin folders after build.

## Logic-style use

1. Add an instrument/synth track.
2. Insert **Sattari Arp** as a MIDI FX / MIDI effect before the synth.
3. Hold a chord. The plugin outputs generated MIDI notes into the synth.

Truth label: internal alpha MIDI plugin, not sale-ready. It still needs DAW listening tests, UI polish, signed installer/notarization, and broader host compatibility checks.
