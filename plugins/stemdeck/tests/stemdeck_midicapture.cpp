// MIDI capture contract: what you played is what lands in the piano roll.
//
// Feeds known note on/off pairs through the audio-thread capture path, then checks
// the message-thread conversion produces the right pitches, beat positions and
// lengths. Covers the three cases that silently corrupt a take:
//   * a normal note pair
//   * retriggering the same pitch before its note-off (must close the first note)
//   * a note still held when the take stops (must be closed at the end, not dropped)
//
//   StemDeckMidiCapture → prints MIDICAPTURE_OK on success
#include "MidiCapture.h"
#include <iostream>
#include <cmath>

namespace
{
constexpr double sr  = 48000.0;
constexpr double bpm = 120.0;                       // 1 beat = 0.5 s = 24000 samples
constexpr double spb = sr * 60.0 / bpm;

bool approx (double a, double b, double tol = 1.0e-6) { return std::abs (a - b) < tol; }

void push (MidiCapture& cap, bool on, int pitch, int vel, juce::int64 atSample, int blockSize = 512)
{
    // Emulate the host: the event arrives inside the block containing atSample.
    const juce::int64 blockStart = (atSample / blockSize) * blockSize;
    const int offset = (int) (atSample - blockStart);
    juce::MidiBuffer mb;
    mb.addEvent (on ? juce::MidiMessage::noteOn (1, pitch, (juce::uint8) vel)
                    : juce::MidiMessage::noteOff (1, pitch), offset);
    cap.captureBlock (mb, blockStart);
}
}

int main()
{
    MidiCapture cap;
    cap.beginTake();

    // 1) Plain note: C4 from beat 1 to beat 2.
    push (cap, true,  60, 100, (juce::int64) (1.0 * spb));
    push (cap, false, 60, 0,   (juce::int64) (2.0 * spb));

    // 2) Retrigger: E4 on at beat 4, on AGAIN at beat 5 without a note-off.
    push (cap, true,  64, 90,  (juce::int64) (4.0 * spb));
    push (cap, true,  64, 110, (juce::int64) (5.0 * spb));
    push (cap, false, 64, 0,   (juce::int64) (6.0 * spb));

    // 3) Held note: G4 on at beat 8, never released before the take ends at beat 10.
    push (cap, true,  67, 80,  (juce::int64) (8.0 * spb));

    cap.endTake();
    const auto notes = cap.takeNotes (sr, bpm, (juce::int64) (10.0 * spb));

    if (notes.size() != 4)
    { std::cerr << "FAIL: expected 4 notes, got " << notes.size() << "\n"; return 1; }

    // Sorted by start: C4@1, E4@4(len 1), E4@5(len 1), G4@8(len 2 — closed at the end).
    auto check = [&] (size_t i, int pitch, double start, double len, int vel)
    {
        const auto& n = notes[i];
        if (n.pitch != pitch || ! approx (n.startBeats, start) || ! approx (n.lengthBeats, len))
        {
            std::cerr << "FAIL: note " << i << " = pitch " << n.pitch
                      << " start " << n.startBeats << " len " << n.lengthBeats
                      << "; expected pitch " << pitch << " start " << start << " len " << len << "\n";
            return false;
        }
        if (n.velocity != vel)
        { std::cerr << "FAIL: note " << i << " velocity " << n.velocity << " expected " << vel << "\n"; return false; }
        return true;
    };

    if (! check (0, 60, 1.0, 1.0, 100)) return 1;   // plain pair
    if (! check (1, 64, 4.0, 1.0, 90))  return 1;   // retrigger closed the first
    if (! check (2, 64, 5.0, 1.0, 110)) return 1;   // second E4
    if (! check (3, 67, 8.0, 2.0, 80))  return 1;   // held note closed at take end

    if (cap.overflowed())
    { std::cerr << "FAIL: unexpected overflow\n"; return 1; }

    // Tempo-independence: the SAME capture read at 140 BPM must yield proportionally
    // scaled beat positions — proof we stored intent (samples), not baked beats.
    const auto at140 = cap.takeNotes (sr, 140.0, (juce::int64) (10.0 * spb));
    const double ratio = 140.0 / 120.0;
    if (at140.empty() || ! approx (at140[0].startBeats, 1.0 * ratio, 1.0e-6))
    { std::cerr << "FAIL: re-reading at 140 BPM did not rescale (" << at140[0].startBeats << ")\n"; return 1; }

    std::cout << "MIDICAPTURE_OK  notes=" << notes.size()
              << "  events=" << cap.getNumEvents() << "\n";
    return 0;
}
