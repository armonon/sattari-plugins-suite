#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace sattari::test
{
// No audio files: the source is F# major except for a trimmed C-major clip
// between 12 and 24 seconds. Analysis must not accidentally scan the whole take.
struct ClipReaderProbe
{
    std::atomic<int> reads { 0 };
    std::atomic<juce::int64> firstSample { -1 }, lastSample { -1 };
    std::atomic<bool> destroyedOnOwnerThread { false };
    int failAfterReads = -1;
    bool slow = false;
    double gain = 1.0;
};

class ArrangementFixtureReader final : public juce::AudioFormatReader
{
public:
    explicit ArrangementFixtureReader (std::shared_ptr<ClipReaderProbe> p)
        : AudioFormatReader (nullptr, "Synthetic arrangement source"), probe (std::move (p)),
          ownerThread (std::this_thread::get_id()), gain (probe->gain)
    {
        sampleRate = 48000;
        lengthInSamples = 36 * 48000;
        numChannels = 2;
        bitsPerSample = 32;
        usesFloatingPointData = true;
    }
    ~ArrangementFixtureReader() override
    {
        probe->destroyedOnOwnerThread.store (ownerThread == std::this_thread::get_id());
    }
    bool readSamples (int* const* dest, int channels, int offset,
                      juce::int64 start, int count) override
    {
        const int readIndex = probe->reads.fetch_add (1);
        if (probe->slow) std::this_thread::sleep_for (std::chrono::milliseconds (2));
        if (probe->failAfterReads >= 0 && readIndex >= probe->failAfterReads) return false;
        if (probe->firstSample.load() < 0) probe->firstSample.store (start);
        probe->lastSample.store (start + count);
        for (int ch = 0; ch < channels; ++ch)
        {
            if (dest[ch] == nullptr) continue;
            auto* output = reinterpret_cast<float*> (dest[ch]) + offset;
            for (int i = 0; i < count; ++i)
            {
                const auto at = start + i;
                const double t = (double) at / sampleRate;
                const double transpose = t >= 12.0 && t < 24.0 ? 1.0 : std::sqrt (2.0);
                const double pulse = std::exp (-(double) (at % 24000) / (sampleRate * 0.035));
                const double envelope = 0.18 + 0.82 * pulse;
                double value = 0;
                for (double hz : { 261.6256, 329.6276, 391.9954 })
                    value += 0.22 * envelope * std::sin (juce::MathConstants<double>::twoPi * hz * transpose * t);
                output[i] = (float) (value * gain);
            }
        }
        return true;
    }
private:
    std::shared_ptr<ClipReaderProbe> probe;
    std::thread::id ownerThread;
    double gain;
};

inline std::unique_ptr<juce::AudioFormatReader> makeArrangementClip (
    const std::shared_ptr<ClipReaderProbe>& probe)
{
    return std::make_unique<juce::AudioSubsectionReader> (
        new ArrangementFixtureReader (probe), 12 * 48000, 12 * 48000, true);
}

inline bool readOnlyTrimmedClip (const ClipReaderProbe& probe)
{
    return probe.reads.load() > 1 && probe.firstSample.load() == 12 * 48000
        && probe.lastSample.load() == 24 * 48000;
}
}
