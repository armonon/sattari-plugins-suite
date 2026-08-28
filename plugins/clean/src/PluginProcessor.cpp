#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariCleanAudioProcessor::SattariCleanAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (Amount 0..1, Sensitivity 1.5..12, Output dB).
// Only listed params are set; anything omitted keeps its default.
void SattariCleanAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Light Cleanup",   PV{ {"amount", 0.35f}, {"sensitivity", 3.0f} });
    presetBank.addFactoryPreset ("Vocal De-Noise",  PV{ {"amount", 0.5f},  {"sensitivity", 4.0f} });
    presetBank.addFactoryPreset ("Hiss Removal",    PV{ {"amount", 0.6f},  {"sensitivity", 5.0f} });
    presetBank.addFactoryPreset ("Heavy Cleanup",   PV{ {"amount", 0.8f},  {"sensitivity", 6.0f} });
    presetBank.addFactoryPreset ("Gentle Room",     PV{ {"amount", 0.3f},  {"sensitivity", 2.5f} });
    presetBank.addFactoryPreset ("Aggressive",      PV{ {"amount", 0.95f}, {"sensitivity", 8.0f}, {"output", 1} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariCleanAudioProcessor::createParameterLayout()
{
    // Declared BY the module with an empty prefix, so the historical parameter IDs are
    // unchanged and saved sessions still load.
    sattari::dsp::CleanModule decl;
    sattari::dsp::SattariModule::ParamList p;
    decl.addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariCleanAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariCleanAudioProcessor::reset()
{
    module.reset();
}

bool SattariCleanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariCleanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariCleanAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariCleanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariCleanAudioProcessor::createEditor()
{
    return new SattariCleanAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariCleanAudioProcessor();
}
