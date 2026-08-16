#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// DesignTokens — a 1:1 mirror of docs/reference/tokens.css, which is the
// ENFORCEABLE source of truth for the perform screen's visual language.
//
//   "Looks consistent" == "uses these tokens". Do not introduce ad-hoc values.
//   When a built component looks different from its reference, the REFERENCE WINS.
//
// If tokens.css changes, change this file in the same commit. Every colour, radius,
// type size and duration a component uses must come from here — not from a hex
// literal typed at the call site (which is how the old palette drifted: it had
// kPanel = #131316 where the token is #101014, and kPanelDeep = #0B0B0D where the
// token is #0E0E11).
// ─────────────────────────────────────────────────────────────────────────────

// juce_graphics only (Colour / Font), NOT JuceHeader.h — the standalone component
// render targets don't link Tracktion, and pulling the umbrella header in here broke
// them. Tokens must stay cheap enough for any target to include.
#include <juce_graphics/juce_graphics.h>

namespace stemdeck::tokens
{

// ── Surfaces (darkest → lightest, by nesting depth) ──────────────────────────
inline constexpr juce::uint32 bgApp          = 0xff0D0D0F;   // deepest outer frame
inline constexpr juce::uint32 bgPanel        = 0xff1A1A1E;   // primary console panel
inline constexpr juce::uint32 bgPanel2       = 0xff202025;   // nested channel / inspector panel
inline constexpr juce::uint32 bgWell         = 0xff0A0A0C;   // recessed waveform / LCD well
inline constexpr juce::uint32 bgWell2        = 0xff08080A;   // deepest meter / timeline recess
inline constexpr juce::uint32 bgControl      = 0xff232328;   // raised control face
inline constexpr juce::uint32 bgControlHover = 0xff2C2C33;
inline constexpr juce::uint32 bgTile         = 0xff1E1E23;   // insert tile / knob face
// Not a named var in tokens.css: the reference's channel components use this inline
// for an IDLE channel panel. Recorded here so it stays a token, not a call-site hex.
inline constexpr juce::uint32 bgIdle         = 0xff141417;   // idle channel panel

// ── Borders ──────────────────────────────────────────────────────────────────
inline constexpr juce::uint32 borderSubtle = 0xff2A2A30;
inline constexpr juce::uint32 borderFaint  = 0xff17171B;     // stem-lane dividers, grid

// ── Foreground (opacity ladder off #F2F2F5) ──────────────────────────────────
inline constexpr juce::uint32 fgPrimary   = 0xffE0E0E4;      // headline numerics, active
inline constexpr juce::uint32 fgSecondary = 0xffA0A0A8;      // body copy
inline constexpr juce::uint32 fgTertiary  = 0xff808088;      // labels, inactive
inline constexpr juce::uint32 fgMuted     = 0xff505058;      // captions, hints
inline constexpr juce::uint32 fgFaint     = 0xff34343B;      // disabled, dividers

// ── Warn / meter ─────────────────────────────────────────────────────────────
inline constexpr juce::uint32 signal   = 0xff00FF87;         // active / signal present LED
inline constexpr juce::uint32 selected = 0xff00D4FF;         // focused control / send
inline constexpr juce::uint32 effects  = 0xffA855F7;         // automation / effects
inline constexpr juce::uint32 warn     = 0xffFFB800;         // solo / caution
inline constexpr juce::uint32 warn2    = 0xffFFB800;         // take marker / caution
inline constexpr juce::uint32 meterHot = 0xffFF3B30;         // clip / overload / record

// ── Deck accents (distinguishable under protanopia/deuteranopia) ─────────────
inline constexpr juce::uint32 deckA = 0xff4AD9C4;   // teal
inline constexpr juce::uint32 deckB = 0xff4A9EFF;   // blue
inline constexpr juce::uint32 deckC = 0xffB47AFF;   // violet
inline constexpr juce::uint32 deckD = 0xffE8A54A;   // amber

// ── Stem colours (FIXED IDENTITY — never remap) ──────────────────────────────
inline constexpr juce::uint32 stemVox      = 0xffD4537E;   // pink
inline constexpr juce::uint32 stemVoxLight = 0xffED93B1;
inline constexpr juce::uint32 stemDrm      = 0xff4A9EFF;   // blue
inline constexpr juce::uint32 stemDrmLight = 0xff85B7EB;
inline constexpr juce::uint32 stemBas      = 0xff4AD9C4;   // teal
inline constexpr juce::uint32 stemBasLight = 0xff5DCAA5;
inline constexpr juce::uint32 stemOth      = 0xff888780;   // gray
inline constexpr juce::uint32 stemOthLight = 0xffB4B2A9;

// ── Radii (by nesting depth — larger outside, tighter inside) ────────────────
inline constexpr float radiusPanel   = 9.0f;   // deck / channel / console group
inline constexpr float radiusWell    = 7.0f;   // well or tile inside a panel
inline constexpr float radiusControl = 6.0f;   // button, stepper
inline constexpr float radiusInner   = 5.0f;   // chip inside a control
inline constexpr float radiusPill    = 3.0f;   // tiny tags

// ── Spacing scale (4px rhythm — do not deviate) ──────────────────────────────
inline constexpr int gap1 = 2, gap2 = 3, gap3 = 4;
inline constexpr int gap4 = 5;    // DEFAULT gap between sibling controls
inline constexpr int gap5 = 6, gap6 = 8;   // gap6 = gap between panels
inline constexpr int gap7 = 9;    // panel inner padding
inline constexpr int gap8 = 11, gap9 = 12, gap10 = 14;

// ── Type sizes (tabular figures required for ALL numerics) ───────────────────
inline constexpr float tCaption   = 8.0f;    // hints
inline constexpr float tLabel     = 9.0f;    // panel headers (uppercase + tracking)
inline constexpr float tSmall     = 10.0f;   // secondary labels, tags
inline constexpr float tBody      = 11.0f;   // control labels
inline constexpr float tControl   = 12.0f;   // button text, values
inline constexpr float tName      = 14.0f;   // track / instrument name
inline constexpr float tReadout   = 15.0f;   // LUFS, gain readouts
inline constexpr float tDisplay   = 26.0f;   // big tempo / key numerics
inline constexpr float tDisplayLg = 30.0f;   // biggest (dual-tempo panel)

inline constexpr float trackLabel = 0.06f;   // uppercase header letter-spacing

// ── Motion (all interruptible) ───────────────────────────────────────────────
inline constexpr int durHover  = 90;
inline constexpr int durPress  = 40;
inline constexpr int durState  = 160;   // loop on, sync lock, segment swap
inline constexpr int durDrawer = 220;   // panel expand, idle→active channel

// ── Helpers ──────────────────────────────────────────────────────────────────
inline juce::Colour c (juce::uint32 argb) noexcept { return juce::Colour (argb); }

// The reference's "active control" pattern: accent at 12% fill + 40% border.
inline juce::Colour activeFill   (juce::Colour accent) noexcept { return accent.withAlpha (0.12f); }
inline juce::Colour activeBorder (juce::Colour accent) noexcept { return accent.withAlpha (0.40f); }

// Numerics MUST be tabular so digits don't jitter as values change.
inline juce::Font numeric (float size, bool medium = false)
{
    return juce::Font (juce::FontOptions ("SF Mono", size,
                                          medium ? juce::Font::bold : juce::Font::plain));
}

// Native technical faces keep the desktop build self-contained. SF Mono is
// bundled with macOS and falls back through JUCE on other hosts.
inline juce::Font technical (float size, bool medium = false)
{
    return juce::Font (juce::FontOptions ("SF Mono", size,
                                          medium ? juce::Font::bold : juce::Font::plain));
}

// Human-facing controls use the system UI face. Reserve SF Mono for changing
// numbers and compact engineering readouts; putting every action in monospace
// makes ordinary language harder to scan and gives the surface a prototype feel.
inline juce::Font ui (float size, bool medium = false)
{
    return juce::Font (juce::FontOptions (size,
                                          medium ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font section (float size, bool bold = true)
{
    return juce::Font (juce::FontOptions ("Avenir Next Condensed", size,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Colour deckAccent (int index) noexcept
{
    switch (index & 3) { case 0: return c (deckA); case 1: return c (deckB);
                         case 2: return c (deckC); default: return c (deckD); }
}

} // namespace stemdeck::tokens
