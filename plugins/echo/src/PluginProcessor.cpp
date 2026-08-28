#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariEchoAudioProcessor::SattariEchoAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (choice index, Hz/ms, ratios). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariEchoAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Slapback",        PV{ {"sync", 0}, {"timems", 110}, {"feedback", 0.12f}, {"pingpong", 0}, {"tone", 0.0f}, {"diffusion", 0.0f}, {"mix", 0.28f} });
    presetBank.addFactoryPreset ("Ping-Pong 1/8",   PV{ {"sync", 1}, {"div", 3}, {"feedback", 0.45f}, {"pingpong", 1}, {"width", 1.0f}, {"tone", -0.2f}, {"mix", 0.35f} });
    presetBank.addFactoryPreset ("Dotted Eighth",   PV{ {"sync", 1}, {"div", 4}, {"feedback", 0.40f}, {"pingpong", 0}, {"tone", -0.15f}, {"mix", 0.32f} });
    presetBank.addFactoryPreset ("Dub Delay",       PV{ {"sync", 1}, {"div", 3}, {"feedback", 0.70f}, {"tone", -0.5f}, {"diffusion", 0.45f}, {"mix", 0.40f} });
    presetBank.addFactoryPreset ("Ambient Wash",    PV{ {"sync", 1}, {"div", 6}, {"feedback", 0.80f}, {"diffusion", 0.70f}, {"tone", -0.3f}, {"width", 1.0f}, {"mix", 0.50f} });
    presetBank.addFactoryPreset ("Quarter Echo",    PV{ {"sync", 1}, {"div", 0}, {"feedback", 0.35f}, {"pingpong", 0}, {"tone", -0.1f}, {"mix", 0.30f} });
    presetBank.addFactoryPreset ("Ducked Delay",    PV{ {"sync", 1}, {"div", 3}, {"feedback", 0.50f}, {"duck", 0.70f}, {"tone", -0.2f}, {"mix", 0.40f} });
    presetBank.addFactoryPreset ("Triplet Bounce",  PV{ {"sync", 1}, {"div", 5}, {"feedback", 0.42f}, {"pingpong", 1}, {"width", 1.0f}, {"mix", 0.35f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariEchoAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::EchoModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariEchoAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariEchoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariEchoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariEchoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariEchoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariEchoAudioProcessor::createEditor()
{
    return new SattariEchoAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariEchoAudioProcessor();
}
