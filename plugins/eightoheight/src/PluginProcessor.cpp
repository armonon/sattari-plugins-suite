#include "PluginProcessor.h"
#include "PluginEditor.h"



Sattari808AudioProcessor::Sattari808AudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (choice index, seconds, 0..1). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void Sattari808AudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Deep 808",       PV{ {"wave", 0}, {"glide", 0.10f}, {"decay", 1.80f}, {"drive", 0.15f}, {"subfold", 0.35f}, {"blend", 0.85f}, {"output", 1.0f} });
    presetBank.addFactoryPreset ("Distorted 808",  PV{ {"wave", 1}, {"glide", 0.08f}, {"decay", 1.20f}, {"drive", 0.75f}, {"subfold", 0.60f}, {"blend", 0.90f}, {"output", 1.0f} });
    presetBank.addFactoryPreset ("Short Punchy",   PV{ {"wave", 0}, {"glide", 0.02f}, {"decay", 0.35f}, {"drive", 0.40f}, {"subfold", 0.20f}, {"blend", 0.90f}, {"output", 1.0f} });
    presetBank.addFactoryPreset ("Sub Boom",       PV{ {"wave", 0}, {"glide", 0.15f}, {"decay", 2.30f}, {"drive", 0.10f}, {"subfold", 0.50f}, {"blend", 0.80f}, {"output", 1.0f} });
    presetBank.addFactoryPreset ("Glide Bass",     PV{ {"wave", 0}, {"glide", 0.60f}, {"decay", 1.00f}, {"drive", 0.20f}, {"subfold", 0.25f}, {"blend", 0.85f}, {"output", 1.0f} });
    presetBank.addFactoryPreset ("Trap Knock",     PV{ {"wave", 1}, {"glide", 0.05f}, {"decay", 0.70f}, {"drive", 0.55f}, {"subfold", 0.40f}, {"blend", 0.90f}, {"output", 1.1f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout Sattari808AudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::EightOhEightModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void Sattari808AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool Sattari808AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void Sattari808AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);
    module.process (buffer, midi);
}

juce::AudioProcessorEditor* Sattari808AudioProcessor::createEditor()
{
    return new Sattari808AudioProcessorEditor (*this);
}

void Sattari808AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void Sattari808AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Sattari808AudioProcessor();
}
