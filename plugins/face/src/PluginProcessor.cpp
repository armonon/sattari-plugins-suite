#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariFaceAudioProcessor::SattariFaceAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (semitones, 0..1 mix, dB). Only listed params
// are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariFaceAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Deeper / Bigger",  PV{ {"pitch",   0}, {"formant", -4}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Smaller / Brighter",PV{ {"pitch",   0}, {"formant",  4}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Feminize",         PV{ {"pitch",   0}, {"formant",  5}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Masculinize",      PV{ {"pitch",   0}, {"formant", -5}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Chipmunk",         PV{ {"pitch",   7}, {"formant",  3}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Monster",          PV{ {"pitch",  -7}, {"formant", -5}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Octave Up",        PV{ {"pitch",  12}, {"formant",  0}, {"mix", 1.0f},  {"output", 0} });
    presetBank.addFactoryPreset ("Subtle Warmth",    PV{ {"pitch",   0}, {"formant", -2}, {"mix", 0.65f}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariFaceAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::FaceModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariFaceAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
    setLatencySamples (module.latencySamples());
}

bool SattariFaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariFaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariFaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariFaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariFaceAudioProcessor::createEditor()
{
    return new SattariFaceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariFaceAudioProcessor();
}
