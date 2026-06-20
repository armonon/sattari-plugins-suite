# Sattari Hub

Truth label: **internal alpha installer/updater app**. This is not notarized, not a public download, and not sale-ready.

Sattari Hub is the Mac-first downloadable application layer for the Sattari Audio Suite. It is designed to become the single app users download to install, update, and open all Sattari software.

## What it does now

- Loads `sattari-products.json` from the app resources.
- Shows all current Sattari Audio products in one hub:
  - Sattari Auto Pitch
  - Sattari VoxKey
  - Sattari StemDeck
  - Sub Conjurer
  - Side Chain Master
  - Sattari Royal Chain
  - Sattari Arp
- Installs available AU, VST3, and Standalone app bundles from local build/package paths.
- Writes install receipts to `~/Library/Application Support/Sattari Hub/receipts.json`.
- Treats version mismatch as an update.
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

- Local/internal update channel only; no signed remote update feed yet.
- No Sparkle or update-server integration yet.
- No notarized DMG/PKG yet.
- No license/login/download entitlement layer yet.
- Product quality remains gated by each plugin's own DAW/listening validation.
