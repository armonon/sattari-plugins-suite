#include "MixerSection.h"

void MixerSection::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xff0d1019));
    g.fillRoundedRectangle (area, 14.0f);
    g.setColour (juce::Colour (0xff2d3448));
    g.drawRoundedRectangle (area, 14.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MIXER", getLocalBounds().removeFromTop (20), juce::Justification::centred);

    // Live master-output scope: scrolling stereo level history, oldest at the left.
    auto scope = area.reduced (10.0f, 6.0f).withTrimmedTop (20.0f);
    if (scope.getHeight() > 8.0f && scope.getWidth() > 8.0f)
    {
        const float cy = scope.getCentreY();
        const juce::Colour cyan (0xff26d9ff);

        g.setColour (cyan.withAlpha (0.18f));
        g.drawLine (scope.getX(), cy, scope.getRight(), cy, 1.0f);

        const int n = (int) history.size();
        const float half = scope.getHeight() * 0.46f;

        juce::Path top, bottom;
        bool started = false;
        float running = 0.0f;   // peak-hold across the window for a headroom readout
        for (int i = 0; i < n; ++i)
        {
            // Read oldest→newest so the waveform scrolls right as new frames arrive.
            const auto& f = history[(size_t) ((writePos + i) % n)];
            const float x = scope.getX() + scope.getWidth() * (float) i / (float) (n - 1);
            const float up   = cy - half * f.l;
            const float down = cy + half * f.r;
            running = juce::jmax (running, f.l, f.r);
            if (! started) { top.startNewSubPath (x, up); bottom.startNewSubPath (x, down); started = true; }
            else           { top.lineTo (x, up);          bottom.lineTo (x, down); }
        }

        // Filled body between the two envelopes.
        juce::Path body (top);
        for (int i = n - 1; i >= 0; --i)
        {
            const auto& f = history[(size_t) ((writePos + i) % n)];
            const float x = scope.getX() + scope.getWidth() * (float) i / (float) (n - 1);
            body.lineTo (x, cy + half * f.r);
        }
        body.closeSubPath();
        g.setColour (cyan.withAlpha (0.16f));
        g.fillPath (body);

        g.setColour (cyan.withAlpha (0.9f));
        g.strokePath (top,    juce::PathStrokeType (1.3f));
        g.setColour (cyan.withAlpha (0.55f));
        g.strokePath (bottom, juce::PathStrokeType (1.1f));

        // Peak-hold headroom chip (dBFS) top-right of the scope.
        const float db = juce::Decibels::gainToDecibels (running, -60.0f);
        g.setColour ((db > -0.5f ? juce::Colour (0xffff5f6a) : cyan).withAlpha (0.85f));
        g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
        const juce::String hdr = running > 0.0001f ? juce::String (db, 1) + " dB"
                                                    : juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e dB"));
        g.drawText (hdr, scope.withHeight (10.0f).translated (0.0f, -2.0f), juce::Justification::topRight);
    }
}
