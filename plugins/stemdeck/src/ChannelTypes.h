#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Channel architecture — foundational types (Step 1 of the channel re-architecture;
// see plan "StemDeck → Channel Architecture: Step 1–2 Foundation").
//
// A session is a list of typed Channels sharing one frame (gain/EQ/FX/routing/
// capture) and differing only in their body. For now only TrackDeck is built; the
// other types are declared so the enum + persistence are forward-compatible.
//
// This header is intentionally dependency-light (JuceHeader + TransportSync) so it
// can be included on the audio path without pulling in the whole processor.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include "TransportSync.h"

// Hard cap on simultaneous channels. Drives the fixed APVTS "slot pool" (each slot
// carries one channel's automatable param bank) so host automation keeps a fixed
// parameter set. 16 gives comfortable headroom for a live set; raising it only
// costs host params, not correctness.
inline constexpr int kMaxChannels = 16;

enum class ChannelType
{
    trackDeck = 0,   // file playback → stem separation → 4 stem columns (the only type built now)
    input,           // live mic / line / instrument (Step 3)
    midiInstrument,  // hosted or Sattari synth driven by MIDI (Step 4)
    sampler,         // Sattari Chop pads (Step 4)
    pluginHost       // third-party VST3/AU (Step 8)
};

inline juce::String channelTypeId (ChannelType t) noexcept
{
    switch (t)
    {
        case ChannelType::trackDeck:      return "trackDeck";
        case ChannelType::input:          return "input";
        case ChannelType::midiInstrument: return "midiInstrument";
        case ChannelType::sampler:        return "sampler";
        case ChannelType::pluginHost:     return "pluginHost";
    }
    return "trackDeck";
}

inline ChannelType channelTypeFromId (const juce::String& s) noexcept
{
    if (s == "input")          return ChannelType::input;
    if (s == "midiInstrument") return ChannelType::midiInstrument;
    if (s == "sampler")        return ChannelType::sampler;
    if (s == "pluginHost")     return ChannelType::pluginHost;
    return ChannelType::trackDeck;
}

// Stable, opaque channel identity. Distinct from a channel's slot index (which is a
// transient binding to an APVTS param bank) and from its list order. Event scopes
// and capture metadata use this identity so removing/reordering channels cannot
// retarget automation.
using ChannelID = juce::int64;

// Crossfader routing side for a channel's contribution to the master bus.
enum class CrossfaderSide { center = 1, ac = 0, bd = 2 };

// Per-block state computed ONCE in processBlock and passed by const-ref to every
// channel. Nothing here is owned by a channel; it is read-only shared context.
struct BlockContext
{
    int    numSamples      = 0;
    float  leftBusGain     = 1.0f;   // A/C crossfader bus
    float  rightBusGain    = 1.0f;   // B/D crossfader bus
    juce::int64 blockStartSample = 0;
    TransportSync::Snapshot transport {};
    juce::MidiBuffer* midi = nullptr;   // shared; a channel must not mutate it in a way that harms later channels

    // Live audio-interface input for this block (mic / line / instrument), already
    // captured by the host. Null when the device has no input channels. Input
    // channels read from it; every other channel type ignores it.
    const juce::AudioBuffer<float>* inputAudio = nullptr;

    // True when at least one channel is soloed. Solo is a MIX-WIDE decision (every
    // non-soloed channel drops out), so it cannot live inside a single channel —
    // the host computes it once per block and every channel reads it here.
    bool anySoloed = false;
};
