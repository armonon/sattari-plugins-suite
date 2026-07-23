// Contract for the Chop slice model (SattariSliceTable.h) — the one-way-door schema.
//
// Locks the behaviour that makes or breaks an auto-chopper (spec §2.3): locked markers
// survive re-detection, deleted markers stay deleted (tombstones), sensitivity only
// touches UNLOCKED markers, and "reset to detected" is the only thing that discards
// hand edits. If any of these regress, hand editing is destroyed on the next slider move.
#include <sattari_audio_core/SattariSliceTable.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}
using sattari::dsp::SliceTable;

// A fixed candidate set standing in for detected onsets at various strengths.
std::vector<SliceTable::Candidate> candidates()
{
    return { {1000, 0.9f}, {2000, 0.8f}, {3000, 0.5f}, {4000, 0.35f}, {5000, 0.95f} };
}

bool hasActiveNear (const SliceTable& t, std::int64_t pos, std::int64_t tol)
{
    for (const auto& m : t.active())
        if (std::llabs (m.samplePos - pos) <= tol) return true;
    return false;
}
}

int main()
{
    std::printf ("SattariAudioCore slice-table contract\n");

    // 1) Detection populates unlocked markers; a higher threshold yields fewer.
    {
        SliceTable t;
        t.applyDetection (candidates(), 0.4f);
        const int lots = t.activeCount();
        t.applyDetection (candidates(), 0.85f);
        const int few = t.activeCount();
        check (lots > few, "higher sensitivity threshold -> fewer auto markers (" + std::to_string (lots) + " -> " + std::to_string (few) + ")");
        check (t.lockedCount() == 0, "auto markers are not locked");
    }

    // 2) A hand-placed marker is locked and present.
    {
        SliceTable t;
        t.addMarker (12345);
        check (t.activeCount() == 1 && t.lockedCount() == 1, "addMarker creates one locked, active marker");
        check (hasActiveNear (t, 12345, 0), "the marker is at the clicked position");
    }

    // 3) THE load-bearing rule: a locked marker survives re-detection at any sensitivity.
    {
        SliceTable t;
        t.addMarker (7777);                         // hand-placed, far from any candidate
        t.applyDetection (candidates(), 0.4f);      // re-detect
        check (hasActiveNear (t, 7777, 0), "locked marker survives detection (dense)");
        t.applyDetection (candidates(), 0.99f);     // crank sensitivity the other way
        check (hasActiveNear (t, 7777, 0), "locked marker survives detection (sparse)");
        check (t.lockedCount() == 1, "still exactly one locked marker");
    }

    // 4) A deleted marker stays deleted even when detection would place one there.
    {
        SliceTable t;
        t.applyDetection (candidates(), 0.4f);      // places an auto marker at 1000
        check (hasActiveNear (t, 1000, 64), "detected onset at 1000 present before delete");
        t.deleteMarkerNear (1000);                  // user drags it off
        check (! hasActiveNear (t, 1000, 64), "deleted onset gone");
        t.applyDetection (candidates(), 0.4f);      // re-detect — must NOT resurrect it
        check (! hasActiveNear (t, 1000, 64), "deleted onset stays deleted after re-detection (tombstone)");
        check (t.tombstoneCount() == 1, "one tombstone recorded");
    }

    // 5) Moving a marker locks it and it survives re-detection at its new spot.
    {
        SliceTable t;
        t.applyDetection (candidates(), 0.4f);
        t.moveMarkerNear (2000, 2500);              // grab the 2000 onset, nudge to 2500
        check (hasActiveNear (t, 2500, 0) && ! hasActiveNear (t, 2000, 10), "moved marker at new position");
        check (t.lockedCount() == 1, "move locks the marker");
        t.applyDetection (candidates(), 0.4f);
        check (hasActiveNear (t, 2500, 0), "moved (locked) marker survives re-detection");
    }

    // 6) Sensitivity only adds/removes UNLOCKED markers — locked count is invariant.
    {
        SliceTable t;
        t.addMarker (500);
        t.addMarker (9000);
        const int lockedBefore = t.lockedCount();
        t.applyDetection (candidates(), 0.3f);
        check (t.lockedCount() == lockedBefore, "sensitivity change preserves locked count (dense)");
        t.applyDetection (candidates(), 0.99f);
        check (t.lockedCount() == lockedBefore, "sensitivity change preserves locked count (sparse)");
    }

    // 7) "Reset to detected" discards locked markers (the destructive escape hatch).
    {
        SliceTable t;
        t.addMarker (4242);
        t.deleteMarkerNear (0);                     // no active near 0 initially; safe no-op path
        t.resetToDetected (candidates(), 0.4f);
        check (! hasActiveNear (t, 4242, 0), "reset-to-detected discards the locked marker");
        check (t.lockedCount() == 0 && t.tombstoneCount() == 0, "reset clears locked + tombstones");
    }

    // 8) Clicking within tolerance of an existing marker locks it rather than duplicating.
    {
        SliceTable t;
        t.setMergeTolerance (64);
        t.applyDetection (candidates(), 0.4f);      // auto marker at 1000
        const int before = t.activeCount();
        t.addMarker (1030);                         // within tolerance of 1000
        check (t.activeCount() == before, "click near an onset does not duplicate it");
        check (t.lockedCount() == 1, "click near an onset locks it");
    }

    // 9) Boundaries are contiguous marker..next, last..end.
    {
        SliceTable t;
        t.addMarker (0);
        t.addMarker (100);
        t.addMarker (250);
        auto b = t.boundaries (400);
        check (b.size() == 3, "three markers -> three slices");
        check (b[0].first == 0 && b[0].second == 100, "slice 0 = [0,100)");
        check (b[2].first == 250 && b[2].second == 400, "last slice runs to total length");
    }

    std::printf (failures == 0 ? "slice-table contract: PASS\n" : "slice-table contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
