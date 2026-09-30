#pragma once

// =============================================================================
//  RoyalChainLookAndFeel — the suite's porcelain look, plus a Portal knob
// -----------------------------------------------------------------------------
//  Only sliders that carry the "rc.accent" property are restyled (a dark cap,
//  a thin macro-coloured value arc and 11 tick dots, like Portal's knobs) with
//  a dark value pill. Combos, buttons, the A/B row and the KeyButton keep the
//  shared porcelain controls — and their ink — untouched.
// =============================================================================

#include <JuceHeader.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include "RoyalChainTheme.h"

class RoyalChainLookAndFeel final : public sattari::ui::SattariLookAndFeel
{
public:
    RoyalChainLookAndFeel()
        : sattari::ui::SattariLookAndFeel (sattari::brand::productAccent ("royal-chain"))
    {
        // A slider's inline editor sits on the dark overlay field, so its ink must
        // be light. The porcelain value labels ignore this colour (they draw ink).
        setColour (juce::Slider::textBoxTextColourId, rc::theme::col (rc::theme::text));
        setColour (juce::Slider::textBoxHighlightColourId, rc::theme::col (rc::theme::mint).withAlpha (0.35f));
        setColour (juce::TooltipWindow::textColourId, rc::theme::col (rc::theme::text));
    }

    static bool hasAccent (const juce::Component& c)
    {
        return c.getProperties().contains (rc::theme::accentProperty);
    }

    static juce::Colour accentOf (const juce::Component& c)
    {
        return juce::Colour ((juce::uint32) (juce::int64) c.getProperties()[rc::theme::accentProperty]);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        if (! hasAccent (slider))
        {
            SattariLookAndFeel::drawRotarySlider (g, x, y, width, height, sliderPos, startAngle, endAngle, slider);
            return;
        }

        using namespace rc::theme;
        const auto accent = accentOf (slider);
        const bool enabled = slider.isEnabled();
        const bool hover = slider.isMouseOverOrDragging();
        const float fade = enabled ? 1.0f : 0.35f;

        const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const auto c = area.getCentre();
        const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 6.0f;
        if (r < 6.0f)
            return;
        const float pos = juce::jlimit (0.0f, 1.0f, sliderPos);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::PathStrokeType thin (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (col (track).withMultipliedAlpha (fade));
        g.strokePath (arc, thin);

        if (pos > 0.0005f)
        {
            arc.clear();
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, angle, true);
            g.setColour (accent.withAlpha ((hover ? 0.26f : 0.18f) * fade));
            g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withMultipliedAlpha (fade));
            g.strokePath (arc, thin);
        }

        // 11 tick dots just outside the arc, lit up to the value.
        const float dotR = r + 5.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float t = (float) i / 10.0f;
            const float a = startAngle + t * (endAngle - startAngle);
            const bool lit = t <= pos + 1.0e-4f;
            const juce::Point<float> p (c.x + std::sin (a) * dotR, c.y - std::cos (a) * dotR);
            g.setColour ((lit ? accent : col (knobDotOff)).withMultipliedAlpha (fade));
            g.fillEllipse (p.x - 1.4f, p.y - 1.4f, 2.8f, 2.8f);
        }

        // Dark machined cap.
        const float capR = r - 8.0f;
        const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3e45).withMultipliedAlpha (fade), c.x, cap.getY(),
                                                 juce::Colour (0xff24262b).withMultipliedAlpha (fade), c.x, cap.getBottom(), false));
        g.fillEllipse (cap);
        {
            juce::Path hi;
            hi.addCentredArc (c.x, c.y, capR - 0.5f, capR - 0.5f, 0.0f,
                              -juce::MathConstants<float>::pi * 0.42f, juce::MathConstants<float>::pi * 0.42f, true);
            g.setColour (juce::Colours::white.withAlpha (0.10f * fade));
            g.strokePath (hi, juce::PathStrokeType (1.0f));
        }
        g.setColour (juce::Colour (0xff16171a).withMultipliedAlpha (fade));
        g.drawEllipse (cap.reduced (0.5f), 1.0f);

        const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
        g.setColour (juce::Colour (0xfff4f5f7).withMultipliedAlpha (fade));
        g.drawLine ({ c + dir * (capR * 0.35f), c + dir * (capR * 0.80f) }, 2.0f);

        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.95f));
            g.drawEllipse (cap.expanded (3.0f), 1.6f);
        }
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        auto* parent = label.getParentComponent();
        if (parent != nullptr && hasAccent (*parent) && dynamic_cast<juce::Slider*> (parent) != nullptr
            && ! label.isBeingEdited())
        {
            using namespace rc::theme;
            const auto b = label.getLocalBounds().toFloat().reduced (0.5f);
            const float rad = b.getHeight() * 0.5f;
            g.setColour (juce::Colour (0xff1d1f24));
            g.fillRoundedRectangle (b, rad);
            g.setColour (juce::Colour (0xff2e3036));
            g.drawRoundedRectangle (b, rad, 1.0f);
            g.setColour (col (text).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.4f));
            g.setFont (juce::Font (body (11.0f)));
            g.drawFittedText (label.getText(), label.getLocalBounds().reduced (4, 0),
                              juce::Justification::centred, 1, 0.9f);
            return;
        }
        SattariLookAndFeel::drawLabel (g, label);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainLookAndFeel)
};
