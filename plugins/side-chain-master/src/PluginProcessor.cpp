#include "PluginProcessor.h"
#include "PluginEditor.h"

SideChainMasterAudioProcessor::SideChainMasterAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

SideChainMasterAudioProcessor::~SideChainMasterAudioProcessor()
{
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout SideChainMasterAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::SideChainMasterModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SideChainMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumInputChannels());
    reportedLatency = module.latencySamples();
    pendingLatency.store (reportedLatency, std::memory_order_relaxed);
    setLatencySamples (reportedLatency);
}

bool SideChainMasterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);

    if (mainIn != mainOut)
        return false;

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void SideChainMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    // Resolve the external key bus and hand it down; the module decides whether the
    // `external` parameter actually lets it be used.
    const juce::AudioBuffer<float>* sidechain = nullptr;
    juce::AudioBuffer<float> scView;
    if (getBusCount (true) > 1)
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto busView = getBusBuffer (buffer, true, 1);
            scView.setDataToReferTo (busView.getArrayOfWritePointers(),
                                     busView.getNumChannels(),
                                     busView.getNumSamples());
            if (scView.getNumChannels() > 0 && scView.getNumSamples() > 0)
                sidechain = &scView;
        }

    // With a second input bus `buffer` holds BOTH buses back to back, so the module
    // must only ever see the main bus.
    auto mainBus = getBusBuffer (buffer, true, 0);

    module.setTransport (sattari::host::read (*this));
    module.setSidechain (sidechain);
    module.process (mainBus, midi);

    // Lookahead is a parameter, so the reported latency can change while running.
    // Republish only on change — setLatencySamples() is not realtime-cheap.
    const int lat = module.latencySamples();
    if (lat != pendingLatency.load (std::memory_order_relaxed))
    {
        pendingLatency.store (lat, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void SideChainMasterAudioProcessor::handleAsyncUpdate()
{
    const int lat = pendingLatency.load (std::memory_order_relaxed);
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }
}

juce::AudioProcessorEditor* SideChainMasterAudioProcessor::createEditor()
{
    return new SideChainMasterAudioProcessorEditor (*this);
}

void SideChainMasterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SideChainMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new SideChainMasterAudioProcessor();
}
