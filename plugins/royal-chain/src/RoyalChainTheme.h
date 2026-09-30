#pragma once

// =============================================================================
//  Royal Chain — "Crown Portal" design tokens
// -----------------------------------------------------------------------------
//  One place for every colour, node, macro and caption the Royal Chain editor
//  draws. The chrome is matte and near-monochrome; light (mint / violet / gold)
//  lives only inside the Compass aperture and on elements that NAME a macro.
//  There are deliberately no per-style themes: a vocal style changes the
//  figure's complexity, never the colour.
// =============================================================================

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <cstdint>

namespace rc::theme
{
// --- Chrome (matte) -------------------------------------------------------------
inline constexpr std::uint32_t canvas      = 0xff0f1013;
inline constexpr std::uint32_t stage       = 0xff0b0c0f;
inline constexpr std::uint32_t stageLift   = 0xff15171c;
inline constexpr std::uint32_t stageEdge   = 0xff23252b;
inline constexpr std::uint32_t voidCore    = 0xff0a121a;
inline constexpr std::uint32_t voidRim     = 0xff06080b;
inline constexpr std::uint32_t spine       = 0xff15161a;
inline constexpr std::uint32_t card        = 0xff1a1b20;
inline constexpr std::uint32_t cardEdge    = 0xff26282e;
inline constexpr std::uint32_t surfaceHigh = 0xff22242a;
inline constexpr std::uint32_t hairline    = 0xff2a2c33;
inline constexpr std::uint32_t track       = 0xff333335;
inline constexpr std::uint32_t text        = 0xffdbdfe6;
inline constexpr std::uint32_t textDim     = 0xff8a9099;
inline constexpr std::uint32_t textFaint   = 0xff555c69;
inline constexpr std::uint32_t bracket     = 0xffe6e6e6;   // drawn at 55 %
inline constexpr std::uint32_t rimViolet   = 0xff3e3b55;
inline constexpr std::uint32_t rimMint     = 0xff1e4a4e;
inline constexpr std::uint32_t knobNeutral = 0xffe6e8ec;
inline constexpr std::uint32_t knobTrim    = 0xff9ea4ae;
inline constexpr std::uint32_t knobDotOff  = 0xff3a3d45;
inline constexpr std::uint32_t pill        = 0xff22242a;

// --- Light (Compass field only, plus macro naming) -------------------------------
inline constexpr std::uint32_t mint    = sattari::brand::kAccent;        // #62F5C8
inline constexpr std::uint32_t violet  = sattari::brand::kAccentViolet;  // #9B7BFF
inline constexpr std::uint32_t bright  = 0xffb1fae4;
inline constexpr std::uint32_t hot     = 0xffeafff8;
inline constexpr std::uint32_t gold    = 0xfff4b35e;                     // ROYAL only
inline constexpr std::uint32_t ringHot = 0xff7bfff6;
inline constexpr std::uint32_t focus   = 0xffc6fffa;

inline juce::Colour col (std::uint32_t argb) noexcept { return juce::Colour ((juce::uint32) argb); }

// --- Compass nodes: a regular pentagon, clockwise in signal-chain order ------------
struct NodeSpec
{
    const char* label;
    const char* paramId;
    float angleDeg;
    std::uint32_t colour, inner;
    const char* description;
};

inline constexpr int kNumNodes = 5;
inline constexpr NodeSpec kNodes[kNumNodes] = {
    { "TUNE",    "tune",    -90.0f, 0xff9b7bff, 0xff62f5c8, "Pitch correction toward the selected key and scale." },
    { "CONTROL", "control", -18.0f, 0xff8a8ff5, 0xff62f5c8, "Compression and level riding." },
    { "SMOOTH",  "smooth",   54.0f, 0xff74b0ec, 0xff62f5c8, "De-essing and harshness control." },
    { "GLOW",    "glow",    126.0f, 0xff66d4dc, 0xff9b7bff, "Harmonic colour and saturation." },
    { "WIDTH",   "width",   198.0f, 0xff62f5c8, 0xff9b7bff, "Stereo width and doubling." },
};

// --- The 12 user macros (deck order, then ROYAL) --------------------------------
enum class MacroId { Input, Clean, Tune, Control, Tone, Air, Smooth, Glow, Width, Space, Output, Royal };
inline constexpr int kNumMacros = 12;

struct MacroSpec
{
    MacroId id;
    const char* paramId;
    const char* caption;
    const char* description;
    std::uint32_t colour;
    int node;      // index into kNodes, or -1
    bool isDb;
};

inline constexpr MacroSpec kMacros[kNumMacros] = {
    { MacroId::Input,   "input",   "INPUT",   "Input trim into the chain.",                          knobTrim,    -1, true  },
    { MacroId::Clean,   "clean",   "CLEAN",   "Rumble high-pass and a gentle noise gate.",           knobNeutral, -1, false },
    { MacroId::Tune,    "tune",    "TUNE",    kNodes[0].description,                                 kNodes[0].colour, 0, false },
    { MacroId::Control, "control", "CONTROL", kNodes[1].description,                                 kNodes[1].colour, 1, false },
    { MacroId::Tone,    "tone",    "TONE",    "Tonal balance: clears mud, adds body.",               knobNeutral, -1, false },
    { MacroId::Air,     "air",     "AIR",     "High-frequency air and presence lift.",               knobNeutral, -1, false },
    { MacroId::Smooth,  "smooth",  "SMOOTH",  kNodes[2].description,                                 kNodes[2].colour, 2, false },
    { MacroId::Glow,    "glow",    "GLOW",    kNodes[3].description,                                 kNodes[3].colour, 3, false },
    { MacroId::Width,   "width",   "WIDTH",   kNodes[4].description,                                 kNodes[4].colour, 4, false },
    { MacroId::Space,   "space",   "SPACE",   "Short stereo ambience behind the vocal.",             knobNeutral, -1, false },
    { MacroId::Output,  "output",  "OUTPUT",  "Output trim into the final limiter.",                 knobTrim,    -1, true  },
    { MacroId::Royal,   "royal",   "ROYAL",   "Global polish: pushes every stage together.",         gold,        -1, false },
};

inline constexpr const MacroSpec& macro (MacroId id) noexcept { return kMacros[(int) id]; }

inline constexpr MacroId macroForNode (int node) noexcept
{
    return node == 0 ? MacroId::Tune : node == 1 ? MacroId::Control : node == 2 ? MacroId::Smooth
         : node == 3 ? MacroId::Glow : MacroId::Width;
}

// --- Vocal style (parameter order) -> figure complexity ----------------------------
inline constexpr int kNumStyles = 10;
inline constexpr const char* kStyleNames[kNumStyles] = {
    "Modern Rap", "R&B", "Pop", "Persian/World Pop", "Indie",
    "Podcast/Spoken", "Background", "Hook", "Demo Rescue", "Live"
};
inline constexpr const char* kStyleCaps[kNumStyles] = {
    "MODERN RAP", "R&B", "POP", "PERSIAN / WORLD POP", "INDIE",
    "PODCAST / SPOKEN", "BACKGROUND", "HOOK", "DEMO RESCUE", "LIVE"
};
inline constexpr float kStyleComplexity[kNumStyles] = { 0.5f, 0.6f, 0.5f, 0.9f, 0.4f, 0.0f, 0.2f, 0.8f, 0.1f, 0.6f };

// --- Musical names ----------------------------------------------------------------
inline constexpr const char* kDegreeNames[12] = {
    "ROOT", "MIN 2ND", "2ND", "MIN 3RD", "3RD", "4TH", "TRITONE", "5TH", "MIN 6TH", "6TH", "MIN 7TH", "7TH"
};
inline constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr const char* kScaleNames[8] = {
    "CHROMATIC", "MAJOR", "MINOR", "PENTA MAJ", "PENTA MIN", "DORIAN", "MIXOLYDIAN", "HARM MINOR"
};

// --- Typography ---------------------------------------------------------------------
// Tracked caps: JUCE's kerning factor is relative to the font height, so 0.14 at
// 10 px adds 1.4 px per glyph — the same tracking the suite hand-rolls, without a
// per-glyph drawText loop.
inline juce::FontOptions caps (float h, float tracking = 0.14f, int style = juce::Font::bold)
{
    return sattari::brand::fontOptions (h, style).withKerningFactor (tracking);
}

inline juce::FontOptions numerals (float h)
{
    return sattari::brand::fontOptions (h).withStyle ("Light");
}

inline juce::FontOptions body (float h)
{
    return sattari::brand::fontOptions (h);
}

// Component property carrying a Portal-knob accent (int64 ARGB) on a slider.
inline const juce::Identifier accentProperty { "rc.accent" };

// A static caption and the rect it must fit in (checked by the layout gate).
struct CaptionCheck
{
    juce::String where, text;
    juce::FontOptions font;
    float availableWidth;
};

inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

} // namespace rc::theme
