#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariFaceAudioProcessor::SattariFaceAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariFaceAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<P> ("pitch", "Pitch",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));    // semitones
    p.push_back (std::make_unique<P> ("formant", "Formant",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));    // semitones (gender/size)
    p.push_back (std::make_unique<P> ("mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<P> ("output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));    // dB
    return { p.begin(), p.end() };
}

void SattariFaceAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    const int nb = juce::jmax (1, samplesPerBlock);
    for (auto& r : rb) r.prepare (sr, nb);
    wetScratch.assign ((size_t) nb, 0.0f);

    reportedLatency = rb[0].latencySamples();
    const int dl = reportedLatency + nb + 8;
    for (auto& d : dryDelay) d.assign ((size_t) dl, 0.0f);
    dryWrite = 0;
    setLatencySamples (reportedLatency);
}

bool SattariFaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariFaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (n <= 0) return;
    if ((int) wetScratch.size() < n) wetScratch.assign ((size_t) n, 0.0f);

    const double pitchScale   = std::pow (2.0, (double) param ("pitch")   / 12.0);
    const double formantScale = std::pow (2.0, (double) param ("formant") / 12.0);
    const float  mix     = param ("mix");
    const float  outGain = juce::Decibels::decibelsToGain (param ("output"));
    const int    dl      = (int) dryDelay[0].size();

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        rb[(size_t) ch].process (in, wetScratch.data(), n, pitchScale, formantScale);

        auto& dd = dryDelay[(size_t) ch];
        float* out = buffer.getWritePointer (ch);
        int w = dryWrite;
        for (int i = 0; i < n; ++i)
        {
            dd[(size_t) w] = in[i];
            int r = w - reportedLatency; while (r < 0) r += dl;
            const float dry = dd[(size_t) r];
            out[i] = (dry * (1.0f - mix) + wetScratch[(size_t) i] * mix) * outGain;
            if (++w >= dl) w = 0;
        }
        if (ch == numCh - 1) dryWrite = w;
    }
}

void SattariFaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariFaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariFaceAudioProcessor::createEditor()
{
    return new SattariFaceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariFaceAudioProcessor();
}
