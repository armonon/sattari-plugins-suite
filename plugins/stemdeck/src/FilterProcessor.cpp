#include "FilterProcessor.h"

void FilterProcessor::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void FilterProcessor::reset()
{
    for (auto& s : state)
        s = {};
}

float FilterProcessor::processOne (float input, ChannelState& s, float control)
{
    control = juce::jlimit (-1.0f, 1.0f, control);

    if (std::abs (control) < 0.015f)
        return input;

    if (control > 0.0f) // low-pass as knob moves right
    {
        const float cutoff = juce::jmap (control, 0.0f, 1.0f, 18000.0f, 450.0f);
        const float coeff = juce::jlimit (0.001f, 0.999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sr));
        s.low += coeff * (input - s.low);
        return s.low;
    }

    const float amount = -control; // high-pass as knob moves left
    const float cutoff = juce::jmap (amount, 0.0f, 1.0f, 30.0f, 1200.0f);
    const float coeff = juce::jlimit (0.001f, 0.999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sr));
    s.highSmooth += coeff * (input - s.highSmooth);
    return input - s.highSmooth;
}

void FilterProcessor::processSample (float& left, float& right, float control)
{
    left = processOne (left, state[0], control);
    right = processOne (right, state[1], control);
}
