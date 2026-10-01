# Sattari release closure — 20 September 2026

Scope: the six-product, non-StemDeck suite, Hub, release tooling and local website qualification. This continues the [19 September audit](2026-09-19-release-today.md); it preserves the pre-existing polyphonic and other working-tree changes. Base revision: `32b7c09155f8a2bd949c60a9c68ce55448ad0309`, plus the recorded dirty inputs. This is not a committed or immutable shipping candidate.

No user plug-ins were installed, removed or replaced. No signing/notarization, publishing, push or private-audio upload was performed. The existing DAW session was left alone. The user's listening scores were not inferred or generated.

Evidence: [20 September logs and fingerprints](2026-09-20-evidence/). The source-input hashes, patch and configuration record identify this qualification work, not a finished distributable installer.

## RELEASE STATUS

**NOT READY** for public distribution. The specific preset, source-recall, memory-lifetime and Hub workflow defects below are corrected and regression-tested. Actual shipping bundles, installer/host acceptance and user listening approval remain separate gates.

## WORK COMPLETED

### Preset feedback and rapid saves

- **Problem:** Auto Pitch and Stack could silently ignore a failed user-preset load/save; same-second saves could reuse a filename.
- **Root cause:** editor call sites discarded failure results and constructed names using only a timestamp.
- **Fix:** visible failure alerts, clear failed selections, and unique save-name selection. Stack explicitly calls these **Settings presets** and explains that imported media and source references belong to DAW project state, not these JSON presets.
- **Validation:** 154 preset checks pass, including rejection without parameter mutation, round trips, 100 repeated loads, rapid distinct saves and deterministic save failure. The Intel slice also passed under Rosetta; this is not physical Intel-Mac qualification. Simultaneous cross-process preset saves are not claimed to be transactionally collision-proof.

### Stack source recall

- **Problem:** restoring missing or empty source references could leave previously loaded audio active.
- **Root cause:** restore loaded only files that existed and did not clear the prior source assignments.
- **Fix:** clear all previous sampler sources, reload selected layer references, preserve missing paths for relinking, show missing/unreadable status, reject foreign state, and withhold output while file restoration is pending. Voice/alignment reset occurs on the audio thread; file decoding does not.
- **Validation:** generated-audio regression covers Chop, Granulator A/B references, state reopen, positive audible playback, moved/missing media, silence rather than stale audio, relink and source-free state clearing. Native and Rosetta checks pass. Actual DAW session migration is **NOT VERIFIED**.

### Sample-buffer ownership and callback safety

- **Problem:** Chop retained every imported source indefinitely; Granulator could delete a retired sample buffer when replacing its engine source in the audio callback.
- **Root cause:** Chop had no unused-source reclamation; Granulator's published/engine shared ownership did not retain a message-thread owner through replacement.
- **Fix:** keep sources alive while the current publication, engine or voices use them, then reclaim unused sources on message-thread publication. Granulator callback handoff uses a try-lock and keeps the prior source for a block if publication overlaps it; it no longer uses a potentially blocking atomic shared-pointer load there.
- **Validation:** Chop's before-test reclaimed **0/98** unused sources; the corrected test reclaims **98/98**, protects held/current sources, then releases all 100 on message-thread cleanup. Granulator's 100-replacement and duplicate-publication regression observes no audio-callback source deletion. Its surrounding synthesis/capture/modulation/reverb contract passes. This is object-lifetime evidence, not an application-wide RSS or dropout guarantee.

### Minimum-size rack layout

- **Problem:** the final shared-rack slot and some Stack controls were clipped or abbreviated; inspector content could exceed the available viewport width.
- **Root cause:** fixed spacing and minimum content widths exceeded the minimum editor geometry.
- **Fix:** compact slot spacing, appropriate button widths and viewport-based inspector sizing.
- **Validation:** fresh minimum-size render/layout assertions pass. Mix and Stack shared-shell PNGs were visually checked; the Stack test uses a fixture instrument, not every production instrument panel.

### Hub install, cancel, retry and recovery

- **Problem:** install/download work could block AppKit; cancellation and secondary rollback failures were poorly handled.
- **Root cause:** synchronous UI work, no operation-wide cancellation/progress state, and swallowed recovery errors.
- **Fix:** a serial worker owns a separate model built from immutable configuration; the UI refreshes receipts on completion. Added progress, cancellation, re-entry protection and safe close/quit handling. Downloads use HTTPS validation and bounded-memory hashing. Failed rollback reports backup locations and stops the batch.
- **Validation:** fresh Swift Release build; **51 isolated fixture/AppKit checks pass**, including cancel at multiple transactional stages, old-bundle/receipt preservation, no leftover staging, responsive run-loop completion, close protection, retry through the real button, successful bundle replacement and UI receipt refresh, plus retained recovery backups after a simulated secondary filesystem failure. The same contract passes outside the checkout. Real downloaded updates and shipping bundle installation are **NOT VERIFIED**; no archive/feed is configured.

### Release tooling and metadata

- **Problem:** there was no explicit all-contract build target without format installation/signing side effects; stale binaries could otherwise give misleading evidence.
- **Fix:** added `SattariReleaseContracts` and a build-first runner that refuses stale-test fallback after build failure, discovers the full CTest inventory and isolates each test's session bus. Updated reproducible qualification/source-recall documentation and removed misleading Hub release-approval wording. Corrected three corpus entries to `cleared`, matching their existing CC BY license records rather than calling them public-domain.
- **Validation:** eight release-tooling regression tests pass. Catalog sync and 264 rack-preset references across 28 modules pass. Embedded CREPE hash/provenance audit passes. Runtime inspection confirms Universal arm64/x86_64 and minimum macOS 13.3 on both slices. Local external models are fingerprinted but are not approved or bundled by this pass.

### Polyphonic UI test event delivery

- **Problem:** the first complete fresh run passed 114/115 tests, with five failed assertions after switching Poly off in the polyphonic test.
- **Root cause:** the test slept on the message thread and called pending timers once, without dispatching native events. Its second mode transition was tested against stale UI state. The failure reproduced in an isolated run.
- **Fix:** enable modal dispatch for the test executable only and pump the real message loop for the same 100 ms interval. No product DSP, audio thresholds or UI assertions were changed for this correction.
- **Validation:** all **117 assertions and 52 production-processor renders** then passed, followed by a new complete **115/115** native run. The original failed log is retained as `full-gate-before-ui-harness.log`.

## PERFORMANCE

- Chop unused-source retention: **0/98 reclaimed before → 98/98 after** the regression fix. No byte/RSS reduction is invented from this object count.
- Granulator sample deletion on the callback is prevented in the new lifetime regression; full DAW glitch/dropout testing remains **NOT VERIFIED**.
- Final fresh live-engine harness: thread CPU at 48 kHz/64 frames measured mean **0.0081 ms**, p95 **0.0305 ms**, p99 **0.0333 ms**; maximum wall interval **0.038 ms**. Harness maximum RSS was **4,833,280 bytes**. Earlier same-day measurements were higher under different load; no algorithmic speedup is claimed. This is not complete plug-in CPU, sustained-session memory or DAW round-trip latency.
- Auto Pitch's separate processor live-mode check reports **96 samples / 2.00 ms** at 48 kHz and corrects the synthetic +45-cent input to **+3.4 cents**. Switching the session-bus key from C major to D-flat major changes the output by **99.8 cents**. This does not certify real microphone/interface latency or real-vocal quality.
- Correction-off/Amount-zero reference nulls are sample-exact in the exercised modes. The separate corrected-signal onset diagnostic measures a worst **69-sample / 1.44 ms wet-versus-dry difference**. That diagnostic always returns success and has no acceptance threshold: its exit code is not a proof of perfect wet alignment. Real automation/timing acceptance remains open.
- Hub long operations now run off AppKit; fixture-driven cancellation and retry complete through the live run loop. Real large-package throughput, cold/warm GUI startup, GPU/frame timing, import/export throughput and long-session memory growth are **NOT VERIFIED**.

## TEST RESULTS

Final full native gate: **115/115 PASS**, after a successful build-first `SattariReleaseContracts` run. The final command was `python3 scripts/run_release_contracts.py build/local --build --jobs 4`. It uses Universal Release, macOS 13.3, required pinned ONNX Runtime and StemDeck disabled. It builds test executables and shared code, not installed AU/VST3/Standalone wrappers. Incremental dependency rebuilding and compiler-cache reuse do not constitute a clean-checkout shipping-package test. SHA-256 fingerprints cover the **84 distinct test executables** used by the 115 registered cases; all recorded source inputs match the final verification.

Completed focused checks: preset/rack/source lifetime and recall; Granulator synthesis; Dynamic Key, Live/Extreme Pitch and Vox; minimum editor layouts; 51 Hub fixture/UI checks; eight tooling tests; catalog/preset references; model/runtime inspection. Preset and Stack/source-lifetime checks additionally executed on the Intel slice under Rosetta, not physical Intel hardware. Website typecheck and production build pass, with **zero reported npm dependency advisories**. This is not an exhaustive security audit or deployed-site validation.

All **11 supplemental Auto Pitch render modes** completed with exit 0: null, wet onset alignment, live mode/key follow, embedded CREPE, features, gain staging, vibrato, polyphonic, transpose, polyphonic stress and AutoKey. Some modes are diagnostic rather than strict acceptance gates; notably the wet-alignment result is recorded above, not converted into a green quality judgment. The synthetic harmonic-melody AutoKey exercise identifies 24/24 keys; this is not a representative-song accuracy or competitor-parity score.

The earlier interrupted native attempts are preserved separately. They are not passes and must not be added to the final gate count.

## REMAINING ISSUES

### P0

No new reproducible crash/data-loss defect remains in the completed checks, and the complete registered native gate is now green. Release-blocking evidence gaps remain: exact shipping artifact identity, actual package install/upgrade/launch and critical supported-DAW workflows. A test harness does not certify these.

### P1

- Freeze/commit the selected source and build the exact shipping artifacts. Untracked binary-bearing source currently prevents traceable distribution packaging by design.
- Run real host scan, key handoff, tuning/harmonies/editing, state recall, automation, bypass, transport, bounce and device/rate/buffer tests on those artifacts. Do not reuse old matrices or installed bundles as current proof.
- Obtain the user's listening approval against the exact candidate; the cleared six-clip corpus is present, but no scores are fabricated and the old sealed listening package is untouched.
- Confirm actual supported OS/architecture/format scope and distribution requirements. Signing/notarization remains excluded, not silently complete.
- Large-media and sustained-session behavior require real supported-host stress tests.
- Accept or resolve the measured wet/dry onset difference using real automation/listening tests. Representative independent key-detection and tuning-quality acceptance is not supplied by synthetic tone/melody checks.

### P2

Non-blocking compiler-warning cleanup and broader layout/accessibility coverage remain. The offline installer has no configured online update feed/archive; this is a limitation, not a claim that remote updating has been qualified. Sample-inclusive portable Stack presets are not implemented; settings-only behavior is now explicit.

### P3

No new competitive feature is needed to close this release pass. Additional formats, DSP engines and features are optional future scope.

## MISSING FOR A PROFESSIONAL RELEASE

True omissions: an immutable, traceable shipping candidate, qualified actual installer and clean-machine launch, supported-DAW/device matrix and user listening approval. Corresponding source/notices/runtime assets must be checked in the final distribution. Optional features such as online updates or more engines are not substitutes for these gates.

## IF WE RELEASED RIGHT NOW

Customers would receive better preset error feedback, more reliable source recall and a responsive installer, provided the corrected code is actually in their package. We cannot yet attest that their downloaded bundle matches this source, scans in their host, recalls and bounces their full session or meets their live-monitoring expectations. Public claims should remain internal-alpha/limited availability.

## RELEASE CHECKLIST

- [x] Preset error feedback and rapid-save regression.
- [x] Stack source recall, missing media and relink regression.
- [x] Chop/Granulator source lifetime regression without callback deletion.
- [x] Minimum-size shared editor layout regression and visual check.
- [x] Hub cancellation, recovery and successful retry through AppKit fixtures.
- [x] Release-tool isolation/build-failure guards and metadata checks.
- [x] Website production build/typecheck/dependency audit.
- [x] Complete final native build/test gate: 115/115; original failure reproduced and test event delivery corrected without weakening assertions.
- [!] Corrected-path onset differs from the dry reference by up to 1.44 ms in the diagnostic; real automation/listening acceptance remains open.
- [ ] Exact shipping bundles/installer and clean-machine qualification.
- [ ] Actual supported-DAW/device/end-to-end acceptance.
- [ ] User listening approval.
- [ ] Public distribution and release-consistent deployment.

## FINAL VERDICT

**The automated engineering gate is green; do not label the suite production-ready yet.** Freeze and qualify the actual package, run the host/device matrix and obtain listening approval, including the timing caveat above. Installed plug-ins have not been updated to this source. This pass closes concrete engineering defects; it does not turn unexecuted customer workflows into passes.
