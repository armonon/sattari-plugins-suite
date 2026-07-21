#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }
}

SattariPeakAudioProcessor::SattariPeakAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariPeakAudioProcessor::createParameterLayout()
{
    // Declared BY the module with an empty prefix, so the historical parameter IDs
    // are unchanged and saved sessions still load.
    sattari::dsp::PeakModule decl;
    sattari::dsp::SattariModule::ParamList p;
    decl.addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariPeakAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
    reportedLatency = module.latencySamples();
    pendingLatency.store (reportedLatency, std::memory_order_relaxed);
    setLatencySamples (reportedLatency);
}

void SattariPeakAudioProcessor::reset()
{
    module.reset();
}

void SattariPeakAudioProcessor::handleAsyncUpdate()
{
    const int want = pendingLatency.load (std::memory_order_relaxed);
    if (want != reportedLatency)
    {
        reportedLatency = want;
        setLatencySamples (want);
    }
}

bool SattariPeakAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariPeakAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);

    // The module records the lookahead it is using; publishing it to the host happens
    // on the message thread, never here.
    const int lat = module.latencySamples();
    if (lat != pendingLatency.load (std::memory_order_relaxed))
    {
        pendingLatency.store (lat, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void SattariPeakAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariPeakAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariPeakAudioProcessor::createEditor()
{
    return new SattariPeakAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariPeakAudioProcessor();
}
