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
}
