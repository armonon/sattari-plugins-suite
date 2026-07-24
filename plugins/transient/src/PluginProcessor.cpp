#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariTransientAudioProcessor::SattariTransientAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (attack/sustain -1..1, output dB). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariTransientAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Add Punch",       PV{ {"attack", 0.5f},  {"sustain", 0.0f} });
    presetBank.addFactoryPreset ("Softer Attack",   PV{ {"attack", -0.4f}, {"sustain", 0.0f} });
    presetBank.addFactoryPreset ("More Body",       PV{ {"attack", 0.0f},  {"sustain", 0.5f} });
    presetBank.addFactoryPreset ("Tighten Tail",    PV{ {"attack", 0.2f},  {"sustain", -0.5f} });
    presetBank.addFactoryPreset ("Snappy Drums",    PV{ {"attack", 0.7f},  {"sustain", -0.3f} });
    presetBank.addFactoryPreset ("Room & Sustain",  PV{ {"attack", -0.2f}, {"sustain", 0.6f} });
    presetBank.addFactoryPreset ("Big & Punchy",    PV{ {"attack", 0.6f},  {"sustain", 0.4f}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariTransientAudioProcessor::createParameterLayout()
{
    // Declared BY the module, with an empty prefix so the historical parameter IDs
    // ("attack", "sustain", "output") are unchanged and saved sessions still load.
    sattari::dsp::TransientModule decl;
    sattari::dsp::SattariModule::ParamList p;
    decl.addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariTransientAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariTransientAudioProcessor::reset()
{
    module.reset();
}

bool SattariTransientAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariTransientAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariTransientAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariTransientAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariTransientAudioProcessor::createEditor()
{
    return new SattariTransientAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariTransientAudioProcessor();
}
