#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// RBJ low-shelf (used for the tape head-bump).
void SattariWarbleAudioProcessor::LowShelf::set (double fs, double f, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi
                       * juce::jlimit (10.0, fs * 0.49, f) / fs;
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

SattariWarbleAudioProcessor::SattariWarbleAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}
SattariWarbleAudioProcessor::~SattariWarbleAudioProcessor() = default;

// Factory presets: real parameter values (0..1 amounts, dB). Only listed params are
// set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariWarbleAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Subtle Warmth",      PV{ {"age", 0.20f}, {"wow", 0.30f}, {"flutter", 0.30f}, {"sat", 0.40f}, {"tone", 0.50f}, {"hiss", 0.15f}, {"crackle", 0.05f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Old Tape",           PV{ {"age", 0.50f}, {"wow", 0.60f}, {"flutter", 0.50f}, {"sat", 0.60f}, {"tone", 0.60f}, {"hiss", 0.40f}, {"crackle", 0.20f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Dusty Vinyl",        PV{ {"age", 0.55f}, {"wow", 0.40f}, {"flutter", 0.30f}, {"sat", 0.45f}, {"tone", 0.55f}, {"hiss", 0.35f}, {"crackle", 0.70f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Wobbly VHS",         PV{ {"age", 0.70f}, {"wow", 0.90f}, {"flutter", 0.80f}, {"sat", 0.55f}, {"tone", 0.50f}, {"hiss", 0.50f}, {"crackle", 0.30f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Destroyed Cassette", PV{ {"age", 0.90f}, {"wow", 0.80f}, {"flutter", 0.90f}, {"sat", 0.80f}, {"tone", 0.70f}, {"hiss", 0.70f}, {"crackle", 0.60f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Lofi Saturator",     PV{ {"age", 0.45f}, {"wow", 0.15f}, {"flutter", 0.15f}, {"sat", 0.85f}, {"tone", 0.65f}, {"hiss", 0.10f}, {"crackle", 0.05f}, {"mix", 1.0f} });
    presetBank.addFactoryPreset ("Parallel Grit",      PV{ {"age", 0.60f}, {"wow", 0.50f}, {"flutter", 0.50f}, {"sat", 0.60f}, {"tone", 0.55f}, {"hiss", 0.40f}, {"crackle", 0.35f}, {"mix", 0.50f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariWarbleAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    using R  = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // The macro. Scales wow/flutter depth, saturation drive, hiss, crackle.
    p.push_back (std::make_unique<FP> ("age",      "Age",       R (0.0f, 1.0f, 0.01f), 0.35f));

    // Per-effect trims (0..1 relative weights the Age macro multiplies).
    p.push_back (std::make_unique<FP> ("wow",      "Wow",       R (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("flutter",  "Flutter",   R (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("sat",      "Saturation",R (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("tone",     "Head Bump", R (0.0f, 1.0f, 0.01f), 0.5f));
    p.push_back (std::make_unique<FP> ("hiss",     "Hiss",      R (0.0f, 1.0f, 0.01f), 0.4f));
    p.push_back (std::make_unique<FP> ("crackle",  "Crackle",   R (0.0f, 1.0f, 0.01f), 0.3f));

    p.push_back (std::make_unique<FP> ("mix",      "Mix",       R (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<FP> ("output",   "Output",    R (-24.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariWarbleAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = sr > 0.0 ? sr : 44100.0;

    // Delay line: a small centre delay (~20 ms) that the wow/flutter modulation
    // swings around. Length covers the centre plus max modulation swing plus a
    // guard so interpolation reads never go out of range.
    baseDelaySamples = (float) (0.020 * sampleRate);
    const double maxSwing = 0.010 * sampleRate;   // generous headroom for depth
    delayLen = (int) std::ceil (baseDelaySamples + maxSwing) + 8;
    for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);
    delayWrite = 0;

    wowPhase = 0.0;
    flutPhase = 0.0;

    for (auto& s : headBump) s.reset();
    hiCutState = { 0.0f, 0.0f };
    hissLpState = { 0.0f, 0.0f };
    crackleEnv = { 0.0f, 0.0f };

    // Tape high-cut one-pole at ~9 kHz (band-limits the wet path for warmth).
    const float fc = juce::jlimit (1000.0f, (float) (sampleRate * 0.49), 9000.0f);
    hiCutCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
}

void SattariWarbleAudioProcessor::releaseResources() {}

bool SattariWarbleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariWarbleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0)
        return;

    const float age     = juce::jlimit (0.0f, 1.0f, param ("age"));
    const float wowTrim  = param ("wow");
    const float flutTrim = param ("flutter");
    const float satTrim  = param ("sat");
    const float toneTrim = param ("tone");
    const float hissTrim = param ("hiss");
    const float crackTrim= param ("crackle");
    const float mix      = juce::jlimit (0.0f, 1.0f, param ("mix"));
    const float outGain  = juce::Decibels::decibelsToGain (param ("output"));

    // Age scales every degradation. Effective amounts are age * trim, so both
    // Age=0 and a given trim=0 disable that stage. mix=0 -> pure dry.
    const float wowDepthSamp  = age * wowTrim  * (0.006f * (float) sampleRate); // up to ~6 ms swing
    const float flutDepthSamp = age * flutTrim * (0.0012f * (float) sampleRate);// up to ~1.2 ms swing
    const float satAmt        = age * satTrim;                                   // 0..1 drive blend
    const float toneDb        = age * toneTrim * 6.0f;                           // up to +6 dB bump
    const float hissAmt       = age * hissTrim  * 0.004f;                        // low-level noise floor
    const float crackAmt      = age * crackTrim;                                 // pop probability/level

    // Tape saturation drive. A tanh soft-clipper normalised by the DRIVE (not by
    // tanh(drive)) keeps unity small-signal gain (slope 1 at the origin) while a
    // hot input is genuinely compressed toward the curve's ceiling — so peaks come
    // out lower, never boosted. drive == 1 (satAmt == 0) is bit-exact linear.
    const float satDrive = 1.0f + satAmt * 4.0f;

    // Head-bump low-shelf per channel (cheap to set per block).
    for (int ch = 0; ch < numCh; ++ch)
        headBump[(size_t) ch].set (sampleRate, 120.0, toneDb);

    // LFO increments. Wow ~0.7 Hz, flutter ~7 Hz.
    const double wowInc  = 2.0 * juce::MathConstants<double>::pi * 0.7 / sampleRate;
    const double flutInc = 2.0 * juce::MathConstants<double>::pi * 7.0 / sampleRate;

    // Sparse crackle: expected pops per second scales with crackAmt. Convert to a
    // per-sample trigger probability.
    const float popsPerSec = crackAmt * 40.0f;
    const float popProb    = popsPerSec / (float) sampleRate;
    const float crackleDecay = std::exp (-1.0f / (float) (0.0008 * sampleRate)); // ~0.8 ms tail

    for (int i = 0; i < n; ++i)
    {
        // A per-channel phase skew (below) decorrelates L/R slightly for a wider
        // tape feel.
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& d = delayBuf[(size_t) ch];
            const float in = buffer.getReadPointer (ch)[i];

            // Write dry input into the delay line.
            d[(size_t) delayWrite] = in;

            // Per-channel modulation swing (small phase offset on the right).
            const float chSkew = (ch == 1) ? 0.5f : 0.0f;
            const float wowCh  = std::sin ((float) wowPhase  + chSkew);
            const float flutCh = std::sin ((float) flutPhase + chSkew * 2.0f);
            const float modSamp = baseDelaySamples
                                  + wowDepthSamp  * wowCh
                                  + flutDepthSamp * flutCh;

            // Fractional read behind the write head.
            double rp = (double) delayWrite - (double) modSamp;
            while (rp < 0.0) rp += delayLen;
            int r0 = (int) std::floor (rp);
            const float fr = (float) (rp - r0);
            r0 %= delayLen;
            const int r1 = (r0 + 1) % delayLen;
            float wet = d[(size_t) r0] * (1.0f - fr) + d[(size_t) r1] * fr;

            // Tape saturation (dry/wet-blended so satAmt=0 is transparent).
            // tanh(x*drive)/drive: slope 1 at origin, compresses hot peaks below
            // the input level (never adds makeup/level).
            const float shaped = std::tanh (wet * satDrive) / satDrive;
            wet = wet + satAmt * (shaped - wet);

            // Head bump low-shelf, then tape high-cut.
            wet = headBump[(size_t) ch].process (wet);
            hiCutState[(size_t) ch] += hiCutCoeff * (wet - hiCutState[(size_t) ch]);
            wet = hiCutState[(size_t) ch];

            // Master coloration gate. The delay-read, head-bump and high-cut colour
            // (and phase-shift) the signal even when their own trims are zero, so
            // blend the whole coloured path back toward the raw input by Age. At
            // Age=0 this yields wet == in exactly (bit-exact pass-through); the
            // filter states above still run so they stay warm for Age>0. Hiss and
            // crackle below are additive Age-scaled terms that already vanish at
            // Age=0, so they are added after the gate.
            wet = in + age * (wet - in);

            // Hiss: filtered white noise, kept low-level.
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            hissLpState[(size_t) ch] += 0.30f * (white - hissLpState[(size_t) ch]);
            wet += hissAmt * hissLpState[(size_t) ch];

            // Crackle: sparse impulsive pops with a fast decay.
            if (crackAmt > 0.0f && rng.nextFloat() < popProb)
            {
                const float sign = (rng.nextFloat() < 0.5f) ? -1.0f : 1.0f;
                // Pop amplitude modest so it never swamps the signal.
                crackleEnv[(size_t) ch] = sign * (0.08f + 0.10f * rng.nextFloat());
            }
            wet += crackAmt * crackleEnv[(size_t) ch];
            crackleEnv[(size_t) ch] *= crackleDecay;

            // Own dry/wet mix, then output trim.
            float outv = in * (1.0f - mix) + wet * mix;
            outv *= outGain;

            // Transparent soft ceiling: linear below |x| = kKnee, tanh-limited
            // above it, so ordinary-level signals pass untouched but hot output
            // stays bounded (|out| < 1). At the knee both pieces meet in value
            // and slope, so there is no discontinuity.
            constexpr float kKnee = 0.9f;
            const float ax = std::fabs (outv);
            if (ax > kKnee)
            {
                const float over = (ax - kKnee) / (1.0f - kKnee);
                const float lim   = kKnee + (1.0f - kKnee) * std::tanh (over);
                outv = std::copysign (lim, outv);
            }
            buffer.getWritePointer (ch)[i] = outv;
        }

        wowPhase  += wowInc;
        flutPhase += flutInc;
        if (wowPhase  >= 2.0 * juce::MathConstants<double>::pi) wowPhase  -= 2.0 * juce::MathConstants<double>::pi;
        if (flutPhase >= 2.0 * juce::MathConstants<double>::pi) flutPhase -= 2.0 * juce::MathConstants<double>::pi;

        if (++delayWrite >= delayLen) delayWrite = 0;
    }
}

void SattariWarbleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariWarbleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariWarbleAudioProcessor::createEditor()
{
    return new SattariWarbleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariWarbleAudioProcessor();
}
