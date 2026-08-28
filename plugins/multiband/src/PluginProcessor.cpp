#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                       const juce::String& name,
                                                       float min, float max, float def,
                                                       float step, float skew = 1.0f,
                                                       const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, step, skew);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

constexpr const char* kThrIds[SattariMultibandAudioProcessor::kBands]   = { "thr1", "thr2", "thr3" };
constexpr const char* kRatioIds[SattariMultibandAudioProcessor::kBands] = { "ratio1", "ratio2", "ratio3" };
constexpr const char* kKneeIds[SattariMultibandAudioProcessor::kBands]  = { "knee1", "knee2", "knee3" };
constexpr const char* kAtkIds[SattariMultibandAudioProcessor::kBands]   = { "atk1", "atk2", "atk3" };
constexpr const char* kRelIds[SattariMultibandAudioProcessor::kBands]   = { "rel1", "rel2", "rel3" };
constexpr const char* kMakeupIds[SattariMultibandAudioProcessor::kBands]= { "mk1", "mk2", "mk3" };
constexpr const char* kSoloIds[SattariMultibandAudioProcessor::kBands]  = { "solo1", "solo2", "solo3" };

// Band-scoped parameter id, e.g. "thr" + band 1 -> "thr2".
juce::String bid (const char* base, int band) { return juce::String (base) + juce::String (band + 1); }
} // namespace

SattariMultibandAudioProcessor::SattariMultibandAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (Hz, dB, ratio). Only listed params are set;
// anything omitted keeps its default, so presets stay valid as params evolve.
void SattariMultibandAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal Master",    PV{ {"xLow", 250}, {"xHigh", 3000}, {"thr1", -18}, {"ratio1", 2},    {"thr2", -20}, {"ratio2", 2.5f}, {"thr3", -22}, {"ratio3", 2},    {"mk3", 1}, {"mix", 100} });
    presetBank.addFactoryPreset ("Full Master Glue",PV{ {"xLow", 200}, {"xHigh", 4000}, {"thr1", -16}, {"ratio1", 2},    {"thr2", -18}, {"ratio2", 2},    {"thr3", -20}, {"ratio3", 2},    {"output", 1} });
    presetBank.addFactoryPreset ("Tame Lows",       PV{ {"xLow", 150}, {"thr1", -24}, {"ratio1", 4},  {"atk1", 20}, {"rel1", 200}, {"thr2", 0}, {"ratio2", 1}, {"thr3", 0}, {"ratio3", 1} });
    presetBank.addFactoryPreset ("De-Harsh Highs",  PV{ {"xHigh", 5000}, {"thr1", 0}, {"ratio1", 1}, {"thr2", 0}, {"ratio2", 1}, {"thr3", -28}, {"ratio3", 4}, {"atk3", 2}, {"rel3", 80} });
    presetBank.addFactoryPreset ("Punch Bus",       PV{ {"xLow", 200}, {"xHigh", 3500}, {"thr1", -20}, {"ratio1", 3}, {"atk1", 8}, {"thr2", -18}, {"ratio2", 2.5f}, {"atk2", 5}, {"thr3", -20}, {"ratio3", 2}, {"mk1", 1} });
    presetBank.addFactoryPreset ("Bass Control",    PV{ {"xLow", 120}, {"thr1", -28}, {"ratio1", 5}, {"atk1", 10}, {"rel1", 150}, {"mk1", 2} });
    presetBank.addFactoryPreset ("Aggressive",      PV{ {"xLow", 250}, {"xHigh", 3000}, {"thr1", -26}, {"ratio1", 6}, {"thr2", -26}, {"ratio2", 6}, {"thr3", -26}, {"ratio3", 6}, {"mix", 100}, {"output", 2} });
    presetBank.addFactoryPreset ("Parallel Multi",  PV{ {"thr1", -30}, {"ratio1", 4}, {"thr2", -30}, {"ratio2", 4}, {"thr3", -30}, {"ratio3", 4}, {"mix", 45} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariMultibandAudioProcessor::createParameterLayout()
{
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::MultibandModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariMultibandAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariMultibandAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariMultibandAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
}

void SattariMultibandAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariMultibandAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMultibandAudioProcessor::createEditor()
{
    return new SattariMultibandAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariMultibandAudioProcessor();
}
