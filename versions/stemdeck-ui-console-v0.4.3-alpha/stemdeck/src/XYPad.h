#pragma once

#include <JuceHeader.h>
#include "StemDeckLookAndFeel.h"

// =============================================================================
//  XYPad — the "MASTER FX" control (flat / minimal).
// -----------------------------------------------------------------------------
//  A flat matte panel with a recessed pad. Horizontal axis = X parameter
//  (filter), vertical axis = Y parameter (space/echo, up = more). Two hidden
//  sliders bridge to APVTS so host automation stays in sync both ways.
// =============================================================================
class XYPad : public juce::Component, private juce::Slider::Listener
{
public:
    XYPad()
    {
        for (auto* s : { &xSlider, &ySlider })
        {
            s->setRange (0.0, 1.0, 0.0);
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->addListener (this);
            addChildComponent (*s);   // kept invisible; source of truth for params
        }
    }

    void attach (juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& xParamId, const juce::String& yParamId)
    {
        xAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, xParamId, xSlider);
        yAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, yParamId, ySlider);
        repaint();
    }

    void setAccent (juce::Colour c)  { accent = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace stemdeck::palette;
        const auto r = getLocalBounds().toFloat();

        // Flat panel.
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colour (kHairline));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        // Title — muted, letter-spaced.
        g.setColour (juce::Colour (kTextMuted));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        drawSpaced (g, "MASTER FX", r.withHeight (16.0f).translated (0.0f, 5.0f), 2.4f);

        // Recessed pad.
        const auto pad = padArea();
        g.setColour (juce::Colour (kPanelDeep));
        g.fillRoundedRectangle (pad, 6.0f);

        // Faint grid — quarters only.
        g.setColour (juce::Colour (kHairline).withAlpha (0.55f));
        for (int i = 1; i < 4; ++i)
        {
            const float gx = pad.getX() + pad.getWidth()  * (float) i / 4.0f;
            const float gy = pad.getY() + pad.getHeight() * (float) i / 4.0f;
            g.drawLine (gx, pad.getY() + 3.0f, gx, pad.getBottom() - 3.0f, 0.5f);
            g.drawLine (pad.getX() + 3.0f, gy, pad.getRight() - 3.0f, gy, 0.5f);
        }
        g.setColour (juce::Colour (kHairline));
        g.drawRoundedRectangle (pad.reduced (0.5f), 6.0f, 1.0f);

        // Axis hints.
        g.setColour (juce::Colour (kTextMuted).withAlpha (0.75f));
        g.setFont (juce::FontOptions (7.5f, juce::Font::plain));
        g.drawText ("FILTER", juce::Rectangle<float> (pad.getX(), pad.getBottom() + 1.0f, pad.getWidth(), 12.0f),
                    juce::Justification::centred);

        drawDot (g, pad);
    }

    void drawDot (juce::Graphics& g, juce::Rectangle<float> pad)
    {
        const float dx = pad.getX() + xVal() * pad.getWidth();
        const float dy = pad.getY() + (1.0f - yVal()) * pad.getHeight();

        // Thin crosshair to the pad edges.
        g.setColour (accent.withAlpha (0.30f));
        g.drawLine (pad.getX() + 2.0f, dy, pad.getRight() - 2.0f, dy, 0.75f);
        g.drawLine (dx, pad.getY() + 2.0f, dx, pad.getBottom() - 2.0f, 0.75f);

        // Clean dot: accent disc + white core, one soft halo.
        g.setColour (accent.withAlpha (0.16f));
        g.fillEllipse (dx - 9.0f, dy - 9.0f, 18.0f, 18.0f);
        g.setColour (accent);
        g.fillEllipse (dx - 5.0f, dy - 5.0f, 10.0f, 10.0f);
        g.setColour (juce::Colour (stemdeck::palette::kTextPrimary));
        g.fillEllipse (dx - 2.0f, dy - 2.0f, 4.0f, 4.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        xSlider.setValue (0.5, juce::sendNotificationSync);
        ySlider.setValue (0.0, juce::sendNotificationSync);
    }

private:
    float xVal() const { return (float) xSlider.getValue(); }
    float yVal() const { return (float) ySlider.getValue(); }

    static void drawSpaced (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float tracking)
    {
        const auto& f = g.getCurrentFont();
        float total = -tracking;
        for (int i = 0; i < text.length(); ++i)
            total += f.getStringWidthFloat (text.substring (i, i + 1)) + tracking;
        float x = area.getCentreX() - total * 0.5f;
        for (int i = 0; i < text.length(); ++i)
        {
            const juce::String ch = text.substring (i, i + 1);
            const float w = f.getStringWidthFloat (ch);
            g.drawText (ch, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()),
                        juce::Justification::centredLeft, false);
            x += w + tracking;
        }
    }

    juce::Rectangle<float> padArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (14.0f);
        r.removeFromTop (14.0f);      // title strip
        r.removeFromBottom (12.0f);   // axis label
        const float sz = juce::jmin (r.getHeight(), r.getWidth());
        return juce::Rectangle<float> (sz, sz).withCentre ({ r.getCentreX(), r.getCentreY() });
    }

    void setFromMouse (const juce::MouseEvent& e)
    {
        const auto pad = padArea();
        const float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - pad.getX()) / pad.getWidth());
        const float ny = juce::jlimit (0.0f, 1.0f, (e.position.y - pad.getY()) / pad.getHeight());
        xSlider.setValue (nx, juce::sendNotificationSync);
        ySlider.setValue (1.0f - ny, juce::sendNotificationSync);   // up = more
    }

    void sliderValueChanged (juce::Slider*) override { repaint(); }

    juce::Slider xSlider, ySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> xAtt, yAtt;
    juce::Colour accent { 0xff26d9ff };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XYPad)
};
