#pragma once

#include <sattari_audio_core/SattariClipPlayback.h>
#include <iostream>

namespace sattari::test
{
class PlaybackToneReader final : public juce::AudioFormatReader
{
public:
    PlaybackToneReader (double rate, int channels, double frequencyIn = 1000)
        : AudioFormatReader (nullptr, "Playback fixture"), frequency (frequencyIn)
    {
        sampleRate = rate; numChannels = (unsigned int) channels;
        lengthInSamples = (juce::int64) (rate * 2);
        usesFloatingPointData = true; bitsPerSample = 32;
    }
    bool readSamples (int* const* output, int channels, int offset, juce::int64 start, int count) override
    {
        first = juce::jmin (first, start); last = juce::jmax (last, start + count);
        for (int ch = 0; ch < channels; ++ch) if (output[ch])
            for (int i = 0; i < count; ++i)
                reinterpret_cast<float*> (output[ch])[offset + i] = (float) ((ch == 0 ? 0.5 : -0.25)
                    * std::sin (juce::MathConstants<double>::twoPi * frequency * (double) (start + i) / sampleRate));
        return ! fail;
    }
    double frequency;
    bool fail = false;
    juce::int64 first = std::numeric_limits<juce::int64>::max(), last = -1;
};

inline bool clipPlaybackContract()
{
    bool passed = true;
    double worstError = 0, worstBlockDifference = 0;
    int cases = 0;
    for (double inputRate : { 44100.0, 48000.0, 96000.0 })
    for (double outputRate : { 44100.0, 48000.0, 96000.0 })
    for (int inputChannels : { 1, 2 })
    for (int outputChannels : { 1, 2 })
    {
        PlaybackToneReader source (inputRate, inputChannels);
        arrangement::ClipPlayback renderer;
        passed &= renderer.prepare (source, outputRate, 4096);
        const juce::int64 trimStart = (juce::int64) (inputRate * 0.25), trimEnd = (juce::int64) inputRate;
        const double start = trimStart + 512.25;
        juce::AudioBuffer<float> reference (outputChannels, 4096); reference.clear();
        passed &= renderer.add (reference, 0, 4096, start, trimStart, trimEnd);
        for (int ch = 0; ch < outputChannels; ++ch)
            for (int i = 0; i < reference.getNumSamples(); ++i)
            {
                const double amplitude = inputChannels == 1 || (outputChannels == 2 && ch == 0) ? 0.5
                    : outputChannels == 1 ? 0.125 : -0.25;
                const double expected = amplitude * std::sin (juce::MathConstants<double>::twoPi * 1000
                    * (start / inputRate + (double) i / outputRate));
                worstError = juce::jmax (worstError, std::abs (reference.getSample (ch, i) - expected));
            }
        for (int chunk : { 1, 31, 64, 512 })
        {
            juce::AudioBuffer<float> split (outputChannels, 4096); split.clear();
            for (int offset = 0; offset < split.getNumSamples(); offset += chunk)
                passed &= renderer.add (split, offset, juce::jmin (chunk, split.getNumSamples() - offset),
                    start + offset * inputRate / outputRate, trimStart, trimEnd);
            for (int ch = 0; ch < outputChannels; ++ch)
                for (int i = 0; i < split.getNumSamples(); ++i)
                    worstBlockDifference = juce::jmax (worstBlockDifference,
                        (double) std::abs (split.getSample (ch, i) - reference.getSample (ch, i)));
        }
        source.first = std::numeric_limits<juce::int64>::max(); source.last = -1;
        reference.clear();
        passed &= renderer.add (reference, 0, 10, (double) trimStart, trimStart, trimEnd);
        passed &= renderer.add (reference, 0, 10, trimEnd - 10 * inputRate / outputRate, trimStart, trimEnd);
        passed &= source.first >= trimStart && source.last <= trimEnd;
        source.fail = true;
        reference.clear();
        passed &= ! renderer.add (reference, 0, 512, start, trimStart, trimEnd)
            && juce::exactlyEqual (reference.getMagnitude (0, reference.getNumSamples()), 0.0f);
        ++cases;
    }
    // A source tone above the output Nyquist frequency must be removed before
    // downsampling, not folded into a new audible note.
    PlaybackToneReader ultrasonic (96000, 2, 30000);
    arrangement::ClipPlayback downsample;
    passed &= downsample.prepare (ultrasonic, 44100, 4096);
    juce::AudioBuffer<float> rejected (2, 4096); rejected.clear();
    passed &= downsample.add (rejected, 0, 4096, 48000.125, 0, ultrasonic.lengthInSamples);
    const double aliasRms = rejected.getRMSLevel (0, 0, rejected.getNumSamples());
    // Integer-rate aligned playback is sample-identical, not filtered.
    PlaybackToneReader native (48000, 2);
    arrangement::ClipPlayback exact;
    passed &= exact.prepare (native, 48000, 4096);
    juce::AudioBuffer<float> copied (2, 4096), original (2, 4096); copied.clear();
    passed &= exact.add (copied, 0, 4096, 12000, 12000, 48000);
    native.read (&original, 0, 4096, 12000, true, true);
    for (int ch = 0; ch < 2; ++ch)
        passed &= std::memcmp (copied.getReadPointer (ch), original.getReadPointer (ch), 4096 * sizeof (float)) == 0;
    passed &= worstError < 0.00002 && worstBlockDifference < 0.000002 && aliasRms < 0.00001;
    std::cout << "Clip playback " << cases << " format cases: max error=" << worstError
              << ", block difference=" << worstBlockDifference << ", alias RMS=" << aliasRms
              << " " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
}
