#pragma once

#include <juce_graphics/juce_graphics.h>

namespace sattari::branding
{

const juce::Image& logo();

inline juce::String compactProductName (juce::String name)
{
    const juce::String prefix { "Sattari " };
    if (name.startsWithIgnoreCase (prefix))
        name = name.substring (prefix.length());
    return name.trim();
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> bounds);

} // namespace sattari::branding
