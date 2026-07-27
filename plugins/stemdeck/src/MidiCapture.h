#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// MidiCapture — records live MIDI played during a performance so it can be edited
// afterwards in the piano roll (spec build-order Step 7, capture half).
//
// This is the "capture intent, not audio" principle applied to MIDI: notes are
// stored as (sample, pitch, velocity, on/off) and converted to BEATS at the end,
// so a later tempo change re-renders correctly instead of baking in the old tempo.
//
// RT-safety: the audio thread only writes into a pre-allocated ring and bumps an
// atomic index — no allocation, no locks. The message thread drains it after the
// take. If a pathologically long take exhausts the ring, further events are dropped
// rather than growing it (and `overflowed()` says so honestly).
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <vector>

class MidiCapture
{
public:
    // ~65k events ≈ 32k notes; a dense hour-long take stays well inside this.
    static constexpr int kCapacity = 65536;

    struct Event
    {
        juce::int64 sample = 0;
        juce::uint8 pitch = 0, velocity = 0;
        bool isNoteOn = false;
    };

    // A completed note, already converted to musical time.
    struct CapturedNote
    {
        int    pitch = 60;
        double startBeats = 0.0;
        double lengthBeats = 0.25;
        int    velocity = 100;
    };

    MidiCapture() = default;

    void beginTake() noexcept
    {
        writeIndex.store (0, std::memory_order_release);
        overflow.store (false, std::memory_order_relaxed);
        capturing.store (true, std::memory_order_release);
    }

    void endTake() noexcept { capturing.store (false, std::memory_order_release); }
    bool isCapturing() const noexcept { return capturing.load (std::memory_order_acquire); }
    bool overflowed() const noexcept  { return overflow.load (std::memory_order_relaxed); }

    // AUDIO THREAD — RT-safe. `blockStartSample` is the take-relative sample of this
    // block's first frame; MIDI event offsets are added to it.
    void captureBlock (const juce::MidiBuffer& midi, juce::int64 blockStartSample) noexcept
    {
        if (! capturing.load (std::memory_order_acquire))
            return;

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const bool on  = m.isNoteOn();
            const bool off = m.isNoteOff();
            if (! on && ! off)
                continue;

            const int w = writeIndex.load (std::memory_order_relaxed);
            if (w >= kCapacity) { overflow.store (true, std::memory_order_relaxed); return; }

            auto& e = ring[(size_t) w];
            e.sample   = blockStartSample + meta.samplePosition;
            e.pitch    = (juce::uint8) juce::jlimit (0, 127, m.getNoteNumber());
            e.velocity = (juce::uint8) juce::jlimit (0, 127, on ? m.getVelocity() : 0);
            e.isNoteOn = on;
            writeIndex.store (w + 1, std::memory_order_release);
        }
    }

    // MESSAGE THREAD — pair note-ons with their note-offs and convert to beats.
    // Unclosed notes (still held when the take ended) are closed at `endSample`.
    std::vector<CapturedNote> takeNotes (double sampleRate, double bpm, juce::int64 endSample) const
    {
        std::vector<CapturedNote> notes;
        if (sampleRate <= 0.0 || bpm <= 0.0)
            return notes;

        const double samplesPerBeat = sampleRate * 60.0 / bpm;
        if (samplesPerBeat <= 0.0)
            return notes;

        const int count = writeIndex.load (std::memory_order_acquire);
        notes.reserve ((size_t) count / 2 + 1);

        // Open note-on per pitch (last one wins — retriggering the same pitch closes
        // the previous note, which is what a player expects).
        std::array<juce::int64, 128> openAt {};
        std::array<int, 128>         openVel {};
        openAt.fill (-1);

        auto closeNote = [&] (int pitch, juce::int64 at)
        {
            if (openAt[(size_t) pitch] < 0) return;
            const juce::int64 start = openAt[(size_t) pitch];
            const double lenBeats = juce::jmax (0.03125, (double) (at - start) / samplesPerBeat);
            CapturedNote n;
            n.pitch       = pitch;
            n.startBeats  = (double) start / samplesPerBeat;
            n.lengthBeats = lenBeats;
            n.velocity    = juce::jlimit (1, 127, openVel[(size_t) pitch]);
            notes.push_back (n);
            openAt[(size_t) pitch] = -1;
        };

        for (int i = 0; i < count && i < kCapacity; ++i)
        {
            const auto& e = ring[(size_t) i];
            const int p = e.pitch;
            if (e.isNoteOn)
            {
                closeNote (p, e.sample);          // retrigger closes the previous
                openAt[(size_t) p]  = e.sample;
                openVel[(size_t) p] = e.velocity;
            }
            else
            {
                closeNote (p, e.sample);
            }
        }

        // Anything still held when the take stopped.
        for (int p = 0; p < 128; ++p)
            closeNote (p, endSample);

        std::sort (notes.begin(), notes.end(),
                   [] (const CapturedNote& a, const CapturedNote& b) { return a.startBeats < b.startBeats; });
        return notes;
    }

    int getNumEvents() const noexcept { return writeIndex.load (std::memory_order_acquire); }

private:
    std::array<Event, kCapacity> ring {};
    std::atomic<int>  writeIndex { 0 };
    std::atomic<bool> capturing  { false };
    std::atomic<bool> overflow   { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiCapture)
};
