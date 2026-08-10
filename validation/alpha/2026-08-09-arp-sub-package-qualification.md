# Arp + Sub Conjurer package qualification — 2026-08-09

Truth label: **machine and package-copy validation for internal alpha; not a
manual DAW, listening, notarization, or sale-readiness pass**.

Source commit: `f3c3e2d` (`Present suite as six products and close alpha machine gates`)

Environment:

- Apple M3 MacBook Air, 24 GB RAM
- macOS 26.5.1
- Release configuration, Ninja, AppleClang 17
- pluginval strictness 5

## Fresh build inputs

The products were configured into new standalone trees rather than reusing the
suite's incremental `build/local` artifacts:

```bash
cmake -S plugins/sattari-arp -B build/qualification/sattari-arp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -S plugins/sub-conjurer -B build/qualification/sub-conjurer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/qualification/sattari-arp --config Release --target SattariArp_All SattariArpTimingContract -j 4
cmake --build build/qualification/sub-conjurer --config Release --target SubConjurer_All SubConjurerContract -j 4
```

The first Sub Conjurer validation correctly failed because its raw Standalone
bundle had no signature. The shared `sattari_add_plugin()` CMake path was fixed
to ad-hoc sign raw Standalone artifacts before packaging, then both products
were rebuilt from that source and all gates below were rerun.

## Functional evidence

- Arp timing contract: 7 note-ons, expected spacing 6000 samples, maximum
  spacing error **0 samples**; Compass response remained opt-in/fallback-safe.
- Sub Conjurer contract: a 60 Hz source in Deep + Follow Pitch produced **30.0
  Hz**, RMS 0.6980, peak 0.820; silence produced RMS 0.00000 and peak 0.0000.
- The Sub Conjurer test now explicitly selects Follow Pitch. The previous test
  accidentally left the plugin in Lock Note and only proved “some sub-band
  output”; the corrected assertion would fail at the old 27.5 Hz lock-note
  result.

## Format evidence

| Product | Source bundle signatures | AU | VST3 | Standalone |
|---|---|---|---|---|
| Sattari Arp | Pass | `aumi/Arp1/Satr` auval pass | pluginval 5 pass | Not a declared format |
| Sub Conjurer | Pass | `aumf/Sc01/Satr` auval pass | pluginval 5 pass | Signature pass |

The same signature and pluginval checks passed again after copying the exact
bundles into the generated alpha package directories.

## Package artifacts

| Artifact | SHA-256 |
|---|---|
| `build/qualification/packages/sattari-arp-plugin-alpha-v0.1.0.tar.gz` | `0b0e4b339a8be6bcce473a4470c841c5aa55cb7eed69c56cdb6b3e54d0b417c7` |
| `build/qualification/packages/sub-conjurer-plugin-alpha-v0.1.0.tar.gz` | `de9d61681e10a8dcbf19b147cb7b22bf2a0ca43c25c900cf5b16f2bdbd5ed4f9` |

These packages are local qualification artifacts and are intentionally ignored
by Git. Their signatures are ad-hoc, not Developer ID.

## Gates still open

- GitHub Actions run `31343378224` was dispatched for commit `aa03550`, but all
  jobs were refused before checkout because the account has failed payments or
  reached its Actions spending limit. This is an account gate, not a test
  failure; the local gates above remain the available evidence until billing is
  restored and the workflow is rerun.
- Logic, Ableton, and FL Studio scan/insert/process/automation/recall/bounce.
- Musical listening on real kick, bass, and MIDI sessions.
- Multi-instance stress at the supported sample rates and buffer sizes.
- Non-developer tester sessions and issue triage.
- Developer ID signing, notarization, clean-machine install/update/uninstall,
  and a public update channel.
