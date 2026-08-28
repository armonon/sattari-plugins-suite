#include "PluginProcessor.h"
#include <sattari_audio_core/SattariHostTransport.h>
#include "PluginEditor.h"
#include <cmath>

juce::StringArray SattariPulseAudioProcessor::rateChoices() { return sattari::dsp::PulseModule::rateChoices(); }

juce::StringArray SattariPulseAudioProcessor::shapeChoices() { return sattari::dsp::PulseModule::shapeChoices(); }

// Ducking curve: phase 0 = the beat (deepest duck / gate), returning to unity
// across the cycle. Returns a gain in [0,1] where 1 leaves the signal untouched.

SattariPulseAudioProcessor::SattariPulseAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}
SattariPulseAudioProcessor::~SattariPulseAudioProcessor() = default;   // module stops its own tempo reader

// Factory presets: real parameter values (choice index, 0..1). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariPulseAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Sidechain Pump",    PV{ {"shape", 0}, {"rate", 2}, {"depth", 0.90f}, {"smooth", 0.30f}, {"filter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("1/8 Pump",          PV{ {"shape", 0}, {"rate", 4}, {"depth", 0.80f}, {"smooth", 0.25f}, {"filter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Hard Gate",         PV{ {"shape", 3}, {"rate", 4}, {"depth", 1.0f}, {"smooth", 0.10f}, {"filter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Trance Gate 1/16",  PV{ {"shape", 3}, {"rate", 6}, {"depth", 1.0f}, {"smooth", 0.15f}, {"filter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Rhythmic Filter",   PV{ {"shape", 0}, {"rate", 2}, {"depth", 0.60f}, {"smooth", 0.40f}, {"filter", 0.80f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Sawtooth Stab",     PV{ {"shape", 1}, {"rate", 4}, {"depth", 0.85f}, {"smooth", 0.20f}, {"filter", 0.30f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Spike Tremolo",     PV{ {"shape", 4}, {"rate", 6}, {"depth", 0.90f}, {"smooth", 0.20f}, {"filter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Half-Bar Swell",    PV{ {"shape", 2}, {"rate", 1}, {"depth", 0.70f}, {"smooth", 0.35f}, {"filter", 0.40f}, {"mix", 1.0f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariPulseAudioProcessor::createParameterLayout()
{
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::PulseModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariPulseAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariPulseAudioProcessor::releaseResources() {}

bool SattariPulseAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}


void SattariPulseAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariPulseAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariPulseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariPulseAudioProcessor::createEditor()
{
    return new SattariPulseAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariPulseAudioProcessor();
}
