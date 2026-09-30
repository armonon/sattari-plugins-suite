#pragma once

// =============================================================================
//  rc::AudioTap — realtime-safe mid/side sample tap for the editor's visuals
// -----------------------------------------------------------------------------
//  Single writer (audio thread) / single reader (message thread). The audio
//  thread writes mid = (L+R)/2 and side = (L-R)/2 (side = 0 for mono) into two
//  fixed power-of-two rings and publishes the write head with a release store;
//  it never waits, locks or allocates. The reader copies the most recent N
//  samples behind the head it acquired. The rings are 4x the largest read, so
//  the writer would have to lap 3/4 of a ring during one copy for a read to
//  tear — and a torn frame is only ever a visual glitch.
// =============================================================================

#include <atomic>
#include <cstdint>

namespace rc
{
class AudioTap
{
public:
    static constexpr int kSize = 1 << 16;          // 65536 samples (~1.4 s @ 48 k)
    static constexpr int kMaxRead = kSize / 4;     // 16384

    // Audio thread.
    void push (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;
        std::uint32_t w = head.load (std::memory_order_relaxed);
        if (numChannels >= 2)
        {
            const float* l = channels[0];
            const float* r = channels[1];
            for (int i = 0; i < numSamples; ++i, ++w)
            {
                mid[w & kMask]  = 0.5f * (l[i] + r[i]);
                side[w & kMask] = 0.5f * (l[i] - r[i]);
            }
        }
        else
        {
            const float* m = channels[0];
            for (int i = 0; i < numSamples; ++i, ++w)
            {
                mid[w & kMask] = m[i];
                side[w & kMask] = 0.0f;
            }
        }
        head.store (w, std::memory_order_release);
    }

    void setSampleRate (double sr) noexcept { rate.store (sr, std::memory_order_relaxed); }
    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    // Message thread. Copies the latest n (<= kMaxRead) samples, oldest first.
    // sideDest may be null. Returns the write head.
    std::uint32_t readLatest (float* midDest, float* sideDest, int n) const noexcept
    {
        if (n > kMaxRead) n = kMaxRead;
        const std::uint32_t w = head.load (std::memory_order_acquire);
        std::uint32_t r = w - (std::uint32_t) n;
        for (int i = 0; i < n; ++i, ++r)
        {
            midDest[i] = mid[r & kMask];
            if (sideDest != nullptr)
                sideDest[i] = side[r & kMask];
        }
        return w;
    }

private:
    static constexpr std::uint32_t kMask = (std::uint32_t) kSize - 1u;
    float mid[kSize] {};
    float side[kSize] {};
    std::atomic<std::uint32_t> head { 0 };
    std::atomic<double> rate { 48000.0 };
};
} // namespace rc
