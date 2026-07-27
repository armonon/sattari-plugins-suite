#pragma once

// StemGainKnob — the 0/100/200 stem gain control (perform-screen §3).
//
//   0   = silence   (full left,  -135deg)
//   100 = unity      (straight up,  0deg)   <- rest position
//   200 = x2.0/+6dB  (full right, +135deg)
//
// Symmetric +/-135deg sweep so unity sits exactly at top with equal travel each way.
// The arc fills FROM 12 o'clock: the stem's colour when boosting above unity (going
// amber as it approaches the 200 red zone), neutral grey when cutting below unity —
// so which stems are hot reads at a glance without numbers.
//
// Header-only so it can be PNG-verified standalone and reused by the editor. The
// value is a linear percent (see StemGain.h); percent->linear happens in the DSP.

#include <juce_gui_basics/juce_gui_basics.h>
#include "StemGain.h"
#include "DesignTokens.h"

namespace stemdeck
{

// Pointer angle (radians, clockwise from 12 o'clock) for a 0..200 percent value.
inline float stemKnobAngle (float percent) noexcept
{
    const float t = juce::jlimit (0.0f, 200.0f, percent) / 200.0f;   // 0..1
    return juce::degreesToRadians (-135.0f + t * 270.0f);
}

// Geometry mirrors the reference's 44px knobSVG, expressed as fractions of the half
// -size so it scales: face r=17, arc r=15, inner r=12, centre dot r=2, arc width 2.5.
inline void drawStemGainKnob (juce::Graphics& g, juce::Rectangle<float> area,
                              float percent, juce::Colour stem, bool muted,
                              juce::Colour stemLight = juce::Colour())
{
    namespace T = stemdeck::tokens;
    if (stemLight.isTransparent()) stemLight = stem.brighter (0.25f);

    const float startA = juce::degreesToRadians (-135.0f);
    const float endA   = juce::degreesToRadians ( 135.0f);
    const float dim    = muted ? 0.4f : 1.0f;

    const float side = juce::jmin (area.getWidth(), area.getHeight());
    auto b = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float half = side * 0.5f;

    const float faceR  = half * 0.773f;   // 17/22
    const float arcR   = half * 0.682f;   // 15/22
    const float innerR = half * 0.545f;   // 12/22
    const float dotR   = juce::jmax (1.0f, half * 0.091f);
    const float arcW   = juce::jmax (1.5f, half * 0.114f);

    // Face.
    g.setColour (juce::Colour (0xff151519));
    g.fillEllipse (cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour (T::c (T::borderSubtle));
    g.drawEllipse (cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f, 0.5f);

    // Full-sweep track.
    juce::Path track;
    track.addCentredArc (cx, cy, arcR, arcR, 0.0f, startA, endA, true);
    g.setColour (T::c (T::borderSubtle));
    g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc, filling FROM 12 o'clock. The reference switches HARD to --warn above
    // 175 rather than fading toward it: a threshold you can see is the point ("amber
    // above ~175 approaching clip"). Lerping across the whole boost range means there
    // is no moment where it starts warning.
    const float a = stemKnobAngle (percent);
    const juce::Colour arcCol = percent > 175.0f          ? T::c (T::warn)
                              : percent >= stemdeck::stemgain::kUnity ? stem
                                                          : T::c (T::fgFaint);
    if (std::abs (percent - stemdeck::stemgain::kUnity) > 0.5f)
    {
        juce::Path arc;
        arc.addCentredArc (cx, cy, arcR, arcR, 0.0f, juce::jmin (0.0f, a), juce::jmax (0.0f, a), true);
        g.setColour (arcCol.withMultipliedAlpha (dim));
        g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Unity tick just outside the arc, so the rest position is legible.
    g.setColour (T::c (T::fgMuted).withMultipliedAlpha (dim));
    g.fillRect (cx - 0.5f, cy - half * 0.795f, 1.0f, half * 0.136f);

    // Inner cap, ringed in the stem's own colour — the knob carries stem identity even
    // when the arc is neutral (at or below unity).
    g.setColour (T::c (T::bgControl));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (stem.withAlpha (0.4f * dim));
    g.drawEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f, 0.5f);

    // Pointer from the centre out to the arc, plus the centre dot.
    const juce::Point<float> dir (std::sin (a), -std::cos (a));
    const juce::Point<float> centre (cx, cy);
    g.setColour (stemLight.withMultipliedAlpha (dim));
    g.drawLine ({ centre, centre + dir * arcR }, juce::jmax (1.0f, half * 0.091f));
    g.fillEllipse (cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
}


// A rotary Slider wired to a 0..200 percent value, drawn by drawStemGainKnob.
// Keeps Slider's drag/attachment behaviour; double-click returns to unity.
class StemGainKnob : public juce::Slider
{
public:
    StemGainKnob()
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setRange (stemdeck::stemgain::kMin, stemdeck::stemgain::kMax, 0.0);
        setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        setDoubleClickReturnValue (true, stemdeck::stemgain::kUnity);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setValue (stemdeck::stemgain::kUnity, juce::dontSendNotification);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);   // ns-resize (§3)
    }

    void setStemColour (juce::Colour c) { stem = c; stemLight = c.brighter (0.25f); repaint(); }
    // The reference pairs every stem with a fixed lighter variant (--stem-*-light) for
    // the pointer and readout; brighter() only approximates it.
    void setStemColours (juce::Colour c, juce::Colour light) { stem = c; stemLight = light; repaint(); }
    void setMuted (bool m)              { if (m != muted) { muted = m; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // A stem at 0% reads as muted (mute drops the gain to 0), so show MUTE + dim.
        const bool mutedLook = muted || getValue() <= 0.5;
        if (b.getHeight() >= 34.0f)   // reserve a readout strip when there's room (§3)
        {
            auto readout = b.removeFromBottom (12.0f);
            drawStemGainKnob (g, b, (float) getValue(), stem, mutedLook, stemLight);
            g.setColour (mutedLook               ? stemdeck::tokens::c (stemdeck::tokens::fgTertiary)
                       : getValue() > 175.0        ? stemdeck::tokens::c (stemdeck::tokens::warn)
                                                   : stemLight);
            g.setFont (juce::FontOptions (stemdeck::tokens::tBody, juce::Font::bold));
            g.drawText (stemdeck::stemgain::displayText ((float) getValue(), mutedLook),
                        readout.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            drawStemGainKnob (g, b, (float) getValue(), stem, mutedLook, stemLight);
        }
    }

private:
    juce::Colour stem { 0xff4AD9C4 };
    juce::Colour stemLight { 0xff5DCAA5 };
    bool muted = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemGainKnob)
};

} // namespace stemdeck
