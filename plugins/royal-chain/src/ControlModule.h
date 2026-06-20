#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

class ControlModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float control, float royal);
    float getGainReductionDb() const { return gainReductionDb; }

private:
    double sr = 44100.0;
    sattari::dsp::EnvelopeFollower envelope;
    float gainSmooth = 1.0f;
    float gainReductionDb = 0.0f;
};
