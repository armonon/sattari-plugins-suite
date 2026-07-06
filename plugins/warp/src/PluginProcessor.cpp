#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
// Male vowel formants (F1, F2) in Hz for A E I O U — the morph sweeps across them.
constexpr int   kNumVowels = 5;
constexpr double kVowelF1[kNumVowels] = { 730.0, 530.0, 270.0, 570.0, 300.0 };
constexpr double kVowelF2[kNumVowels] = { 1090.0, 1840.0, 2290.0, 840.0, 870.0 };

const juce::StringArray stutterRateChoices { "1/4", "1/8", "1/8T", "1/16", "1/32" };
const double stutterRateBeats[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };

// Morph position 0..1 -> interpolated formant pair.
void vowelFormants (double morph, double& f1, double& f2) noexcept
{
    const double x = juce::jlimit (0.0, 1.0, morph) * (kNumVowels - 1);
    const int    i = juce::jlimit (0, kNumVowels - 2, (int) std::floor (x));
    const double t = x - i;
    f1 = kVowelF1[i] + (kVowelF1[i + 1] - kVowelF1[i]) * t;
    f2 = kVowelF2[i] + (kVowelF2[i + 1] - kVowelF2[i]) * t;
}
} // namespace

void SattariWarpAudioProcessor::Biquad::setBandpass (double sampleRate, double freq, double q) noexcept
{
    freq = juce::jlimit (20.0, sampleRate * 0.45, freq);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha;
    b0 =  alpha / a0;   b1 = 0.0;   b2 = -alpha / a0;
    a1 = (-2.0 * cw) / a0;   a2 = (1.0 - alpha) / a0;
}

SattariWarpAudioProcessor::SattariWarpAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariWarpAudioProcessor::createParameterLayout()
{
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "input",  1 }, "Input",  juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "mix",    1 }, "Mix",    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // DOSE — the master intensity macro. Scales every block below.
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "dose",   1 }, "Dose",   juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    // VOWEL
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "vowel",  1 }, "Vowel",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.4f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "vowelAmt",1 }, "Vowel Amount", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));

    // ROBOT (ring mod)
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "robotFreq",1 }, "Robot Freq", juce::NormalisableRange<float> (20.0f, 1200.0f, 0.1f, 0.35f), 120.0f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "robotAmt", 1 }, "Robot",      juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));

    // GRIT (drive + crush)
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "drive",  1 }, "Drive",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "crush",  1 }, "Crush",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f));

    // STUTTER (beat-repeat)
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "stutter",    1 }, "Stutter", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "stutterRate",1 }, "Stutter Rate", stutterRateChoices, 3));

    return { p.begin(), p.end() };
}

void SattariWarpAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& b : vowelF1) b.reset();
    for (auto& b : vowelF2) b.reset();
    ringPhase = { 0.0, 0.0 };
    crushHold = { 0.0f, 0.0f };
    crushCount = { 0, 0 };

    // Stutter buffer: up to a 1/4 note at 40 BPM (worst case) — plenty of slack.
    stutterCap = (int) (sr * 1.6);
    for (auto& v : stutterBuf) v.assign ((size_t) stutterCap, 0.0f);
    stutterWrite = stutterPlay = stutterPos = 0;
    stutterLen = juce::jlimit (1, stutterCap, (int) (sr * 0.125));
}

bool SattariWarpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SattariWarpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm(); bpm && *bpm > 20.0)
                hostBpm = *bpm;

    const float inGain  = juce::Decibels::decibelsToGain (param ("input"));
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));
    const float mix     = param ("mix");
    const float dose    = param ("dose");

    // Apply input gain, then stash the dry signal for the dry/wet blend.
    buffer.applyGain (inGain);
    dryScratch.setSize (numCh, n, false, false, true);
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

    // Effective (dose-scaled) block amounts.
    double f1 = 730.0, f2 = 1090.0;
    vowelFormants (param ("vowel"), f1, f2);
    const float vowelAmt = param ("vowelAmt") * dose;
    const float robotAmt = param ("robotAmt") * dose;
    const double robotInc = 2.0 * juce::MathConstants<double>::pi * (double) param ("robotFreq") / sr;
    const float drive    = param ("drive") * dose;
    const float driveK   = 1.0f + drive * 24.0f;
    const float crush    = param ("crush") * dose;
    const int   crushBits = (int) std::round (juce::jmap (crush, 0.0f, 1.0f, 16.0f, 3.0f));   // 16 -> 3 bits
    const int   crushDown = 1 + (int) std::round (crush * 12.0f);                             // 1..13x downsample
    const float crushLevels = (float) (1 << juce::jlimit (2, 16, crushBits));
    const float stutterAmt = param ("stutter") * dose;

    // Stutter slice length from tempo (falls back to 120 BPM).
    const int rateIdx = juce::jlimit (0, 4, (int) std::round (param ("stutterRate")));
    const double beatSec = 60.0 / juce::jmax (20.0, hostBpm);
    stutterLen = juce::jlimit (32, stutterCap, (int) (beatSec * stutterRateBeats[rateIdx] * sr));

    // Configure the vowel resonators for this block.
    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
    {
        vowelF1[(size_t) ch].setBandpass (sr, f1, 6.0);
        vowelF2[(size_t) ch].setBandpass (sr, f2, 9.0);
    }

    float peak = 0.0f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        const int c = juce::jmin (ch, 1);   // reuse channel-0 state for >2 ch
        float* d = buffer.getWritePointer (ch);
        double rp = ringPhase[(size_t) c];

        for (int i = 0; i < n; ++i)
        {
            float x = d[i];   // input gain already applied buffer-wide

            // 1) VOWEL — blend in the formant-filtered signal.
            if (vowelAmt > 1.0e-4f)
            {
                const float formant = 0.6f * vowelF1[(size_t) c].process (x)
                                    + 0.5f * vowelF2[(size_t) c].process (x);
                x = x * (1.0f - vowelAmt) + formant * vowelAmt * 1.8f;
            }

            // 2) ROBOT — ring modulation.
            if (robotAmt > 1.0e-4f)
            {
                const float carrier = (float) std::sin (rp);
                x = x * (1.0f - robotAmt) + (x * carrier) * robotAmt;
            }
            rp += robotInc; if (rp > 2.0 * juce::MathConstants<double>::pi) rp -= 2.0 * juce::MathConstants<double>::pi;

            // 3) GRIT — waveshaper drive then bit / sample-rate crush.
            if (drive > 1.0e-4f)
                x = std::tanh (x * driveK) * (1.0f / std::tanh (driveK)) * (0.6f + 0.4f * drive) + x * (1.0f - drive);
            if (crush > 1.0e-4f)
            {
                if (crushCount[(size_t) c] <= 0)
                {
                    crushHold[(size_t) c] = std::round (x * crushLevels) / crushLevels;
                    crushCount[(size_t) c] = crushDown;
                }
                --crushCount[(size_t) c];
                x = x * (1.0f - crush) + crushHold[(size_t) c] * crush;
            }

            d[i] = x;   // write wet-so-far; STUTTER captures/replays below per channel
        }

        ringPhase[(size_t) c] = rp;
    }

    // 4) STUTTER — capture a slice and loop it, blended by amount. Done after the
    //    per-sample chain so all channels share the same slice timing.
    if (stutterAmt > 1.0e-4f && stutterCap > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            // On a slice boundary, start capturing a fresh slice (and rewind play).
            if (stutterPos >= stutterLen) { stutterPos = 0; stutterWrite = 0; stutterPlay = 0; }
            for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                if (stutterWrite < stutterLen)
                    stutterBuf[(size_t) ch][(size_t) stutterWrite] = d[i];      // fill the slice
                const int pIdx = stutterPlay % juce::jmax (1, stutterLen);
                const float rep = stutterBuf[(size_t) ch][(size_t) pIdx];
                d[i] = d[i] * (1.0f - stutterAmt) + rep * stutterAmt;
            }
            ++stutterWrite; ++stutterPlay; ++stutterPos;
        }
    }
    else { stutterWrite = stutterPlay = stutterPos = 0; }

    // Dry/wet blend (against the pre-FX dry copy) + output gain.
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* d = buffer.getWritePointer (ch);
        const float* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float wet = juce::jlimit (-1.5f, 1.5f, d[i]);
            const float y = (wet * mix + dry[i] * (1.0f - mix)) * outGain;
            d[i] = y;
            peak = juce::jmax (peak, std::fabs (y));
        }
    }

    outLevel.store (juce::jlimit (0.0f, 1.5f, peak), std::memory_order_relaxed);
}

void SattariWarpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SattariWarpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto restored = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (restored);
        apvts.replaceState (restored);
    }
}

juce::AudioProcessorEditor* SattariWarpAudioProcessor::createEditor()
{
    return new SattariWarpAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariWarpAudioProcessor();
}
