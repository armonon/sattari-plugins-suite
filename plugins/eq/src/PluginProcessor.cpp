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
    registerFactoryPresets();
}

// Factory presets: real parameter values (Hz, dB, Q, Dyn 0..1). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariEQAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal Presence",    PV{ {"hpf", 80}, {"f1", 200}, {"g1", -2}, {"q1", 0.8f}, {"f2", 3000}, {"g2", 3},  {"q2", 1.2f}, {"f3", 8000}, {"g3", 2.5f}, {"q3", 0.9f} });
    presetBank.addFactoryPreset ("De-Mud",            PV{ {"hpf", 60}, {"f1", 300}, {"g1", -4}, {"q1", 1.5f}, {"f2", 250}, {"g2", -3}, {"q2", 1.0f} });
    presetBank.addFactoryPreset ("Air & Sparkle",     PV{ {"f3", 12000}, {"g3", 4}, {"q3", 0.7f}, {"f2", 6000}, {"g2", 2}, {"q2", 0.9f}, {"lpf", 20000} });
    presetBank.addFactoryPreset ("Warm Body",         PV{ {"f1", 150}, {"g1", 2.5f}, {"q1", 0.8f}, {"f2", 500}, {"g2", 1.5f}, {"q2", 1.0f} });
    presetBank.addFactoryPreset ("Dynamic De-Harsh",  PV{ {"f2", 4000}, {"g2", -5}, {"q2", 2.0f}, {"d2", 0.8f}, {"f3", 7000}, {"g3", -4}, {"q3", 1.5f}, {"d3", 0.7f} });
    presetBank.addFactoryPreset ("Telephone",         PV{ {"hpf", 400}, {"lpf", 3000}, {"f2", 1500}, {"g2", 4}, {"q2", 1.5f} });
    presetBank.addFactoryPreset ("Tighten Lows",      PV{ {"hpf", 100}, {"f1", 120}, {"g1", -3}, {"q1", 1.2f}, {"d1", 0.6f} });
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
