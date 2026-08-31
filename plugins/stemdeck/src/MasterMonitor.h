#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace stemdeck
{
// Control-room processing lives after every recording/export tap. Mono, Dim and
// Mute therefore change monitoring only; they can never damage a captured take.
class MasterMonitor
{
public:
    void setMono (bool enabled) noexcept { mono.store (enabled, std::memory_order_relaxed); }
    void setDim  (bool enabled) noexcept { dim.store  (enabled, std::memory_order_relaxed); }
    void setMute (bool enabled) noexcept { mute.store (enabled, std::memory_order_relaxed); }

    bool isMono() const noexcept { return mono.load (std::memory_order_relaxed); }
    bool isDim()  const noexcept { return dim.load  (std::memory_order_relaxed); }
    bool isMute() const noexcept { return mute.load (std::memory_order_relaxed); }

    float getCorrelation() const noexcept
    {
        return correlation.load (std::memory_order_relaxed);
    }

    // Analyze the mastered signal before monitor-only processing.
    void analyze (const juce::AudioBuffer<float>& buffer) noexcept
    {
        if (buffer.getNumChannels() < 2 || buffer.getNumSamples() <= 0)
        {
            correlation.store (1.0f, std::memory_order_relaxed);
            return;
        }

        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        double cross = 0.0, leftEnergy = 0.0, rightEnergy = 0.0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double l = left[i], r = right[i];
            cross += l * r;
            leftEnergy += l * l;
            rightEnergy += r * r;
        }

        const double denominator = std::sqrt (leftEnergy * rightEnergy);
        const float measured = denominator > 1.0e-12
                                 ? juce::jlimit (-1.0f, 1.0f,
                                                (float) (cross / denominator))
                                 : 0.0f;
        const float previous = correlation.load (std::memory_order_relaxed);
        correlation.store (previous * 0.82f + measured * 0.18f,
                           std::memory_order_relaxed);
    }

    void processOutput (juce::AudioBuffer<float>& buffer) const noexcept
    {
        if (buffer.getNumSamples() <= 0)
            return;
        if (mute.load (std::memory_order_relaxed))
        {
            buffer.clear();
            return;
        }

        if (mono.load (std::memory_order_relaxed) && buffer.getNumChannels() >= 2)
        {
            auto* left = buffer.getWritePointer (0);
            auto* right = buffer.getWritePointer (1);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float centre = (left[i] + right[i]) * 0.5f;
                left[i] = centre;
                right[i] = centre;
            }
        }

        if (dim.load (std::memory_order_relaxed))
            buffer.applyGain (juce::Decibels::decibelsToGain (-12.0f));
    }

    void resetMeter() noexcept { correlation.store (0.0f, std::memory_order_relaxed); }

private:
    std::atomic<bool> mono { false };
    std::atomic<bool> dim  { false };
    std::atomic<bool> mute { false };
    std::atomic<float> correlation { 0.0f };
};
} // namespace stemdeck
