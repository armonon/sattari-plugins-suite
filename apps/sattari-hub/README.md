# Sattari Hub

Truth label: **internal alpha installer/updater app**. This is not notarized, not a public download, and not sale-ready.

Sattari Hub is the Mac-first downloadable application layer for the Sattari Audio Suite. It is designed to become the single app users download to install, update, and open all Sattari software.

## What it does now

- Loads `sattari-products.json` from the app resources.
- Presents six customer-facing products generated from `config/products.json`:
  Compass, Auto Pitch, Instruments, Vocal, Mix, and Create.
- Expands each product into its implementation components only when requested,
  so the customer sees a coherent suite instead of 48 unrelated plugin cards.
- Installs a product only when every declared AU, VST3, and Standalone bundle is
  present, executable, staged, copied, and post-copy validated.
- Rolls an interrupted/failed multi-bundle install back instead of writing a
  partial receipt.
- Performs install/update work on a serial background worker, with progress,
  cancellation, and close/quit protection while recovery is in progress. A copy
  already underway finishes before cancellation rolls back the current product;
  products completed earlier in a batch remain installed.
- Reports secondary recovery failures with the backup location instead of
  concealing them behind a generic cancellation message.
- Accepts remote archives only when the manifest pins both SHA-256 and byte size;
  archive paths and links are checked before extraction.
- Writes install receipts to `~/Library/Application Support/Sattari Hub/receipts.json`.
- Treats version mismatch as an update. Installation eligibility comes from the
  generated catalog's shipping profile and complete local payload availability;
  an `alpha` profile is not evidence that this particular build passed release
  qualification.
- Disables installation for preview and experimental components instead of
  presenting unqualified products as available downloads.
- Backs up replaced bundles under `~/Library/Application Support/Sattari Hub/Backups/` before copying a new build.

## Build

From the repository root, the test-only Release path is:

```bash
swift build --package-path apps/sattari-hub --scratch-path build/hub-release-readiness -c release
SATTARI_HUB_OFFLINE=1 build/hub-release-readiness/release/SattariHub --contract-check
```

The contract exercises isolated fixture bundles and AppKit cancellation controls;
it does not install into the user's plug-in folders or qualify a real DAW bundle.

The broader validation/package commands below can install or sign bundles. Do
not run them while those operations are excluded from the current release scope:

```bash
./scripts/validate_sattari_hub.sh
./scripts/package_sattari_hub_alpha.sh
```

The package script writes:

```text
deliverables/sattari-test-builds/sattari-hub-alpha-v0.1.0/Sattari Hub.app
deliverables/sattari-test-builds/sattari-hub-alpha-v0.1.0.tar.gz
```

## Run from source

```bash
cd apps/sattari-hub
SATTARI_REPO_ROOT=/path/to/repos/sattari-plugins swift run SattariHub
```

## Headless contract check

```bash
cd apps/sattari-hub
SATTARI_REPO_ROOT=/path/to/repos/sattari-plugins swift run SattariHub --contract-check
```

## Current limitations

- Local/internal update channel only; the remote feed is disabled until an
  integrity pin is configured.
- No Sparkle or update-server integration yet.
- No notarized DMG/PKG yet.
- No license/login/download entitlement layer yet.
- Product quality remains gated by each plugin's own DAW/listening validation.
- Catalog lifecycle/profile labels and the six product groupings communicate
  intended availability, not release approval. The exact artifacts still need
  the [release qualification gates](../../docs/release-qualification.md).
