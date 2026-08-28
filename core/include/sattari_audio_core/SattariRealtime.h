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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
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
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free,
                   "AtomicSnapshot<T> requires lock-free 32-bit atomics");

    AtomicSnapshot()
    {
        const T initial {};
        store (initial);
    }

    // Call from the audio thread only (single producer).
    void store (const T& fresh) noexcept
    {
        std::array<unsigned char, sizeof (T)> bytes {};
        std::memcpy (bytes.data(), &fresh, sizeof (T));

        // Use one global order for the generation and payload words. A weaker
        // ordering permits another core to observe a new payload word before it
        // observes the odd "write in progress" generation.
        sequence.fetch_add (1, std::memory_order_seq_cst); // -> odd: write in progress
        for (std::size_t i = 0; i < wordCount; ++i)
        {
            const std::size_t offset = i * sizeof (std::uint32_t);
            const std::size_t count = std::min (sizeof (std::uint32_t), sizeof (T) - offset);
            std::uint32_t word = 0;
            std::memcpy (&word, bytes.data() + offset, count);
            words[i].store (word, std::memory_order_seq_cst);
        }
        sequence.fetch_add (1, std::memory_order_seq_cst); // -> even: write complete
    }

    // Call from any reader thread (e.g. the editor timer).
    T load() const noexcept
    {
        std::array<unsigned char, sizeof (T)> bytes {};
        for (;;)
        {
            const std::uint32_t before = sequence.load (std::memory_order_seq_cst);
            if ((before & 1u) != 0u)
                continue;

            for (std::size_t i = 0; i < wordCount; ++i)
            {
                const std::size_t offset = i * sizeof (std::uint32_t);
                const std::size_t count = std::min (sizeof (std::uint32_t), sizeof (T) - offset);
                const std::uint32_t word = words[i].load (std::memory_order_seq_cst);
                std::memcpy (bytes.data() + offset, &word, count);
            }

            const std::uint32_t after = sequence.load (std::memory_order_seq_cst);
            if (before == after)
                break;
        }

        T out;
        std::memcpy (&out, bytes.data(), sizeof (T));
        return out;
    }

private:
    static constexpr std::size_t wordCount =
        (sizeof (T) + sizeof (std::uint32_t) - 1) / sizeof (std::uint32_t);

    std::atomic<std::uint32_t> sequence { 0 };
    std::array<std::atomic<std::uint32_t>, wordCount> words;
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
