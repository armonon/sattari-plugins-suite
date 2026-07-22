// Contract for the playable drum-pad bindings + learn flow (SattariDrumPads.h).
//
// Locks the "press Set, press anything, it's mapped" experience: defaults are sane,
// learning captures the next note OR key, a binding moves off whatever pad held it
// (so one button never fires two sounds), unbound inputs do nothing, and state
// round-trips.
#include <sattari_audio_core/SattariDrumPads.h>

#include <cstdio>
#include <string>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}
}

int main()
{
    using sattari::dsp::DrumPadMap;
    std::printf ("SattariAudioCore drum-pads contract\n");

    // 1) Sensible GM defaults, playable out of the box.
    {
        DrumPadMap m (8);
        check (m.padForNote (36) == 0, "note 36 -> kick pad");
        check (m.padForNote (38) == 1, "note 38 -> snare pad");
        check (m.padForKey ('A') == 0, "key A -> kick pad");
        check (m.handleNoteOn (36) == 0, "playing note 36 triggers pad 0");
        check (m.handleKey ('S') == 1, "pressing S triggers pad 1");
        check (m.handleNoteOn (99) == -1, "unbound note triggers nothing");
    }

    // 2) Learn a MIDI note: arm a pad, next note-on binds (and does NOT also trigger).
    {
        DrumPadMap m (8);
        m.armLearn (2);
        check (m.learning() == 2, "pad 2 armed for learn");
        bool learned = false;
        const int fired = m.handleNoteOn (60, &learned);
        check (learned,        "note 60 was captured as a binding");
        check (fired == -1,    "learning input does not also fire a hit");
        check (m.learning() == -1, "learn disarms after one capture");
        check (m.padForNote (60) == 2, "note 60 now triggers pad 2");
        check (m.handleNoteOn (60) == 2, "and playing it fires pad 2");
    }

    // 3) Learn a computer KEY the same way.
    {
        DrumPadMap m (8);
        m.armLearn (5);
        bool learned = false;
        check (m.handleKey ('Q', &learned) == -1 && learned, "key Q captured for pad 5");
        check (m.padForKey ('Q') == 5, "key Q now triggers pad 5");
    }

    // 4) Binding an input MOVES it off the pad that had it — one button, one sound.
    {
        DrumPadMap m (8);
        // note 36 defaults to pad 0; rebind it to pad 3.
        m.armLearn (3);
        m.handleNoteOn (36);
        check (m.padForNote (36) == 3, "note 36 moved to pad 3");
        check (m.midiNote (0) == -1,   "pad 0 lost note 36 (no double trigger)");
        check (m.handleNoteOn (36) == 3, "note 36 now fires only pad 3");
    }

    // 5) Cancel learn leaves bindings untouched.
    {
        DrumPadMap m (8);
        m.armLearn (4);
        m.cancelLearn();
        check (m.learning() == -1, "learn cancelled");
        bool learned = false;
        check (m.handleNoteOn (36, &learned) == 0 && ! learned, "after cancel, notes trigger normally");
    }

    // 6) State round-trips.
    {
        DrumPadMap a (8);
        a.armLearn (1); a.handleNoteOn (70);   // custom binding
        a.armLearn (2); a.handleKey ('Z');
        const auto st = a.toState();
        DrumPadMap b (8);
        b.fromState (st);
        check (b.padForNote (70) == 1, "restored MIDI binding");
        check (b.padForKey ('Z') == 2, "restored key binding");
        check (b.padForNote (36) == 0, "restored default kept");
    }

    // 7) clearPad unbinds both inputs.
    {
        DrumPadMap m (8);
        m.clearPad (0);
        check (m.padForNote (36) == -1 && m.padForKey ('A') == -1, "clearPad removes both bindings");
    }

    std::printf (failures == 0 ? "drum-pads contract: PASS\n" : "drum-pads contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
