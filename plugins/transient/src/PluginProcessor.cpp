#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariTransientAudioProcessor::SattariTransientAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariTransientAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<P> ("attack", "Attack",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<P> ("sustain", "Sustain",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<P> ("output", "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariTransientAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    // Attack pair: same (medium) release, different attack speeds.
    atkFast.prepare (sr, 0.2f, 120.0f);
    atkSlow.prepare (sr, 18.0f, 120.0f);
    // Sustain pair: same (medium) attack, different release speeds.
    susFast.prepare (sr, 8.0f, 40.0f);
    susSlow.prepare (sr, 8.0f, 320.0f);
    for (auto* e : { &atkFast, &atkSlow, &susFast, &susSlow }) e->reset (0.0f);
    gainDb.store (0.0f);
}

bool SattariTransientAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariTransientAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const float attack  = param ("attack");
    const float sustain = param ("sustain");
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));

    float lastGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        float xm = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) xm = std::max (xm, std::fabs (buffer.getReadPointer (ch)[i]));

        const float aF = atkFast.processRectified (xm);
        const float aS = atkSlow.processRectified (xm);
        const float sF = susFast.processRectified (xm);
        const float sS = susSlow.processRectified (xm);

        const float ref = aS + 1.0e-4f;              // stable level estimate
        const float attackEnv  = juce::jlimit (0.0f, 1.0f, (aF - aS) / ref);   // onset spike
        const float sustainEnv = juce::jlimit (0.0f, 1.0f, (sS - sF) / ref);   // tail

        // Boost/cut up to ~+9/-inf dB via a linear factor per envelope.
        float g = 1.0f + 2.0f * attack * attackEnv + 1.5f * sustain * sustainEnv;
        g = juce::jlimit (0.05f, 4.0f, g);
        lastGain = g;

        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] = buffer.getReadPointer (ch)[i] * g * outGain;
    }

    gainDb.store (juce::Decibels::gainToDecibels (lastGain), std::memory_order_relaxed);
}

void SattariTransientAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariTransientAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariTransientAudioProcessor::createEditor()
{
    return new SattariTransientAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariTransientAudioProcessor();
}
