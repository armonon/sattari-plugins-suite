#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
const juce::StringArray modelChoices { "Tube", "Tape", "Transformer", "Transistor" };

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

// Transparent soft ceiling. Below the knee the signal is returned bit-exact;
// above it the overshoot is tanh-compressed into the remaining headroom so the
// magnitude asymptotes to 1.0 and the output is hard-bounded to [-1, 1].
constexpr float kKnee = 0.90f;
inline float softCeiling (float x) noexcept
{
    const float ax = std::abs (x);
    if (ax <= kKnee)
        return x;
    const float over = ax - kKnee;             // amount above the knee
    const float room = 1.0f - kKnee;           // remaining headroom to +-1
    const float shaped = kKnee + room * std::tanh (over / room);
    return std::copysign (shaped, x);
}
}

// ---- Biquad coefficient setters (RBJ cookbook, Q = 0.707) -------------------


// ---- Per-model waveshapers --------------------------------------------------
// Each returns a value in a bounded range near [-1,1]. `bias` (-1..1) skews the
// curve so positive and negative excursions clip differently, which is what
// creates even-order harmonics (DC-blocked after the shaper). All are smooth
// (soft) curves so ADAA/oversampling behaves and nothing ever explodes.

// ---- Processor --------------------------------------------------------------
SattariHeatAudioProcessor::SattariHeatAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetBank (apvts, "heat")
{
    registerFactoryPresets();
}

void SattariHeatAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // model: 0 Tube, 1 Tape, 2 Transformer, 3 Transistor. Only listed set.
    presetBank.addFactoryPreset ("Clean / Off",
        PV { {"model", 0.0f}, {"drive", 0.0f}, {"output", 0.0f}, {"tone", 0.0f},
             {"mix", 1.0f}, {"bias", 0.0f} });
    presetBank.addFactoryPreset ("Tube Warmth",
        PV { {"model", 0.0f}, {"drive", 8.0f}, {"output", -1.5f}, {"tone", 0.15f},
             {"mix", 1.0f}, {"bias", 0.25f} });
    presetBank.addFactoryPreset ("Tape Glue",
        PV { {"model", 1.0f}, {"drive", 10.0f}, {"output", -2.0f}, {"tone", -0.2f},
             {"mix", 1.0f}, {"bias", 0.1f} });
    presetBank.addFactoryPreset ("Iron Transformer",
        PV { {"model", 2.0f}, {"drive", 14.0f}, {"output", -3.0f}, {"tone", 0.0f},
             {"mix", 1.0f}, {"bias", 0.35f} });
    presetBank.addFactoryPreset ("Parallel Grit",
        PV { {"model", 3.0f}, {"drive", 22.0f}, {"output", -1.0f}, {"tone", 0.3f},
             {"mix", 0.4f}, {"bias", 0.0f} });
    presetBank.addFactoryPreset ("Bright Bite",
        PV { {"model", 3.0f}, {"drive", 16.0f}, {"output", -2.5f}, {"tone", 0.5f},
             {"mix", 0.7f}, {"bias", -0.2f} });
}

int SattariHeatAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariHeatAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariHeatAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets())
        return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariHeatAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariHeatAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::HeatModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

float SattariHeatAudioProcessor::getParam (const char* id) const noexcept
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void SattariHeatAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (newSampleRate, juce::jmax (1, samplesPerBlock), getTotalNumOutputChannels());
    setLatencySamples (module.latencySamples());
}

bool SattariHeatAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SattariHeatAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // MIDI Learn stays in the shell: it binds host CCs to parameters, which is a
    // plugin concern, not DSP.
    midiLearn.processMidi (midi);
    module.process (buffer, midi);
}

juce::AudioProcessorEditor* SattariHeatAudioProcessor::createEditor()
{
    return new SattariHeatAudioProcessorEditor (*this);
}

void SattariHeatAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariHeatAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariHeatAudioProcessor();
}
