# Sattari Audio v0.2 closed-beta protocol

Truth label: **engineering candidate, not a public or sale-ready release**.

## Shipping scope

The internal Hub package contains all six primary products: Sattari Compass,
Auto Pitch, Vocal, Mix, Create, and Stack. Sattari Arp and Sub Conjurer are also
included as qualified component plug-ins. The remaining components stay
validation or experimental until their product-level evidence is complete.

## Tester sequence

1. Record the package SHA-256 and current Git commit in the issue/report.
2. Install through Sattari Hub, restart the DAW, and force a plugin rescan.
3. Launch the DAW from Hub with a fresh Project Session ID.
4. Run the machine gates in the included validation guide.
5. Complete one full musical flow: Compass -> Auto Pitch -> Vocal -> Mix, plus
   Stack and Create in the same reference session.
6. Open a second DAW under a different Project Session ID and verify no session bleed.
7. For Auto Pitch, render owned/cleared real vocals in Natural and Hard modes and
   complete the generated listening report.
8. Save, close, reopen, automate controls, and compare realtime playback with an
   offline bounce.

## Required report fields

- macOS version, Mac model/chip/RAM, DAW/version, plugin format
- sample rate, buffer size, audio interface/driver
- exact reproduction steps and expected/actual behavior
- project session ID (non-sensitive test ID only)
- crash log or host diagnostic report when applicable
- short owned/cleared audio excerpt only when permission allows sharing
- severity: blocker, major, moderate, cosmetic

## Hard stops

- crash, hang, NaN/Inf, full-scale burst, lost state, corrupt project, or cross-project session bleed
- Auto Pitch crackle, consonant chirps, severe formant instability, or correction targeting the wrong note
- plugin scan rejection in any supported shipping format
- installer payload mismatch or failed embedded-model verification

Do not share the installer or audio corpus publicly. Developer ID signing and Apple
notarization are not present until the release record explicitly says otherwise.
