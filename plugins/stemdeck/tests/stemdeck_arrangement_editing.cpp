#include "MainPluginEditor.h"
#include "GainScale.h"

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const char* description)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << description << '\n';
    if (! ok) ++failures;
}

juce::ValueTree clipState (double start = 0.0, double end = 10.0,
                           double timeline = 0.0)
{
    juce::ValueTree root ("ARRANGEMENT");
    root.setProperty ("visibleLanes", 3, nullptr);
    root.setProperty ("overlapMode", 2, nullptr);
    root.setProperty ("snapEnabled", true, nullptr);
    root.setProperty ("snapBeatGrid", 0.25, nullptr);
    root.setProperty ("snapBpm", 128.0, nullptr);
    root.setProperty ("loopStart", 5.0, nullptr);
    root.setProperty ("loopEnd", 3.0, nullptr); // damaged range: must be disabled
    root.setProperty ("loopEnabled", true, nullptr);

    juce::ValueTree lane ("LANE");
    lane.setProperty ("index", 2, nullptr);
    lane.setProperty ("muted", true, nullptr);
    lane.setProperty ("gainDb", 99.0, nullptr); // corrupt/out of range
    juce::ValueTree clip ("CLIP");
    clip.setProperty ("file", "/missing/arrangement-source.wav", nullptr);
    clip.setProperty ("dur", 10.0, nullptr);
    clip.setProperty ("trimStart", start, nullptr);
    clip.setProperty ("trimEnd", end, nullptr);
    clip.setProperty ("tlStart", timeline, nullptr);
    clip.setProperty ("fadeIn", 30.0, nullptr); // must clamp to clip length
    clip.setProperty ("followAct", 999, nullptr);
    clip.setProperty ("followProb", -2.0, nullptr);
    lane.appendChild (clip, nullptr);

    juce::ValueTree midi ("MIDICLIP");
    midi.setProperty ("tlStart", -4.0, nullptr);
    juce::ValueTree note ("MNOTE");
    note.setProperty ("pitch", 999, nullptr);
    note.setProperty ("start", -1.0, nullptr);
    note.setProperty ("len", 0.0, nullptr);
    note.setProperty ("vel", 0, nullptr);
    midi.appendChild (note, nullptr);
    lane.appendChild (midi, nullptr);
    root.appendChild (lane, nullptr);
    return root;
}
}

struct StemDeckArrangementTestAccess
{
    static void run()
    {
        using View = MainPluginEditor::ArrangementRecorderView;
        using Clip = View::LaneClip;

        View view;
        int mixCallbacks = 0, loopCallbacks = 0;
        view.onLaneMixChanged = [&] { ++mixCallbacks; };
        view.onLoopChanged = [&] (double, double, bool) { ++loopCallbacks; };
        view.fromState (clipState());

        check (view.hasArrangement(), "valid saved clips restore an arrangement");
        check (view.getVisibleLanes() == 3,
               "visible lane count survives project restore");
        check (std::abs (view.getLaneGainDb (2) - stemdeck::gainscale::kMaxDb) < 1.0e-6f,
               "damaged lane gain clamps to the supported range");
        check (view.isLaneMuted (2), "lane mute survives project restore");
        check (! view.isLoopEnabled(),
               "an invalid saved loop range cannot arm playback");
        check (mixCallbacks == 1 && loopCallbacks == 1,
               "restore synchronises engine mix and loop state");

        const auto& clip = view.getClips()[2].front();
        check (std::abs (clip.fadeInSeconds - clip.lengthSeconds()) < 1.0e-6,
               "saved fades clamp to the restored clip length");
        check (clip.followAction == View::FollowAction::Stop
                   && clip.followProbability == 0.0f,
               "saved launcher metadata is range-safe");
        const auto& note = view.midiClipNotes (2, 0).front();
        check (view.midiClipStart (2, 0) == 0.0
                   && note.pitch == 127 && note.startBeats == 0.0
                   && note.lengthBeats > 0.0 && note.velocity == 1,
               "saved MIDI clips are sanitised before playback");

        view.setLaneGainDb (2, -12.0f);
        check (view.canUndo() && std::abs (view.getLaneGainDb (2) + 12.0f) < 1.0e-6f,
               "lane gain edits enter arranger undo history");
        view.undo();
        check (std::abs (view.getLaneGainDb (2) - stemdeck::gainscale::kMaxDb) < 1.0e-6f,
               "undo restores lane gain");
        view.redo();
        check (std::abs (view.getLaneGainDb (2) + 12.0f) < 1.0e-6f,
               "redo restores lane gain edit");

        view.toggleLaneMute (2);
        check (! view.isLaneMuted (2), "mute is editable");
        view.undo();
        check (view.isLaneMuted (2), "mute participates in arranger undo");

        view.deleteClip (2, 0);
        check (view.getClips()[2].empty(),
               "the final clip on a lane can be deleted");
        view.undo();
        check (view.getClips()[2].size() == 1,
               "deleting the final clip is undoable");

        // Push is true insert-time editing: a clip crossing the insertion point
        // splits around the incoming region instead of remaining overlapped.
        view.fromState (clipState());
        view.overlapMode = View::OverlapMode::Push;
        Clip incoming;
        incoming.durationSeconds = 2.0;
        incoming.trimEndSeconds = 2.0;
        incoming.timelineStartSeconds = 4.0;
        view.applyOverlapResolution (2, incoming);
        view.clips[2].push_back (incoming);
        bool pushLeft = false, pushIncoming = false, pushRight = false;
        for (const auto& region : view.clips[2])
        {
            pushLeft = pushLeft
                    || (std::abs (region.timelineStartSeconds) < 1.0e-6
                        && std::abs (region.lengthSeconds() - 4.0) < 1.0e-6);
            pushIncoming = pushIncoming
                        || (std::abs (region.timelineStartSeconds - 4.0) < 1.0e-6
                            && std::abs (region.lengthSeconds() - 2.0) < 1.0e-6);
            pushRight = pushRight
                     || (std::abs (region.timelineStartSeconds - 6.0) < 1.0e-6
                         && std::abs (region.trimStartSeconds - 4.0) < 1.0e-6);
        }
        check (pushLeft && pushIncoming && pushRight,
               "Push mode splits a crossing clip and inserts clean time");

        // Replace must preserve both outside halves when one long clip surrounds
        // the replacement range.
        view.fromState (clipState());
        view.overlapMode = View::OverlapMode::Replace;
        view.applyOverlapResolution (2, incoming);
        view.clips[2].push_back (incoming);
        bool replaceLeft = false, replaceRight = false;
        for (const auto& region : view.clips[2])
        {
            replaceLeft = replaceLeft
                       || (std::abs (region.timelineStartSeconds) < 1.0e-6
                           && std::abs (region.trimEndSeconds - 4.0) < 1.0e-6);
            replaceRight = replaceRight
                        || (std::abs (region.timelineStartSeconds - 6.0) < 1.0e-6
                            && std::abs (region.trimStartSeconds - 6.0) < 1.0e-6);
        }
        check (replaceLeft && replaceRight,
               "Replace mode preserves both sides of a spanning clip");

        const auto roundTrip = view.toState();
        check ((int) roundTrip.getProperty ("visibleLanes") == 3
                   && (bool) roundTrip.getProperty ("snapEnabled")
                   && std::abs ((double) roundTrip.getProperty ("snapBpm") - 128.0) < 1.0e-6,
               "arranger workspace settings persist with the project");

        view.clearArrangement();
        check (! view.hasArrangement(), "clear leaves the arranger idle");
        bool anySavedClip = false;
        const auto cleared = view.toState();
        for (int laneIndex = 0; laneIndex < cleared.getNumChildren(); ++laneIndex)
            anySavedClip = anySavedClip
                        || cleared.getChild (laneIndex).getNumChildren() > 0;
        check (! anySavedClip,
               "clear removes hidden audio/MIDI/automation state from project saves");

        const int midiIndex = view.addMidiClip (1, 2.0);
        check (midiIndex == 0 && view.hasArrangement(),
               "adding a MIDI clip creates arranger content");
        view.undo();
        check (! view.hasArrangement() && view.numMidiClips (1) == 0,
               "undoing the only MIDI clip returns the arranger to idle");
        view.redo();
        check (view.hasArrangement() && view.numMidiClips (1) == 1,
               "redo restores MIDI content and arranger mode");

        std::vector<View::MidiNote> editedNotes {
            { 64, 0.0, 1.0, 110 }, { 67, 1.0, 0.5, 96 }
        };
        view.setMidiClipNotes (1, 0, editedNotes);
        check (view.midiClipNotes (1, 0).size() == 2,
               "piano-roll note edits are applied");
        view.undo();
        check (view.midiClipNotes (1, 0).empty(),
               "piano-roll note edits participate in arranger undo");
        view.redo();
        check (view.midiClipNotes (1, 0).size() == 2,
               "redo restores piano-roll note edits");
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    std::cout << "StemDeck arranger editing contract\n";
    StemDeckArrangementTestAccess::run();
    std::cout << (failures == 0 ? "ARRANGEMENT_EDITING_OK\n"
                                : "ARRANGEMENT_EDITING_FAILED\n");
    return failures == 0 ? 0 : 1;
}
