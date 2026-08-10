#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace stemdeck::sandbox
{
constexpr std::uint32_t magic = 0x53445342; // SDSB
constexpr std::uint32_t version = 1;
constexpr int maxChannels = 8;
constexpr int maxSamples = 4096;
constexpr int ringSlots = 3;

enum SlotState : std::uint32_t { free = 0, inputReady = 1, outputReady = 2 };

struct alignas (64) AudioSlot
{
    std::uint32_t state = free;
    std::uint32_t sequence = 0;
    std::uint32_t channels = 0;
    std::uint32_t samples = 0;
    float audio[(size_t) maxChannels * (size_t) maxSamples] {};
};

struct alignas (64) SharedAudio
{
    std::uint32_t fileMagic = magic;
    std::uint32_t fileVersion = version;
    std::uint32_t shutdown = 0;
    std::uint32_t helperAlive = 0;
    double sampleRate = 44100.0;
    std::uint32_t blockSize = 512;
    std::uint32_t channels = 2;
    AudioSlot slots[ringSlots];
};

inline std::atomic_ref<std::uint32_t> atomic (std::uint32_t& value) noexcept
{
    return std::atomic_ref<std::uint32_t> (value);
}
}
