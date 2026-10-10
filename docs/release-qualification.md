# Current Launch Scope

The consolidated October 2026 candidate scope and workflow are documented in
`docs/launch-readiness.md`. The six-product descriptions below are historical;
shared contract-runner instructions still apply. Always configure with
`SATTARI_COPY_PLUGINS_AFTER_BUILD=OFF` when qualifying without installation.

## Non-StemDeck Release Qualification

This checklist records the earlier release baseline, not public-release approval.
Keep internal labels until the exact shipping artifacts pass their acceptance gates.

## Safe native build/test entry point

From the repository root, configure the production-shaped neural lane. Its pinned runtime requires macOS 13.3 on both architectures; do not use the older generic deployment floor for this lane:

```sh
cmake -S . -B build/local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 \
  -DSATTARI_UNIVERSAL=ON -DSATTARI_INCLUDE_STEMDECK=OFF \
  -DSATTARI_USE_ONNX=ON -DSATTARI_REQUIRE_ONNX=ON \
  -DONNXRUNTIME_ROOT="$PWD/third_party/onnxruntime-osx-universal2-1.22.0"
python3 scripts/run_release_contracts.py build/local --build --jobs 2
```

The recorded 20 September run additionally used the already installed `ccache` compiler launcher. A correctly configured compiler cache is optional; it does not replace the build dependency checks or qualification tests. Missing optional Elastique/Signalsmith SDKs leave the existing Rubber Band fallback in use; they must not be advertised as enabled engines.

The explicit `SattariReleaseContracts` target builds the registered test executables and Stack shared code. It does not build AU/VST3/Standalone wrappers, invoke bundle signing, or install into the user's plug-in folders. Each test uses a private session-bus identity. A failed build, missing executable, empty inventory, timeout or failed test fails the gate. Do not turn those outcomes into a green result by running old binaries or omitting a test.

`scripts/release_check.sh` and the normal distribution packagers have broader side effects, including format builds, installation or signing. They are **not** substitutes for the test-only command while signing/notarization remains excluded.

## Exact candidate and package

Record the source revision/dirty-state status, source inputs, compiler/toolchain, CMake configuration, model/runtime hashes, bundle versions/identities and final artifact hashes. Finish code fixes before freezing that candidate. Rebuilding or editing afterward invalidates the affected evidence; rerun the relevant gates.

Verify the actual complete installer in an isolated destination and on a clean supported machine: launch outside the checkout, correct resources, all promised payloads, install, prior-version upgrade, cancellation and failure recovery. Confirm source/notices and the neural runtime's actual minimum OS. Fixture-bundle tests do not replace real-format qualification.

## Supported-DAW acceptance

For each supported host/format, using that exact package:

1. Scan and instantiate the expected bundle identity, without stale duplicates.
2. Analyze a cleared file and host playback; verify supported clip/ARA workflows only where the host supports them.
3. Verify Compass/Key to Auto Pitch handoff, manual lock, Dynamic Follow, and isolation across sessions/instances.
4. Exercise tuning, harmonies, captured-note edits and undo/redo; save, close, reopen and compare state/output.
5. Exercise Stack source loading, missing/moved media and relinking; compare restored sound and layer state.
6. Exercise Mix/Vocal/Create chain changes, bypass, automation, transport/tempo changes and offline bounce.
7. Change supported sample rates, buffer sizes and devices; run a sustained session and monitor dropouts, thread CPU, memory growth and responsiveness.

Run synthetic and cleared-audio measurements without publishing private audio. CPU measurements use the thread CPU clock. Engine delay, correction acquisition and interface/DAW round-trip latency are distinct metrics. The user's listening approval must use the actual candidate and cannot be inferred from null tests or synthetic scores.

## Go/no-go

No public release until unresolved P0s are closed, P1 workflows are qualified, the complete fresh gate passes, the actual package/host matrix is approved, and listening/distribution requirements are satisfied. StemDeck and signing/notarization remain excluded unless the user explicitly changes those constraints. Unexecuted device/platform/host checks remain **NOT VERIFIED**.
