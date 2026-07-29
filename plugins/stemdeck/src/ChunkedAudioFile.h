#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

// Bounded disk-backed sample window for long recordings. The audio thread only reads
// an immutable cache snapshot and publishes the desired position; file I/O and cache
// allocation happen on this object's worker thread.
class ChunkedAudioFile final : private juce::Thread
{
public:
    static std::shared_ptr<ChunkedAudioFile> create (const juce::File& file);
    ~ChunkedAudioFile() override;

    double sampleRate() const noexcept { return sourceRate; }
    juce::int64 lengthInSamples() const noexcept { return sourceLength; }
    int numChannels() const noexcept { return channels; }

    // Lock-free playback read. A cache miss requests the relevant window and returns
    // silence until the worker publishes it (normally only after a distant seek).
    float readInterpolated (int channel, double sourcePosition) const noexcept;
    std::size_t residentBytes() const noexcept;

private:
    struct Cache
    {
        juce::int64 start = 0;
        juce::AudioBuffer<float> audio;
    };

    explicit ChunkedAudioFile (juce::File source);
    bool initialise();
    bool loadAround (juce::int64 position);
    void run() override;

    juce::File file;
    double sourceRate = 0.0;
    juce::int64 sourceLength = 0;
    int channels = 0;
    juce::int64 windowSamples = 0;
    mutable std::atomic<juce::int64> requestedPosition { 0 };
    std::shared_ptr<const Cache> cache; // atomic_load/store only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChunkedAudioFile)
};
