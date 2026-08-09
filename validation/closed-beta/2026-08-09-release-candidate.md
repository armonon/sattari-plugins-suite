# Sattari Audio v0.2 closed-beta build record — 2026-08-09

Truth label: **controlled internal beta candidate; not signed, notarized, public, or sale-ready**.

Source commit: `6e0cd25e8d6f1d765a02de1cf856971b6dbadb9a`

## Machine evidence

- Local non-StemDeck CTest lane: **91/91 passed** in 174.11 seconds.
- Sattari Compass: contract passed; `aumf/Br01/Satr` AU passed auval; VST3 passed pluginval strictness 5.
- Sattari Auto Pitch: neural bundle/runtime verification passed for AU, VST3, and standalone; AU passed auval; VST3 passed pluginval strictness 5.
- Sattari Hub: release build and packaged binary contract passed with 48 products, 143 bundle records, 25 locally available products, and 0 public-download products.
- Internal installer: expanded successfully; contains Sattari Hub plus 11 alpha payload bundles.
- Website: Sites version 8 published from its pushed source commit with current Compass, Auto Pitch Pro, and eight-stage Vocal renders.

## Artifacts

| Artifact | SHA-256 |
|---|---|
| `build/release-candidate/Sattari-Audio-Suite.pkg` | `a1ab1ff7c20640e79e9cce1f2bbdc6916f9db31d3072ab7b8f804360d60e655a` |
| `build/tester-packages/sattari-auto-pitch-neural-tester-v0.4.1.tar.gz` | `0046e1e67973498174b82e8b5f2af222bc3dd828c8bd179eade64bf5e3d74c76` |
| `build/closed-beta/Sattari-Audio-Closed-Beta-v0.2.0-alpha.tar.gz` | `c5260bdec09c74d1100acb297fe2e75a41138d9b2eff01d54bd0ce02f2f4257a` |

The suite package is ad-hoc/internal and `pkgutil --check-signature` correctly
reports no Developer ID package signature.

## Open gates

1. Fill the real-vocal corpus with at least five owned/cleared clips covering all required vocal tags, render, and score it.
2. Complete the Logic Pro Creator Studio, Ableton Live 12 Suite, and FL Studio 2026 host matrix.
3. Rebuild and machine-validate Sattari Arp and Sub Conjurer at package time.
4. Install Developer ID Application and Installer certificates and configure the `sattari-notary` keychain profile.
5. Repackage, sign, submit, staple, and verify the final public artifact.
