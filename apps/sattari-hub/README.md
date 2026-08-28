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
- Accepts remote archives only when the manifest pins both SHA-256 and byte size;
  archive paths and links are checked before extraction.
- Writes install receipts to `~/Library/Application Support/Sattari Hub/receipts.json`.
- Treats version mismatch as an update, but installation is limited to the four
  qualified internal-alpha plugins: Compass, Auto Pitch, Arp, and Sub Conjurer.
- Disables installation for preview and experimental components instead of
  presenting unqualified products as available downloads.
- Backs up replaced bundles under `~/Library/Application Support/Sattari Hub/Backups/` before copying a new build.

## Build

From `repos/sattari-plugins`:

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
- Instruments, Vocal, Mix, and Create are customer-facing product directions,
  but their primary rack plugins remain preview builds; their component lists
  communicate scope, not release readiness.
