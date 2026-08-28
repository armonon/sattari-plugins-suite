#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariRiserAudioProcessor::SattariRiserAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (choice index, Hz, semitones, 0..1). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
// (Arm is deliberately left at its default so loading a preset never fires a build.)
void SattariRiserAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Classic 1-Bar Riser", PV{ {"mode", 0}, {"length", 1}, {"noiseTone", 0.70f}, {"sweepLo", 180}, {"sweepHi", 9000}, {"reso", 2.0f}, {"pitchRise", 12}, {"stutter", 0.40f}, {"mix", 0.80f} });
    presetBank.addFactoryPreset ("Long 8-Bar Build",    PV{ {"mode", 0}, {"length", 4}, {"noiseTone", 0.80f}, {"sweepLo", 120}, {"sweepHi", 12000}, {"reso", 2.5f}, {"pitchRise", 18}, {"stutter", 0.50f}, {"mix", 0.85f} });
    presetBank.addFactoryPreset ("Noise Sweep Up",      PV{ {"mode", 0}, {"length", 2}, {"noiseTone", 1.0f}, {"sweepLo", 200}, {"sweepHi", 16000}, {"reso", 3.0f}, {"pitchRise", 0}, {"stutter", 0.20f}, {"mix", 0.90f} });
    presetBank.addFactoryPreset ("Downlifter",          PV{ {"mode", 1}, {"length", 1}, {"noiseTone", 0.60f}, {"sweepLo", 150}, {"sweepHi", 8000}, {"reso", 2.0f}, {"pitchRise", 12}, {"stutter", 0.30f}, {"mix", 0.85f} });
    presetBank.addFactoryPreset ("Impact Hit",          PV{ {"mode", 2}, {"length", 0}, {"noiseTone", 0.50f}, {"sweepLo", 100}, {"sweepHi", 7000}, {"reso", 2.5f}, {"pitchRise", 8}, {"stutter", 0.50f}, {"mix", 0.90f} });
    presetBank.addFactoryPreset ("Tonal Riser",         PV{ {"mode", 0}, {"length", 2}, {"noiseTone", 0.20f}, {"sweepLo", 220}, {"sweepHi", 6000}, {"reso", 4.0f}, {"pitchRise", 24}, {"stutter", 0.10f}, {"mix", 0.80f} });
    presetBank.addFactoryPreset ("Stutter Build",       PV{ {"mode", 0}, {"length", 1}, {"noiseTone", 0.60f}, {"sweepLo", 180}, {"sweepHi", 10000}, {"reso", 2.0f}, {"pitchRise", 12}, {"stutter", 0.90f}, {"mix", 0.85f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariRiserAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::RiserModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariRiserAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariRiserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariRiserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariRiserAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariRiserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariRiserAudioProcessor::createEditor()
{
    return new SattariRiserAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariRiserAudioProcessor();
}
