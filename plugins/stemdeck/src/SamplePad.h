#pragma once

#include <JuceHeader.h>
#include "DeckPlayer.h"

class SamplePad
{
public:
    void prepare (double sampleRate);
    void setLoadedAudio (std::shared_ptr<const DeckPlayer::LoadedAudio> newAudio);
    std::shared_ptr<const DeckPlayer::LoadedAudio> getLoadedAudio() const;
    juce::String getPath() const;
    void trigger();
    void stop();

    void process (juce::AudioBuffer<float>& destination, int startSample, int numSamples, float gainDb);

private:
    std::shared_ptr<const DeckPlayer::LoadedAudio> loadedAudio;
    double currentSampleRate = 44100.0;
    double playheadSamples = 0.0;
    std::atomic<bool> active { false };
};
