#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariShimmerAudioProcessor::SattariShimmerAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (0..1 amounts, choice index, ms). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariShimmerAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Classic Octave",  PV{ {"size", 0.75f}, {"shimmer", 0.45f}, {"interval", 0}, {"damp", 0.35f}, {"predelay", 20}, {"width", 1.0f}, {"mix", 0.35f} });
    presetBank.addFactoryPreset ("Angelic Pad",     PV{ {"size", 0.90f}, {"shimmer", 0.60f}, {"interval", 0}, {"damp", 0.30f}, {"predelay", 40}, {"width", 1.0f}, {"mix", 0.50f} });
    presetBank.addFactoryPreset ("Fifth Bloom",     PV{ {"size", 0.70f}, {"shimmer", 0.50f}, {"interval", 1}, {"damp", 0.40f}, {"predelay", 25}, {"mix", 0.40f} });
    presetBank.addFactoryPreset ("Subtle Sparkle",  PV{ {"size", 0.60f}, {"shimmer", 0.30f}, {"interval", 0}, {"damp", 0.50f}, {"predelay", 15}, {"mix", 0.25f} });
    presetBank.addFactoryPreset ("Cathedral",       PV{ {"size", 0.95f}, {"shimmer", 0.70f}, {"interval", 0}, {"damp", 0.25f}, {"predelay", 60}, {"width", 1.0f}, {"mix", 0.55f} });
    presetBank.addFactoryPreset ("Fourth Glass",    PV{ {"size", 0.80f}, {"shimmer", 0.55f}, {"interval", 2}, {"damp", 0.35f}, {"predelay", 30}, {"mix", 0.45f} });
    presetBank.addFactoryPreset ("Dark Shimmer",    PV{ {"size", 0.85f}, {"shimmer", 0.50f}, {"interval", 0}, {"damp", 0.70f}, {"predelay", 35}, {"mix", 0.40f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariShimmerAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::ShimmerModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariShimmerAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariShimmerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariShimmerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariShimmerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariShimmerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariShimmerAudioProcessor::createEditor()
{
    return new SattariShimmerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariShimmerAudioProcessor();
}
