#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// ChannelBody — the type-specific part of a Channel. The Channel frame (gain / EQ /
// FX / routing / capture) is identical across all types; only the body differs.
//
//   TrackDeck   → stem playback (DeckPlayer + RTSS + deckFX)
//   Input       → live audio-interface input (Step 3)
//   MidiInstrument / Sampler / PluginHost → later steps
//
// The body renders its audio into a scratch buffer the Channel provides. The
// Channel then applies the shared frame (FX chain, gain, crossfader bus) and sums
// into the master bus.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "ChannelTypes.h"

class DeckPlayer;
struct TrackDeckParams;

class ChannelBody
{
public:
    virtual ~ChannelBody() = default;

    virtual ChannelType type() const noexcept = 0;

    // Typed access to a TrackDeck's player without RTTI (dynamic_cast must never
    // run on the audio thread). Returns nullptr for every non-TrackDeck body.
    virtual DeckPlayer* trackDeckPlayer() noexcept { return nullptr; }

    // Per-block parameter block for a TrackDeck body; the host fills it immediately
    // before processInto on the same (audio) thread. nullptr for other types.
    virtual TrackDeckParams* trackDeckParams() noexcept { return nullptr; }

    // Message thread. Pre-allocate everything the audio path needs at this rate/size.
    virtual void prepare (double sampleRate, int blockSize, int numChannels) = 0;

    // AUDIO THREAD — must be real-time safe: no allocation, no locks, no file I/O,
    // no unbounded work. Render this body's contribution into `scratch` (already
    // sized to ctx.numSamples and CLEARED by the caller — write additively or
    // replace). The Channel frame handles gain / FX / bus routing afterwards.
    virtual void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept = 0;

    // Whether this body's processed signal should be summed into the monitor/master
    // mix. Input channels use this to keep recording and metering alive while monitor
    // is off; capture happens before Channel consults this flag.
    virtual bool contributesToMonitorMix() const noexcept { return true; }

    // Latency this body introduces right now, in samples (e.g. a time-stretcher).
    // Read on the audio thread each block, so implementations must be RT-safe.
    virtual int getLatencySamples() const noexcept { return 0; }

    // Called when the audio device stops; release rate/size-dependent resources but
    // do NOT destroy anything an open editor/UI still references (mirror HostGraph).
    virtual void release() {}
};
