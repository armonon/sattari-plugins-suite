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
    mode = Mode::bypass;
    cachedControl = 2.0f;
    coefficient = 0.0f;
}

void FilterProcessor::updateCoefficients (float control) noexcept
{
    control = juce::jlimit (-1.0f, 1.0f, control);
    if (std::abs (control - cachedControl) <= 1.0e-6f)
        return;

    cachedControl = control;

    if (std::abs (control) < 0.015f)
    {
        mode = Mode::bypass;
        return;
    }

    if (control > 0.0f) // low-pass as knob moves right
    {
        const float cutoff = juce::jmap (control, 0.0f, 1.0f, 18000.0f, 450.0f);
        coefficient = juce::jlimit (0.001f, 0.999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sr));
        mode = Mode::lowPass;
        return;
    }

    const float amount = -control; // high-pass as knob moves left
    const float cutoff = juce::jmap (amount, 0.0f, 1.0f, 30.0f, 1200.0f);
    coefficient = juce::jlimit (0.001f, 0.999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sr));
    mode = Mode::highPass;
}

float FilterProcessor::processOne (float input, ChannelState& s) noexcept
{
    if (mode == Mode::lowPass)
    {
        s.low += coefficient * (input - s.low);
        return s.low;
    }
    if (mode == Mode::highPass)
    {
        s.highSmooth += coefficient * (input - s.highSmooth);
        return input - s.highSmooth;
    }
    return input;
}

void FilterProcessor::processSample (float& left, float& right, float control)
{
    updateCoefficients (control);
    left = processOne (left, state[0]);
    right = processOne (right, state[1]);
}
