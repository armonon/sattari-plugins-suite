#include "ChopProcessor.h"
#include "ChopEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout SattariChopAudioProcessor::createParameterLayout()
{
    using FloatP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    // The load-bearing control (§2.2): re-thresholds the stored onset curve live.
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "sensitivity", 1 }, "Sensitivity", unit(), 0.5f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "output",      1 }, "Output",      unit(), 0.8f));

    return { p.begin(), p.end() };
}

SattariChopAudioProcessor::SattariChopAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void SattariChopAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

bool SattariChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SattariChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();   // scaffold: voices land on top of this. RT discipline (§5) applies here.
}

void SattariChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariChopAudioProcessor::createEditor()
{
    return new SattariChopAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariChopAudioProcessor();
}
