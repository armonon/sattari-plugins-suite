#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// InputBody — a live audio-interface input (mic / line / DI instrument) as a
// channel (spec §3.4 / build-order Step 3).
//
// The host captures the device input once per block into BlockContext::inputAudio;
// this body copies it (with input gain and mono/stereo handling) into the channel
// scratch. The Channel frame then applies the shared FX chain / capture / metering
// / crossfader bus, exactly as it does for a TrackDeck — which is the whole point
// of the channel architecture: reverb on a live vocal is the same mechanism as
// reverb on a deck.
//
// Monitoring note (honest scope): this is the COMPENSATED mix path. A true
// low-latency monitoring path (direct/hardware monitoring) is a separate concern
// the spec calls out in §3.3 and is NOT implemented here.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <atomic>
#include "ChannelBody.h"
#include "EQProcessor.h"
#include "FilterProcessor.h"

class InputBody : public ChannelBody
{
public:
    InputBody() = default;

    ChannelType type() const noexcept override { return ChannelType::input; }

    // Message thread setters; read on the audio thread → atomic.
    void  setInputGain (float linear) noexcept { gain.store (juce::jlimit (0.0f, 4.0f, linear), std::memory_order_relaxed); }
    float getInputGain() const noexcept        { return gain.load (std::memory_order_relaxed); }
    void  setMonitorEnabled (bool on) noexcept { monitor.store (on, std::memory_order_relaxed); }
    bool  isMonitorEnabled() const noexcept    { return monitor.load (std::memory_order_relaxed); }
    bool  contributesToMonitorMix() const noexcept override
    {
        return monitor.load (std::memory_order_relaxed);
    }
    // Which device input channel to take (mono source); -1 = take the buffer as-is.
    void  setSourceChannel (int ch) noexcept   { sourceChannel.store (ch, std::memory_order_relaxed); }
    int   getSourceChannel() const noexcept    { return sourceChannel.load (std::memory_order_relaxed); }

    // Peak of the last block, for the input meter / clip indication (UI thread reads).
    float getInputPeak() const noexcept { return peak.load (std::memory_order_relaxed); }

    // ── Channel-strip controls (real DSP, same processors the decks use) ──────
    void  setLowDb  (float db) noexcept { lowDb.store  (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    void  setMidDb  (float db) noexcept { midDb.store  (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    void  setHighDb (float db) noexcept { highDb.store (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    float getLowDb()  const noexcept { return lowDb.load  (std::memory_order_relaxed); }
    float getMidDb()  const noexcept { return midDb.load  (std::memory_order_relaxed); }
    float getHighDb() const noexcept { return highDb.load (std::memory_order_relaxed); }

    // 0.5 = bypass; <0.5 low-pass, >0.5 high-pass (same law as the deck FILT knob).
    void  setFilter (float f) noexcept { filterControl.store (juce::jlimit (0.0f, 1.0f, f), std::memory_order_relaxed); }
    float getFilter() const noexcept   { return filterControl.load (std::memory_order_relaxed); }

    void prepare (double sampleRate, int, int) override
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        eq.prepare (sr, 2);
        filter.prepare (sr);
        eq.reset();
        filter.reset();
    }

    // AUDIO THREAD — RT-safe: a bounded copy, no allocation, no locks.
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        const auto* in = ctx.inputAudio;
        if (in == nullptr || ctx.numSamples <= 0 || in->getNumChannels() <= 0)
        { peak.store (0.0f, std::memory_order_relaxed); return; }

        const int n = juce::jmin (ctx.numSamples, in->getNumSamples());
        if (n <= 0) return;

        const float g   = gain.load (std::memory_order_relaxed);
        const int   src = sourceChannel.load (std::memory_order_relaxed);

        float blockPeak = 0.0f;
        for (int ch = 0; ch < scratch.getNumChannels(); ++ch)
        {
            // src >= 0 pins a single device input (mono source fed to both sides);
            // otherwise map channel-for-channel, duplicating a mono input.
            const int from = src >= 0 ? juce::jlimit (0, in->getNumChannels() - 1, src)
                                      : juce::jmin (ch, in->getNumChannels() - 1);
            scratch.copyFrom (ch, 0, *in, from, 0, n);
            blockPeak = juce::jmax (blockPeak, in->getMagnitude (from, 0, n));
        }

        // ── EQ + filter (same processors the decks use) ──
        const float lo = lowDb.load (std::memory_order_relaxed);
        const float mi = midDb.load (std::memory_order_relaxed);
        const float hi = highDb.load (std::memory_order_relaxed);
        const float fc = filterControl.load (std::memory_order_relaxed);
        const bool needEq     = std::abs (lo) > 0.01f || std::abs (mi) > 0.01f || std::abs (hi) > 0.01f;
        const bool needFilter = std::abs (fc - 0.5f) > 0.005f;

        if (needEq || needFilter)
        {
            auto* L = scratch.getWritePointer (0);
            auto* R = scratch.getNumChannels() > 1 ? scratch.getWritePointer (1) : L;
            for (int i = 0; i < n; ++i)
            {
                float l = L[i], r = R[i];
                if (needEq)     eq.processSample (l, r, lo, mi, hi);
                if (needFilter) filter.processSample (l, r, fc);
                L[i] = l;
                if (scratch.getNumChannels() > 1) R[i] = r;
            }
        }

        // Always render the processed input at its record gain. The Channel frame
        // invokes capture/metering first, then consults contributesToMonitorMix() to
        // decide whether it reaches the master. This keeps monitor-off recordings
        // intact instead of recording silence.
        scratch.applyGain (0, n, g);
        peak.store (blockPeak * g, std::memory_order_relaxed);
    }

private:
    std::atomic<float> gain    { 1.0f };
    std::atomic<bool>  monitor { true };
    std::atomic<int>   sourceChannel { -1 };
    std::atomic<float> peak    { 0.0f };
    std::atomic<float> lowDb { 0.0f }, midDb { 0.0f }, highDb { 0.0f };
    std::atomic<float> filterControl { 0.5f };
    EQProcessor     eq;
    FilterProcessor filter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InputBody)
};
