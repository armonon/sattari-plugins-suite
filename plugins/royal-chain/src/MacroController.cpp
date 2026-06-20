#include "MacroController.h"

namespace
{
float readParam (juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (auto* value = state.getRawParameterValue (id))
        return juce::jlimit (0.0f, 1.0f, value->load());
    return 0.0f;
}
}

RoyalMacros MacroController::readMacros (juce::AudioProcessorValueTreeState& state)
{
    RoyalMacros m;
    m.clean = readParam (state, "clean");
    m.tune = readParam (state, "tune");
    m.control = readParam (state, "control");
    m.tone = readParam (state, "tone");
    m.air = readParam (state, "air");
    m.smooth = readParam (state, "smooth");
    m.glow = readParam (state, "glow");
    m.width = readParam (state, "width");
    m.space = readParam (state, "space");
    m.royal = readParam (state, "royal");
    return m;
}

void MacroController::setParameterValue (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, float plainValue)
{
    if (auto* parameter = state.getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }
}
