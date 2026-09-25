# Clean-machine qualification: Sattari Auto Pitch + Sattari Key (macOS)

Candidate: tag `auto-pitch-rc-baseline-20260925` (cbd1ce4), or its signed successor.
Release rule (2026-09-25): results count only for the exact tagged candidate. Record the tag
and artefact sha256 below. The script also records a signature-removed sha256 for every
binary, so a Developer ID signed build can still be matched to the unsigned candidate.

Operator: ________  Date: ________  Machine: ________ (VM? Y/N)  macOS: ________  Arch: ________

## 0. Before you start

- [ ] Use a fresh macOS VM or Mac (macOS 13.3 or later), signed in to a normal GUI session.
      Do not use your dev machine and do not reuse a machine a previous run touched. If you
      can, also run once on Intel or under Rosetta.
- [ ] Do not install Xcode or Homebrew. The script only needs tools that ship with macOS.
- [ ] Download the artefact (signed `.pkg`, `.dmg` or tester `.zip`) **with Safari**, so it
      gets a real quarantine flag. Download `scripts/clean_machine_qualify.sh` the same way.
- [ ] Optional: install pluginval to `/Applications`. It is used if present, never required.
- [ ] Optional: to prove the build is the candidate, bring the candidate's `fingerprints.tsv`.
      Make it on the build machine with
      `scripts/clean_machine_qualify.sh --fingerprint-only --bundles <Release dir or kit> --candidate <tag>`.

## 1. Automated run (Terminal, in the GUI session, not over ssh)

```bash
chmod +x clean_machine_qualify.sh
./clean_machine_qualify.sh --dry-run --pkg ~/Downloads/<installer>.pkg      # read the plan first
./clean_machine_qualify.sh --pkg ~/Downloads/<installer>.pkg \
    --candidate <tag> [--expect-team <TEAMID>] [--expect-fingerprints fingerprints.tsv]
# tester kit instead of a pkg:  --bundles ~/Downloads/<kit folder or .zip>
```

- Exit code 3 means the machine was not clean. Nothing was changed. Use a fresh machine.
- `sudo` prompts appear for a pkg install/uninstall only.
- If the Suite pkg installs only Sattari Hub, the script pauses. Install Auto Pitch and Key
  from the Hub, write down every dialog, quit the Hub, and press Return.
- The run leaves the reinstalled plug-ins in place for sections 2–3. When you are done:
  `./clean_machine_qualify.sh --uninstall-from <results>/manifest-reinstall.tsv`

**Expected until the owner signs and notarizes:** every *identity / Gatekeeper /
notarization / Team ID* check FAILs, and the quarantine copies are blocked. That is the
correct result for an ad-hoc build. Run the full kit again on the signed candidate.

## 2. Manual steps

| # | Step | PASS/FAIL | NOTES (exact dialog text, screenshots) |
|---|---|---|---|
| M1 | Gatekeeper: double-click each item in `<results>/quarantine-copies/`. A pkg must open Installer without "cannot be opened because Apple cannot check it" or "damaged". An app may show "downloaded from the Internet" at most. | | |
| M2 | Installer UI: install the original download by double-clicking it (on a second clean machine, or after `--uninstall-from`). Check each pane, the licence text, the destination, and the "installed successfully" message. | | |
| M3 | Standalone first launch, **from Finder**: Auto Pitch.app and Key.app. Note any Gatekeeper and microphone prompts. Pick an audio device and check the UI opens. Quit and relaunch: there should be no dialog the second time. | | |
| M4 | Standalone: save a user preset in Auto Pitch. Write down where the file appears. Expected: `~/Library/Sattari Audio/Presets/auto-pitch/`, although a header comment says `~/Library/Application Support/Sattari Audio/Presets`. Reopen the app and load the preset. | | |
| M5 | Logic Pro: Plug-in Manager, Reset & Rescan the Sattari plug-ins. Both must say "successfully validated". Insert Auto Pitch and Key. Check the Auto Pitch factory presets menu (Natural, Pitch Center, Hard Tune, ...). | | |
| M6 | Logic: save the project, quit, reopen. Check the settings are restored and there is no "missing plug-in" warning. | | |
| M7 | Ableton Live: turn on AU and VST3 system folders and rescan. Insert both (VST3, then AU). Save, quit, reopen. | | |
| M8 | FL Studio: Manage plugins, Find installed plugins (Verify). Insert both. Save, quit, reopen. | | |
| M9 | Run the full host matrix in `validation/daw/2026-09-25-daw-qualification-checklist.md`. | | |
| M10 | Uninstall: run `--uninstall-from`, then rescan in each DAW. The plug-ins must be gone, with no crash. Record anything left in `~/Library/Sattari Audio` (runtime data). | | |
| M11 | Reinstall over nothing: install again, open the M6 project, and check it loads the same. | | |

## 3. Automated results (copy from `summary.md`, Phases table)

| Phase | PASS/FAIL/SKIP | NOTES |
|---|---|---|
| preflight (clean machine proven; SIP, Gatekeeper, macOS, arch) | | |
| inventory (both products, AU+VST3+Standalone present; fingerprints) | | |
| signing (codesign strict, Developer ID, Team ID, runtime, timestamp, spctl, stapled, universal, self-contained load paths) | | |
| quarantine (downloaded copy passes Gatekeeper) | | |
| install (paths, sha256, permissions, embedded runtime, factory presets) | | |
| discovery (auval -a, auval -v per AU, VST3 sanity, pluginval) | | |
| uninstall (manifest removed, receipts forgotten, AU registry clean) | | |
| reinstall (files + auval again) | | |

## 4. What to send back

Zip and send the whole results folder `sattari-clean-machine-results/<stamp>-<host>/`,
**without** `quarantine-copies/` (it is large). The folder contains `summary.md`,
`results.tsv`, `evidence/`, `actions.log`, `manifest-*.tsv` and `fingerprints-*.tsv`.
Also send:

- this checklist, filled in;
- screenshots of every Gatekeeper, Installer and permission dialog, and every DAW scan result;
- the DAW checklist results;
- the artefact's filename and sha256, and the tag you tested.
