#include "PadBank.h"

void PadBank::prepare (double sampleRate)
{
    for (auto& pad : pads)
        pad.prepare (sampleRate);
}

void PadBank::setPadAudio (int index, std::shared_ptr<const DeckPlayer::LoadedAudio> audio)
{
    if (juce::isPositiveAndBelow (index, numPads))
        pads[(size_t) index].setLoadedAudio (std::move (audio));
}

juce::String PadBank::getPadPath (int index) const
{
    if (juce::isPositiveAndBelow (index, numPads))
        return pads[(size_t) index].getPath();
    return {};
}

void PadBank::triggerPad (int index)
{
    if (juce::isPositiveAndBelow (index, numPads))
        pads[(size_t) index].trigger();
}

void PadBank::stopPad (int index)
{
    if (juce::isPositiveAndBelow (index, numPads))
        pads[(size_t) index].stop();
}

void PadBank::process (juce::AudioBuffer<float>& destination, int startSample, int numSamples, const std::array<float, numPads>& gainsDb)
{
    for (int i = 0; i < numPads; ++i)
        pads[(size_t) i].process (destination, startSample, numSamples, gainsDb[(size_t) i]);
}
