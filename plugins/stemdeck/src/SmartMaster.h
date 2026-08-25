#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "EQProcessor.h"

// =============================================================================
//  SmartMaster — the AI mastering assistant on the master bus.
// -----------------------------------------------------------------------------
//  Fully DSP-driven (no ML): continuously analyses the master signal and
//  applies three gentle, slow-moving corrections toward a mode target:
//
//    1. Spectral tilt  — low/mid/high energy balance is measured (~3 s window)
//                        and nudged toward the mode's target balance via the
//                        shared 3-band EQProcessor (max ±4 dB per band).
//    2. Bus dynamics   — a transparent 2:1 peak compressor (10 ms / 200 ms).
//    3. Loudness       — a simplified K-weighted short-term loudness estimate
//                        (LUFS-approx) steers makeup gain toward the mode's
//                        target (max ±8 dB, ~3 s time constant, gated).
//
//  Modes: 0 = Streaming (-14 LUFS, neutral), 1 = Club (-9, bass-forward),
//         2 = Warm (-16, mid-forward).
//  `amount` scales all corrections; 0 = fully bypassed (transparent).
// =============================================================================
class SmartMaster
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        eq.prepare (sr, 2);
        reset();
    }

    void reset()
    {
        eq.reset();
        lowEnergy = midEnergy = highEnergy = 1.0e-6f;
        loudnessMs = 1.0e-6f;
        appliedLow = appliedMid = appliedHigh = 0.0f;
        loudnessGainDb = 0.0f;
        compEnvelope = 0.0f;
        kHpState = { 0.0f, 0.0f };
        bandLpState = { 0.0f, 0.0f };
        bandHpState = { 0.0f, 0.0f };
        shortTermLufs.store (-70.0f, std::memory_order_relaxed);
        gainReductionDb.store (0.0f, std::memory_order_relaxed);
    }

    void process (juce::AudioBuffer<float>& buffer, int mode, float amount)
    {
        const int n  = buffer.getNumSamples();
        const int ch = juce::jmin (2, buffer.getNumChannels());
        if (n <= 0 || ch < 1)
            return;

        amount = std::isfinite (amount) ? juce::jlimit (0.0f, 1.0f, amount) : 0.0f;

        // A hostile hosted plug-in or an extreme automation transition must not
        // poison this processor's recursive analysis/filter state indefinitely.
        // Resetting these few scalar states is real-time safe.
        if (! stateIsFinite())
            reset();

        // This processor is called on every master block even while the feature
        // is disabled. Keep bypass genuinely free: the analysis below walks the
        // complete buffer and performs several filters/logarithms.
        if (amount <= 0.001f)
        {
            appliedLow = appliedMid = appliedHigh = 0.0f;
            loudnessGainDb = 0.0f;
            compEnvelope = 0.0f;
            shortTermLufs.store (-70.0f, std::memory_order_relaxed);
            gainReductionDb.store (0.0f, std::memory_order_relaxed);
            return;
        }

        // ── Mode targets ────────────────────────────────────────────────────
        float targetLufs, tLow, tMid, tHigh;
        switch (juce::jlimit (0, 2, mode))
        {
            case 1:  targetLufs = -9.0f;  tLow = 0.42f; tMid = 0.38f; tHigh = 0.20f; break;   // Club
            case 2:  targetLufs = -16.0f; tLow = 0.33f; tMid = 0.48f; tHigh = 0.19f; break;   // Warm
            default: targetLufs = -14.0f; tLow = 0.36f; tMid = 0.42f; tHigh = 0.22f; break;   // Streaming
        }

        // ── Analysis pass (block-rate accumulation, exponential ~3 s window) ─
        {
            float low = 0.0f, high = 0.0f, total = 0.0f, kw = 0.0f;
            const float lpC = onePoleCoeff (250.0f);
            const float hpC = onePoleCoeff (4000.0f);
            const float kC  = onePoleCoeff (100.0f);   // crude K-weighting: HP at 100 Hz

            const float* L = buffer.getReadPointer (0);
            const float* R = ch > 1 ? buffer.getReadPointer (1) : L;
            for (int i = 0; i < n; ++i)
            {
                const float left  = std::isfinite (L[i]) ? juce::jlimit (-32.0f, 32.0f, L[i]) : 0.0f;
                const float right = std::isfinite (R[i]) ? juce::jlimit (-32.0f, 32.0f, R[i]) : 0.0f;
                const float mono = 0.5f * (left + right);
                bandLpState[0] += lpC * (mono - bandLpState[0]);          // low band
                bandHpState[0] += hpC * (mono - bandHpState[0]);          // (lp for subtraction)
                const float lo = bandLpState[0];
                const float hi = mono - bandHpState[0];
                low   += lo * lo;
                high  += hi * hi;
                total += mono * mono;

                kHpState[0] += kC * (mono - kHpState[0]);                 // K-weight HP
                const float k = mono - kHpState[0];
                kw += k * k;
            }

            const float alpha = juce::jlimit (0.0005f, 0.5f, (float) n / (float) (sr * 3.0));
            const float mid = juce::jmax (0.0f, total - low - high);
            lowEnergy  += alpha * (low   / (float) n - lowEnergy);
            midEnergy  += alpha * (mid   / (float) n - midEnergy);
            highEnergy += alpha * (high  / (float) n - highEnergy);
            loudnessMs += alpha * (kw    / (float) n - loudnessMs);

            shortTermLufs.store (10.0f * std::log10 (loudnessMs + 1.0e-12f) - 0.69f,   // BS.1770-ish offset
                                 std::memory_order_relaxed);
        }

        const float lufs = shortTermLufs.load (std::memory_order_relaxed);
        const bool signalPresent = lufs > -45.0f;

        // ── Spectral tilt targets (slow-moving, clamped ±4 dB) ──────────────
        {
            const float sum = lowEnergy + midEnergy + highEnergy + 1.0e-12f;
            auto bandDb = [&] (float actualFrac, float targetFrac)
            {
                const float db = 10.0f * std::log10 (juce::jmax (1.0e-6f, targetFrac / juce::jmax (1.0e-6f, actualFrac)));
                return juce::jlimit (-4.0f, 4.0f, db) * amount;
            };
            const float gLow  = signalPresent ? bandDb (lowEnergy  / sum, tLow)  : 0.0f;
            const float gMid  = signalPresent ? bandDb (midEnergy  / sum, tMid)  : 0.0f;
            const float gHigh = signalPresent ? bandDb (highEnergy / sum, tHigh) : 0.0f;
            const float smooth = 0.05f;
            appliedLow  += smooth * (gLow  - appliedLow);
            appliedMid  += smooth * (gMid  - appliedMid);
            appliedHigh += smooth * (gHigh - appliedHigh);
        }

        // ── Loudness makeup toward target (gated, clamped ±8 dB, ~3 s) ──────
        {
            const float desired = signalPresent
                                ? juce::jlimit (-8.0f, 8.0f, (targetLufs - lufs) * amount)
                                : loudnessGainDb;
            loudnessGainDb += 0.03f * (desired - loudnessGainDb);
        }
        const float makeup = juce::Decibels::decibelsToGain (loudnessGainDb);

        // ── Per-sample: EQ tilt → makeup → 2:1 bus compressor ───────────────
        const float thresh   = juce::Decibels::decibelsToGain (-9.0f);
        const float atkC     = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));
        const float relC     = 1.0f - std::exp (-1.0f / (0.200f * (float) sr));
        float maxGr = 0.0f;

        float* L = buffer.getWritePointer (0);
        float* R = ch > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            float l = std::isfinite (L[i]) ? L[i] : 0.0f;
            float r = R != nullptr && std::isfinite (R[i]) ? R[i] : l;

            eq.processSample (l, r, appliedLow, appliedMid, appliedHigh);
            l *= makeup;
            r *= makeup;

            const float peak = juce::jmax (std::abs (l), std::abs (r));
            const float c    = peak > compEnvelope ? atkC : relC;
            compEnvelope += c * (peak - compEnvelope);

            float gain = 1.0f;
            if (compEnvelope > thresh)
            {
                // 2:1 above threshold, scaled by amount
                const float overDb = juce::Decibels::gainToDecibels (compEnvelope / thresh);
                const float grDb   = overDb * 0.5f * amount;
                gain  = juce::Decibels::decibelsToGain (-grDb);
                maxGr = juce::jmax (maxGr, grDb);
            }

            const float outL = l * gain;
            const float outR = r * gain;
            L[i] = std::isfinite (outL) ? outL : 0.0f;
            if (R != nullptr) R[i] = std::isfinite (outR) ? outR : 0.0f;
        }

        gainReductionDb.store (maxGr, std::memory_order_relaxed);
    }

    float getShortTermLufs() const noexcept   { return shortTermLufs.load (std::memory_order_relaxed); }
    float getGainReductionDb() const noexcept { return gainReductionDb.load (std::memory_order_relaxed); }

private:
    bool stateIsFinite() const noexcept
    {
        return std::isfinite (lowEnergy) && std::isfinite (midEnergy)
            && std::isfinite (highEnergy) && std::isfinite (loudnessMs)
            && std::isfinite (appliedLow) && std::isfinite (appliedMid)
            && std::isfinite (appliedHigh) && std::isfinite (loudnessGainDb)
            && std::isfinite (compEnvelope) && std::isfinite (kHpState[0])
            && std::isfinite (bandLpState[0]) && std::isfinite (bandHpState[0]);
    }

    float onePoleCoeff (float hz) const noexcept
    {
        return juce::jlimit (0.0001f, 0.9999f,
                             1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sr));
    }

    double sr = 44100.0;
    EQProcessor eq;

    // analysis state (audio thread only)
    float lowEnergy = 0.0f, midEnergy = 0.0f, highEnergy = 0.0f;
    float loudnessMs = 0.0f;
    float appliedLow = 0.0f, appliedMid = 0.0f, appliedHigh = 0.0f;
    float loudnessGainDb = 0.0f;
    float compEnvelope = 0.0f;
    std::array<float, 2> kHpState { 0.0f, 0.0f };
    std::array<float, 2> bandLpState { 0.0f, 0.0f };
    std::array<float, 2> bandHpState { 0.0f, 0.0f };

    // UI readouts
    std::atomic<float> shortTermLufs { -70.0f };
    std::atomic<float> gainReductionDb { 0.0f };

    JUCE_LEAK_DETECTOR (SmartMaster)
};
