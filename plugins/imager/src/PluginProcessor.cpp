#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>


// -- 2nd-order Butterworth biquads (RBJ, Q = 1/sqrt2). Two cascaded = LR4. -----


// -- Processor ----------------------------------------------------------------
SattariImageAudioProcessor::SattariImageAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      presetBank (apvts, "imager")
{
    registerFactoryPresets();
}

void SattariImageAudioProcessor::registerFactoryPresets()
{
    presetBank.addFactoryPreset ("Init / Neutral",
        { {"width", 1.0f}, {"widthLow", 1.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.0f},
          {"lowMono", 0.0f}, {"lowMonoFreq", 120.0f}, {"haas", 0.0f}, {"monoAudition", 0.0f} });
    presetBank.addFactoryPreset ("Wide & Safe",
        { {"width", 1.15f}, {"widthLow", 0.5f}, {"widthMid", 1.2f}, {"widthHigh", 1.6f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 120.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Bass Mono",
        { {"width", 1.0f}, {"widthLow", 0.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.1f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 160.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Mono Collapse",
        { {"width", 0.0f}, {"widthLow", 1.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.0f},
          {"lowMono", 0.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Air Spread",
        { {"width", 1.1f}, {"widthLow", 0.6f}, {"widthMid", 1.1f}, {"widthHigh", 1.8f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 110.0f}, {"haas", 0.35f} });
}

int SattariImageAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariImageAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariImageAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets())
        return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariImageAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariImageAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::ImageModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariImageAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sr, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
}

bool SattariImageAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariImageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // MIDI Learn stays in the shell: binding host CCs to parameters is a plugin
    // concern, not DSP.
    midiLearn.processMidi (midi);
    module.process (buffer, midi);
}

void SattariImageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariImageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* SattariImageAudioProcessor::createEditor()
{
    return new SattariImageAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariImageAudioProcessor();
}
