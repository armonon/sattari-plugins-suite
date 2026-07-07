#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

SattariMeterAudioProcessor::SattariMeterAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariMeterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    // Target loudness reference line for the display only (does not alter audio).
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("target", "Target LUFS",
        juce::NormalisableRange<float> (-36.0f, -6.0f, 0.1f), -14.0f));
    return { p.begin(), p.end() };
}

void SattariMeterAudioProcessor::designKWeighting (double sr)
{
    const double pi = juce::MathConstants<double>::pi;
    // Stage 1: high shelf, fc 1681.97 Hz, +3.9997 dB, S=1.
    {
        const double A  = std::pow (10.0, 3.9997 / 40.0);
        const double w0 = 2.0 * pi * 1681.974450 / sr;
        const double c = std::cos (w0), s = std::sin (w0);
        const double alpha = s * 0.5 * std::sqrt (2.0);
        const double tsa = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * c + tsa;
        for (auto& b : shelf)
        {
            b.b0 =  A * ((A + 1.0) + (A - 1.0) * c + tsa) / a0;
            b.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
            b.b2 =  A * ((A + 1.0) + (A - 1.0) * c - tsa) / a0;
            b.a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
            b.a2 =  ((A + 1.0) - (A - 1.0) * c - tsa) / a0;
            b.reset();
        }
    }
    // Stage 2: high pass, fc 38.135 Hz, Q 0.5.
    {
        const double w0 = 2.0 * pi * 38.135470 / sr;
        const double c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / (2.0 * 0.5);
        const double a0 = 1.0 + alpha;
        for (auto& b : hp)
        {
            b.b0 = ((1.0 + c) * 0.5) / a0;
            b.b1 = (-(1.0 + c)) / a0;
            b.b2 = ((1.0 + c) * 0.5) / a0;
            b.a1 = (-2.0 * c) / a0;
            b.a2 = (1.0 - alpha) / a0;
            b.reset();
        }
    }
}

void SattariMeterAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    designKWeighting (sr);
    shortLen = (int) std::round (sr * 3.0);
    momLen   = (int) std::round (sr * 0.4);
    zCap = juce::jmax (1, shortLen);
    zRing.assign ((size_t) zCap, 0.0f);
    zWrite = 0; momSum = shortSum = 0.0;
    intSum = 0.0; intCount = 0;
    tpHist = { 0.0f, 0.0f }; tpMax = 0.0f;
    corrLL = corrRR = corrLR = 0.0; corrCount = 0;
}

bool SattariMeterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    if (intResetFlag.exchange (false)) { intSum = 0.0; intCount = 0; }

    long long total = 0; // running processed count is implicit via ring fill; use intCount-independent

    for (int i = 0; i < n; ++i)
    {
        double zsum = 0.0;
        float lch = 0.0f, rch = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = buffer.getReadPointer (ch)[i];
            const double kw = hp[(size_t) ch].process (shelf[(size_t) ch].process ((double) x));
            zsum += kw * kw;

            // True peak: 4x linear-interpolated max between the last sample and now.
            const float prev = tpHist[(size_t) ch];
            for (int k = 1; k <= 4; ++k)
            {
                const float v = prev + (x - prev) * (float) k / 4.0f;
                tpMax = std::max (tpMax, std::fabs (v));
            }
            tpHist[(size_t) ch] = x;
            if (ch == 0) lch = x; else rch = x;
        }
        if (numCh == 1) rch = lch;

        // Sliding sums for momentary (0.4 s) and short-term (3 s).
        const float leavingShort = zRing[(size_t) zWrite];
        const int momIdx = (zWrite - momLen + zCap) % zCap;
        const float leavingMom = zRing[(size_t) momIdx];
        shortSum += zsum - leavingShort;
        momSum   += zsum - leavingMom;
        zRing[(size_t) zWrite] = (float) zsum;
        if (++zWrite >= zCap) zWrite = 0;

        // Integrated: absolute-gated running mean of z^2 (gate at -70 LUFS).
        if (zsum > 0.0)
        {
            const double instLoud = -0.691 + 10.0 * std::log10 (zsum);
            if (instLoud > -70.0) { intSum += zsum; ++intCount; }
        }

        corrLL += (double) lch * lch; corrRR += (double) rch * rch; corrLR += (double) lch * rch;
        if (++corrCount >= (int) (sampleRate * 0.3))
        {
            const double den = std::sqrt (corrLL * corrRR);
            corr.store (den > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR / den) : 0.0f,
                        std::memory_order_relaxed);
            corrLL = corrRR = corrLR = 0.0; corrCount = 0;
        }
        juce::ignoreUnused (total);
    }

    auto lufs = [] (double meanSq) { return meanSq > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSq)) : -100.0f; };
    momLufs.store    (lufs (momSum   / juce::jmax (1, momLen)),   std::memory_order_relaxed);
    shortLufs_.store (lufs (shortSum / juce::jmax (1, shortLen)), std::memory_order_relaxed);
    intLufs.store    (intCount > 0 ? lufs (intSum / (double) intCount) : -100.0f, std::memory_order_relaxed);
    tpDb.store       (tpMax > 0.0f ? juce::Decibels::gainToDecibels (tpMax) : -100.0f, std::memory_order_relaxed);
    tpMax *= 0.90f;   // slow decay so the reading falls back
}

void SattariMeterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariMeterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMeterAudioProcessor::createEditor()
{
    return new SattariMeterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariMeterAudioProcessor();
}
