#pragma once

#include <JuceHeader.h>
#include "SamplePad.h"

class PadBank
{
public:
    static constexpr int numPads = 8;

    void prepare (double sampleRate);
    void setPadAudio (int index, std::shared_ptr<const DeckPlayer::LoadedAudio> audio);
    juce::String getPadPath (int index) const;
    void triggerPad (int index);
    void stopPad (int index);
    void process (juce::AudioBuffer<float>& destination, int startSample, int numSamples, const std::array<float, numPads>& gainsDb);

private:
    std::array<SamplePad, numPads> pads;
};
