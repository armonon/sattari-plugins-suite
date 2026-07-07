#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::StringArray SattariSpaceAudioProcessor::divisionChoices()
{ return { "Off", "1/4", "1/4 dot", "1/8", "1/8 dot", "1/8 trip", "1/16" }; }

// Beats (quarter-note = 1.0) for each division.
static double beatsFor (int div)
{
    switch (div)
    {
        case 1: return 1.0;         // 1/4
        case 2: return 1.5;         // 1/4 dotted
        case 3: return 0.5;         // 1/8
        case 4: return 0.75;        // 1/8 dotted
        case 5: return 1.0 / 3.0;   // 1/8 triplet
        case 6: return 0.25;        // 1/16
        default: return 0.0;        // Off
    }
}

SattariSpaceAudioProcessor::SattariSpaceAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}
SattariSpaceAudioProcessor::~SattariSpaceAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariSpaceAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<FP> ("size", "Size", juce::NormalisableRange<float> (0.0f,1.0f,0.01f), 0.6f));
    p.push_back (std::make_unique<FP> ("damp", "Damping", juce::NormalisableRange<float> (0.0f,1.0f,0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("revmix", "Reverb Mix", juce::NormalisableRange<float> (0.0f,1.0f,0.01f), 0.3f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("div", "Delay Time", divisionChoices(), 3));
    p.push_back (std::make_unique<FP> ("feedback", "Feedback", juce::NormalisableRange<float> (0.0f,0.95f,0.01f), 0.35f));
    p.push_back (std::make_unique<FP> ("delaymix", "Delay Mix", juce::NormalisableRange<float> (0.0f,1.0f,0.01f), 0.25f));
    p.push_back (std::make_unique<FP> ("width", "Width", juce::NormalisableRange<float> (0.0f,1.0f,0.01f), 1.0f));
    p.push_back (std::make_unique<FP> ("output", "Output", juce::NormalisableRange<float> (-24.0f,12.0f,0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariSpaceAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    reverb.setSampleRate (sr);
    dryScratch.setSize (2, juce::jmax (1, samplesPerBlock));
    tempoReader.start();
    delayLen = (int) std::ceil (sr * 3.0) + 8;    // up to a slow whole-note delay
    for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);
    delayWrite = 0; fbLpState = { 0.0f, 0.0f };
}
void SattariSpaceAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariSpaceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double SattariSpaceAudioProcessor::resolveBpm()
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm(); bpm && *bpm > 1.0)
                return *bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // tempo bus fallback
        return s.bpm;
    return 120.0;
}

void SattariSpaceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const double bpm = resolveBpm();
    lastBpm.store (bpm, std::memory_order_relaxed);

    const int div = (int) std::round (param ("div"));
    const double beats = beatsFor (div);
    const bool delayOn = beats > 0.0;
    double delaySamples = beats * (60.0 / bpm) * sampleRate;
    delaySamples = juce::jlimit (1.0, (double) delayLen - 4.0, delaySamples);

    const float fb       = param ("feedback");
    const float delayMix = param ("delaymix");
    const float width    = param ("width");
    const float outGain  = juce::Decibels::decibelsToGain (param ("output"));

    // --- Tempo-synced stereo delay (into the reverb) -------------------------
    if (delayOn)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delayBuf[(size_t) ch];
                const double rp = (double) delayWrite - delaySamples;
                int r0 = (int) std::floor (rp); double fr = rp - r0;
                while (r0 < 0) r0 += delayLen;
                const int r1 = (r0 + 1) % delayLen;
                const float delayed = d[(size_t) r0] * (1.0f - (float) fr) + d[(size_t) r1] * (float) fr;

                const float in = buffer.getReadPointer (ch)[i];
                // Damped feedback (one-pole LP in the loop).
                fbLpState[(size_t) ch] += 0.35f * (delayed - fbLpState[(size_t) ch]);
                d[(size_t) delayWrite] = in + fbLpState[(size_t) ch] * fb;
                buffer.getWritePointer (ch)[i] = in + delayMix * delayed;
            }
            if (++delayWrite >= delayLen) delayWrite = 0;
        }
    }

    // --- Reverb (Freeverb) ---------------------------------------------------
    // juce::Reverb scales dry by 2x and wet by 3x internally, so its dry/wet is
    // not a clean crossfade. We take a PURE wet from it (dryLevel 0) and do our
    // own dry/wet mix against the saved dry(+delay) signal — revmix=0 is then an
    // exact pass-through.
    const float revmix = param ("revmix");
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

    juce::Reverb::Parameters rp;
    rp.roomSize = param ("size");
    rp.damping  = param ("damp");
    rp.wetLevel = 1.0f / 3.0f;   // wet is *3 internally -> ~unity
    rp.dryLevel = 0.0f;
    rp.width    = width;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    if (numCh >= 2)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    else
        reverb.processMono (buffer.getWritePointer (0), n);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            out[i] = (dry[i] * (1.0f - revmix) + out[i] * revmix) * outGain;
    }
}

void SattariSpaceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariSpaceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariSpaceAudioProcessor::createEditor()
{
    return new SattariSpaceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariSpaceAudioProcessor();
}
