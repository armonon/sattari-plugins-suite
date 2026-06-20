#include "ControlModule.h"

void ControlModule::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    envelope.prepare (sr, 6.0f, 120.0f);   // fixed times = original inline coefficients
    reset();
}

void ControlModule::reset()
{
    envelope.reset();
    gainSmooth = 1.0f;
    gainReductionDb = 0.0f;
}

void ControlModule::process (juce::AudioBuffer<float>& buffer, float control, float royal)
{
    const float amount = juce::jlimit (0.0f, 1.0f, 0.75f * control + 0.25f * royal);
    const float thresholdDb = -26.0f + 14.0f * (1.0f - amount);
    const float ratio = 1.5f + 4.0f * amount;
    const float makeup = juce::Decibels::decibelsToGain (1.0f + 3.0f * amount);

    float maxReduction = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float detector = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            detector += std::abs (buffer.getSample (ch, i));
        detector /= (float) juce::jmax (1, buffer.getNumChannels());

        const float env = envelope.process (detector);

        const float levelDb = juce::Decibels::gainToDecibels (env + 1.0e-8f);
        float gainDb = 0.0f;
        if (levelDb > thresholdDb)
            gainDb = -((levelDb - thresholdDb) * (1.0f - 1.0f / ratio));

        const float targetGain = juce::Decibels::decibelsToGain (gainDb) * makeup;
        gainSmooth += 0.012f * (targetGain - gainSmooth);
        maxReduction = juce::jmax (maxReduction, -gainDb);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * gainSmooth);
    }

    gainReductionDb += 0.10f * (maxReduction - gainReductionDb);
}
