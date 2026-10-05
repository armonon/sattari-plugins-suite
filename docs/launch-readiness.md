# Consolidated Suite Launch

Updated October 3, 2026. **Public launch is not approved.** This is the current
release program; older six-product and wrapper candidate reports are historical.

## Candidate Scope

`config/products.json` remains the product identity source. `config/launch.json`
selects active and specialist products and requires explicit native contracts for
each. `python3 scripts/qualify_suite.py --plan` prints the exact native versions,
build targets, formats and tests rather than assuming the suite version is every
binary's version.

| Offering | Candidate binaries |
|---|---|
| Modular | Modular FX and Modular Instrument 0.2.0 |
| Vocal finishing | Royal Chain 0.1.0 |
| Vocal tuning | Auto Pitch 0.5.0, neural runtime and ARA enabled |
| Session mixing | Mix 0.2.1 |
| Tools | Arp 0.1.1, Sub Conjurer 0.1.1, Side Chain, Align, Scaler |
| Compass | Compass, Key 0.3.0, Tuner, Scope, Meter |
| Specialist | Maqam |

This is 16 binaries, not 16 approved releases. Arp has AU/VST3 only; the other
15 have standalone builds too. Windows omits AU. Vocal/Create are native Modular
collections, not additional launch binaries. Stack, Vocal, Create, Bus and Vocal
Master preserve legacy sessions but stay outside the new candidate package.
Harmony migration, VoxKey, VoxSynth, Morph and StemDeck are not launch promises.

## Automated Qualification

The dedicated `Consolidated Suite Qualification` GitHub workflow builds macOS
universal and Windows x64 candidates. It does not publish a release, update a Hub
feed, or deploy the website. The Windows SDK and validators are checksum-pinned;
the exact ARA dependency revisions are checked. Native source snapshots include
local changes and exact project build inputs/SDKs, with per-file hashes. This is
traceability evidence, not certification of complete corresponding-source
obligations for prebuilt dependencies; the owner must review those too.

Safe local macOS configuration, without replacing installed plug-ins:

```sh
cmake -S . -B build/local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
  -DSATTARI_UNIVERSAL=ON -DSATTARI_COPY_PLUGINS_AFTER_BUILD=OFF \
  -DSATTARI_INCLUDE_STEMDECK=OFF -DBUILD_TESTING=ON \
  -DSATTARI_USE_ONNX=ON -DSATTARI_REQUIRE_ONNX=ON \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-osx-universal2-1.22.0" \
  -DSATTARI_USE_RUBBERBAND=ON -DSATTARI_AUTOPITCH_PRINT_ENGINE=RUBBERBAND \
  -DSATTARI_USE_ELASTIQUE=OFF -DSATTARI_USE_SIGNALSMITH=OFF \
  -DSATTARI_AUTOPITCH_ENABLE_ARA=ON -DSATTARI_SONGKEY_ENABLE_ARA=ON

python3 scripts/qualify_suite.py --build-dir build/local \
  --output-dir build/launch-packages --candidate rc1 --jobs 2 \
  --pluginval /Applications/pluginval.app/Contents/MacOS/pluginval \
  --validate-au
```

Add `--sign-identity 'Developer ID Application: ...'` for staged Developer ID
signing. It does not notarize or authorize distribution. `--validate-au` temporarily
places each candidate AU in the user component folder, then restores the prior
bundle even when validation fails. Quit DAWs before running it. VST3 and standalone
bundles are tested/staged without replacing user installations.

While a DAW remains open, use `--prepare-only` in place of `--validate-au`. It
retains a `.prepared` folder with source, evidence and staged payloads, explicitly
marked not fully qualified. No final release archive is created and AU validation
stays open. This is useful for preparing without changing installed components.

The qualifier builds actual product wrappers, rebuilds the entire non-StemDeck
CTest inventory, records tested executable hashes, validates each staged VST3 at
pluginval strictness 10, and runs macOS AU validation for each tested architecture.
It checks native bundle versions, portable Mac dependencies, microphone purpose
strings and signed standalone audio-input entitlements, embedded neural
runtime/model provenance, notices and archive integrity. Plug-ins inherit their
host's permissions; their signatures cannot grant permissions to a DAW.
Failure or changing source produces no final archive. Evidence remains in the
build tree for diagnosis. Existing candidate archives are never overwritten.

Every current native factory has a deterministic boolean-state recall contract.
It exercises fractional host automation on both sides of the switch boundary,
then restores saved on/off states. Existing parameter identities, order, ranges
and defaults stay intact; binary controls use the suite's canonical Boolean type.

Apple Silicon executes the Intel validator through Rosetta. That is not physical
Intel Mac acceptance. macOS 13.3 is the pinned neural runtime's minimum deployment
target, not proof that all supported OS versions have been tested. Likewise,
Windows Server CI is build/validator evidence, not Windows 10/11 DAW acceptance.

## Launch Gates Still Requiring Evidence

| Gate | Required evidence |
|---|---|
| Licensing | Owner-confirmed JUCE arrangement and review of the exact project's license, Rubber Band, ARA, model and runtime notices |
| Mac trust | Valid Developer ID-signed payloads; Installer certificate for a PKG; accepted notarization and Gatekeeper test of the final download |
| Windows trust | Authenticode-signed final installer/payloads and clean Windows installation; runtime prerequisites verified |
| Host acceptance | Exact candidate hashes; Logic AU and Ableton/FL VST3 on Mac; named Windows hosts; scan, audio/MIDI, automation, reopen, bounce and removal |
| Compatibility | Original released parameter IDs, old session/preset fixtures, and no accidental retired-wrapper replacement |
| Sound | Real-material listening verdicts for tuning, vocal finishing, Mix proposals and representative Modular chains, including bypass/level comparisons |
| Install/update | Fresh install, update from prior version, interrupted install recovery, uninstall without deleting user presets, offline operation |
| Storefront | Final version/OS claims, verified downloads and checksums, matching Hub feed, contact/support and refund policy |

For every product/host row, record tester, date, OS/host versions, exact archive
SHA256, sample rate/buffer, fixture or project reference, and verdict. Test 44.1,
48 and 96 kHz and 64/128/256/512-sample buffers where the host supports them.
Exercise transport looping, four instances, sample-rate changes, editor resize,
automation, save/reopen and offline rendering. Blank rows are not passes.

Modular additionally requires hosted instrument/effect recall, relinking a missing
third-party VST3, hosted latency changes, four source layers and Vocal/Create preset
imports. Hosting remains in-process for trusted mono/stereo VST3 plug-ins. It is
not a crash sandbox, AU host, multi-output host or generated-MIDI routing system.
Mix requires linked-track isolation, preview/compare/undo and rejection of stale
analysis. Royal requires level-matched bypass and genuine vocal listening. Auto
Pitch requires natural/hard modes, delay compensation and ARA clip recall.

## Owner-Only Setup

The October 3 preflight found a usable Developer ID Application certificate,
but no Developer ID Installer certificate and no usable `sattari-notary` keychain
profile. Configure credentials in Keychain/Apple tooling, never in chat or Git.
`scripts/sign_preflight.sh` reports the current state without changing it.

The repository had substantial uncommitted work at the start of qualification.
An internal candidate can preserve the exact source snapshot, but the normal Hub
installer's tracked-source guard stays intact. Freeze an approved source revision
before public packaging. A dedicated GitHub branch requires owner authorization;
local readiness work does not imply permission to publish binaries or change main.

No public-release label, completed gate checkbox, signed installer claim or
supported-host claim should be added until the matching final artifact has passed
the corresponding test. Cosmetic completion is not a substitute for these gates.
