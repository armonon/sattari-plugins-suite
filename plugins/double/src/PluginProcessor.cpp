#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    // Fractional read from a ring buffer, `pos` in samples (may be negative).
    inline float readDelay (const std::vector<float>& ring, int len, double pos) noexcept
    {
        while (pos < 0.0)    pos += len;
        while (pos >= len)   pos -= len;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % len;
        const float f = (float) (pos - i0);
        return ring[(size_t) i0] * (1.0f - f) + ring[(size_t) i1] * f;
    }
}

SattariDoubleAudioProcessor::SattariDoubleAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    voices[0].rng.setSeed (0x1234abcd);
    voices[1].rng.setSeed (0x7f3e2a11);
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariDoubleAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<P> ("detune", "Detune",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 12.0f));   // cents
    p.push_back (std::make_unique<P> ("timing", "Timing",
        juce::NormalisableRange<float> (3.0f, 45.0f, 0.1f), 18.0f));   // ms
    p.push_back (std::make_unique<P> ("drift", "Drift",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    p.push_back (std::make_unique<P> ("width", "Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.85f));
    p.push_back (std::make_unique<P> ("level", "Doubles",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));
    p.push_back (std::make_unique<P> ("output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));  // dB
    return { p.begin(), p.end() };
}

void SattariDoubleAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    detector.prepare (sr, 60.0f, 1000.0f, 0.045f);
    const int nb = juce::jmax (1, samplesPerBlock);
    monoScratch.assign  ((size_t) nb, 0.0f);
    outLScratch.assign  ((size_t) nb, 0.0f);
    outRScratch.assign  ((size_t) nb, 0.0f);
    shiftScratch.assign ((size_t) nb, 0.0f);

    delayLen = (int) std::ceil (sr * 0.09) + 64;   // covers max timing + drift + slack
    for (auto& v : voices)
    {
        v.psola.prepare (sr, 60.0, 1000.0);
        v.delay.assign ((size_t) delayLen, 0.0f);
        v.writePos = 0;
        v.driftPos = v.driftTarget = 0.0;
        v.driftCountdown = 0;
    }
}

bool SattariDoubleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariDoubleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    if (n <= 0) return;
    if ((int) monoScratch.size() < n)
    {
        monoScratch.assign ((size_t) n, 0.0f); outLScratch.assign ((size_t) n, 0.0f);
        outRScratch.assign ((size_t) n, 0.0f); shiftScratch.assign ((size_t) n, 0.0f);
    }

    // Mono sum for pitch detection (drives PSOLA's grain spacing).
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) s += buffer.getReadPointer (ch)[i];
        monoScratch[(size_t) i] = numCh > 0 ? s / (float) numCh : 0.0f;
    }
    detector.pushBlock (monoScratch.data(), n);
    const auto det = detector.analyse (0.25f);
    const double period = (det.voiced && det.frequencyHz > 0.0f)
                        ? juce::jlimit (40.0, 800.0, sampleRate / (double) det.frequencyHz)
                        : sampleRate / 150.0;   // nominal fallback keeps grains sane

    const float detuneC  = param ("detune");
    const float timingMs = param ("timing");
    const float drift    = param ("drift");
    const float width    = param ("width");
    const float level    = param ("level");
    const float outGain  = juce::Decibels::decibelsToGain (param ("output"));

    const double baseDelay = timingMs * 0.001 * sampleRate;
    const float  maxDrift  = drift * (float) (0.006 * sampleRate);   // up to ~6 ms wobble

    const int Lc = 0, Rc = juce::jmin (1, numCh - 1);

    // Equal-power pan: voice 0 to the left, voice 1 to the right, spread by width.
    auto panGain = [] (float pan, float& gl, float& gr)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
        gl = std::cos (t * juce::MathConstants<float>::halfPi);
        gr = std::sin (t * juce::MathConstants<float>::halfPi);
    };
    float g0l, g0r, g1l, g1r;
    panGain (-width, g0l, g0r);
    panGain (+width, g1l, g1r);

    // Start from the dry signal (doubles are added around it).
    for (int i = 0; i < n; ++i)
    {
        outLScratch[(size_t) i] = buffer.getReadPointer (Lc)[i];
        outRScratch[(size_t) i] = buffer.getReadPointer (Rc)[i];
    }

    for (int v = 0; v < 2; ++v)
    {
        auto& vc = voices[(size_t) v];
        const double ratio = std::pow (2.0, (v == 0 ? +detuneC : -detuneC) / 1200.0);
        const double delaySamples = baseDelay * (v == 0 ? 1.0 : 1.28);
        const float  gl = (v == 0 ? g0l : g1l);
        const float  gr = (v == 0 ? g0r : g1r);

        // Formant-preserving detune of the mono take.
        vc.psola.process (monoScratch.data(), shiftScratch.data(), n, ratio, period);

        for (int i = 0; i < n; ++i)
        {
            // Slow random-walk drift so the two takes breathe independently.
            if (--vc.driftCountdown <= 0)
            {
                vc.driftTarget    = (vc.rng.nextDouble() * 2.0 - 1.0) * maxDrift;
                vc.driftCountdown = (int) (sampleRate * (0.18 + 0.12 * vc.rng.nextDouble()));
            }
            vc.driftPos += 0.0006 * (vc.driftTarget - vc.driftPos);

            vc.delay[(size_t) vc.writePos] = shiftScratch[(size_t) i];
            const double readPos = (double) vc.writePos - (delaySamples + vc.driftPos);
            const float d = readDelay (vc.delay, delayLen, readPos);
            outLScratch[(size_t) i] += level * gl * d;
            outRScratch[(size_t) i] += level * gr * d;
            if (++vc.writePos >= delayLen) vc.writePos = 0;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        buffer.getWritePointer (Lc)[i] = outLScratch[(size_t) i] * outGain;
        if (numCh > 1) buffer.getWritePointer (1)[i] = outRScratch[(size_t) i] * outGain;
    }
}

void SattariDoubleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariDoubleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariDoubleAudioProcessor::createEditor()
{
    return new SattariDoubleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariDoubleAudioProcessor();
}
