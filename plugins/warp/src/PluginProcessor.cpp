#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariWarpAudioProcessor::SattariWarpAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (0..1 macros, Hz, dB, choice index). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariWarpAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Subtle Character", PV{ {"dose", 0.30f}, {"vowel", 0.40f}, {"vowelAmt", 0.40f}, {"robotAmt", 0.10f}, {"drive", 0.20f}, {"crush", 0.10f}, {"stutter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Talkbox Vowel",    PV{ {"dose", 0.70f}, {"vowel", 0.50f}, {"vowelAmt", 0.85f}, {"robotAmt", 0.00f}, {"drive", 0.15f}, {"crush", 0.00f}, {"stutter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Robot Voice",      PV{ {"dose", 0.70f}, {"vowel", 0.20f}, {"vowelAmt", 0.30f}, {"robotFreq", 90}, {"robotAmt", 0.80f}, {"drive", 0.20f}, {"crush", 0.10f}, {"stutter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Lo-Fi Grit",       PV{ {"dose", 0.65f}, {"vowel", 0.30f}, {"vowelAmt", 0.20f}, {"robotAmt", 0.00f}, {"drive", 0.70f}, {"crush", 0.60f}, {"stutter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Glitch Stutter",   PV{ {"dose", 0.70f}, {"vowelAmt", 0.20f}, {"robotAmt", 0.10f}, {"drive", 0.20f}, {"crush", 0.20f}, {"stutter", 0.80f}, {"stutterRate", 3}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Metallic Swarm",   PV{ {"dose", 0.80f}, {"vowel", 0.60f}, {"vowelAmt", 0.50f}, {"robotFreq", 300}, {"robotAmt", 0.60f}, {"drive", 0.30f}, {"crush", 0.30f}, {"stutter", 0.0f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Fully Mangled",    PV{ {"dose", 1.00f}, {"vowel", 0.50f}, {"vowelAmt", 0.70f}, {"robotFreq", 160}, {"robotAmt", 0.70f}, {"drive", 0.80f}, {"crush", 0.70f}, {"stutter", 0.50f}, {"stutterRate", 4}, {"mix", 1.0f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariWarpAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::WarpModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariWarpAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariWarpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SattariWarpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariWarpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SattariWarpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto restored = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (restored);
        apvts.replaceState (restored);
    }
}

juce::AudioProcessorEditor* SattariWarpAudioProcessor::createEditor()
{
    return new SattariWarpAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariWarpAudioProcessor();
}
