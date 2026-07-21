#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
inline double clampF (double f, double sr) { return juce::jlimit (20.0, sr * 0.45, f); }

constexpr const char* kFreqIds[SattariEQAudioProcessor::kBands] = { "f1", "f2", "f3" };
constexpr const char* kGainIds[SattariEQAudioProcessor::kBands] = { "g1", "g2", "g3" };
constexpr const char* kQIds[SattariEQAudioProcessor::kBands]    = { "q1", "q2", "q3" };
constexpr const char* kDynIds[SattariEQAudioProcessor::kBands]  = { "d1", "d2", "d3" };
}


SattariEQAudioProcessor::SattariEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariEQAudioProcessor::createParameterLayout()
{
    // Declared BY the module with an empty prefix, so the historical parameter IDs
    // ("hpf", "f1", "g1", ...) are unchanged and saved sessions still load.
    sattari::dsp::EqModule decl;
    sattari::dsp::SattariModule::ParamList p;
    decl.addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariEQAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariEQAudioProcessor::reset()
{
    module.reset();
}

bool SattariEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariEQAudioProcessor::createEditor()
{
    return new SattariEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariEQAudioProcessor();
}
