#include "TuneModule.h"

#if SATTARI_ROYALCHAIN_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

#include <cmath>

TuneModule::TuneModule() = default;
TuneModule::~TuneModule() = default;

void TuneModule::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlock = juce::jmax (1, maxBlockSize);

    // Vocal fundamental range (~70 Hz - 1 kHz), shared detector owns the ring.
    pitchDetector.prepare (sr, 70.0f, 1000.0f, 0.045f);
    monoScratch.assign ((size_t) maxBlock, 0.0f);
    samplesSinceAnalysis = 0;

    reset();

#if SATTARI_ROYALCHAIN_HAS_RUBBERBAND
    stretchers.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        auto s = std::make_unique<RubberBand::RubberBandStretcher> (
            (size_t) sr, 1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime
                | RubberBand::RubberBandStretcher::OptionPitchHighQuality
                | RubberBand::RubberBandStretcher::OptionFormantPreserved
                | RubberBand::RubberBandStretcher::OptionTransientsMixed,
            1.0, 1.0);
        s->setMaxProcessSize ((size_t) maxBlock);
        s->setPitchScale (1.0);
        stretchers.push_back (std::move (s));
    }
    rbOut.setSize (2, maxBlock);
#endif
}

void TuneModule::reset()
{
    confidence = 0.0f;
    smoothedScale = 1.0;
    pitchDetector.reset();
    samplesSinceAnalysis = 0;
}

void TuneModule::process (juce::AudioBuffer<float>& buffer, float tuneAmount,
                          int keyRoot,
                          sattari::dsp::ScaleId scale)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Mono sum into the shared detector's ring (always, so detection stays live).
    if ((int) monoScratch.size() < numSamples)
        monoScratch.assign ((size_t) numSamples, 0.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getSample (ch, i);
        monoScratch[(size_t) i] = mono / (float) numChannels;
    }
    pitchDetector.pushBlock (monoScratch.data(), numSamples);
    samplesSinceAnalysis += numSamples;

#if SATTARI_ROYALCHAIN_HAS_RUBBERBAND
    if (tuneAmount <= 1.0e-4f || stretchers.empty())
    {
        if (samplesSinceAnalysis >= juce::jmax (256, maxBlock))
        {
            samplesSinceAnalysis = 0;
            const auto r = pitchDetector.analyse (0.30f);
            confidence += 0.1f * (r.confidence - confidence);
        }
        smoothedScale = 1.0;
        return;   // Tune fully down: pass audio through untouched.
    }

    if (samplesSinceAnalysis >= juce::jmax (256, maxBlock))
    {
        samplesSinceAnalysis = 0;
        const auto r = pitchDetector.analyse (0.30f);
        confidence += 0.1f * (r.confidence - confidence);

        double targetScale = 1.0;
        if (r.voiced && r.frequencyHz > 0.0f)
        {
            // Key-aware snap: go via MIDI, choose the nearest in-key tone (or the
            // nearest chromatic semitone when scale = ChromaticOff). Same shared
            // helper as Auto Pitch, so the two engines now share snap behaviour.
            const float midi = sattari::dsp::hzToMidi (r.frequencyHz);
            const int nearestMidi = (int) std::lround (midi);
            const int snappedMidi = sattari::dsp::snapMidiToScale (nearestMidi, keyRoot, scale);
            const float targetHz = sattari::dsp::midiToHz ((float) snappedMidi);
            targetScale = juce::jlimit (0.5, 2.0, (double) targetHz / (double) r.frequencyHz);
        }
        // Blend toward the target by the Tune amount (0 = none, 1 = full snap).
        const double blended = std::pow (targetScale, (double) juce::jlimit (0.0f, 1.0f, tuneAmount));
        smoothedScale += 0.35 * (blended - smoothedScale);
    }

    const int useChannels = juce::jmin (numChannels, (int) stretchers.size());
    rbOut.setSize (juce::jmax (rbOut.getNumChannels(), useChannels), numSamples, false, false, true);

    for (int ch = 0; ch < useChannels; ++ch)
    {
        auto& stretcher = *stretchers[(size_t) ch];
        stretcher.setPitchScale (smoothedScale);
        const float* in = buffer.getReadPointer (ch);
        stretcher.process (&in, (size_t) numSamples, false);

        int written = 0;
        while (written < numSamples && stretcher.available() > 0)
        {
            const int count = juce::jmin (numSamples - written, (int) stretcher.available());
            float* out = rbOut.getWritePointer (ch, written);
            stretcher.retrieve (&out, (size_t) count);
            written += count;
        }
        if (written < numSamples)
            rbOut.copyFrom (ch, written, buffer, ch, written, numSamples - written);

        buffer.copyFrom (ch, 0, rbOut, ch, 0, numSamples);
    }
#else
    juce::ignoreUnused (tuneAmount);
    if (samplesSinceAnalysis >= juce::jmax (256, maxBlock))
    {
        samplesSinceAnalysis = 0;
        const auto r = pitchDetector.analyse (0.30f);
        confidence += 0.1f * (r.confidence - confidence);
    }
#endif
}
