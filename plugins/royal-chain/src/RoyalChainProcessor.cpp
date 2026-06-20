#include "RoyalChainProcessor.h"
#include "RoyalChainEditor.h"

RoyalChainAudioProcessor::RoyalChainAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout RoyalChainAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto macroRange = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("style", "Vocal Style",
        juce::StringArray { "Modern Rap", "R&B", "Pop", "Persian/World Pop", "Indie", "Podcast/Spoken", "Background", "Hook", "Demo Rescue", "Live" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("input", "Input", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("output", "Output", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("clean", "Clean", macroRange, 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tune", "Tune", macroRange, 0.00f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("control", "Control", macroRange, 0.58f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tone", "Tone", macroRange, 0.52f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("air", "Air", macroRange, 0.42f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("smooth", "Smooth", macroRange, 0.44f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("glow", "Glow", macroRange, 0.32f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("width", "Width", macroRange, 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("space", "Space", macroRange, 0.22f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("royal", "Royal", macroRange, 0.50f));

    return { params.begin(), params.end() };
}

void RoyalChainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    cleanModule.prepare (sampleRate);
    tuneModule.prepare (sampleRate, samplesPerBlock);
    controlModule.prepare (sampleRate);
    toneModule.prepare (sampleRate);
    deEsserModule.prepare (sampleRate);
    colorModule.prepare (sampleRate);
    widthModule.prepare (sampleRate, samplesPerBlock);
    spaceModule.prepare (sampleRate, samplesPerBlock);
    finishModule.prepare (sampleRate);
}

bool RoyalChainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

float RoyalChainAudioProcessor::measurePeak (const juce::AudioBuffer<float>& buffer) const
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
    }
    return juce::jlimit (0.0f, 1.0f, peak);
}

void RoyalChainAudioProcessor::updateMeter (std::atomic<float>& meter, float target)
{
    const float current = meter.load();
    const float coeff = target > current ? 0.35f : 0.08f;
    meter.store (current + coeff * (target - current));
}

void RoyalChainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    const int totalInputs = getTotalNumInputChannels();
    const int totalOutputs = getTotalNumOutputChannels();
    for (int ch = totalInputs; ch < totalOutputs; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateMeter (inputMeter, measurePeak (buffer));

    const auto macros = MacroController::readMacros (apvts);
    const float inputDb = apvts.getRawParameterValue ("input")->load();
    const float outputDb = apvts.getRawParameterValue ("output")->load();

    cleanModule.process (buffer, inputDb, macros.clean, macros.royal);
    tuneModule.process (buffer, macros.tune);
    controlModule.process (buffer, macros.control, macros.royal);
    toneModule.process (buffer, macros.tone, macros.air, macros.royal);
    deEsserModule.process (buffer, macros.smooth, macros.royal);
    colorModule.process (buffer, macros.glow, macros.royal);
    widthModule.process (buffer, macros.width, macros.royal);
    spaceModule.process (buffer, macros.space, macros.royal);
    finishModule.process (buffer, outputDb, macros.royal);

    updateMeter (outputMeter, measurePeak (buffer));
    tuneConfidence.store (tuneModule.getConfidence());
    controlMeter.store (juce::jlimit (0.0f, 1.0f, controlModule.getGainReductionDb() / 12.0f));
    sibilanceMeter.store (deEsserModule.getSibilance());
    widthMeter.store (widthModule.getWidthMeter());
    limiterMeter.store (finishModule.getLimiterMeter());
}

juce::AudioProcessorEditor* RoyalChainAudioProcessor::createEditor()
{
    return new RoyalChainAudioProcessorEditor (*this);
}

void RoyalChainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RoyalChainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoyalChainAudioProcessor();
}
