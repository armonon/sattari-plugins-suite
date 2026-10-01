# Sattari release-today audit — 19 September 2026

Scope: the six-product, non-StemDeck suite; Hub installer; release tooling; marketing website. Base commit: `32b7c09155f8a2bd949c60a9c68ce55448ad0309`, plus the existing working-tree changes and this audit's changes. This is **not an immutable release candidate**. Prior polyphonic changes were preserved, not represented as newly implemented here.

No installation into the user's plug-in folders, signing/notarization, deployment, commit, or push was performed. No private source audio was uploaded; no listening scores were generated. Only owned test processes were stopped. The existing DAW and other user applications were left alone.

Evidence: [selected logs and fingerprints](2026-09-19-evidence/). Source fingerprints identify the audit's principal edited inputs, not a complete source/package manifest. Executable fingerprints identify the specific test executables used, not shipping AU/VST3 bundles.

## RELEASE STATUS

**NOT READY** for a public, paying-customer release of the full suite.

Several concrete reliability defects are fixed and independently exercised. That does not establish end-to-end release readiness. A complete, fresh native regression run and qualification of the exact shipping package remain incomplete. Existing alpha labels and closed public downloads are appropriate.

## WORK COMPLETED

### Preset validation and error handling

- **Problem:** malformed, incompatible, or wrongly typed preset data could be reported as successfully loaded and change parameters.
- **Root cause:** permissive JSON-to-number casts and parameter application without validating the entire document first.
- **Fix:** enforce product/schema/parameter structure, finite numeric values, size and nesting limits, valid UTF-8, and complete-document parsing. Validate before any parameter notification; retain compatibility with numeric unknown parameters and missing existing parameters. Return the real result for loads and A/B recall. Shared serial-rack editors now show load/save failures.
- **Validation:** the initial regression run against the old implementation exposed 12 failures in 141 checks. The rebuilt Universal Release harness passed **147 checks with zero failures**, including malformed/deep/oversized files, wrong products, rejection without partial changes, valid round trips, A/B recall, and 100 repeated loads. Surrounding rack contract and minimum-size shared editor rendering passed. Executed on arm64; Intel runtime **NOT VERIFIED**. Auto Pitch and Stack's separate editor error-feedback paths are not fixed by the shared-rack UI change.

### Hub startup and transactional installation

- **Problem:** the SwiftPM Release executable failed to find `sattari-products.json` outside the checkout. Receipt-write failure could leave replaced bundles installed despite reporting an unsuccessful install. Rapid installs reused backup names.
- **Root cause:** missing sibling resource-bundle lookup; receipt persistence outside the transaction; second-resolution backup names. Manifest paths and Swift/C++ session identifiers also lacked consistent constraints.
- **Fix:** support sibling SwiftPM resources and packaged app resources without an eager `Bundle.module` trap; validate manifest IDs and allowlisted relative bundle paths; use ASCII-compatible session IDs; include receipt persistence in rollback; use unique backup names; clean failed archive staging.
- **Validation:** freshly built Release Hub passed **19 isolated fixture checks**, including missing payload, receipt-write failure, two-bundle restoration, repeated install/backups, receipt reopen, and staging cleanup. It loaded six suites/48 products outside the repository. A temporary, unbundled `.app` launched and its suite/component UI was inspected. Fixture bundles are not real AU/VST3 validation. The temporary shell deliberately contained no plug-in payloads; its zero available products is not a missing-payload defect in a completed installer. Final installer **NOT VERIFIED**.

### Release gates and package traceability

- **Problem:** the release script moved/deleted the live cross-plugin bus; globbing built executables omitted missing tests and argument-based modes. An otherwise clean package could contain untracked compiled source not represented by its source record. Required dependency notices were absent from the packaging copy list.
- **Root cause:** filesystem-based test isolation, incomplete test discovery, a late/conditional source guard, incomplete notice assembly.
- **Fix:** private session IDs for each CTest/auval process, complete registered CTest inventory, fail-closed missing/empty inventory, explicit StemDeck exclusion, correct selected-tree freshness lookup, early untracked-source rejection, and Rubber Band/ONNX Runtime notice copies.
- **Validation:** **six Python regression tests passed**, exercising isolation, exact test matching, failure continuation, empty inventory, excluded products, untracked C++/header/Swift inputs, and byte-identical notice copies. Shell syntax passed. Actual suite packaging was not executed because its normal build path also installs/signs, outside this pass's constraints.

### Website build and release claims

- **Problem:** an unresolved `Fetcher` type failed type checking; dependency audit reported 15 advisories; marketing copied historical green gates as if they qualified the current build.
- **Root cause:** missing runtime type declaration, outdated direct/transitive packages, stale release assertions.
- **Fix:** type the worker's asset binding, add a typecheck command, update compatible framework/build dependencies, and replace stale validation/availability assertions with the current internal-release status.
- **Validation:** production web build and typecheck passed; full dependency audit **15 advisories to zero reported advisories**. This is not a claim of an exhaustive security audit or confirmed exploitability. Six product pages, empty search, Stack demo add/clear, and narrow-screen layout were exercised locally in the in-app browser. No console warnings/errors or broken loaded images were observed in those checks. Other browsers and deployment **NOT VERIFIED**. Relevant primary advisory: [Next.js image optimizer security advisory](https://github.com/vercel/next.js/security/advisories/GHSA-2xp9-vwfh-vxw4).

## PERFORMANCE

No DSP optimization speedup is claimed. The live-engine test's CPU measurement was corrected to `CLOCK_THREAD_CPUTIME_ID`, preserving all existing CPU thresholds and reporting wall delay separately.

| Measurement | Observed result | Interpretation / limit |
| --- | --- | --- |
| Hub outside-checkout contract | Before: missing manifest; after: pass, 0.95 s wall | CLI/model plus fixture checks; cold/warm GUI startup **NOT VERIFIED** |
| Hub contract maximum RSS | 14.2 MiB | Not full installed suite memory or leak testing |
| Live engine, 48 kHz / 64 frames | Thread CPU mean 0.0243 ms; p95 0.1015 ms; p99 0.1255 ms | Isolated engine, not complete plug-in CPU; no comparable before/after optimization metric |
| Same engine's maximum wall delay | 113.431 ms | Loaded machine and non-real-time harness; dropout-free monitoring **NOT VERIFIED** |
| Extreme engine | 96 samples / 2 ms at 48 kHz; correction acquisition 25.33 ms | Audio delay differs from detection/acquisition time; DAW/device round trip **NOT VERIFIED** |
| Fresh Mix processor contract | 1.87 s wall; 17.7 MiB maximum RSS | Whole test duration and harness RSS, not render throughput or steady plug-in memory |
| GUI frames/GPU, memory growth, large imports/exports, long sessions | **NOT VERIFIED** | Require real supported-host/device profiling against the final artifact |

The workstation was under substantial contention, with roughly 8.6 GiB swap in use during inspection. Some compilation steps spent tens of minutes elapsed with little CPU time. This is an environmental limitation, not a demonstrated explanation for every timeout. Do not reinterpret timed-out tests as passes.

## TEST RESULTS

Fresh passes:

- Universal Release shared-rack/preset executable: 147 preset checks, normal rack contract, editor render; native arm64 execution.
- Clean Swift Release Hub build and fixture safety contract: 19 checks.
- Fresh Release Live Pitch and Extreme engine contracts: passed accuracy, latency and relevant stability assertions on synthetic inputs.
- Clean arm64 Release Mix shared library built from source; its freshly compiled product-contract driver was linked against that fresh archive and passed **14 modules, eight programs, three sample rates, two block sizes**. The ordinary contract target's redundant JUCE recompilation was stopped, then the fresh driver/archive were linked using the generated framework/LTO flags. This is processor validation, **not a completed AU/VST3 package build**.
- Release-tool Python tests: six passed. Website production build, typecheck, audit, and scoped local UI smoke: passed.
- Catalog/registry, rack/factory-preset reference checks, and bundled CREPE distribution hash/provenance check passed. Optional external key models are not implicitly approved for redistribution.

Full native regression: **FAILED / INCOMPLETE**. The attempted suite rebuild expanded to 1,595 compilation steps and was stopped under severe contention. A diagnostic run against the mixed-age build tree reached 49 of 115 registered tests: **42 passed, seven did not pass**; test 50 was interrupted and the remaining tests were not reached. These 42 passes do not qualify current shipping binaries.

Non-passing diagnostic results:

- Missing executables: `SattariDynamicKeyTrackerContract`, `SattariExtremeLivePitchContract`.
- Failed old wall-clock CPU test: `SattariAudioCoreLivePitchContract`. A fresh standalone build of the corrected thread-CPU test passed; the complete release tree still requires rebuilding.
- Timeouts: `SattariAudioCoreVoxContract`, `SattariVocalProductContract`, `SattariCreateProductContract`, `AutoPitchCoreContract`. Each needs a fresh, quiet-machine rerun and investigation if persistent.

Platform validation: current installed AU/VST3 scanning, clean-machine installation, supported older macOS versions, Intel execution, and live DAW validation **NOT VERIFIED**. Signing/notarization explicitly excluded. No AAX/Windows validation is claimed.

### Critical journey coverage

| Journey | Evidence this pass | Remaining end-to-end gate |
| --- | --- | --- |
| Install, launch, update, recover | Fresh Hub launch/UI; fixture install/recovery | Real complete payloads, clean machine, update from previous release; **NOT VERIFIED** |
| Preset save/load and A/B | 147 fresh checks; shared rack UI error handling | Host project recall and separate editor failure UX; **NOT VERIFIED** |
| Compass full-file/drop/live analysis | Source/config/catalog inspection | Actual DAW clip access, cancellation, key accuracy and file/live modes; **NOT VERIFIED** |
| Compass to Auto Pitch key handoff | Existing session-bus architecture reviewed | Multiple instances, manual lock/dynamic follow, conflicting publishers; **NOT VERIFIED** |
| Vocal/instrument/polyphonic tuning | Fresh isolated Live/Extreme tests; prior poly proof is separate | Shipping processor in DAW, real listening and microphone round trip; **NOT VERIFIED** |
| Captured notes, undo/redo, harmonies | Implementation exists | Full edit/save/reopen/render cycle; **NOT VERIFIED** |
| Stack layers and source recall | Shared state/preset implementation inspected | Sample-based preset/project recall, moved/missing samples; **NOT VERIFIED** |
| Mix/Vocal/Create chain workflow | Fresh Mix processor and shared rack contracts | Supported DAWs, automation, transport, complete project reopen; **NOT VERIFIED** |
| Offline bounce, device/rate/buffer changes, long session | Scoped synthetic rate/block assertions only | Real host/device stress and sustained sessions; **NOT VERIFIED** |
| Website discovery and product detail | Six pages, search empty state, demo, mobile-width local smoke | Other browsers and deployed artifact; **NOT VERIFIED** |

## REMAINING ISSUES

### P0

No new reproducible crash or data-corruption defect remained in the completed fresh checks. This is not proof of their absence.

Release-blocking evidence gaps: no single frozen, fully built and qualified shipping artifact; incomplete/non-green complete native gate; unverified installer and supported-DAW save/reload/automation/render journeys. These must close before calling the full suite production-ready. Public macOS distribution qualification remains outside the authorized work, not silently complete.

### P1

- Auto Pitch and Stack's separate user-preset editors ignore failed loads and do not surface save failures. The manager now rejects bad data safely, but users can still receive no useful failure feedback. Confirmed from call sites; affected GUI scenarios **NOT VERIFIED** end to end.
- Hub download and installation work runs synchronously on the AppKit path; large real updates need meaningful progress/cancellation and responsiveness verification. No risky concurrency rewrite was attempted in this release pass.
- Stack's JSON user presets capture parameters, while sample-source paths are serialized separately in DAW state. A portable, sample-aware stack preset is not established; qualify it or clearly present these presets as settings-only. Full sample round trip **NOT VERIFIED**, not a claim of reproduced DAW project data loss.
- Exact neural package runtime dependencies/minimum OS, source correspondence, and supported-host/device compatibility need qualification. The dedicated neural lane's macOS 13.3 floor must not be confused with older generic build defaults.
- Complete the user's listening gate using the exact candidate. Existing sealed scores were not inferred or replaced by synthetic measurements.

### P2

- Minimum-size shared-rack test rendering partly clips the final slot; normal production layouts need confirmation.
- Secondary rollback failures have limited diagnostics. The primary receipt-failure recovery is tested; simultaneous failure to restore backups is **NOT VERIFIED**.
- Update-feed/archive URLs are currently unconfigured. This is acceptable for an explicitly offline installer, not a basis for advertising online updates as working.

### P3

No new competitive feature is required to complete this pass. Additional formats and deeper editing capabilities are optional unless they are part of the actual sale/support promise.

## MISSING FOR A PROFESSIONAL RELEASE

True release omissions: one immutable source/artifact identity, completed compatibility matrix, qualified installer/upgrade recovery, consistent save/load error feedback, useful long-operation feedback, and clear supported-format/OS/known-limit documentation tied to the shipped build. Source and notices must accompany the actual distribution as applicable; adding copy commands is not final package verification.

Optional features: additional DAW formats, new DSP engines, more presets, broader editing features, and online updates for a deliberately offline release. These should not displace the acceptance gates above.

## IF WE RELEASED RIGHT NOW

Customers would receive an internal-alpha product line whose strongest tested pieces are the shared engine/processor contracts and newly hardened preset/Hub paths. We cannot yet guarantee that their actual host scans the final bundles, recalls their entire session, restores sample sources, produces correct offline bounces, or delivers reliable low-latency monitoring. Some preset failures would be silent; a large Hub operation could appear unresponsive. The corrected local website no longer represents old test counts as current qualification. It has not been deployed.

## RELEASE CHECKLIST

- [x] Concrete preset rejection defects fixed and fresh regression-tested.
- [x] Hub resource loading and primary install rollback fixed and fixture-tested.
- [x] Release tests isolated from the user's live session bus.
- [x] Packaging source guard and required notice copy commands tested.
- [x] Fresh scoped native Release builds/tests; web production build/typecheck/audit.
- [x] Synthetic CPU and engine latency measured with explicit limitations.
- [!] Full native gate incomplete; missing executables and timeouts unresolved in the release tree.
- [ ] Exact complete shipping AU/VST3/standalone artifacts and installer qualified.
- [ ] Supported DAW scan, state recall, automation, bounce, transport, key handoff, and device matrix.
- [ ] User listening approval of the exact candidate.
- [ ] Long-session/memory-growth/UI responsiveness and large-operation validation.
- [ ] Final distribution platform and legal/source packaging qualification; excluded work remains excluded.
- [ ] Public website deployment and downloadable-release consistency.

## FINAL VERDICT

**Do not give this exact current build to thousands of paying customers.** The maximum useful work now is release closure, not more features:

1. Freeze an explicit product/format/OS support scope and a source revision, including the preserved existing changes and these fixes. A smaller first release is an option requiring a product decision, not a scope change made by this audit.
2. Fix the P1 workflow omissions within that scope, rebuild the entire relevant Release test lane on a quiet machine, and resolve every failure/timeout. Do not reuse mixed-age binaries to claim a green gate.
3. Assemble one traceable package; verify actual models/runtime assets/notices, clean install, upgrade/rollback, and launch. Signing/notarization remains excluded until separately authorized.
4. Run each promised workflow in supported DAWs against that exact package: scan, key analysis/handoff, tuning/harmonies, edits, presets/project recall, automation, offline bounce, and device/rate/buffer changes. Complete the user's listening gate, then publish only the capabilities and compatibility actually proven.

The pass produced meaningful, tested improvements. It did not turn incomplete release evidence into a production approval.
