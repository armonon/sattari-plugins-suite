#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
inline double clampF (double f, double sr) { return juce::jlimit (20.0, sr * 0.45, f); }

constexpr const char* kFreqIds[SattariEQAudioProcessor::kBands] = { "f1", "f2", "f3" };
constexpr const char* kGainIds[SattariEQAudioProcessor::kBands] = { "g1", "g2", "g3" };
constexpr const char* kQIds[SattariEQAudioProcessor::kBands]    = { "q1", "q2", "q3" };
constexpr const char* kDynIds[SattariEQAudioProcessor::kBands]  = { "d1", "d2", "d3" };
}

void SattariEQAudioProcessor::Biquad::setPeaking (double f, double gainDb, double Q, double sr)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0; b1 = (-2.0 * c) / a0; b2 = (1.0 - alpha * A) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha / A) / a0;
}
void SattariEQAudioProcessor::Biquad::setBandpass (double f, double Q, double sr)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0; a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}
void SattariEQAudioProcessor::Biquad::setHP (double f, double sr)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}
void SattariEQAudioProcessor::Biquad::setLP (double f, double sr)
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 - c) * 0.5) / a0; b1 = (1.0 - c) / a0; b2 = ((1.0 - c) * 0.5) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}

SattariEQAudioProcessor::SattariEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariEQAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto hz = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi, 1.0f, 0.35f); };

    p.push_back (std::make_unique<FP> ("hpf", "HPF", hz (20.0f, 500.0f), 20.0f));    // 20 = off
    p.push_back (std::make_unique<FP> ("lpf", "LPF", hz (2000.0f, 20000.0f), 20000.0f)); // 20k = off

    const float defF[kBands] = { 120.0f, 1000.0f, 6000.0f };
    for (int i = 0; i < kBands; ++i)
    {
        const juce::String b (i + 1);
        p.push_back (std::make_unique<FP> ("f" + b, "Band " + b + " Freq", hz (30.0f, 18000.0f), defF[i]));
        p.push_back (std::make_unique<FP> ("g" + b, "Band " + b + " Gain",
            juce::NormalisableRange<float> (-15.0f, 15.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<FP> ("q" + b, "Band " + b + " Q",
            juce::NormalisableRange<float> (0.3f, 8.0f, 0.01f, 0.5f), 0.9f));
        p.push_back (std::make_unique<FP> ("d" + b, "Band " + b + " Dyn",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    }
    p.push_back (std::make_unique<FP> ("output", "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariEQAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& band : bell) for (auto& b : band) b.reset();
    for (auto& b : detBP) b.reset();
    for (auto& e : detEnv) e.prepare (sr, 5.0f, 80.0f), e.reset (0.0f);
    for (auto& b : hp) b.reset();
    for (auto& b : lp) b.reset();
}

bool SattariEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const float hpf = param ("hpf"), lpf = param ("lpf");
    hpOn = hpf > 21.0f; lpOn = lpf < 19900.0f;
    if (hpOn) for (auto& b : hp) b.setHP (hpf, sampleRate);
    if (lpOn) for (auto& b : lp) b.setLP (lpf, sampleRate);

    float f[kBands], g[kBands], q[kBands], dyn[kBands];
    for (int i = 0; i < kBands; ++i)
    {
        f[i] = param (kFreqIds[(size_t) i]); g[i] = param (kGainIds[(size_t) i]);
        q[i] = param (kQIds[(size_t) i]); dyn[i] = param (kDynIds[(size_t) i]);
        for (auto& ch : bell[(size_t) i]) ch.setPeaking (f[i], g[i], q[i], sampleRate);
        detBP[(size_t) i].setBandpass (f[i], 2.0, sampleRate);
    }
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));

    for (int s = 0; s < n; ++s)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mono += buffer.getReadPointer (ch)[s];
        mono /= (float) juce::jmax (1, numCh);

        float k[kBands];
        for (int i = 0; i < kBands; ++i)
        {
            const float det = (float) detBP[(size_t) i].process (mono);
            const float e = detEnv[(size_t) i].processRectified (std::fabs (det));
            const float eDb = juce::Decibels::gainToDecibels (e + 1.0e-6f);
            const float activity = juce::jlimit (0.0f, 1.0f, (eDb + 45.0f) / 30.0f);   // -45..-15 dB
            k[i] = 1.0f - dyn[i] * (1.0f - activity);   // dyn 0 -> static (k=1)
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            double x = buffer.getReadPointer (ch)[s];
            if (hpOn) x = hp[(size_t) ch].process (x);
            if (lpOn) x = lp[(size_t) ch].process (x);
            for (int i = 0; i < kBands; ++i)
            {
                const double peaked = bell[(size_t) i][(size_t) ch].process (x);
                x = x + (double) k[i] * (peaked - x);
            }
            buffer.getWritePointer (ch)[s] = (float) x * outGain;
        }
    }
}

void SattariEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariEQAudioProcessor::createEditor()
{
    return new SattariEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariEQAudioProcessor();
}
