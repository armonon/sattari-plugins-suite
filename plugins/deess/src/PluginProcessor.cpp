#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariDeEssAudioProcessor::SattariDeEssAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (Hz, dB, choice index). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariDeEssAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Vocal — Gentle",   PV{ {"freq", 6500}, {"threshold", -26}, {"range",  6}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Vocal — Firm",     PV{ {"freq", 7000}, {"threshold", -34}, {"range", 10}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Bright / Airy",    PV{ {"freq", 8500}, {"threshold", -30}, {"range",  7}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Rap Vocal",        PV{ {"freq", 7500}, {"threshold", -36}, {"range", 12}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Aggressive",       PV{ {"freq", 6000}, {"threshold", -40}, {"range", 16}, {"mode", 0}, {"output", 0} });
    presetBank.addFactoryPreset ("Wideband Ess",     PV{ {"freq", 6500}, {"threshold", -30}, {"range",  8}, {"mode", 1}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariDeEssAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("freq", "Frequency",
        juce::NormalisableRange<float> (2500.0f, 11000.0f, 1.0f, 0.5f), 6500.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -28.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("range", "Range",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 8.0f));      // max reduction dB
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("mode", "Mode",
        juce::StringArray { "Split", "Wideband" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterBool> ("listen", "Listen", false));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("output", "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariDeEssAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& b : shelf) b.reset();
    for (auto& b : hp) b.reset();
    env.prepare (sr, 0.8f, 45.0f);   // fast attack catches the "ess", medium release
    env.reset (0.0f);
    reductionDb.store (0.0f);
}

bool SattariDeEssAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariDeEssAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const double freq   = (double) param ("freq");
    const float rangeDb = param ("range");
    for (int ch = 0; ch < 2; ++ch)
    {
        hp[(size_t) ch].setHP (freq, sampleRate);
        shelf[(size_t) ch].setHighShelf (freq, -rangeDb, sampleRate);   // full-cut shape
    }

    const float threshLin = juce::Decibels::decibelsToGain (param ("threshold"));
    const bool  wideband  = param ("mode") > 0.5f;
    const bool  listen    = param ("listen") > 0.5f;
    const float outGain   = juce::Decibels::decibelsToGain (param ("output"));

    float maxRed = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Detect on the high (sibilance) band; stereo-linked (max) so the image
        // stays put. hpBand is also what Listen solos.
        float hpBand[2] = { 0.0f, 0.0f };
        float peak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            hpBand[ch] = hp[(size_t) ch].process (buffer.getReadPointer (ch)[i]);
            peak = std::max (peak, std::fabs (hpBand[ch]));
        }
        const float e = env.processRectified (peak);

        // Activity: 0 below threshold, ramping to 1 by the time we've hit Range.
        float redDb = 0.0f;
        if (e > threshLin && threshLin > 0.0f && rangeDb > 0.0f)
        {
            const float overDb = juce::Decibels::gainToDecibels (e / threshLin);
            redDb = juce::jmin (rangeDb, overDb);
            maxRed = std::max (maxRed, redDb);
        }
        const float k  = rangeDb > 0.0f ? redDb / rangeDb : 0.0f;   // split crossfade
        const float gr = juce::Decibels::decibelsToGain (-redDb);   // wideband gain

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float in = buffer.getReadPointer (ch)[i];
            const float sh = shelf[(size_t) ch].process (in);   // run continuously
            float y;
            if (listen)         y = hpBand[ch];              // solo the detector band
            else if (wideband)  y = in * gr;                 // duck everything
            else                y = in + k * (sh - in);      // crossfade dry -> shelved
            buffer.getWritePointer (ch)[i] = y * outGain;
        }
    }

    reductionDb.store (maxRed, std::memory_order_relaxed);
}

void SattariDeEssAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariDeEssAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariDeEssAudioProcessor::createEditor()
{
    return new SattariDeEssAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariDeEssAudioProcessor();
}
