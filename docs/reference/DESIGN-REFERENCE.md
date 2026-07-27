# StemDeck Visual Reference — how to use this

This folder is the **visual source of truth** for the StemDeck perform screen. It exists because prose specs
underdetermine visual execution — two engineers reading the same paragraph produce two different-looking
results. These files remove the guesswork.

## Files

- **`stemdeck-components.html`** — open in a browser. Every core component rendered live, several interactive
  (drag the knobs, click the segmented tabs, click the Camelot wheel). This is the target. A built component
  should be indistinguishable from its reference here.
- **`tokens.css`** — the exact colors, spacing, radii, type sizes, and motion values every component uses.
  This is the enforceable part of "looks good."

## The rule

**When a built component looks different from its reference here, the reference wins.**

Match to **token names, not eyeballed values**:
- Not "dark panel" → `--bg-panel` (#101014)
- Not "quick fade" → `--dur-state` (160ms) with `--ease`
- Not "some padding" → the 4px spacing scale (`--gap-4` = 5px is the default sibling gap)

## Porting to JUCE

1. Read `tokens.css` first. Recreate it as JUCE constants/`LookAndFeel` values — one source of truth in code,
   mirroring this file. Every colour, radius, and font size a component uses must come from there.
2. Build **one component to pixel-parity first** (suggest the stem gain knob or the stem column) against its
   reference here. That calibrates the visual language; everything else copies its patterns.
3. Then build the rest, checking each against its reference in the browser side-by-side.

## The two things that make it look right

1. **Restraint, not addition.** These look good because of what's *left out* — generous space, two type
   weights, one accent doing meaningful work, nothing rendered louder than it deserves. The failure mode is
   rendering a feature list at uniform weight. If a built screen looks busy, the fix is **remove and enlarge**,
   never add.
2. **Visual weight matches importance.** Three tiers: primary (play, cue, big numerics) large and full-contrast;
   secondary (loop, sync, EQ) medium; tertiary (hotcues, mode tags) small and ~55% opacity. This is a
   proportion you *feel*, and it's the single instruction most likely to get flattened — check it hardest.

## Components included

0. Palette
1. Stem gain knob (0/100/200 center-unity) — interactive
2. Vertical stem column
3. Segmented deck panel — interactive
4. Channel states (active vs idle)
5. FX chain (three scopes)
6. Camelot wheel — interactive
7. Dual-tempo panel

Components not yet drawn (build against the written briefs, then add here): master-stems console section,
whole-track overview strip, populated MIDI instrument body, the full assembled focus-model layout.
