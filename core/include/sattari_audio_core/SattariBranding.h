#pragma once

// =============================================================================
//  Sattari Audio Suite - shared branding / identity
// -----------------------------------------------------------------------------
//  Single source of truth for the suite palette, typography, and the product
//  registry. Every plugin editor + the Hub should pull colours/names from here
//  instead of hard-coding hex values, so the whole line reads as one product.
//
//  This header is intentionally dependency-light: the palette is exposed as raw
//  0xAARRGGBB constants (usable anywhere, including non-JUCE contexts), with
//  juce::Colour conveniences compiled in only when juce_graphics is available.
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

#if __has_include(<juce_graphics/juce_graphics.h>)
 #include <juce_graphics/juce_graphics.h>
 #define SATTARI_HAS_JUCE_GRAPHICS 1
#else
 #define SATTARI_HAS_JUCE_GRAPHICS 0
#endif

namespace sattari::brand
{

// --- Core suite palette (ARGB) ----------------------------------------------
// Derived from the established Sub Conjurer / Auto Pitch editor look so existing
// builds stay visually consistent while gaining a shared system.
// Modern, near-monochrome cool-dark base. Flat surfaces, hairline separators,
// one accent per product. Deliberately minimal: no gradients, no glow.
inline constexpr std::uint32_t kBackground   = 0xff0c0d11; // app/plugin backdrop (flat)
inline constexpr std::uint32_t kPanel        = 0xff14161b; // primary surface
inline constexpr std::uint32_t kPanelRaised  = 0xff1c1f26; // combos / inset fields
inline constexpr std::uint32_t kPanelDeep    = 0xff0e0f13; // subtle low surface
inline constexpr std::uint32_t kPanelViolet  = 0xff14161b; // (legacy gradient stop; now flat)
inline constexpr std::uint32_t kHairline     = 0xff272a31; // 1px separators / knob tracks

inline constexpr std::uint32_t kAccent       = 0xff62f5c8; // signature mint (default)
inline constexpr std::uint32_t kAccentViolet = 0xff9b7bff; // secondary highlight
inline constexpr std::uint32_t kTextPrimary  = 0xffeef1f6;
inline constexpr std::uint32_t kTextMuted     = 0xff8b93a1;
inline constexpr std::uint32_t kTextFaint     = 0xff555c69;
inline constexpr std::uint32_t kSuccess       = 0xff62f5c8;
inline constexpr std::uint32_t kWarning       = 0xfff5c84a;
inline constexpr std::uint32_t kDanger        = 0xffff6b6b;

inline constexpr float kCornerRadius = 12.0f;
inline constexpr float kPanelInset   = 10.0f;

// --- Typography (point sizes) ----------------------------------------------
inline constexpr float kTitleSize    = 22.0f;
inline constexpr float kSubtitleSize = 12.5f;
inline constexpr float kLabelSize    = 11.5f;
inline constexpr float kValueSize    = 11.5f;

// --- Product registry --------------------------------------------------------
// Keep ids identical to plugin slugs and the Hub manifest (sattari-products.json)
// so accents/names resolve consistently across plugin, suite, and installer.
struct Product
{
    std::string id;       // canonical slug, e.g. "auto-pitch"
    std::string name;     // display name, e.g. "Sattari Auto Pitch"
    std::string category; // e.g. "Vocal tuning"
    std::uint32_t accent; // per-product accent over the shared system
};

inline const std::vector<Product>& products()
{
    // Unified suite identity: every product uses the Sattari Auto Pitch mint
    // accent so the whole suite reads as one family ("everything looks like the
    // Auto Pitch plugin"). The shared LookAndFeel already unifies background,
    // shapes and layout; this unifies colour too. (Per-product accents are kept
    // as a single constant here so it stays trivial to re-diversify later.)
    static const std::vector<Product> registry {
        { "auto-pitch",        "Sattari Auto Pitch",  "Vocal tuning",     kAccent },
        { "voxkey",            "Sattari VoxKey",      "Voice-to-MIDI",    kAccent },
        { "stemdeck",          "Sattari StemDeck",    "DJ / remix",       kAccent },
        { "sub-conjurer",      "Sub Conjurer",        "Low-end",          kAccent },
        { "side-chain-master", "Side Chain Master",   "Dynamics",         kAccent },
        { "royal-chain",       "Sattari Royal Chain", "Vocal chain",      kAccent },
        { "harmony",           "Sattari Harmony",     "Vocal harmony",    kAccent },
        { "maqam",             "Sattari Maqam",       "Microtonal tuning",kAccent },
        { "vocal-master",      "Sattari Vocal Master","Vocal master",     kAccent },
        { "rhythm-genie",      "Sattari Rhythm Genie","MIDI / drums",     kAccent },
        { "sattari-arp",       "Sattari Arp",         "MIDI / creative",  kAccent },
        { "bus",               "Sattari Bus",         "Bus / mix",        kAccent },
        { "align",             "Sattari Align",       "Timing alignment", kAccent },
    };
    return registry;
}

inline std::uint32_t productAccent (const std::string& id)
{
    for (const auto& p : products())
        if (p.id == id)
            return p.accent;
    return kAccent;
}

inline std::string productName (const std::string& id)
{
    for (const auto& p : products())
        if (p.id == id)
            return p.name;
    return id;
}

#if SATTARI_HAS_JUCE_GRAPHICS
inline juce::Colour colour (std::uint32_t argb) { return juce::Colour (argb); }
inline juce::Colour accentFor (const std::string& id) { return juce::Colour (productAccent (id)); }
#endif

} // namespace sattari::brand
