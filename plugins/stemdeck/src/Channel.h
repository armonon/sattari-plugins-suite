#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Channel — the shared frame wrapped around every ChannelBody. Identical UI/audio
// treatment for every type; this consistency is what stops the app feeling like
// five apps bolted together (spec §2.1).
//
// Step-1 scaffolding: the frame + a real (RT-safe) processBlock that renders the
// body into a scratch buffer and sums into the master with crossfader-bus gain.
// The per-channel FX-chain graph, recorder handle, and peak atomics are folded in
// during Step 3 (they currently live per-deck in MainPluginProcessor / HostGraph).
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "ChannelTypes.h"
#include "ChannelBody.h"

class Channel
{
public:
    Channel (ChannelID channelId, std::unique_ptr<ChannelBody> channelBody)
        : id (channelId), body (std::move (channelBody)) {}

    ChannelID       id;
    juce::String    name;
    juce::Colour    accent { 0xff4AD9C4 };
    int             slotIndex = -1;                 // APVTS param-bank binding (Step 2)
    CrossfaderSide  cfSide    = CrossfaderSide::center;

    ChannelType type() const noexcept { return body != nullptr ? body->type() : ChannelType::trackDeck; }
    ChannelBody* getBody() const noexcept { return body.get(); }

    // Frame extension point, invoked on the AUDIO THREAD between the body render and
    // the crossfader-bus sum. The host wires the per-channel FX chain, capture, and
    // peak metering here. Assigned once (message thread) before the channel is
    // published; must itself be RT-safe.
    std::function<void (juce::AudioBuffer<float>&, int numSamples)> onPostBody;

    // ── Plugin delay compensation (spec §3.3) ────────────────────────────────
    // A channel whose FX chain adds latency would drift out of phase with the rest.
    // The host sets each channel's compensation to (maxLatency - ownLatency) so every
    // channel lands aligned at the master bus. Set from the message thread, read on
    // the audio thread; the delay line is pre-allocated (no RT allocation).
    void setCompensationSamples (int samples) noexcept
    {
        pdcTarget.store (juce::jlimit (0, pdc.getNumSamples() - 1, samples), std::memory_order_relaxed);
    }
    int getCompensationSamples() const noexcept { return pdcTarget.load (std::memory_order_relaxed); }

    // ── Mute / Solo (frame-level: applies to every channel type) ─────────────
    void setSoloed (bool s) noexcept { soloed.store (s, std::memory_order_relaxed); }
    bool isSoloed() const noexcept   { return soloed.load (std::memory_order_relaxed); }
    void setMuted (bool m) noexcept  { frameMuted.store (m, std::memory_order_relaxed); }
    bool isMuted() const noexcept    { return frameMuted.load (std::memory_order_relaxed); }

    // FX-chain latency, cached from HostGraph on the message thread (querying the
    // graph takes a lock, so it must never happen on the audio thread).
    void setChainLatencySamples (int s) noexcept { chainLatency.store (juce::jmax (0, s), std::memory_order_relaxed); }
    // Total latency this channel contributes right now: cached chain + live body.
    int getTotalLatencySamples() const noexcept
    {
        return chainLatency.load (std::memory_order_relaxed)
             + (body != nullptr ? juce::jmax (0, body->getLatencySamples()) : 0);
    }

    void prepare (double sampleRate, int blockSize, int numChannels)
    {
        preparedChannels = juce::jmax (1, numChannels);
        scratch.setSize (preparedChannels, juce::jmax (1, blockSize), false, false, true);

        // PDC ring: sized for the worst compensation we allow (0.5 s) so the audio
        // thread never reallocates when the amount changes.
        const int pdcMax = juce::jmax (juce::jmax (1, blockSize) * 2, (int) (sampleRate * 0.5));
        pdc.setSize (preparedChannels, pdcMax, false, true, true);
        pdc.clear();
        pdcWrite = 0;

        if (body != nullptr)
            body->prepare (sampleRate, blockSize, preparedChannels);
    }

    void release()
    {
        if (body != nullptr)
            body->release();
    }

    // AUDIO THREAD — RT-safe. Render the body, apply the crossfader-bus gain for this
    // channel's side, and sum into the master output. No allocation/locks/IO here.
    void processBlock (juce::AudioBuffer<float>& masterOut, const BlockContext& ctx) noexcept
    {
        if (body == nullptr || ctx.numSamples <= 0)
            return;

        const int chans = juce::jmin (masterOut.getNumChannels(), scratch.getNumChannels());
        if (chans <= 0)
            return;

        // Grow-only: only resizes if the host block exceeded the prepared size (rare).
        if (scratch.getNumSamples() < ctx.numSamples)
            scratch.setSize (scratch.getNumChannels(), ctx.numSamples, false, false, true);

        scratch.clear (0, ctx.numSamples);
        body->processInto (scratch, ctx);

        // Per-channel FX chain / capture / metering (host-supplied, RT-safe).
        if (onPostBody)
            onPostBody (scratch, ctx.numSamples);

        // Plugin delay compensation: hold this channel back so it lands aligned with
        // the most-delayed channel. Ring-buffer, no allocation, no branching per sample.
        applyCompensation (ctx.numSamples);

        float busGain = cfSide == CrossfaderSide::ac ? ctx.leftBusGain
                      : cfSide == CrossfaderSide::bd ? ctx.rightBusGain
                                                     : 1.0f;   // center: unaffected by crossfader

        // Muted, or something else is soloed and this isn't it -> contribute nothing.
        // The body still ran above, so meters stay live and un-soloing is click-free.
        if (frameMuted.load (std::memory_order_relaxed)
            || (ctx.anySoloed && ! soloed.load (std::memory_order_relaxed)))
            busGain = 0.0f;

        if (busGain <= 0.0f)
            return;

        for (int ch = 0; ch < chans; ++ch)
            masterOut.addFrom (ch, 0, scratch, ch, 0, ctx.numSamples, busGain);
    }

private:
    // AUDIO THREAD. Delay `scratch` by the current compensation amount using a
    // pre-allocated ring. A change in amount takes effect at the next block boundary
    // (no interpolation) — compensation only changes when plugins are added/removed,
    // which is not a per-block event.
    void applyCompensation (int numSamples) noexcept
    {
        const int delay = pdcTarget.load (std::memory_order_relaxed);
        const int ringLen = pdc.getNumSamples();
        if (ringLen <= 1) return;

        if (delay <= 0)
        {
            // Keep the ring coherent so enabling compensation later doesn't replay stale audio.
            if (pdcActive) { pdc.clear(); pdcWrite = 0; pdcActive = false; }
            return;
        }
        pdcActive = true;

        const int chans = juce::jmin (pdc.getNumChannels(), scratch.getNumChannels());
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* ring = pdc.getWritePointer (ch);
            auto* sig  = scratch.getWritePointer (ch);
            int w = pdcWrite;
            for (int i = 0; i < numSamples; ++i)
            {
                const int r = (w - delay + ringLen) % ringLen;
                const float out = ring[r];
                ring[w] = sig[i];
                sig[i]  = out;
                w = (w + 1) % ringLen;
            }
        }
        pdcWrite = (pdcWrite + numSamples) % ringLen;
    }

    std::unique_ptr<ChannelBody> body;
    juce::AudioBuffer<float>     scratch;   // pre-allocated, grown-only
    juce::AudioBuffer<float>     pdc;       // pre-allocated PDC ring
    std::atomic<int>             pdcTarget { 0 };
    std::atomic<int>             chainLatency { 0 };
    std::atomic<bool>            soloed { false };
    std::atomic<bool>            frameMuted { false };
    int                          pdcWrite = 0;
    bool                         pdcActive = false;
    int                          preparedChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Channel)
};
