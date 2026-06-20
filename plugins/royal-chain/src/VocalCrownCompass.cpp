#include "VocalCrownCompass.h"
#include "RoyalChainProcessor.h"

VocalCrownCompass::VocalCrownCompass (RoyalChainAudioProcessor& processorRef)
    : processor (processorRef)
{
}

void VocalCrownCompass::drawNode (juce::Graphics& g, juce::Point<float> centre, float value, juce::Colour colour, const juce::String& label)
{
    const float radius = 8.0f + juce::jlimit (0.0f, 1.0f, value) * 16.0f;
    g.setColour (colour.withAlpha (0.20f));
    g.fillEllipse (centre.x - radius * 1.8f, centre.y - radius * 1.8f, radius * 3.6f, radius * 3.6f);
    g.setColour (colour);
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (label, (int) centre.x - 48, (int) centre.y + (int) radius + 2, 96, 18, juce::Justification::centred);
}

void VocalCrownCompass::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (juce::Colour (0xff11131f));
    g.fillRoundedRectangle (area, 18.0f);
    g.setColour (juce::Colour (0xffd8b45a).withAlpha (0.70f));
    g.drawRoundedRectangle (area, 18.0f, 1.4f);

    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.28f;

    g.setColour (juce::Colour (0xffd8b45a).withAlpha (0.12f));
    g.fillEllipse (centre.x - radius * 1.55f, centre.y - radius * 1.55f, radius * 3.1f, radius * 3.1f);
    g.setColour (juce::Colour (0xffd8b45a).withAlpha (0.55f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    g.drawEllipse (centre.x - radius * 1.45f, centre.y - radius * 1.45f, radius * 2.9f, radius * 2.9f, 1.0f);

    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText ("Vocal Crown Compass", area.removeFromTop (30.0f), juce::Justification::centred);

    drawNode (g, { centre.x, centre.y - radius }, processor.getTuneConfidence(), juce::Colour (0xff7dd3fc), "Tune" );
    drawNode (g, { centre.x + radius, centre.y }, processor.getControlMeter(), juce::Colour (0xff62f5c8), "Control" );
    drawNode (g, { centre.x, centre.y + radius }, processor.getLimiterMeter(), juce::Colour (0xfffacc15), "Finish" );
    drawNode (g, { centre.x - radius, centre.y }, processor.getSibilanceMeter(), juce::Colour (0xfff472b6), "Smooth" );
    drawNode (g, { centre.x + radius * 0.72f, centre.y - radius * 0.72f }, processor.getWidthMeter(), juce::Colour (0xffa78bfa), "Width" );
}
