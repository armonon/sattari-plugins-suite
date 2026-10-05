#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sattari::parameters
{
// Match APVTS's snapped boolean state even when hosts send fractional automation.
class Bool final : public juce::AudioParameterBool
{
public:
    using juce::AudioParameterBool::AudioParameterBool;
private:
    float getValue() const override { return get() ? 1.0f : 0.0f; }
};
}
