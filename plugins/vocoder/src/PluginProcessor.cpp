#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariVocoderAudioProcessor::SattariVocoderAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();   // held-MIDI init lives in the module's constructor
}


// Factory presets: real parameter values (band count, choice index, semitones, ms, dB).
// Only listed params are set; anything omitted keeps its default. Key params (followKey /
// root / scale) are intentionally left untouched so a preset never fights the song key.
void SattariVocoderAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Classic Robot",   PV{ {"bands", 20}, {"wave", 0}, {"voicing", 1}, {"octave",  0}, {"formant",  0}, {"unvoiced", 0.30f}, {"attack",  5}, {"release",  60}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Fat Synth Choir", PV{ {"bands", 28}, {"wave", 0}, {"voicing", 3}, {"octave",  0}, {"formant",  2}, {"unvoiced", 0.25f}, {"attack",  8}, {"release", 120}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Bright Talk",     PV{ {"bands", 32}, {"wave", 1}, {"voicing", 0}, {"octave",  0}, {"formant",  3}, {"unvoiced", 0.50f}, {"attack",  3}, {"release",  40}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Deep Menace",     PV{ {"bands", 16}, {"wave", 0}, {"voicing", 2}, {"octave", -1}, {"formant", -4}, {"unvoiced", 0.20f}, {"attack", 10}, {"release", 150}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Airy Sibilant",   PV{ {"bands", 24}, {"wave", 0}, {"voicing", 1}, {"octave",  1}, {"formant",  4}, {"unvoiced", 0.70f}, {"attack",  2}, {"release",  30}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Seventh Pad",     PV{ {"bands", 26}, {"wave", 0}, {"voicing", 4}, {"octave",  0}, {"formant",  0}, {"unvoiced", 0.30f}, {"attack", 15}, {"release", 200}, {"mix", 1.0f}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariVocoderAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::VocoderModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariVocoderAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariVocoderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariVocoderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariVocoderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariVocoderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariVocoderAudioProcessor::createEditor()
{
    return new SattariVocoderAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariVocoderAudioProcessor();
}
