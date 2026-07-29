#pragma once

// =============================================================================
//  Sattari Studio — compact analysis primitives
// -----------------------------------------------------------------------------
//  Metering products need denser information than creative instruments, but
//  they should still feel like the same suite. These components provide the
//  shared visual grammar for exact readouts, plots, waiting states and bipolar
//  measurements without owning any product-specific analysis.
// =============================================================================

#include <sattari_audio_core/SattariStudio.h>

namespace sattari::studio::analysis
{

class Metric final : public juce::Component
{
public:
    Metric (juce::String label, juce::Colour accent)
        : label_ (std::move (label)), accent_ (accent)
    {
        setAccessible (true);
        setTitle (label_);
    }

    void setValue (juce::String value, juce::String unit = {},
                   juce::String detail = {}, bool warning = false)
    {
        if (value_ == value && unit_ == unit && detail_ == detail
            && warning_ == warning)
            return;
        value_ = std::move (value);
        unit_ = std::move (unit);
        detail_ = std::move (detail);
        warning_ = warning;
        setDescription (
            label_ + ": " + value_ + " " + unit_ + " " + detail_);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        studio::paintSurface (
            g, getLocalBounds().toFloat(), false, 11.0f);
        auto area = getLocalBounds().reduced (13, 9);

        g.setColour (studio::colour::tertiary);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (label_.toUpperCase(), area.removeFromTop (14),
                    juce::Justification::centredLeft, false);

        auto valueRow = area.removeFromTop (32);
        g.setColour (warning_ ? studio::colour::warning : studio::colour::text);
        g.setFont (juce::FontOptions (23.0f, juce::Font::bold));
        const auto unitWidth =
            unit_.isNotEmpty() ? juce::jmin (58, valueRow.getWidth() / 3) : 0;
        g.drawText (value_.isNotEmpty() ? value_ : "--",
                    valueRow.withTrimmedRight (unitWidth),
                    juce::Justification::centredLeft, false);
        if (unitWidth > 0)
        {
            g.setColour (warning_ ? studio::colour::warning
                                  : studio::colour::secondary);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (unit_, valueRow.removeFromRight (unitWidth),
                        juce::Justification::bottomLeft, false);
        }

        if (detail_.isNotEmpty())
        {
            g.setColour (studio::colour::secondary);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (detail_, area, juce::Justification::centredLeft, false);
        }

        g.setColour ((warning_ ? studio::colour::warning : accent_)
                         .withAlpha (0.82f));
        g.fillRoundedRectangle (
            0.0f, 12.0f, 3.0f,
            juce::jmax (10.0f, (float) getHeight() - 24.0f), 1.5f);
    }

private:
    juce::String label_, value_, unit_, detail_;
    juce::Colour accent_;
    bool warning_ = false;
};

inline juce::Rectangle<int> paintPlotFrame (
    juce::Graphics& g, juce::Rectangle<int> bounds,
    const juce::String& title, const juce::String& detail = {})
{
    studio::paintSurface (g, bounds.toFloat(), true);
    auto inner = bounds.reduced (14);
    studio::paintSectionTitle (
        g, inner.removeFromTop (18), title, detail);
    inner.removeFromTop (8);
    return inner;
}

inline void paintWaitingState (juce::Graphics& g,
                               juce::Rectangle<int> bounds,
                               const juce::String& message)
{
    g.setColour (studio::colour::tertiary);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (message, bounds, juce::Justification::centred, true);
}

inline void paintBipolarBar (juce::Graphics& g,
                             juce::Rectangle<float> bounds,
                             float value, juce::Colour accent,
                             bool warning, const juce::String& label)
{
    const float clamped = juce::jlimit (-1.0f, 1.0f, value);
    g.setColour (studio::colour::surfaceHigh);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (studio::colour::separator);
    g.drawRoundedRectangle (
        bounds.reduced (0.5f), bounds.getHeight() * 0.5f, 1.0f);

    const float mid = bounds.getCentreX();
    const float x = mid + clamped * (bounds.getWidth() * 0.5f - 4.0f);
    auto fill = juce::Rectangle<float> (
        juce::jmin (mid, x), bounds.getY() + 3.0f,
        juce::jmax (1.0f, std::abs (x - mid)), bounds.getHeight() - 6.0f);
    g.setColour (warning ? studio::colour::warning : accent);
    g.fillRoundedRectangle (fill, fill.getHeight() * 0.5f);

    g.setColour (studio::colour::secondary);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (label, bounds.toNearestInt(),
                juce::Justification::centred, false);
}

} // namespace sattari::studio::analysis
