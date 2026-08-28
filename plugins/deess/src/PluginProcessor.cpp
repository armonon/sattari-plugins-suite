#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariDeEssAudioProcessor::SattariDeEssAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (Hz, dB, choice index). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariDeEssAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal — Gentle",   PV{ {"freq", 6500}, {"threshold", -26}, {"range",  6}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Vocal — Firm",     PV{ {"freq", 7000}, {"threshold", -34}, {"range", 10}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Bright / Airy",    PV{ {"freq", 8500}, {"threshold", -30}, {"range",  7}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Rap Vocal",        PV{ {"freq", 7500}, {"threshold", -36}, {"range", 12}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Aggressive",       PV{ {"freq", 6000}, {"threshold", -40}, {"range", 16}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Wideband Ess",     PV{ {"freq", 6500}, {"threshold", -30}, {"range",  8}, {"mode", 1}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariDeEssAudioProcessor::createParameterLayout()
{
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::DeEssModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariDeEssAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariDeEssAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariDeEssAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariDeEssAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariDeEssAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariDeEssAudioProcessor::createEditor()
{
    return new SattariDeEssAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariDeEssAudioProcessor();
}
