#include "PluginProcessor.h"
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include "PluginEditor.h"
#include <cmath>

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                       const juce::String& name,
                                                       float min, float max, float def,
                                                       float step = 0.001f,
                                                       const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, step);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

// Release-division choices for the tempo-release mode (quarter-note = 1.0 beat).
double releaseBeatsFor (int div)
{
    switch (div)
    {
        case 0: return 0.25;        // 1/16
        case 1: return 1.0 / 3.0;   // 1/8 triplet
        case 2: return 0.5;         // 1/8
        case 3: return 0.75;        // 1/8 dotted
        case 4: return 1.0;         // 1/4
        case 5: return 1.5;         // 1/4 dotted
        case 6: return 2.0;         // 1/2
        default: return 0.5;
    }
}
} // namespace

SattariCompAudioProcessor::SattariCompAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

SattariCompAudioProcessor::~SattariCompAudioProcessor() = default;   // module stops its own tempo reader

// Factory presets: real parameter values (dB, ms, ratio, choice index). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariCompAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal — Gentle",    PV{ {"threshold", -20}, {"ratio", 2.5f}, {"knee", 8},  {"attack", 12}, {"release", 180}, {"makeup", 2},  {"mix", 100} });
    presetBank.addFactoryPreset ("Vocal — Punchy",    PV{ {"threshold", -18}, {"ratio", 4},    {"knee", 4},  {"attack", 5},  {"release", 120}, {"makeup", 3},  {"mix", 100} });
    presetBank.addFactoryPreset ("Vocal — Smooth",    PV{ {"threshold", -22}, {"ratio", 3},    {"knee", 10}, {"attack", 20}, {"release", 250}, {"makeup", 3},  {"autoRelease", 1} });
    presetBank.addFactoryPreset ("Rap Vocal",         PV{ {"threshold", -16}, {"ratio", 5},    {"knee", 3},  {"attack", 3},  {"release", 90},  {"makeup", 4} });
    presetBank.addFactoryPreset ("Bus Glue",          PV{ {"threshold", -14}, {"ratio", 2},    {"knee", 12}, {"attack", 30}, {"release", 300}, {"makeup", 2},  {"autoGain", 1} });
    presetBank.addFactoryPreset ("Drum Smack",        PV{ {"threshold", -20}, {"ratio", 6},    {"knee", 2},  {"attack", 1},  {"release", 80},  {"makeup", 3} });
    presetBank.addFactoryPreset ("Parallel Squash",   PV{ {"threshold", -30}, {"ratio", 10},   {"knee", 6},  {"attack", 1},  {"release", 100}, {"makeup", 6},  {"mix", 40} });
    presetBank.addFactoryPreset ("Tempo Pump",        PV{ {"threshold", -18}, {"ratio", 4},    {"knee", 4},  {"attack", 8},  {"release", 140}, {"tempoRelease", 1}, {"releaseDiv", 4} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariCompAudioProcessor::createParameterLayout()
{
    // Parameters come from the module so the standalone and the Mix rack slot can
    // never drift. Empty prefix keeps the standalone's IDs exactly as they shipped.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::CompModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariCompAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

void SattariCompAudioProcessor::releaseResources() {}

bool SattariCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}




void SattariCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // The module has no playhead of its own, so the shell reads the transport and
    // pushes it down — exactly what the Mix rack does for its slots.
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariCompAudioProcessor::createEditor()
{
    return new SattariCompAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariCompAudioProcessor();
}
