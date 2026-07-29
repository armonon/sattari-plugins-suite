# Sattari Hub

Truth label: **internal alpha installer/updater app**. This is not notarized, not a public download, and not sale-ready.

Sattari Hub is the Mac-first downloadable application layer for the Sattari Audio Suite. It is designed to become the single app users download to install, update, and open all Sattari software.

## What it does now

- Loads `sattari-products.json` from the app resources.
- Shows the 48 non-StemDeck products generated from `config/products.json`, with
  lifecycle status and a three-product internal alpha profile.
- Installs a product only when every declared AU, VST3, and Standalone bundle is
  present, executable, staged, copied, and post-copy validated.
- Rolls an interrupted/failed multi-bundle install back instead of writing a
  partial receipt.
- Accepts remote archives only when the manifest pins both SHA-256 and byte size;
  archive paths and links are checked before extraction.
- Writes install receipts to `~/Library/Application Support/Sattari Hub/receipts.json`.
- Treats version mismatch as an update, but batch install is limited to Brain,
  Arp, and Sub Conjurer rather than presenting 48 experiments as release-ready.
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
