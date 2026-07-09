#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::StringArray SattariPulseAudioProcessor::rateChoices()
{ return { "1/1", "1/2", "1/4", "1/4 dot", "1/8", "1/8 trip", "1/16" }; }

juce::StringArray SattariPulseAudioProcessor::shapeChoices()
{ return { "Sine", "Saw Down", "Saw Up", "Square", "Spike" }; }

namespace
{
// Quarter-note multiples for each rate division (quarter-note = 1.0).
double quartersFor (int rate)
{
    switch (rate)
    {
        case 0: return 4.0;         // 1/1  (whole)
        case 1: return 2.0;         // 1/2
        case 2: return 1.0;         // 1/4
        case 3: return 1.5;         // 1/4 dotted
        case 4: return 0.5;         // 1/8
        case 5: return 1.0 / 3.0;   // 1/8 triplet
        case 6: return 0.25;        // 1/16
        default: return 1.0;
    }
}
}

// Ducking curve: phase 0 = the beat (deepest duck / gate), returning to unity
// across the cycle. Returns a gain in [0,1] where 1 leaves the signal untouched.
float SattariPulseAudioProcessor::curveGain (int shape, float phase) noexcept
{
    phase = phase - std::floor (phase);   // wrap to [0,1)
    switch (shape)
    {
        case 0: // Sine: smooth dip at the beat, up to unity mid-cycle.
            //  0.5 - 0.5*cos(2*pi*phase): 0 at phase 0, 1 at phase 0.5, 0 at phase 1.
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);

        case 1: // Saw Down: instant duck on the beat, linear ramp back to unity.
            return phase;

        case 2: // Saw Up: unity into the beat, then snap down (mirror of Saw Down).
            return 1.0f - phase;

        case 3: // Square / gate: hard-ducked for the first half of the cycle, open after.
            return phase < 0.5f ? 0.0f : 1.0f;

        case 4: // Spike: narrow ducked notch right on the beat, open the rest.
        {
            // Short cosine notch over the first 25% of the cycle.
            constexpr float w = 0.25f;
            if (phase >= w) return 1.0f;
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (phase / w));
        }
        default: return 1.0f;
    }
}

SattariPulseAudioProcessor::SattariPulseAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}
SattariPulseAudioProcessor::~SattariPulseAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariPulseAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> ("shape", "Shape", shapeChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("rate",  "Rate",  rateChoices(),  2));
    p.push_back (std::make_unique<FP> ("depth",  "Depth",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f));
    p.push_back (std::make_unique<FP> ("smooth", "Smooth", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));
    p.push_back (std::make_unique<FP> ("filter", "Filter", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("mix",    "Mix",    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    return { p.begin(), p.end() };
}

void SattariPulseAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    tempoReader.start();
    phase = 0.0;
    smoothedGain = 1.0f;
    smoothCoeff = 0.0f;
    lpStateL = lpStateR = 0.0f;
    lastBpm.store (120.0, std::memory_order_relaxed);
    lastGain.store (1.0f, std::memory_order_relaxed);
}

void SattariPulseAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariPulseAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double SattariPulseAudioProcessor::resolveBpm()
{
    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm && t.bpm > 1.0)
        return t.bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // tempo-bus fallback
        return s.bpm;
    return 120.0;
}

void SattariPulseAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0)
        return;

    const double bpm = resolveBpm();
    lastBpm.store (bpm, std::memory_order_relaxed);

    const int   shape  = juce::jlimit (0, shapeChoices().size() - 1, (int) std::round (param ("shape")));
    const int   rate   = juce::jlimit (0, rateChoices().size() - 1, (int) std::round (param ("rate")));
    const float depth  = juce::jlimit (0.0f, 1.0f, param ("depth"));
    const float smooth = juce::jlimit (0.0f, 1.0f, param ("smooth"));
    const float filter = juce::jlimit (0.0f, 1.0f, param ("filter"));
    const float mix    = juce::jlimit (0.0f, 1.0f, param ("mix"));

    // Cycle length in quarter-notes -> phase increment per sample.
    const double cycleQuarters = quartersFor (rate);
    const double cyclesPerSample = (bpm / 60.0) / sampleRate / cycleQuarters;

    // Re-anchor phase to the host timeline while the transport plays, so the pump
    // stays locked to the grid across loops / relocation. Free-runs otherwise.
    const auto t = sattari::host::read (*this);
    if (t.isPlaying && t.hasPpq)
        phase = std::fmod (t.ppqPosition / cycleQuarters, 1.0);
    if (phase < 0.0) phase += 1.0;

    // One-pole gain smoother: map Smooth (0..1) to a time constant of ~0 .. 60 ms.
    // At Smooth=0 the coefficient is 0 -> gain follows the curve exactly (needed so
    // depth=0 is an exact pass-through and the modulation is fully audible).
    const double smoothMs = (double) smooth * 60.0;
    smoothCoeff = smoothMs > 0.0
        ? (float) std::exp (-1.0 / (smoothMs * 0.001 * sampleRate))
        : 0.0f;

    // Filter cutoff sweep: when filter>0, a one-pole LP whose cutoff drops toward
    // ~400 Hz on the beat (curveGain small) and opens toward ~18 kHz off the beat.
    // At filter=0 no filtering is applied at all (bit-exact bypass of the LP).
    const bool  filterOn = filter > 1.0e-4f;
    const float nyquist  = (float) (sampleRate * 0.5);

    float lastGainThisBlock = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        // Raw curve gain for this sample, then blend by depth so depth=0 -> unity.
        const float raw = curveGain (shape, (float) phase);
        const float targetGain = 1.0f - depth * (1.0f - raw);   // in [1-depth, 1]

        // Smooth the gain (one-pole toward the target).
        smoothedGain = smoothCoeff * smoothedGain + (1.0f - smoothCoeff) * targetGain;
        const float g = juce::jlimit (0.0f, 1.0f, smoothedGain);
        lastGainThisBlock = g;

        // Optional filter movement, driven by the SAME curve. cutoff follows g:
        // low (dark) on the beat, high (open) off it. filter scales how far it moves.
        float cutoffHz = nyquist;
        float lpCoeff  = 0.0f;
        if (filterOn)
        {
            const float openHz  = juce::jmin (18000.0f, nyquist * 0.95f);
            const float floorHz = 400.0f;
            // Move from open down toward floor as g -> 0, weighted by the filter amount.
            const float movedHz = openHz - filter * (openHz - floorHz) * (1.0f - g);
            cutoffHz = juce::jlimit (20.0f, nyquist * 0.99f, movedHz);
            const float x = std::exp (-juce::MathConstants<float>::twoPi * cutoffHz / (float) sampleRate);
            lpCoeff = juce::jlimit (0.0f, 0.9999f, x);
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float dry = d[i];

            float shaped = dry * g;   // ducked / gated signal (bounded to |dry|)

            if (filterOn)
            {
                float& st = (ch == 0) ? lpStateL : lpStateR;
                st = lpCoeff * st + (1.0f - lpCoeff) * shaped;
                shaped = st;

                // The one-pole LP has unity DC gain, but its phase lag means the
                // filtered output can momentarily exceed the instantaneous |dry*g|
                // (it carries memory of larger recent samples across a zero-crossing).
                // Since Pulse may only ever ATTENUATE, clamp the filtered magnitude
                // back to the un-filtered ducked bound so |shaped| <= |dry*g| <= |dry|.
                const float bound = std::fabs (dry * g);
                shaped = juce::jlimit (-bound, bound, shaped);
            }

            // Dry/wet: mix=1 -> full effect, mix=0 -> untouched dry. Both `dry` and
            // `shaped` are bounded by |dry|, and a mix in [0,1] interpolates between
            // them, so the result is guaranteed to satisfy |out| <= |dry|.
            const float outSample = dry + mix * (shaped - dry);
            d[i] = outSample;
        }

        phase += cyclesPerSample;
        if (phase >= 1.0) phase -= 1.0;
    }

    lastGain.store (lastGainThisBlock, std::memory_order_relaxed);
}

void SattariPulseAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariPulseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariPulseAudioProcessor::createEditor()
{
    return new SattariPulseAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariPulseAudioProcessor();
}
