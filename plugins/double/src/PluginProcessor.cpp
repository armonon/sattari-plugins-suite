#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariDoubleAudioProcessor::SattariDoubleAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();   // voice RNG seeding lives in the module's constructor
}

// Factory presets: real parameter values (cents, ms, 0..1, dB). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariDoubleAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Subtle Glue",     PV{ {"detune",  6}, {"timing", 10}, {"drift", 0.15f}, {"width", 0.45f}, {"level", 0.45f}, {"output", 0} });
    presetBank.addFactoryPreset ("Tight & Close",   PV{ {"detune",  4}, {"timing",  6}, {"drift", 0.10f}, {"width", 0.40f}, {"level", 0.50f}, {"output", 0} });
    presetBank.addFactoryPreset ("Natural Double",  PV{ {"detune", 12}, {"timing", 18}, {"drift", 0.40f}, {"width", 0.75f}, {"level", 0.60f}, {"output", 0} });
    presetBank.addFactoryPreset ("Wide Doubler",    PV{ {"detune", 16}, {"timing", 24}, {"drift", 0.50f}, {"width", 1.00f}, {"level", 0.75f}, {"output", 0} });
    presetBank.addFactoryPreset ("Lush Thickener",  PV{ {"detune", 22}, {"timing", 30}, {"drift", 0.70f}, {"width", 0.90f}, {"level", 0.85f}, {"output", -1} });
    presetBank.addFactoryPreset ("Chorus Wash",     PV{ {"detune", 28}, {"timing", 40}, {"drift", 0.90f}, {"width", 1.00f}, {"level", 0.90f}, {"output", -2} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariDoubleAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::DoubleModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariDoubleAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariDoubleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariDoubleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariDoubleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariDoubleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariDoubleAudioProcessor::createEditor()
{
    return new SattariDoubleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariDoubleAudioProcessor();
}
