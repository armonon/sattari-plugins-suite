#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariAlignAudioProcessor::SattariAlignAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput  ("Guide",     juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Presets tune the detector/smoothing for different
// source material; Bypass is left untouched.
void SattariAlignAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // algorithm: 0=Bulk Shift (v1) 1=DTW Warp (v2 alpha)
    // searchRangeMs 10..300, sensitivity/smoothing 0..1, manualOffsetMs -150..150
    presetBank.addFactoryPreset ("Vocal Doubles",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 80.0f}, {"sensitivity", 0.6f}, {"smoothing", 0.75f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("Drum Stems",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 60.0f}, {"sensitivity", 0.7f}, {"smoothing", 0.5f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("Guitar Layers",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 100.0f}, {"sensitivity", 0.55f}, {"smoothing", 0.7f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("Wide Search",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 250.0f}, {"sensitivity", 0.5f}, {"smoothing", 0.7f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("Tight & Fast",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 40.0f}, {"sensitivity", 0.8f}, {"smoothing", 0.3f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("Gentle Follow",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 0}, {"searchRangeMs", 120.0f}, {"sensitivity", 0.45f}, {"smoothing", 0.9f}, {"manualOffsetMs", 0.0f} });
    presetBank.addFactoryPreset ("DTW Drift (v2)",
        PV{ {"autoAlign", 1}, {"lock", 0}, {"algorithm", 1}, {"searchRangeMs", 150.0f}, {"sensitivity", 0.55f}, {"smoothing", 0.7f}, {"manualOffsetMs", 0.0f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariAlignAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::AlignModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariAlignAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());

    // The base delay is fixed at prepare time and the moving alignment lag is
    // deliberately NOT reported — it is the correction, not a latency to undo.
    setLatencySamples (module.latencySamples());
}

bool SattariAlignAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SattariAlignAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    // Resolve the Guide bus once and hand it down.
    const juce::AudioBuffer<float>* guide = nullptr;
    juce::AudioBuffer<float> guideView;
    if (getBusCount (true) > 1)
        if (auto* sc = getBus (true, 1); sc != nullptr && sc->isEnabled())
        {
                // setDataToReferTo, NOT assignment: AudioBuffer::operator= calls
                // setSize(..., avoidReallocating=false), so it REALLOCATES AND COPIES
                // on every block — an allocation on the audio thread. This re-points
                // at the host's channels and owns nothing.
            {
                auto g = getBusBuffer (buffer, true, 1);
                guideView.setDataToReferTo (g.getArrayOfWritePointers(),
                                            g.getNumChannels(), g.getNumSamples());
            }
            if (guideView.getNumChannels() > 0 && guideView.getNumSamples() > 0)
                guide = &guideView;
        }

    // With a second input bus `buffer` holds BOTH buses back to back, so the module
    // must only ever see the main bus.
    auto mainBus = getBusBuffer (buffer, true, 0);

    module.setSidechain (guide);
    module.process (mainBus, midi);
}

juce::AudioProcessorEditor* SattariAlignAudioProcessor::createEditor()
{
    return new SattariAlignAudioProcessorEditor (*this);
}

void SattariAlignAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SattariAlignAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new SattariAlignAudioProcessor();
}
