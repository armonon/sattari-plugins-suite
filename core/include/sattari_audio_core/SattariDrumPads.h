#pragma once

#include <array>
#include <vector>

// =============================================================================
//  SattariDrumPads — playable pad bindings with a one-touch learn flow.
// -----------------------------------------------------------------------------
//  Every pad (a kit voice) can be fired by a MIDI note AND/OR a computer key. The
//  "seamless" part is the learn flow: arm a pad, then the very next MIDI note-on or
//  keypress becomes that pad's trigger — the SAME gesture whether the user reaches
//  for their controller or their laptop keyboard.
//
//  A note/key belongs to at most one pad: binding an input that was on another pad
//  MOVES it, so you can never trigger two sounds from one button.
//
//  Pure-std (no JUCE), so it is contract-tested. The processor calls handleNoteOn /
//  handleKey from its MIDI + key paths; the editor calls armLearn when the user taps
//  "Set" on a pad. Trigger dispatch is allocation-free.
// =============================================================================

namespace sattari::dsp
{

class DrumPadMap
{
public:
    explicit DrumPadMap (int numPads = 8)
    {
        pads_.resize ((size_t) (numPads > 0 ? numPads : 1));
        // General-MIDI-ish defaults for the first lanes: kick, snare, closed/open hat,
        // clap, tom, rim, ride. Keyboard defaults walk the home row (A S D F G H J K).
        static const int gm[]  = { 36, 38, 42, 46, 39, 45, 37, 51 };
        static const int key[] = { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K' };
        for (int i = 0; i < numPads; ++i)
        {
            pads_[(size_t) i].midiNote = i < 8 ? gm[i]  : -1;
            pads_[(size_t) i].keyCode  = i < 8 ? key[i] : -1;
        }
    }

    int numPads() const noexcept { return (int) pads_.size(); }

    int  midiNote (int pad) const noexcept { return valid (pad) ? pads_[(size_t) pad].midiNote : -1; }
    int  keyCode  (int pad) const noexcept { return valid (pad) ? pads_[(size_t) pad].keyCode  : -1; }

    // Bind an input to a pad, moving it off any pad that currently holds it.
    void bindNote (int pad, int note) noexcept
    {
        if (! valid (pad)) return;
        for (auto& p : pads_) if (p.midiNote == note) p.midiNote = -1;
        pads_[(size_t) pad].midiNote = note;
    }
    void bindKey (int pad, int key) noexcept
    {
        if (! valid (pad)) return;
        for (auto& p : pads_) if (p.keyCode == key) p.keyCode = -1;
        pads_[(size_t) pad].keyCode = key;
    }
    void clearPad (int pad) noexcept
    {
        if (valid (pad)) { pads_[(size_t) pad].midiNote = -1; pads_[(size_t) pad].keyCode = -1; }
    }

    // --- Learn flow -----------------------------------------------------------
    void armLearn (int pad) noexcept { learningPad_ = valid (pad) ? pad : -1; }
    void cancelLearn() noexcept { learningPad_ = -1; }
    int  learning() const noexcept { return learningPad_; }

    // --- Query (no side effects) ---------------------------------------------
    int padForNote (int note) const noexcept
    {
        for (int i = 0; i < numPads(); ++i) if (pads_[(size_t) i].midiNote == note) return i;
        return -1;
    }
    int padForKey (int key) const noexcept
    {
        for (int i = 0; i < numPads(); ++i) if (pads_[(size_t) i].keyCode == key) return i;
        return -1;
    }

    // --- Input dispatch -------------------------------------------------------
    // Returns the pad to TRIGGER (>= 0), or -1 when nothing should fire. If a pad is
    // armed for learning, the input is captured as that pad's binding and -1 is
    // returned with *wasLearned = true (so the caller doesn't also trigger a hit).
    int handleNoteOn (int note, bool* wasLearned = nullptr) noexcept
    {
        if (wasLearned) *wasLearned = false;
        if (learningPad_ >= 0)
        {
            bindNote (learningPad_, note);
            learningPad_ = -1;
            if (wasLearned) *wasLearned = true;
            return -1;
        }
        return padForNote (note);
    }
    int handleKey (int key, bool* wasLearned = nullptr) noexcept
    {
        if (wasLearned) *wasLearned = false;
        if (learningPad_ >= 0)
        {
            bindKey (learningPad_, key);
            learningPad_ = -1;
            if (wasLearned) *wasLearned = true;
            return -1;
        }
        return padForKey (key);
    }

    // Serialise the bindings (flat ints) so state save/restore is trivial.
    std::vector<int> toState() const
    {
        std::vector<int> v; v.reserve (pads_.size() * 2);
        for (const auto& p : pads_) { v.push_back (p.midiNote); v.push_back (p.keyCode); }
        return v;
    }
    void fromState (const std::vector<int>& v) noexcept
    {
        for (int i = 0; i < numPads() && (size_t) (2 * i + 1) < v.size(); ++i)
        {
            pads_[(size_t) i].midiNote = v[(size_t) (2 * i)];
            pads_[(size_t) i].keyCode  = v[(size_t) (2 * i + 1)];
        }
        learningPad_ = -1;
    }

private:
    struct Pad { int midiNote = -1; int keyCode = -1; };
    bool valid (int pad) const noexcept { return pad >= 0 && pad < numPads(); }

    std::vector<Pad> pads_;
    int learningPad_ = -1;
};

} // namespace sattari::dsp
