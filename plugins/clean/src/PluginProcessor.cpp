#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariCleanAudioProcessor::SattariCleanAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariCleanAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<FP> ("amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("sensitivity", "Sensitivity",
        juce::NormalisableRange<float> (1.5f, 12.0f, 0.1f), 4.0f));   // threshold = floor * this
    p.push_back (std::make_unique<FP> ("output", "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariCleanAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    lr1.prepare (spec); lr1.setType (juce::dsp::LinkwitzRileyFilterType::allpass); lr1.setCutoffFrequency (450.0f);
    lr2.prepare (spec); lr2.setType (juce::dsp::LinkwitzRileyFilterType::allpass); lr2.setCutoffFrequency (3000.0f);
    lr1.reset(); lr2.reset();
    for (auto& e : env) { e.prepare (sr, 10.0f, 150.0f); e.reset (0.0f); }   // steady band level
    floor = { 1.0f, 1.0f, 1.0f };
    gSmooth = { 1.0f, 1.0f, 1.0f };
    reductionDb.store (0.0f);
}

bool SattariCleanAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariCleanAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const float amount  = param ("amount");
    const float threshM = param ("sensitivity");
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));

    float maxRed = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float band[kBands][2] = { { 0,0 }, { 0,0 }, { 0,0 } };
        for (int ch = 0; ch < numCh; ++ch)
        {
            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            lr1.processSample (ch, buffer.getReadPointer (ch)[i], low, rest);
            lr2.processSample (ch, rest, mid, high);
            band[0][ch] = low; band[1][ch] = mid; band[2][ch] = high;
        }

        float g[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            const float peak = std::max (std::fabs (band[b][0]), std::fabs (band[b][1]));
            const float e = env[(size_t) b].processRectified (peak);

            // Adaptive noise floor: settle DOWN toward the (already smoothed) band
            // level over ~40 ms so it sits on the steady noise, not the momentary
            // troughs; creep UP slowly so signal peaks don't drag it along.
            if (e < floor[(size_t) b]) floor[(size_t) b] += 0.0006f * (e - floor[(size_t) b]);
            else floor[(size_t) b] *= 1.00001f;
            const float fl = std::max (floor[(size_t) b], 1.0e-6f);

            const float thresh = fl * threshM;
            float target = 1.0f;
            if (e < thresh) target = (1.0f - amount) + amount * (e / thresh);   // downward expand
            gSmooth[(size_t) b] += 0.02f * (target - gSmooth[(size_t) b]);       // de-zipper
            g[b] = gSmooth[(size_t) b];
            maxRed = std::max (maxRed, 1.0f - g[b]);
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            float y = 0.0f;
            for (int b = 0; b < kBands; ++b) y += band[b][ch] * g[b];
            buffer.getWritePointer (ch)[i] = y * outGain;
        }
    }

    reductionDb.store (juce::Decibels::gainToDecibels (1.0f - juce::jlimit (0.0f, 0.999f, maxRed)),
                       std::memory_order_relaxed);
}

void SattariCleanAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariCleanAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariCleanAudioProcessor::createEditor()
{
    return new SattariCleanAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariCleanAudioProcessor();
}
