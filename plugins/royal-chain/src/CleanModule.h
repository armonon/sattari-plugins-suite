#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

class CleanModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float inputGainDb, float clean, float royal);

private:
    double sr = 44100.0;
    sattari::dsp::EnvelopeFollower envelope;
    float hpX[2] { 0.0f, 0.0f };
    float hpY[2] { 0.0f, 0.0f };
};
