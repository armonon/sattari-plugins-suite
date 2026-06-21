#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

namespace
{
void setParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* parameter = apvts.getParameter (id);
    if (parameter == nullptr)
        throw std::runtime_error ("missing parameter: " + id.toStdString());

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

double rmsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    int count = 0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const double diff = (double) a.getSample (channel, i) - (double) b.getSample (channel, i);
            sum += diff * diff;
            ++count;
        }
    }

    return std::sqrt (sum / (double) juce::jmax (1, count));
}
}

int main()
{
    SattariAutoPitchAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto& apvts = processor.parameters();
    setParameter (apvts, "keyRoot", 0.0f); // C
    setParameter (apvts, "scale", 0.0f); // major
    setParameter (apvts, "style", 2.0f); // hard
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);

    constexpr int blockSize = 512;
    constexpr int blocks = 60;
    constexpr double sampleRate = 48000.0;
    const double detunedFrequency = 440.0 * std::pow (2.0, 35.0 / 1200.0); // A4 +35 cents

    double phase = 0.0;
    double changedEnergy = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = 0.18f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * detunedFrequency / sampleRate;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }

        juce::AudioBuffer<float> dry;
        dry.makeCopyOf (buffer);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        changedEnergy += rmsDifference (buffer, dry);
    }

    const auto status = processor.getLiveStatusSummary();
    // Assert on what proves the engine ran, not on marketing copy: the status
    // must name either the real Rubber Band engine or the fallback guide.
    if (! (status.containsIgnoreCase ("Rubber Band") || status.containsIgnoreCase ("fallback")))
    {
        std::cerr << "unexpected status: " << status << "\n";
        return 1;
    }

    const auto keyTabSummary = processor.getKeyDetectionTabSummary();
    if (! keyTabSummary.containsIgnoreCase ("Sattari Wheel")
        || ! keyTabSummary.containsIgnoreCase ("compatibility")
        || ! keyTabSummary.containsIgnoreCase ("Send to Tune"))
    {
        std::cerr << "key detection tab did not expose harmonic-lab bridge: " << keyTabSummary << "\n";
        return 1;
    }

    if (changedEnergy <= 0.01)
    {
        std::cerr << "pitch path did not audibly alter the detuned test signal; changedEnergy=" << changedEnergy << "\n";
        return 1;
    }

    const juce::StringArray expectedModes { "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" };
    for (int mode = 0; mode < expectedModes.size(); ++mode)
    {
        SattariAutoPitchAudioProcessor modeProcessor;
        modeProcessor.prepareToPlay (48000.0, 512);
        auto& modeApvts = modeProcessor.parameters();
        setParameter (modeApvts, "keyRoot", 0.0f);
        setParameter (modeApvts, "scale", 0.0f);
        setParameter (modeApvts, "style", (float) mode);
        setParameter (modeApvts, "amount", 1.0f);
        setParameter (modeApvts, "speed", 1.0f);
        setParameter (modeApvts, "enableCorrection", 1.0f);
        setParameter (modeApvts, "doNoHarm", 0.0f);

        double modePhase = 0.0;
        double modeChangedEnergy = 0.0;
        for (int block = 0; block < 30; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const float sample = 0.18f * std::sin ((float) modePhase);
                modePhase += juce::MathConstants<double>::twoPi * detunedFrequency / sampleRate;
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, i, sample);
            }

            juce::AudioBuffer<float> dry;
            dry.makeCopyOf (buffer);
            juce::MidiBuffer midi;
            modeProcessor.processBlock (buffer, midi);
            modeChangedEnergy += rmsDifference (buffer, dry);
        }

        const auto modeStatus = modeProcessor.getLiveStatusSummary();
        if (! modeStatus.contains ("mode " + expectedModes[mode]))
        {
            std::cerr << "mode status missing " << expectedModes[mode] << ": " << modeStatus << "\n";
            return 1;
        }

        if (modeChangedEnergy <= 0.005)
        {
            std::cerr << "mode did not produce audible correction: " << expectedModes[mode]
                      << ", changedEnergy=" << modeChangedEnergy << "\n";
            return 1;
        }
    }

    std::cout << "Auto Pitch core contract ok\n";
    std::cout << "  status=" << status << "\n";
    std::cout << "  keyTab=" << keyTabSummary << "\n";
    std::cout << "  changedEnergy=" << changedEnergy << "\n";
    std::cout << "  modes=Natural, Classic, Modern Rap, Robot, Big Harmony\n";
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    std::cout << "  engine=rubberband\n";
#else
    std::cout << "  engine=fallback-guide\n";
#endif
    return 0;
}
