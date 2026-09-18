#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <numeric>
#include <vector>

namespace sattari::arrangement
{
// Random-access, zero-timeline-offset sample-rate conversion. ARA exposes the
// source's future samples, so a centred filter needs no added playback latency.
// All storage and the band-limited interpolation table are prepared off audio.
// No streaming phase is carried between callbacks: seeking and block size do
// not change the result. Samples outside the clip trim never enter the filter.
class ClipPlayback
{
public:
    bool prepare (juce::AudioFormatReader& reader, double outputRate, int maximumBlock)
    {
        source = &reader;
        ready = false;
        if (! std::isfinite (reader.sampleRate) || ! std::isfinite (outputRate)
            || reader.sampleRate < 8000 || reader.sampleRate > 384000
            || outputRate < 8000 || outputRate > 384000 || maximumBlock <= 0
            || maximumBlock > 65536 || reader.numChannels == 0 || reader.numChannels > 64)
            return false;
        ratio = reader.sampleRate / outputRate;
        blockLimit = maximumBlock;
        taps = 128 * (int) std::ceil (juce::jmax (1.0, ratio));
        radius = taps / 2;
        const double cutoff = 0.94 * juce::jmin (1.0, 1.0 / ratio);
        table.resize ((size_t) (phases + 1) * (size_t) taps);
        for (int phase = 0; phase <= phases; ++phase)
        {
            double sum = 0.0;
            auto* row = table.data() + (size_t) phase * (size_t) taps;
            for (int tap = 0; tap < taps; ++tap)
            {
                const double distance = tap - radius + 1 - (double) phase / phases;
                const double x = juce::MathConstants<double>::pi * distance;
                const double sinc = std::abs (x) < 1.0e-12 ? cutoff : std::sin (cutoff * x) / x;
                const double window = std::abs (distance) >= radius ? 0.0
                    : 0.42 + 0.5 * std::cos (x / radius) + 0.08 * std::cos (2 * x / radius);
                row[tap] = (float) (sinc * window);
                sum += row[tap];
            }
            for (int tap = 0; tap < taps; ++tap) row[tap] /= (float) sum;
        }
        scratch.setSize ((int) reader.numChannels, (int) std::ceil (maximumBlock * ratio) + taps + 4);
        converted.setSize ((int) reader.numChannels, maximumBlock);
        ready = true;
        return true;
    }

    // Adds one region into an already-cleared/mixed host buffer. A failed read
    // adds nothing, never a partially filled or previous source buffer.
    bool add (juce::AudioBuffer<float>& output, int destination, int count,
              double sourcePosition, juce::int64 trimStart, juce::int64 trimEnd)
    {
        if (! ready || source == nullptr) return false;
        const int sourceChannels = (int) source->numChannels;
        const int outputChannels = output.getNumChannels();
        if (outputChannels <= 0 || count < 0 || count > blockLimit || destination < 0
            || destination + count > output.getNumSamples() || ! std::isfinite (sourcePosition)
            || trimStart < 0 || trimEnd <= trimStart || trimEnd > source->lengthInSamples
            || sourcePosition < (double) trimStart - ratio || sourcePosition >= (double) trimEnd + ratio
            || ! (sourceChannels == outputChannels || (sourceChannels <= 2 && outputChannels <= 2)))
            return false;
        if (count == 0) return true;
        const double nearest = std::round (sourcePosition);
        const bool exact = std::abs (ratio - 1.0) < 1.0e-12 && std::abs (sourcePosition - nearest) < 1.0e-7;
        const auto first = exact ? (juce::int64) nearest : (juce::int64) std::floor (sourcePosition) - radius + 1;
        const auto end = exact ? first + count
            : (juce::int64) std::floor (sourcePosition + (count - 1) * ratio) + radius + 1;
        if (end - first > scratch.getNumSamples()) return false;
        scratch.clear();
        const auto readStart = juce::jmax (first, trimStart);
        const auto readEnd = juce::jmin (end, trimEnd);
        if (readEnd > readStart
            && ! source->read (&scratch, (int) (readStart - first), (int) (readEnd - readStart), readStart, true, true))
            return false;

        for (int channel = 0; channel < sourceChannels; ++channel)
        {
            const auto* input = scratch.getReadPointer (channel);
            auto* out = converted.getWritePointer (channel);
            if (exact) juce::FloatVectorOperations::copy (out, input, count);
            else for (int i = 0; i < count; ++i)
            {
                const double at = sourcePosition + i * ratio;
                const double base = std::floor (at);
                const double phase = (at - base) * phases;
                const int index = juce::jlimit (0, phases - 1, (int) phase);
                const float fraction = (float) (phase - index);
                const auto* weights = table.data() + (size_t) index * (size_t) taps;
                const auto* samples = input + ((juce::int64) base - radius + 1 - first);
                // Interpolate adjacent phase-table rows, avoiding quantised
                // phase steps (and their modulation sidebands) during playback.
                const float left = std::inner_product (samples, samples + taps, weights, 0.0f);
                const float right = std::inner_product (samples, samples + taps, weights + taps, 0.0f);
                out[i] = left + fraction * (right - left);
            }
        }
        if (sourceChannels == outputChannels)
            for (int ch = 0; ch < outputChannels; ++ch) output.addFrom (ch, destination, converted, ch, 0, count);
        else if (sourceChannels == 1)
            for (int ch = 0; ch < outputChannels; ++ch) output.addFrom (ch, destination, converted, 0, 0, count);
        else
        {
            output.addFrom (0, destination, converted, 0, 0, count, 0.5f);
            output.addFrom (0, destination, converted, 1, 0, count, 0.5f);
        }
        return true;
    }

private:
    static constexpr int phases = 1024;
    juce::AudioFormatReader* source = nullptr;
    juce::AudioBuffer<float> scratch, converted;
    std::vector<float> table;
    double ratio = 1.0;
    int taps = 0, radius = 0, blockLimit = 0;
    bool ready = false;
};
}
