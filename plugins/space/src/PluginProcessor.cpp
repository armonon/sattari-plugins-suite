#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariSpaceAudioProcessor::SattariSpaceAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (0..1 amounts, choice index, dB). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariSpaceAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Small Room",        PV{ {"size", 0.30f}, {"damp", 0.60f}, {"revmix", 0.20f}, {"div", 0}, {"width", 0.8f} });
    presetBank.addFactoryPreset ("Vocal Plate",       PV{ {"size", 0.55f}, {"damp", 0.40f}, {"revmix", 0.32f}, {"div", 0}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Huge Hall",         PV{ {"size", 0.90f}, {"damp", 0.30f}, {"revmix", 0.45f}, {"div", 0}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Synced Slap + Verb",PV{ {"size", 0.50f}, {"damp", 0.50f}, {"revmix", 0.25f}, {"div", 3}, {"feedback", 0.30f}, {"delaymix", 0.25f}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Dotted Dream",      PV{ {"size", 0.70f}, {"damp", 0.40f}, {"revmix", 0.35f}, {"div", 4}, {"feedback", 0.45f}, {"delaymix", 0.30f}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Ambient Wash",      PV{ {"size", 0.95f}, {"damp", 0.25f}, {"revmix", 0.55f}, {"div", 6}, {"feedback", 0.60f}, {"delaymix", 0.35f}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Dark Chamber",      PV{ {"size", 0.65f}, {"damp", 0.80f}, {"revmix", 0.35f}, {"div", 0}, {"width", 0.9f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariSpaceAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::SpaceModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariSpaceAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariSpaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariSpaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariSpaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariSpaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariSpaceAudioProcessor::createEditor()
{
    return new SattariSpaceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariSpaceAudioProcessor();
}
