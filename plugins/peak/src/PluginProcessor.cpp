#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }
}

SattariPeakAudioProcessor::SattariPeakAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariPeakAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Input "push": drive into the limiter (pre-ceiling gain).
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID ("push", 1), "Push",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    // Output ceiling in dBTP (true-peak). Default -1.0 dBTP.
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID ("ceiling", 1), "Ceiling",
        juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -1.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dBTP")));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID ("release", 1), "Release",
        juce::NormalisableRange<float> (1.0f, 1000.0f, 0.1f, 0.4f), 120.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID ("lookahead", 1), "Lookahead",
        juce::NormalisableRange<float> (0.5f, 10.0f, 0.01f), 3.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Oversampled inter-sample-peak detection for a true "true-peak" ceiling.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("truePeak", 1),
        "True-Peak Detect", true));

    // One-knob loudness mode: auto-drives push toward this integrated target.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("autoLoud", 1),
        "Target Loudness", false));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID ("targetLufs", 1), "Target LUFS",
        juce::NormalisableRange<float> (-24.0f, -6.0f, 0.1f), -14.0f,
        juce::AudioParameterFloatAttributes().withLabel ("LUFS")));

    return { p.begin(), p.end() };
}

void SattariPeakAudioProcessor::designKWeighting (double sr)
{
    const double pi = juce::MathConstants<double>::pi;
    // Stage 1: high shelf, fc 1681.97 Hz, +3.9997 dB (BS.1770 K-weighting).
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

void SattariPeakAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    // Lookahead delay line sized for the 10 ms maximum (allocation-free after this).
    maxLookaheadSamples = juce::jmax (1, (int) std::ceil (0.010 * sampleRate));
    ringLen = maxLookaheadSamples + 1;
    lookaheadRing.setSize (2, ringLen);
    lookaheadRing.clear();
    // Empty slots mean "no reduction required" -> unity gain (1.0), NOT 0.0.
    // Priming with 0 would make the window-min force a full duck on the first block.
    peakRing.assign ((size_t) ringLen, 1.0f);
    ringPos = 0;
    reportedLatency = -1;

    envGain = 1.0f;
    tpHist = { 0.0f, 0.0f };
    outTpHist = { 0.0f, 0.0f };
    tpMax = 0.0f;

    // LUFS engine (short-term, 3 s window) for the target-loudness drive/readout.
    designKWeighting (sampleRate);
    shortLen = juce::jmax (1, (int) std::round (sampleRate * 3.0));
    zCap = shortLen;
    zRing.assign ((size_t) zCap, 0.0f);
    zWrite = 0;
    shortSum = 0.0;
    autoPushDb = 0.0f;

    grDb.store (0.0f);
    tpDb.store (-100.0f);
    shortLufs.store (-100.0f);
    pushDbOut.store (0.0f);
}

bool SattariPeakAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariPeakAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const float pushDb    = param ("push");
    const float ceilingDb = param ("ceiling");
    const float releaseMs = juce::jmax (1.0f, param ("release"));
    const float lookMs    = param ("lookahead");
    const bool  truePeak  = boolParam ("truePeak");
    const bool  autoLoud  = boolParam ("autoLoud");
    const float targetLufs = param ("targetLufs");

    const float ceilingLin = dbToGain (ceilingDb);

    // Lookahead in samples; report latency, re-notifying host only on change.
    const int look = juce::jlimit (1, maxLookaheadSamples,
                                   (int) std::lround (lookMs * 0.001 * sampleRate));
    if (look != reportedLatency)
    {
        reportedLatency = look;
        setLatencySamples (look);
    }

    // Program-dependent release: one-pole recovery toward unity gain.
    relCoeff = std::exp (-1.0f / (float) (sampleRate * (releaseMs * 0.001f)));

    // Auto target-loudness: nudge an extra push toward the target based on the
    // last block's short-term loudness. Slow + bounded so it can't runaway; the
    // ceiling always holds regardless of how much push is requested.
    if (autoLoud)
    {
        const float lastShort = shortLufs.load (std::memory_order_relaxed);
        if (lastShort > -60.0f)
        {
            const float errDb = targetLufs - lastShort;          // positive => need louder
            autoPushDb += juce::jlimit (-0.5f, 0.5f, errDb * 0.05f); // small step per block
            autoPushDb = juce::jlimit (0.0f, 24.0f, autoPushDb);
        }
    }
    else
    {
        // Relax the auto drive back toward zero so toggling off is graceful.
        autoPushDb *= 0.95f;
        if (autoPushDb < 0.01f) autoPushDb = 0.0f;
    }

    const float effectivePushDb = juce::jlimit (0.0f, 36.0f, pushDb + (autoLoud ? autoPushDb : 0.0f));
    const float pushGain = dbToGain (effectivePushDb);
    pushDbOut.store (effectivePushDb, std::memory_order_relaxed);

    float grForMeter = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // ---- Detector: push-scaled inter-sample (or sample) peak of this input.
        float detPeak = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = buffer.getReadPointer (ch)[i] * pushGain;
            const float p = truePeak ? interSamplePeak (tpHist[(size_t) ch], x) : std::fabs (x);
            detPeak = std::max (detPeak, p);
            tpHist[(size_t) ch] = x;
        }

        // Required instantaneous gain so this (future) sample lands on the ceiling.
        const float reqGain = detPeak > ceilingLin ? ceilingLin / detPeak : 1.0f;

        // ---- Write pushed dry + its required gain into the delay slot.
        const int writePos = ringPos;
        for (int ch = 0; ch < numCh; ++ch)
            lookaheadRing.setSample (ch, writePos, buffer.getReadPointer (ch)[i] * pushGain);
        if (numCh == 1) lookaheadRing.setSample (1, writePos, lookaheadRing.getSample (0, writePos));
        peakRing[(size_t) writePos] = reqGain;

        // ---- Envelope: attack instantly to the MINIMUM required gain anywhere in
        // the lookahead window (so the reduction is fully in place before the loud
        // sample is output); otherwise release (recover) toward unity. This makes
        // the guarantee: the delayed output sample is scaled by <= its own reqGain.
        float windowMin = 1.0f;
        for (int k = 0; k <= look; ++k)
        {
            const int idx = (writePos - k + ringLen) % ringLen;
            windowMin = std::min (windowMin, peakRing[(size_t) idx]);
        }
        if (windowMin < envGain) envGain = windowMin;                 // instant attack
        else envGain = windowMin + relCoeff * (envGain - windowMin);  // program-dependent release
        envGain = juce::jlimit (0.0f, 1.0f, envGain);

        // ---- Read the delayed dry sample and apply the envelope.
        const int readPos = (writePos - look + ringLen) % ringLen;
        float outSampleAbsTp = 0.0f;
        const int outCh = juce::jmin (2, buffer.getNumChannels());
        for (int ch = 0; ch < outCh; ++ch)
        {
            const int ringCh = juce::jmin (ch, 1);
            float y = lookaheadRing.getSample (ringCh, readPos) * envGain;

            // Final true-peak-aware soft ceiling: a transparent tanh knee just under
            // the ceiling catches any residual inter-sample overshoot so |out| never
            // crosses the ceiling (and thus never 0 dBFS).
            const float lim = ceilingLin;
            if (std::fabs (y) > lim * 0.5f)
            {
                const float sign = y < 0.0f ? -1.0f : 1.0f;
                const float a = std::fabs (y);
                // Soft knee mapping [0.5*lim .. inf) -> [0.5*lim .. lim).
                const float over = (a - lim * 0.5f) / (lim * 0.5f);
                const float shaped = lim * 0.5f + lim * 0.5f * std::tanh (over);
                y = sign * shaped;
            }
            y = juce::jlimit (-lim, lim, y);

            buffer.getWritePointer (ch)[i] = y;

            // Output true-peak (inter-sample) for the readout.
            const float otp = interSamplePeak (outTpHist[(size_t) ringCh], y);
            outSampleAbsTp = std::max (outSampleAbsTp, otp);
            outTpHist[(size_t) ringCh] = y;
        }

        tpMax = std::max (tpMax, outSampleAbsTp);
        grForMeter = std::max (grForMeter, 1.0f - envGain);

        // ---- Short-term loudness of the OUTPUT (K-weighted mono square, 3 s).
        double zsum = 0.0;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float y = buffer.getReadPointer (ch)[i];
            const double kw = hp[(size_t) ch].process (shelf[(size_t) ch].process ((double) y));
            zsum += kw * kw;
        }
        const float leaving = zRing[(size_t) zWrite];
        shortSum += zsum - leaving;
        zRing[(size_t) zWrite] = (float) zsum;
        if (++zWrite >= zCap) zWrite = 0;

        if (++ringPos >= ringLen) ringPos = 0;
    }

    // ---- Publish meters (lock-free). grForMeter is the max reduction fraction
    // this block; its dB magnitude is -gainToDecibels(minEnvGain), a positive value.
    const float minEnvGain = juce::jlimit (1.0e-4f, 1.0f, 1.0f - grForMeter);
    grDb.store (-juce::Decibels::gainToDecibels (minEnvGain, -60.0f), std::memory_order_relaxed);
    tpDb.store (tpMax > 0.0f ? juce::Decibels::gainToDecibels (tpMax) : -100.0f, std::memory_order_relaxed);
    tpMax *= 0.90f;   // slow decay so the reading falls back

    const double meanSq = shortSum / juce::jmax (1, shortLen);
    shortLufs.store (meanSq > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSq)) : -100.0f,
                     std::memory_order_relaxed);
}

void SattariPeakAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariPeakAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariPeakAudioProcessor::createEditor()
{
    return new SattariPeakAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariPeakAudioProcessor();
}
