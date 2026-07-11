#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                       const juce::String& name,
                                                       float min, float max, float def,
                                                       float step, float skew = 1.0f,
                                                       const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, step, skew);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

constexpr const char* kThrIds[SattariMultibandAudioProcessor::kBands]   = { "thr1", "thr2", "thr3" };
constexpr const char* kRatioIds[SattariMultibandAudioProcessor::kBands] = { "ratio1", "ratio2", "ratio3" };
constexpr const char* kKneeIds[SattariMultibandAudioProcessor::kBands]  = { "knee1", "knee2", "knee3" };
constexpr const char* kAtkIds[SattariMultibandAudioProcessor::kBands]   = { "atk1", "atk2", "atk3" };
constexpr const char* kRelIds[SattariMultibandAudioProcessor::kBands]   = { "rel1", "rel2", "rel3" };
constexpr const char* kMakeupIds[SattariMultibandAudioProcessor::kBands]= { "mk1", "mk2", "mk3" };
constexpr const char* kSoloIds[SattariMultibandAudioProcessor::kBands]  = { "solo1", "solo2", "solo3" };

// Band-scoped parameter id, e.g. "thr" + band 1 -> "thr2".
juce::String bid (const char* base, int band) { return juce::String (base) + juce::String (band + 1); }
} // namespace

SattariMultibandAudioProcessor::SattariMultibandAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariMultibandAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Crossover frequencies. Log-ish skew so the low control has usable resolution.
    // Vocal-tuned: low/mid split at 250 Hz (chest vs body), mid/high at 3 kHz
    // (presence vs air / sibilance).
    p.push_back (floatParam ("xLow",  "Low/Mid X",  40.0f,  800.0f,  250.0f, 1.0f, 0.35f, "Hz"));
    p.push_back (floatParam ("xHigh", "Mid/High X", 800.0f, 12000.0f, 3000.0f, 1.0f, 0.35f, "Hz"));

    // Per-band dynamics. Vocal-tuned gentle defaults; ratio 1:1 + threshold 0 dB is
    // a hard-neutral pass-through per band.
    const char* names[kBands]   = { "Low", "Mid", "High" };
    const float defThr[kBands]  = { -18.0f, -20.0f, -22.0f };
    const float defRatio[kBands]= {  2.5f,   3.0f,   2.5f };
    const float defAtk[kBands]  = { 15.0f,  10.0f,   4.0f };
    const float defRel[kBands]  = { 160.0f, 120.0f,  90.0f };

    for (int b = 0; b < kBands; ++b)
    {
        const juce::String nm (names[b]);
        p.push_back (floatParam (bid ("thr",   b), nm + " Threshold", -60.0f, 0.0f, defThr[b],  0.1f, 1.0f, "dB"));
        p.push_back (floatParam (bid ("ratio", b), nm + " Ratio",       1.0f, 20.0f, defRatio[b], 0.01f, 0.5f, ":1"));
        p.push_back (floatParam (bid ("knee",  b), nm + " Knee",        0.0f, 24.0f, 6.0f,       0.1f, 1.0f, "dB"));
        p.push_back (floatParam (bid ("atk",   b), nm + " Attack",      0.1f, 120.0f, defAtk[b], 0.1f, 0.5f, "ms"));
        p.push_back (floatParam (bid ("rel",   b), nm + " Release",    20.0f, 1500.0f, defRel[b], 1.0f, 0.5f, "ms"));
        p.push_back (floatParam (bid ("mk",    b), nm + " Makeup",    -12.0f, 24.0f, 0.0f,       0.1f, 1.0f, "dB"));
        p.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (bid ("solo", b), 1), nm + " Solo", false));
    }

    p.push_back (floatParam ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0.1f, 1.0f, "%"));
    p.push_back (floatParam ("output", "Output", -24.0f, 24.0f, 0.0f, 0.1f, 1.0f, "dB"));

    return { p.begin(), p.end() };
}

void SattariMultibandAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    lr1.prepare (spec); lr1.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lr2.prepare (spec); lr2.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lr1.reset(); lr2.reset();

    for (auto& g : grSmoother) { g.prepare (sampleRate, 10.0f, 120.0f); g.reset (0.0f); }
    smoothedGrDb = { { 0.0f, 0.0f, 0.0f } };
    for (auto& a : bandGrDb) a.store (0.0f, std::memory_order_relaxed);
}

bool SattariMultibandAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariMultibandAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    // --- Crossover frequencies (clamp mid/high above low so the split stays valid).
    const float xLow  = juce::jlimit (20.0f, (float) (sampleRate * 0.45), param ("xLow"));
    const float xHigh = juce::jlimit (xLow * 1.1f, (float) (sampleRate * 0.45), param ("xHigh"));
    lr1.setCutoffFrequency (xLow);
    lr2.setCutoffFrequency (xHigh);

    // --- Per-band compressor coefficients (computed once per block). -----------
    float thr[kBands], invRatio[kBands], halfKnee[kBands], kneeW[kBands], makeup[kBands];
    bool  anySolo = false;
    bool  solo[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        thr[b]      = param (kThrIds[(size_t) b]);
        const float ratio = juce::jmax (1.0f, param (kRatioIds[(size_t) b]));
        invRatio[b] = 1.0f / ratio;
        kneeW[b]    = juce::jmax (0.0f, param (kKneeIds[(size_t) b]));
        halfKnee[b] = 0.5f * kneeW[b];
        const float atkMs = param (kAtkIds[(size_t) b]);
        const float relMs = param (kRelIds[(size_t) b]);
        grSmoother[(size_t) b].setTimes (atkMs, relMs);
        makeup[b]   = dbToGain (param (kMakeupIds[(size_t) b]));
        solo[b]     = boolParam (kSoloIds[(size_t) b]);
        anySolo     = anySolo || solo[b];
    }

    const float mix     = juce::jlimit (0.0f, 1.0f, param ("mix") * 0.01f);
    const float outGain = dbToGain (param ("output"));

    float grForMeter[kBands] = { 0.0f, 0.0f, 0.0f };

    for (int i = 0; i < n; ++i)
    {
        // --- Split into 3 bands per channel (recombines flat at unity gain). ---
        float band[kBands][2] = { { 0,0 }, { 0,0 }, { 0,0 } };
        for (int ch = 0; ch < numCh; ++ch)
        {
            float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
            lr1.processSample (ch, buffer.getReadPointer (ch)[i], low, rest);
            lr2.processSample (ch, rest, mid, high);
            band[0][ch] = low; band[1][ch] = mid; band[2][ch] = high;
        }

        // --- Per-band feed-forward compressor gain (stereo-linked detector). ---
        float bandGain[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            float det = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                det = std::max (det, std::fabs (band[b][ch]));

            const float detDb = juce::Decibels::gainToDecibels (det + 1.0e-9f, -120.0f);

            // Log-domain soft-knee gain computer (mirrors plugins/comp). GR <= 0.
            const float over = detDb - thr[b];
            float targetGrDb;
            if (kneeW[b] > 0.0001f && over > -halfKnee[b] && over < halfKnee[b])
            {
                const float x = over + halfKnee[b];                    // 0 .. kneeW
                const float outAboveDb = (1.0f - invRatio[b]) * (x * x) / (2.0f * kneeW[b]);
                targetGrDb = -outAboveDb;
            }
            else if (over <= -halfKnee[b])
            {
                targetGrDb = 0.0f;                                     // below knee: no reduction
            }
            else
            {
                targetGrDb = -(over) * (1.0f - invRatio[b]);           // hard-knee region above
            }

            // Fast toward MORE reduction, slow recovery — natural compressor timing.
            smoothedGrDb[(size_t) b] = grSmoother[(size_t) b].processFalling (targetGrDb);
            bandGain[b] = dbToGain (smoothedGrDb[(size_t) b]);
            grForMeter[b] = std::max (grForMeter[b], -smoothedGrDb[(size_t) b]);
        }

        // --- Recombine: sum compressed bands (+ per-band makeup), honour solo. -
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = buffer.getReadPointer (ch)[i];
            float wet = 0.0f;
            for (int b = 0; b < kBands; ++b)
            {
                if (anySolo && ! solo[b]) continue;                   // solo mutes the rest
                wet += band[b][ch] * bandGain[b] * makeup[b];
            }

            // Global dry/wet blend. When soloing we still blend against dry so the
            // mix knob behaves; solo is primarily an audition/tuning aid.
            float outv = dry * (1.0f - mix) + wet * mix;
            outv *= outGain;

            // Transparent soft ceiling so nothing can blow up (tanh only bites near
            // the rails; below ~0.8 it is essentially linear).
            if (outv > 0.8f || outv < -0.8f)
                outv = std::tanh (outv);
            buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, outv);
        }
    }

    for (int b = 0; b < kBands; ++b)
        bandGrDb[(size_t) b].store (grForMeter[b], std::memory_order_relaxed);
}

void SattariMultibandAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariMultibandAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMultibandAudioProcessor::createEditor()
{
    return new SattariMultibandAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariMultibandAudioProcessor();
}
