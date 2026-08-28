#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariMeterAudioProcessor::SattariMeterAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariMeterAudioProcessor::createParameterLayout()
{
    // Declared BY the module with an empty prefix, so the historical parameter ID
    // ("target") is unchanged and saved sessions still load.
    sattari::dsp::MeterModule decl;
    sattari::dsp::SattariModule::ParamList p;
    decl.addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariMeterAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariMeterAudioProcessor::reset()
{
    module.reset();
}

bool SattariMeterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMeterAudioProcessor::createEditor()
{
    return new SattariMeterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariMeterAudioProcessor();
}
