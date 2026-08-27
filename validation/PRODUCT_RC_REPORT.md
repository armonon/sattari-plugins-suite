# Auto Pitch — Product RC Campaign Report
**Date:** 2026-08-25  
**Engine freeze commit:** b939124 — **NOT PRESENT IN THIS REPOSITORY**  
**Base commit (Intent v3 merge):** d92093f — **NOT PRESENT IN THIS REPOSITORY**  
**Strategy G commit:** 6c022cb — **NOT PRESENT IN THIS REPOSITORY**  
**Plugin version:** 0.1.4 — **STALE**: the tree declares 0.5.0
(`plugins/auto-pitch/CMakeLists.txt`, `config/products.json`), and every other
record in `validation/auto-pitch/` calls the candidate 0.5.0 RC3.  
**Plugin ID:** aufx / Ap01 / Satr (com.Sattari.SattariAutoPitch)  
**Formats:** AU (universal x86_64+arm64), VST3 (universal x86_64+arm64)

---

## Phase 1 — Host Matrix

| Host | Version | Format | Status | Notes |
|------|---------|--------|--------|-------|
| Ableton Live | 12 Suite | AU/VST3 | ⚠ MANUAL REQUIRED | Installed. Host launch requires UI. |
| Logic Pro | — | AU | ⚠ MANUAL REQUIRED | Installed. Host launch requires UI. |
| FL Studio | 2025 | VST3 | ⚠ MANUAL REQUIRED | Installed. Host launch requires UI. |
| REAPER | — | — | NOT INSTALLED | — |
| Studio One | — | — | NOT INSTALLED | — |
| Cubase | — | — | NOT INSTALLED | — |

**Automated validations (host-independent):**  
- AU format: auval PASS (aufx Ap01 Satr)  
- VST3 format: codesign PASS, Info.plist PASS  
- Latency reporting: auval VERIFYING PROPERTY: Latency PASS  
- Bundle structure: AU + VST3 both universal binary (x86_64 arm64)

---

## Phase 2 — Audio Configuration Matrix

### Sample rate × Block size (v2_active, sine 440Hz A minor)

| SR | bs=32 | bs=64 | bs=128 | bs=256 | bs=512 | bs=1024 |
|----|-------|-------|--------|--------|--------|---------|
| 44100 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 48000 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 88200 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 96000 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |

24/24 PASS — crashes=0, NaN/Inf=0

### Edge cases

| Case | Result |
|------|--------|
| Silence | ✅ |
| Very low level (-60dBFS) | ✅ |
| Clipped (2× amplitude hard clip) | ✅ |
| Rapid chromatic run | ✅ |
| Vibrato | ✅ |
| Long sustained (30s) | ✅ |
| Stop/start (gap silence) | ✅ |

7/7 PASS

### Latency
- RubberBand OptionPitchHighQuality realtime mode
- Reported via setLatencySamples (auval PASS)
- Low-latency mode: OptionWindowShort (host-selectable)

---

## Phase 3 — Automation Torture

| Sweep | Cases | Result |
|-------|-------|--------|
| Amount (0.0→1.0, 5 values) | 5 | ✅ |
| Speed (0.0→1.0, 5 values) | 5 | ✅ |
| All 12 keys | 12 | ✅ |
| All 3 scales | 3 | ✅ |
| Bypass (amount=0) | 1 | ✅ |
| Hardtune (amount=1, speed=1) | 1 | ✅ |

27/27 PASS — crashes=0, NaN/Inf=0

---

## Phase 4 — Session Stability

- 10× extended 30-second runs, block=256, v2_active
- Crashes: 0
- NaN/Inf: 0
- Total wall time: 59.9s
- Status: PASS

---

## Phase 5 — State / Preset Validation

- 3 preset configurations, 3 runs each
- Determinism: output WAV MD5 identical across all runs
- Presets tested: C major / F# minor / A chromatic
- Status: PASS

---

## Phase 6 — Real Vocal Corpus Expansion

15 synthetic vocal types across registers and voice characters:

| Type | Result |
|------|--------|
| male_low_E2 | ✅ |
| male_low_A2 | ✅ |
| male_mid_E3 | ✅ |
| male_high_B3 | ✅ |
| male_high_E4 | ✅ |
| female_low_A3 | ✅ |
| female_mid_D4 | ✅ |
| female_high_A4 | ✅ |
| female_high_E5 | ✅ |
| soft_breathy_A3 | ✅ |
| soft_breathy_A4 | ✅ |
| vibrato_A3 | ✅ |
| vibrato_A4 | ✅ |
| vibrato_heavy_A4 | ✅ |
| vibrato_breathy_A4 | ✅ |

15/15 PASS — crashes=0, NaN/Inf=0

No systemic severe artifact found. RUN_up_350ms ascending-run transient remains the only known moderate artifact (documented in known issues).

---

## Phase 7 — Release Packaging

| Check | Result |
|-------|--------|
| AU codesign | ✅ PASS |
| VST3 codesign | ✅ PASS |
| AU Info.plist | ✅ PASS |
| VST3 Info.plist | ✅ PASS |
| User install path (AU) | ✅ INSTALLED |
| User install path (VST3) | ✅ INSTALLED |
| System install path | ⚠ Not installed (user install only) |
| Notarization | ⚠ NOT NOTARIZED — dev build, requires Apple signing credentials for distribution |
| Universal binary | ✅ x86_64 + arm64 |
| Plugin ID (type) | aufx |
| Plugin ID (subtype) | Ap01 |
| Plugin ID (manufacturer) | Satr |
| Bundle ID | com.Sattari.SattariAutoPitch |
| Version | 0.1.4 |

---

## Phase 8 — UI Product QA

⚠ MANUAL REQUIRED — UI validation requires host launch.  
Automated coverage: parameter names/ranges verified via bench --help and trace keys.

Parameters confirmed exposed:
- key (C..B)
- scale (major/minor/chromatic)
- amount (0..1)
- speed (0..1)
- block size (internal, 512 default)
- intent-mode (production/v2_shadow/v2_active)

---

## Known Accepted Risk (Permanent)

```
RUN_up_350ms
  max_jump: 248.5¢ < 350¢ gate
  perceptual proxy: 0 SEVERE / 120 MODERATE
  trigger: ascending vocal run, CME vel ≥ 28 MIDI/sec, conf ≥ 0.99
  SLIDE_DESTINATION_V2 fires, transient 11.6ms, PASSING_TONE recovers in 1-2 frames
  Documentation: validation/torture/RUN_up_350ms/manifest.json
  Regression test: permanent, must pass max_jump < 350¢ on every build
```

---

## Gate Summary

| Gate | Status |
|------|--------|
| Engine freeze intact | ✅ FROZEN (b939124) |
| Host tests AU | ✅ auval PASS (automated) |
| Host tests DAW | ⚠ MANUAL PENDING (Ableton/Logic/FL) |
| AU/VST3 valid | ✅ PASS |
| crashes=0 | ✅ |
| NaN/Inf=0 | ✅ |
| State-loss bug | ✅ NONE (deterministic) |
| Automation artifacts | ✅ NONE (27/27 param sweep clean) |
| Major latency mismatch | ✅ NONE (auval PASS) |
| Long-session stability | ✅ PASS |
| Packaging/install | ✅ PASS (notarization pending for distribution) |
| Real-vocal expansion | ✅ PASS (15/15, no systemic severe artifact) |

---

## Verdict

```
PRODUCT RC HOLD — MANUAL HOST TESTING PENDING
```

> ### CORRECTION — 2026-08-27
>
> **The "ALL PASS" line below was false when written and stayed false for two
> days.** The default `AutoPitchRender` golden contract — the only gate in this
> repository that can observe a change in the printed sound — was FAILING at
> residual 0.173507 (print peak 0.4305 -> 0.2570, a 4.5 dB drop), and
> `scripts/verify_all.sh` exited 1 with VERIFY FAILED. CI stayed green
> throughout because CI runs only `--smoke` and `--hitracktest`, neither of
> which could see it.
>
> This mattered because the only remaining condition this report placed on
> shipping was the manual host matrix. Anyone who ran that matrix and passed it
> would have shipped on the strength of a green automated net that was red.
>
> **Root cause of the drift (diagnosed 2026-08-27):** the golden was blessed at
> `4512076` on a branch that had not yet absorbed `5438e30` "Fix Auto Pitch
> distortion" (2026-08-06). That commit removed a Modern Rap edge enhancer whose
> differentiator reset to zero at every block boundary, injecting a
> discontinuity and an inflated peak on the first sample of every block. The
> golden therefore encoded the *pre-fix* sound; the drift was an intended
> quality improvement arriving by merge, not a regression. The golden has been
> re-blessed against the corrected engine and now passes at residual 4.9e-7.
>
> **Also fixed so this cannot recur silently:** `--smoke` (which CI *does* run)
> now carries a portable peak/RMS envelope check against the golden at 25%
> tolerance. The strict 1e-4 fingerprint remains local-only for the documented
> reason that Rubber Band output is not bit-portable — but a change large enough
> to hear now fails in CI. The drop that hid here was 40% of peak.
>
> Full record: `validation/auto-pitch/2026-08-27-review-fixes.md`.

**Automated gates: ALL PASS** *(see the correction above — this was not true when
written; it is true as of 2026-08-27, after the re-bless and the fixes recorded
in `validation/auto-pitch/2026-08-27-review-fixes.md`.)*

**Blocker:** Phases 1, 8 require manual DAW launch (Ableton Live, Logic Pro, FL Studio).  
Once manual host matrix passes, status upgrades to:

```
PRODUCT RC PASS
```

**Do not upgrade this status on the strength of the line above alone.** Re-run
`scripts/verify_all.sh` and confirm it exits 0 at the commit you intend to ship;
that is the check this report failed to make.

---

## Release Candidate Checklist (pending manual host sign-off)

1. [ ] Ableton Live 12: insert, bounce, export, preset cycle
2. [ ] Logic Pro: insert, bounce, export, preset cycle, AU validation
3. [ ] FL Studio 2025: insert, bounce, export, preset cycle (VST3)
4. [ ] Notarize for external distribution (requires Apple Developer signing credentials)
5. [ ] System-wide install test (copy to /Library/Audio/Plug-Ins/)
6. [ ] UI labels, tooltips, Retina rendering review

---

## Release Candidate Info (contingent on manual sign-off)

- **Build:** 0.1.4 (b939124)
- **Supported formats:** AU, VST3
- **Supported sample rates:** 44.1 / 48 / 88.2 / 96 kHz
- **Supported block sizes:** 32–1024
- **Universal binary:** Yes (x86_64 + arm64)
- **Rollback:** flip `kSlideDestinationV2Enabled = false` in `PluginProcessor.h` and rebuild
- **Rollback commit:** 6c022cb (Strategy G, pre-Intent-v3)
- **Known issues:** RUN_up_350ms ascending-run transient (248.5¢, MODERATE, non-severe)
