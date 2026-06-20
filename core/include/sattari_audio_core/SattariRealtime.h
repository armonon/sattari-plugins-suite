#pragma once

// =============================================================================
//  Sattari Audio Suite — realtime-safety utilities
// -----------------------------------------------------------------------------
//  Small, dependency-light helpers used by the audio thread:
//
//   * AtomicSnapshot<T> — publish a small trivially-copyable struct from the
//     audio thread to the UI thread with no locks and no data race (seqlock).
//     Replaces the pattern of letting the editor read a plain member struct
//     (e.g. a "last detected pitch" frame) straight off the audio thread.
//
//   * ScratchBuffer — a juce::AudioBuffer sized once in prepareToPlay and reused
//     every block, so processBlock never allocates. Includes sumToMono(), the
//     operation several Sattari plugins do by hand.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <type_traits>

#if __has_include(<juce_audio_basics/juce_audio_basics.h>)
 #include <juce_audio_basics/juce_audio_basics.h>
 #define SATTARI_HAS_JUCE_AUDIO 1
#else
 #define SATTARI_HAS_JUCE_AUDIO 0
#endif

namespace sattari::rt
{

// -----------------------------------------------------------------------------
//  AtomicSnapshot<T> : single-writer (audio thread) / multi-reader (UI) seqlock.
//  T must be trivially copyable (a small POD of meter/analysis values).
// -----------------------------------------------------------------------------
template <typename T>
class AtomicSnapshot
{
public:
    static_assert (std::is_trivially_copyable<T>::value,
                   "AtomicSnapshot<T> requires a trivially-copyable T");

    AtomicSnapshot() = default;

    // Call from the audio thread only (single producer).
    void store (const T& fresh) noexcept
    {
        sequence.fetch_add (1, std::memory_order_release); // -> odd: write in progress
        value = fresh;
        sequence.fetch_add (1, std::memory_order_release); // -> even: write complete
    }

    // Call from any reader thread (e.g. the editor timer).
    T load() const noexcept
    {
        T out;
        std::uint32_t before, after;
        do
        {
            before = sequence.load (std::memory_order_acquire);
            out    = value;
            after  = sequence.load (std::memory_order_acquire);
        } while ((before & 1u) != 0u || before != after);
        return out;
    }

private:
    std::atomic<std::uint32_t> sequence { 0 };
    T value {};
};

#if SATTARI_HAS_JUCE_AUDIO

// -----------------------------------------------------------------------------
//  ScratchBuffer : preallocated reusable buffer for the audio thread.
// -----------------------------------------------------------------------------
class ScratchBuffer
{
public:
    // Call from prepareToPlay. Allocates once.
    void prepare (int numChannels, int maxBlockSize)
    {
        channels = juce::jmax (1, numChannels);
        capacity = juce::jmax (1, maxBlockSize);
        buffer.setSize (channels, capacity, false, false, false);
        buffer.clear();
    }

    // Realtime-safe: returns a view sized to numSamples without reallocating.
    // If a host ever exceeds the prepared block size, we grow once (rare) and
    // assert in debug so it gets caught and the prepared size raised.
    juce::AudioBuffer<float>& get (int numChannels, int numSamples)
    {
        if (numChannels > buffer.getNumChannels() || numSamples > capacity)
        {
            jassertfalse; // prepared size too small — bump it in prepareToPlay
            capacity = juce::jmax (capacity, numSamples);
            buffer.setSize (juce::jmax (numChannels, buffer.getNumChannels()), capacity, false, false, true);
        }
        active.setDataToReferTo (buffer.getArrayOfWritePointers(),
                                 juce::jmax (1, numChannels), numSamples);
        return active;
    }

    // Sum an interleaved/multichannel buffer down to a single mono scratch row.
    // Returns a pointer to numSamples mono samples (channel 0 of the scratch).
    const float* sumToMono (const juce::AudioBuffer<float>& source, int numSamples)
    {
        const int n = juce::jmin (numSamples, capacity);
        if (n > capacity)
            jassertfalse;
        auto* mono = buffer.getWritePointer (0);
        juce::FloatVectorOperations::clear (mono, n);
        const int srcChannels = juce::jmax (1, source.getNumChannels());
        for (int ch = 0; ch < srcChannels; ++ch)
            juce::FloatVectorOperations::addWithMultiply (mono, source.getReadPointer (ch), 1.0f / (float) srcChannels, n);
        return mono;
    }

    int getCapacity() const noexcept { return capacity; }

private:
    juce::AudioBuffer<float> buffer;
    juce::AudioBuffer<float> active;
    int channels = 1;
    int capacity = 0;
};

#endif // SATTARI_HAS_JUCE_AUDIO

} // namespace sattari::rt
