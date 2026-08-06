# Sattari Audio — Licensing

**The entire Sattari Audio suite is distributed under the GNU General Public
License v3 (GPLv3).** See [`LICENSE`](LICENSE) for the full text.

## Why GPLv3 (and why the whole suite)

Two dependencies drive this, and GPLv3 is the common denominator that satisfies both:

| Dependency | Its terms | Used by |
|---|---|---|
| **JUCE** | Paid license **or** GPLv3 | the full plugin suite |
| **Rubber Band** | Commercial license **or** GPLv2-or-later | time/pitch products that link it, including Auto Pitch |

We are not holding a commercial JUCE or Rubber Band license, so:

- JUCE under its open-source terms requires **GPLv3** for every distributed plugin → the whole suite is GPLv3.
- Rubber Band is "GPLv2 **or later**," which **includes GPLv3**, so products that link it are covered by the same GPLv3.

There is therefore **one license for everything** and no per-product carve-outs.

## What GPLv3 means in practice

GPLv3 does **not** stop you from selling the plugins. It changes *how*:

- **You may sell binaries.** Charge for prebuilt, signed, notarized installers; for
  presets, support, updates, bundles — anything.
- **You must offer the complete corresponding source** (under GPLv3) to anyone you
  give a binary to.
- **Recipients may redistribute** the binary and source freely, including for free.

So the durable business model is **convenience and trust, not exclusivity**: people
pay you for the packaged, supported, easy-to-install product even though the source
is open. (Ardour, Surge XT, Vital's engine, and many others ship this way.)

## What you must do when you distribute

1. Include `LICENSE` (GPLv3) with every distribution.
2. Make the **exact source** that produced the binary available to recipients
   (a public git tag/release is the easy way).
3. Keep the upstream license notices intact:
   - Rubber Band: `third_party/rubberband/COPYING`
   - JUCE: `third_party/JUCE/LICENSE.md`
   - CREPE model: `plugins/auto-pitch/models/CREPE-LICENSE.txt`
4. Don't add further restrictions on top of GPLv3 (no "you may not redistribute" EULA).

## Splash screen

Under GPLv3 you are **not** bound by JUCE's free-tier splash-screen requirement, so
the build sets `JUCE_DISPLAY_SPLASH_SCREEN=0` (in `cmake/SattariPlugin.cmake`). This
is legitimate *only because* the suite is GPLv3 — do not disable it under a JUCE
"Personal" (non-GPL) license.

## If you ever want closed-source / paid-exclusive

You would need to **buy** the relevant commercial licenses instead:
- A commercial **JUCE** license (covers the suite), and
- A commercial **Rubber Band** license for every product that links it —
  or replace Rubber Band with a permissive engine such as Signalsmith Stretch (MIT).

Until then, GPLv3 is the license and closed-source distribution is not permitted.

*This is a plain-language summary, not legal advice; `LICENSE` is the binding text.*
