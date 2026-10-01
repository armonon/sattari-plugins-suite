# Modular Release Gate

Version 0.1.1 is a qualification candidate, not an approved public release.
Only an exact package with current passing evidence can be considered for launch.
The manifest identifies the source, runtime-tested architectures, signatures,
individual file hashes, and checks actually run. No platform is inferred from
the existence of a CMake target.

## Changes From 0.1.0

- Instrument moves and duplicates carry the source audio, with undo/redo.
- Reordering shares immutable PCM payloads across undo history.
- Factory presets restore their own source; duplicates capture macro settings.
- Sample imports start a new undo session so old undo entries cannot silently
  restore an earlier imported file.
- Bounded binary-state parsing rejects excessive depth, allocation lengths,
  truncation, trailing bytes, and invalid property/parameter types.
- Disk preset saves check write and flush errors before replacing the old file.
- Host reset drains stale keyboard events; state recall resets DSP histories.
- Bypass parameters report canonical boolean values under fractional host automation.
- An all-notes-off button releases stuck MIDI notes.
- The numeric peak meter repaints during playback.
- Pulse no longer constructs choice-label strings on the audio callback.
- The FX standalone declares macOS audio-input permission and its signed build
  carries the audio-input entitlement. Permission is still granted by the user.
- Additional malformed-state, source-ownership, MIDI, automation, allocation,
  offline-render, and old-preset regression checks run in both native products.

## Unresolved Distribution Decision

The repository's `LICENSING.md` describes GPLv3, but the pinned JUCE 8.0.12
`LICENSE.md` specifies AGPLv3 or JUCE commercial terms. These documents must be
reconciled with the owner's actual licensing arrangement before distribution.
No project license is changed by this hardening work. The candidate carries the
existing notices and the exact source snapshot for review; that is not a legal
compliance certification.

Primary terms: https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md
and https://juce.com/legal/juce-8-licence/.

## Required Acceptance

| Gate | Evidence Required |
| --- | --- |
| Native engine and UI | Both fresh contract suites, no skips or failures |
| Host API | Strictness-10 pluginval for each VST3 architecture; auval for each Mac AU architecture |
| Windows | Native x64 build, tests, validation, installation, and host sessions |
| macOS | Universal bundle verification, Developer ID signing, notarization, clean-machine installation |
| Supported hosts | A completed named DAW/version/OS/format matrix below |
| Performance | Long real-session dropout, callback deadline, memory, and multi-instance measurements |
| Listening | Owner-approved listening comparisons and musical parameter sweeps |
| Licensing | Confirmed distribution terms and complete required notices/source |

Rosetta is useful Intel coverage but does not replace a physical Intel Mac.
Allocation tests count C++ new/new[] on the callback thread; they do not prove
the absence of all C malloc calls, operating-system locks, or deadline overruns.
Automated wall-time profiles are diagnostic, not performance guarantees.
No Windows binary or DAW approval is claimed unless supplied with that evidence.

## DAW Session Checklist

Record host/version, OS/hardware, format, sample rate, block size, package hash,
tester, date, result, and reproduction details for any failure.

1. Scan, instantiate FX and Instrument, and open/close/resize their editors.
2. Build chains with duplicate modules, bypass, return routing, and automation.
3. Load distinct samples into both layers; reorder, duplicate, undo, and redo.
4. Exercise keyboard, sustain, pitch bend, dense MIDI, and all-notes-off.
5. Change scenes and A/B; save the DAW session, close, reopen, and compare sound.
6. Reopen a 0.1.0 session and preset; verify IDs, controls, sources, and automation.
7. Loop and seek transport, change tempo and devices, freeze, and offline-bounce.
8. Exercise mono/stereo, 44.1/48/96 kHz, small and large buffers, and many instances.
9. Run a sustained session and record dropouts, peak callback time, and memory.
10. Install, upgrade, and roll back the exact package on a clean supported machine.

No output limiter is present. Evaluate loud/high-feedback chains at low monitor
levels. External plugin binaries, arbitrary patch cables, external sidechains,
automatic mixing, and full deep module editors are outside this version's scope.
