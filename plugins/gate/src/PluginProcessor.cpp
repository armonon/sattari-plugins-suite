#include "PluginProcessor.h"
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

// Musical division -> beats (quarter-note = 1.0 beat) for the tempo-gate cycle.
double cycleBeatsFor (int div)
{
    switch (div)
    {
        case 0: return 4.0;         // 1/1
        case 1: return 2.0;         // 1/2
        case 2: return 1.0;         // 1/4
        case 3: return 0.5;         // 1/8
        case 4: return 1.0 / 3.0;   // 1/8T
        case 5: return 0.25;        // 1/16
        default: return 0.5;
    }
}
} // namespace

SattariGateAudioProcessor::SattariGateAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

SattariGateAudioProcessor::~SattariGateAudioProcessor() = default;   // module stops its own tempo reader

// Factory presets: real parameter values (dB, ms, choice index). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariGateAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal Gate",       PV{ {"threshold", -45}, {"range", 40}, {"attack", 1},   {"hold", 60}, {"release", 150}, {"hysteresis", 6}, {"scHpf", 80} });
    presetBank.addFactoryPreset ("Tight Vocal",      PV{ {"threshold", -38}, {"range", 60}, {"attack", 0.5f},{"hold", 30}, {"release", 80},  {"hysteresis", 8}, {"scHpf", 100} });
    presetBank.addFactoryPreset ("Drum Gate",        PV{ {"threshold", -30}, {"range", 70}, {"attack", 0.1f},{"hold", 20}, {"release", 60},  {"lookahead", 2},  {"hysteresis", 5} });
    presetBank.addFactoryPreset ("Tom Gate",         PV{ {"threshold", -35}, {"range", 50}, {"attack", 0.3f},{"hold", 80}, {"release", 200}, {"hysteresis", 6} });
    presetBank.addFactoryPreset ("Gentle Expander",  PV{ {"threshold", -50}, {"range", 18}, {"ratio", 3},    {"attack", 5},{"hold", 50}, {"release", 250}, {"expander", 1}, {"hysteresis", 3} });
    presetBank.addFactoryPreset ("Deep Expander",    PV{ {"threshold", -45}, {"range", 30}, {"ratio", 4},    {"attack", 2},{"hold", 40}, {"release", 300}, {"expander", 1} });
    presetBank.addFactoryPreset ("Trance Gate 1/8",  PV{ {"tempoGate", 1}, {"tempoDiv", 3}, {"tempoDuty", 50}, {"attack", 2}, {"release", 40}, {"range", 90} });
    presetBank.addFactoryPreset ("Trance Gate 1/16", PV{ {"tempoGate", 1}, {"tempoDiv", 5}, {"tempoDuty", 45}, {"attack", 1}, {"release", 20}, {"range", 90} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariGateAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::GateModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariGateAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
    setLatencySamples (module.latencySamples());
}

void SattariGateAudioProcessor::releaseResources() {}

bool SattariGateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

// One-pole HP followed by one-pole LP, forming the detector sidechain band-pass.


void SattariGateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);

    // Lookahead is a parameter, so the reported latency can change while running.
    // Republish only on change — setLatencySamples() is not realtime-cheap.
    const int lat = module.latencySamples();
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }
}

void SattariGateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariGateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariGateAudioProcessor::createEditor()
{
    return new SattariGateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariGateAudioProcessor();
}
