#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// MidiInstrumentBody — a playable instrument channel (spec build-order Step 4).
//
// Renders incoming MIDI through an in-tree polyphonic synth (the same voice engine
// MidiClipPlayer uses for its live path), so a controller plugged into StemDeck can
// be played as a channel alongside the decks. Being a Channel means it inherits the
// whole frame for free: per-channel FX chain, capture, metering, crossfader routing
// and delay compensation.
//
// Scope note: this is the CONTAINED tier — an internal instrument (spec §4.1
// "Tier A"). Hosting third-party VST3/AU instruments is Tier B and deliberately
// separate; it needs out-of-process sandboxing before it belongs in a live tool.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <atomic>
#include "ChannelBody.h"
#include "MidiClipPlayer.h"
#include "EQProcessor.h"
#include "FilterProcessor.h"

class MidiInstrumentBody : public ChannelBody
{
public:
    MidiInstrumentBody() = default;

    ChannelType type() const noexcept override { return ChannelType::midiInstrument; }

    // Message thread; read on the audio thread → atomic.
    void  setGain (float linear) noexcept { gain.store (juce::jlimit (0.0f, 4.0f, linear), std::memory_order_relaxed); }
    float getGain() const noexcept        { return gain.load (std::memory_order_relaxed); }
    // Only respond to one MIDI channel (1-16); 0 = omni.
    void  setMidiChannel (int ch) noexcept { midiChannel.store (juce::jlimit (0, 16, ch), std::memory_order_relaxed); }
    int   getMidiChannel() const noexcept  { return midiChannel.load (std::memory_order_relaxed); }
    // True while any note is sounding — drives the channel's MIDI-activity indicator.
    bool  isActive() const noexcept { return active.load (std::memory_order_relaxed); }
    // Latches on the first MIDI event this channel ever accepts. Drives the strip's
    // idle→active weight: an instrument channel that has never been played recedes.
    bool  hasReceivedMidi() const noexcept { return everPlayed.load (std::memory_order_relaxed); }

    // ── Channel-strip controls (same DSP the decks use) ──────────────────────
    void  setLowDb  (float db) noexcept { lowDb.store  (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    void  setMidDb  (float db) noexcept { midDb.store  (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    void  setHighDb (float db) noexcept { highDb.store (juce::jlimit (-24.0f, 24.0f, db), std::memory_order_relaxed); }
    float getLowDb()  const noexcept { return lowDb.load  (std::memory_order_relaxed); }
    float getMidDb()  const noexcept { return midDb.load  (std::memory_order_relaxed); }
    float getHighDb() const noexcept { return highDb.load (std::memory_order_relaxed); }
    void  setFilter (float f) noexcept { filterControl.store (juce::jlimit (0.0f, 1.0f, f), std::memory_order_relaxed); }
    float getFilter() const noexcept   { return filterControl.load (std::memory_order_relaxed); }

    void prepare (double sampleRate, int blockSize, int) override
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        synth.prepare (sr, juce::jmax (1, blockSize));
        // 4096, not 256: see the matching comment on teMidiScratch.ensureSize() in
        // MainPluginProcessor.cpp — a dense MIDI block can outgrow 256 bytes and
        // force addEvent() below to reallocate on the audio thread, silently
        // invalidating this class's own "RT-safe" comment on processInto().
        filtered.ensureSize (4096);
        eq.prepare (sr, 2);
        filter.prepare (sr);
        eq.reset();
        filter.reset();
    }

    void release() override { synth.liveAllNotesOff(); active.store (false, std::memory_order_relaxed); }

    // AUDIO THREAD — RT-safe. MidiBuffer::ensureSize was done in prepare(), and
    // addEvent below reuses that capacity for the small number of events per block.
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        if (ctx.midi == nullptr || ctx.numSamples <= 0)
            return;

        const int wantChannel = midiChannel.load (std::memory_order_relaxed);

        const juce::MidiBuffer* source = ctx.midi;
        if (wantChannel > 0)
        {
            // Channel filter: keep only events for this instrument's MIDI channel.
            filtered.clear();
            for (const auto meta : *ctx.midi)
            {
                const auto m = meta.getMessage();
                if (m.getChannel() == wantChannel)
                    filtered.addEvent (m, meta.samplePosition);
                    // RT-safe: a relaxed one-way latch, never cleared on the audio thread.
                    if (m.isNoteOn()) everPlayed.store (true, std::memory_order_relaxed);
            }
            source = &filtered;
        }

        bool sawNoteOn = false;
        for (const auto meta : *source)
            if (meta.getMessage().isNoteOn()) { sawNoteOn = true; break; }
        if (sawNoteOn) active.store (true, std::memory_order_relaxed);

        // Mute/solo are frame-level (Channel), so the body always renders — that
        // keeps meters live and makes un-muting click-free.
        synth.processLive (scratch, *source, ctx.numSamples, gain.load (std::memory_order_relaxed));

        // EQ + filter, same processors as a deck.
        const float lo = lowDb.load (std::memory_order_relaxed);
        const float mi = midDb.load (std::memory_order_relaxed);
        const float hi = highDb.load (std::memory_order_relaxed);
        const float fc = filterControl.load (std::memory_order_relaxed);
        const bool needEq     = std::abs (lo) > 0.01f || std::abs (mi) > 0.01f || std::abs (hi) > 0.01f;
        const bool needFilter = std::abs (fc - 0.5f) > 0.005f;
        if ((needEq || needFilter) && scratch.getNumChannels() > 0)
        {
            auto* L = scratch.getWritePointer (0);
            auto* R = scratch.getNumChannels() > 1 ? scratch.getWritePointer (1) : L;
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                float l = L[i], r = R[i];
                if (needEq)     eq.processSample (l, r, lo, mi, hi);
                if (needFilter) filter.processSample (l, r, fc);
                L[i] = l;
                if (scratch.getNumChannels() > 1) R[i] = r;
            }
        }

        // Activity clears once the buffer goes quiet (voices released).
        if (scratch.getMagnitude (0, ctx.numSamples) < 1.0e-5f)
            active.store (false, std::memory_order_relaxed);
    }

private:
    MidiClipPlayer     synth;      // reuses the in-tree 16-voice live synth
    juce::MidiBuffer   filtered;   // pre-sized in prepare(); channel-filtered events
    std::atomic<float> gain        { 1.0f };
    std::atomic<int>   midiChannel { 0 };   // 0 = omni
    std::atomic<bool>  active      { false };
    std::atomic<bool>  everPlayed  { false };   // one-way latch: has this channel ever seen a note?
    std::atomic<float> lowDb { 0.0f }, midDb { 0.0f }, highDb { 0.0f };
    std::atomic<float> filterControl { 0.5f };
    EQProcessor     eq;
    FilterProcessor filter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInstrumentBody)
};
