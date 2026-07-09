#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
const juce::StringArray modelChoices { "Tube", "Tape", "Transformer", "Transistor" };

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

// Transparent soft ceiling. Below the knee the signal is returned bit-exact;
// above it the overshoot is tanh-compressed into the remaining headroom so the
// magnitude asymptotes to 1.0 and the output is hard-bounded to [-1, 1].
constexpr float kKnee = 0.90f;
inline float softCeiling (float x) noexcept
{
    const float ax = std::abs (x);
    if (ax <= kKnee)
        return x;
    const float over = ax - kKnee;             // amount above the knee
    const float room = 1.0f - kKnee;           // remaining headroom to +-1
    const float shaped = kKnee + room * std::tanh (over / room);
    return std::copysign (shaped, x);
}
}

// ---- Biquad coefficient setters (RBJ cookbook, Q = 0.707) -------------------
void SattariHeatAudioProcessor::Biquad::setLowShelf (double fs, double f, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw  = std::cos (w0);
    const double sw  = std::sin (w0);
    const double alpha = sw / (2.0 * 0.707);
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha;
    b0 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha)) / a0);
    b1 = (float) ((2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0);
    b2 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha)) / a0);
    a1 = (float) ((-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0);
    a2 = (float) (((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha) / a0);
}

void SattariHeatAudioProcessor::Biquad::setHighShelf (double fs, double f, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw  = std::cos (w0);
    const double sw  = std::sin (w0);
    const double alpha = sw / (2.0 * 0.707);
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha;
    b0 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha)) / a0);
    b1 = (float) ((-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0);
    b2 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha)) / a0);
    a1 = (float) ((2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0);
    a2 = (float) (((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha) / a0);
}

// ---- Per-model waveshapers --------------------------------------------------
// Each returns a value in a bounded range near [-1,1]. `bias` (-1..1) skews the
// curve so positive and negative excursions clip differently, which is what
// creates even-order harmonics (DC-blocked after the shaper). All are smooth
// (soft) curves so ADAA/oversampling behaves and nothing ever explodes.
float SattariHeatAudioProcessor::shape (int model, float pre, float bias) noexcept
{
    // Asymmetric offset added before the curve, normalised out after so the
    // rest position stays ~0. Kept modest so bias colours rather than dominates.
    const float off = 0.6f * bias;

    switch (model)
    {
        case 0: // Tube — smooth asymmetric tanh, gentle even/odd mix, musical.
        {
            const float x = pre + off;
            const float y = std::tanh (x);
            const float y0 = std::tanh (off);
            return y - y0;
        }
        case 1: // Tape — softer knee (arctan-like), rounds the top, compresses.
        {
            const float k = 1.5f;
            const float x = pre + off;
            const float y = (2.0f / juce::MathConstants<float>::pi) * std::atan (k * x);
            const float y0 = (2.0f / juce::MathConstants<float>::pi) * std::atan (k * off);
            return y - y0;
        }
        case 2: // Transformer — cubic soft-clip with a firmer bend + hysteresis-ish
        {        // asymmetry; richer low-order harmonics, slight core saturation.
            auto softCubic = [] (float x)
            {
                x = juce::jlimit (-1.5f, 1.5f, x);
                return juce::jlimit (-1.0f, 1.0f, x - (x * x * x) / 3.0f * 0.9f);
            };
            const float x = pre + off;
            return softCubic (x) - softCubic (off);
        }
        default: // 3 Transistor — harder soft-clip (x/(1+|x|)^n feel), edgier.
        {
            auto clip = [] (float x)
            {
                const float ax = std::abs (x);
                return x / (1.0f + ax + 0.25f * ax * ax);
            };
            const float x = pre + off;
            return clip (x) - clip (off);
        }
    }
}

// ---- Processor --------------------------------------------------------------
SattariHeatAudioProcessor::SattariHeatAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetBank (apvts, "heat")
{
    registerFactoryPresets();
}

void SattariHeatAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // model: 0 Tube, 1 Tape, 2 Transformer, 3 Transistor. Only listed set.
    presetBank.addFactoryPreset ("Clean / Off",
        PV { {"model", 0.0f}, {"drive", 0.0f}, {"output", 0.0f}, {"tone", 0.0f},
             {"mix", 1.0f}, {"bias", 0.0f} });
    presetBank.addFactoryPreset ("Tube Warmth",
        PV { {"model", 0.0f}, {"drive", 8.0f}, {"output", -1.5f}, {"tone", 0.15f},
             {"mix", 1.0f}, {"bias", 0.25f} });
    presetBank.addFactoryPreset ("Tape Glue",
        PV { {"model", 1.0f}, {"drive", 10.0f}, {"output", -2.0f}, {"tone", -0.2f},
             {"mix", 1.0f}, {"bias", 0.1f} });
    presetBank.addFactoryPreset ("Iron Transformer",
        PV { {"model", 2.0f}, {"drive", 14.0f}, {"output", -3.0f}, {"tone", 0.0f},
             {"mix", 1.0f}, {"bias", 0.35f} });
    presetBank.addFactoryPreset ("Parallel Grit",
        PV { {"model", 3.0f}, {"drive", 22.0f}, {"output", -1.0f}, {"tone", 0.3f},
             {"mix", 0.4f}, {"bias", 0.0f} });
    presetBank.addFactoryPreset ("Bright Bite",
        PV { {"model", 3.0f}, {"drive", 16.0f}, {"output", -2.5f}, {"tone", 0.5f},
             {"mix", 0.7f}, {"bias", -0.2f} });
}

int SattariHeatAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariHeatAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariHeatAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets())
        return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariHeatAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariHeatAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID ("model", 1), "Model", modelChoices, 0));
    p.push_back (std::make_unique<P> (juce::ParameterID ("drive", 1),  "Drive",
        R (0.0f, 36.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("output", 1), "Output",
        R (-24.0f, 12.0f, 0.1f), 0.0f));
    // Tone tilt: -1 dark (boost low / cut high), +1 bright (cut low / boost high).
    p.push_back (std::make_unique<P> (juce::ParameterID ("tone", 1),   "Tone",
        R (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("mix", 1),    "Mix",
        R (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("bias", 1),   "Bias",
        R (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID ("bypass", 1), "Bypass", false));

    return { p.begin(), p.end() };
}

float SattariHeatAudioProcessor::getParam (const char* id) const noexcept
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void SattariHeatAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    for (int ch = 0; ch < 2; ++ch)
    {
        lowShelf[(size_t) ch].reset();
        highShelf[(size_t) ch].reset();
        dcBlock[(size_t) ch].reset();
    }
    // DC blocker pole scaled with sample rate so the corner stays ~5 Hz.
    const float dcR = std::exp (-2.0f * juce::MathConstants<float>::pi * 5.0f / (float) sampleRate);
    for (auto& d : dcBlock) d.r = dcR;

    driveMeter.store (0.0f, std::memory_order_relaxed);

    // 4x oversampler for the shaper (allocation-free after this).
    osChannels = juce::jlimit (1, 2, juce::jmax (1, getTotalNumOutputChannels()));
    const int block = juce::jmax (1, samplesPerBlock);
    // useIntegerLatency=true so the reported latency is a whole number of
    // samples and the dry parallel-mix ring lines up exactly (no fractional skew).
    driveOS = std::make_unique<juce::dsp::Oversampling<float>> (
                  (size_t) osChannels, 2,
                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    driveOS->initProcessing ((size_t) block);
    driveOS->reset();

    dryStash.setSize (osChannels, block);
    dryStash.clear();
    dryRingLen = juce::jmax (1, (int) std::lround (driveOS->getLatencyInSamples()));
    dryRing.setSize (osChannels, dryRingLen);
    dryRing.clear();
    dryRingPos = 0;

    setLatencySamples (dryRingLen);
}

bool SattariHeatAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SattariHeatAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI Learn: feed any CCs to the learner so mapped params respond and a
    // freshly-armed param binds to the next CC.
    midiLearn.processMidi (midi);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (getParam ("bypass") > 0.5f)
        return; // leave input untouched

    const int   model    = juce::jlimit (0, 3, (int) std::round (getParam ("model")));
    const float driveDb  = getParam ("drive");
    const float outGain  = dbToGain (getParam ("output"));
    const float tone     = juce::jlimit (-1.0f, 1.0f, getParam ("tone"));
    const float mix      = juce::jlimit (0.0f, 1.0f, getParam ("mix"));
    const float bias     = juce::jlimit (-1.0f, 1.0f, getParam ("bias"));

    // Drive: dB -> linear pre-gain into the shaper. 36 dB is a lot of push; the
    // shapers all saturate so this only adds harmonics, never runaway level.
    const float drivePre = dbToGain (driveDb);
    // Output compensation. The shapers all asymptote to ~+-1 regardless of how
    // hard they are driven — pushing drive adds HARMONICS, it does NOT multiply
    // level by drivePre — so we must NOT divide the wet path by the pre-gain
    // (doing so collapses the output ~15-20x at high drive and silences it).
    // Instead apply a gentle, bounded trim that is exactly unity at drive=0 and
    // only backs off slightly as drive climbs, to tame the RMS bump from a sine
    // squaring up into a near-square while keeping level in the input ballpark.
    const float driveComp = juce::jmax (0.7f,
                                1.0f / (1.0f + 0.05f * (std::sqrt (drivePre) - 1.0f)));

    // Tone-tilt shelf pair, pre-shaper. Positive tone = brighter.
    const float tiltDb = 9.0f * tone;   // up to +/-9 dB of tilt
    for (int ch = 0; ch < 2; ++ch)
    {
        lowShelf[(size_t) ch].setLowShelf   (sampleRate, 250.0,  -tiltDb);
        highShelf[(size_t) ch].setHighShelf (sampleRate, 3500.0,  tiltDb);
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // ---- Pre-shaper: stash dry (for parallel mix) then tone-tilt in place ----
    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : left[i];
        dryStash.setSample (0, i, dryL);
        if (osChannels > 1) dryStash.setSample (1, i, dryR);

        float l = lowShelf[0].process (dryL);
        l = highShelf[0].process (l);
        left[i] = l;
        if (right != nullptr)
        {
            float r = lowShelf[1].process (dryR);
            r = highShelf[1].process (r);
            right[i] = r;
        }
    }

    // ---- Shaper, 4x oversampled (only the nonlinearity runs hot) -----------
    const int mdl = model;
    const float dp = drivePre, dc = driveComp, bs = bias;
    auto applyShaper = [mdl, dp, dc, bs] (float* d, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const float shaped = SattariHeatAudioProcessor::shape (mdl, d[i] * dp, bs);
            d[i] = shaped * dc;
        }
    };

    if (driveOS != nullptr && numChannels == osChannels)
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                            (size_t) numChannels, (size_t) numSamples);
        auto up = driveOS->processSamplesUp (block);
        for (int c = 0; c < numChannels; ++c)
            applyShaper (up.getChannelPointer ((size_t) c), (int) up.getNumSamples());
        driveOS->processSamplesDown (block);
    }
    else
    {
        applyShaper (left, numSamples);
        if (right != nullptr) applyShaper (right, numSamples);
    }

    // ---- Post-shaper: DC-block -> parallel mix (dry delayed to match the
    // oversampler) -> output -> soft ceiling. -------------------------------
    int rp = dryRingPos;
    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float l = dcBlock[0].process (left[i]);
        float r = right != nullptr ? dcBlock[1].process (right[i]) : l;

        // Pull the latency-comped dry out of the ring, push this block's dry in.
        const float ddL = dryRing.getSample (0, rp);
        const float ddR = osChannels > 1 ? dryRing.getSample (1, rp) : ddL;
        dryRing.setSample (0, rp, dryStash.getSample (0, i));
        if (osChannels > 1) dryRing.setSample (1, rp, dryStash.getSample (1, i));
        if (++rp >= dryRingLen) rp = 0;

        l = ddL * (1.0f - mix) + l * mix;
        r = ddR * (1.0f - mix) + r * mix;

        l *= outGain; r *= outGain;

        // Transparent soft ceiling: signal is passed through UNCHANGED below the
        // knee (|x| < kKnee) so normal levels are bit-exact; only the excursion
        // above the knee is tanh-compressed into the last (1 - kKnee) of head-
        // room, so the output asymptotes to +-1.0 and can never blow up.
        l = softCeiling (l);
        r = softCeiling (r);

        left[i] = l;
        if (right != nullptr) right[i] = r;

        blockPeak = juce::jmax (blockPeak, std::abs (l), std::abs (r));
    }
    dryRingPos = rp;

    // Cheap "how hot" read-out for the UI (peak * drive engagement, clamped).
    driveMeter.store (juce::jlimit (0.0f, 1.0f, blockPeak * mix), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SattariHeatAudioProcessor::createEditor()
{
    return new SattariHeatAudioProcessorEditor (*this);
}

void SattariHeatAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariHeatAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariHeatAudioProcessor();
}
