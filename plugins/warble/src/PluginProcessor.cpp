#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariWarbleAudioProcessor::SattariWarbleAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (0..1 amounts, dB). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariWarbleAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Subtle Warmth",      PV{ {"age", 0.20f}, {"wow", 0.30f}, {"flutter", 0.30f}, {"sat", 0.40f}, {"tone", 0.50f}, {"hiss", 0.15f}, {"crackle", 0.05f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Old Tape",           PV{ {"age", 0.50f}, {"wow", 0.60f}, {"flutter", 0.50f}, {"sat", 0.60f}, {"tone", 0.60f}, {"hiss", 0.40f}, {"crackle", 0.20f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Dusty Vinyl",        PV{ {"age", 0.55f}, {"wow", 0.40f}, {"flutter", 0.30f}, {"sat", 0.45f}, {"tone", 0.55f}, {"hiss", 0.35f}, {"crackle", 0.70f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Wobbly VHS",         PV{ {"age", 0.70f}, {"wow", 0.90f}, {"flutter", 0.80f}, {"sat", 0.55f}, {"tone", 0.50f}, {"hiss", 0.50f}, {"crackle", 0.30f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Destroyed Cassette", PV{ {"age", 0.90f}, {"wow", 0.80f}, {"flutter", 0.90f}, {"sat", 0.80f}, {"tone", 0.70f}, {"hiss", 0.70f}, {"crackle", 0.60f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Lofi Saturator",     PV{ {"age", 0.45f}, {"wow", 0.15f}, {"flutter", 0.15f}, {"sat", 0.85f}, {"tone", 0.65f}, {"hiss", 0.10f}, {"crackle", 0.05f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Parallel Grit",      PV{ {"age", 0.60f}, {"wow", 0.50f}, {"flutter", 0.50f}, {"sat", 0.60f}, {"tone", 0.55f}, {"hiss", 0.40f}, {"crackle", 0.35f}, {"mix", 0.50f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariWarbleAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::WarbleModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariWarbleAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariWarbleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariWarbleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariWarbleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariWarbleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariWarbleAudioProcessor::createEditor()
{
    return new SattariWarbleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariWarbleAudioProcessor();
}
