#pragma once

#include <cstdint>
#include <cstdlib>    // std::llabs — NOT transitively provided by libstdc++ (GCC/Linux)
#include <utility>    // std::pair (boundaries)
#include <vector>
#include <algorithm>

// =============================================================================
//  SattariSliceTable — the slice model for Sattari Chop (and StemDeck's chopper).
// -----------------------------------------------------------------------------
//  This is a ONE-WAY-DOOR schema (Chop spec §10): everything downstream — pad
//  mapping, per-slice params, the event stream — is built on it, so it is worth
//  getting right and locking with a contract.
//
//  The load-bearing behaviour (§2.3): ANY marker the user places, moves, or keeps
//  is LOCKED and survives re-detection. A marker the user deletes leaves a
//  tombstone that stays deleted even if detection would place one there. Sensitivity
//  changes only add/remove UNLOCKED markers. Destroying hand edits on re-detect is
//  the single behaviour that makes people abandon auto-choppers — so it is a rule
//  here, not a nicety.
//
//  Pure-std, allocation on edits only (never read on the audio thread — publish an
//  immutable snapshot via active() and swap a pointer, §5).
// =============================================================================

namespace sattari::dsp
{

class SliceTable
{
public:
    struct Marker
    {
        std::int64_t samplePos  = 0;
        bool         locked      = false;   // user-placed or user-moved: survives re-detection
        bool         userDeleted = false;   // tombstone: suppressed even if detection finds it here
        float        strength    = 0.0f;    // detection confidence, 0..1 (0 for hand-placed)
    };

    struct Candidate { std::int64_t samplePos = 0; float strength = 0.0f; };

    void clear() noexcept { markers_.clear(); }
    void setMergeTolerance (std::int64_t samples) noexcept { mergeTol_ = samples > 0 ? samples : 1; }
    std::int64_t mergeTolerance() const noexcept { return mergeTol_; }

    // --- User gestures (the §3 grammar) --------------------------------------
    // Click: add a locked marker. If one already sits within tolerance, lock THAT one
    // instead of stacking a duplicate (and clear any tombstone there).
    void addMarker (std::int64_t pos)
    {
        if (int i = indexNear (pos); i >= 0)
        {
            markers_[(size_t) i].locked = true;
            markers_[(size_t) i].userDeleted = false;
            markers_[(size_t) i].samplePos = pos;
        }
        else
        {
            markers_.push_back ({ pos, true, false, 0.0f });
            sort();
        }
    }

    // Drag: grab the nearest active marker and move it — the move locks it.
    void moveMarkerNear (std::int64_t fromPos, std::int64_t toPos)
    {
        const int i = indexNearActive (fromPos);
        if (i < 0) return;
        markers_[(size_t) i].samplePos = toPos;
        markers_[(size_t) i].locked = true;
        markers_[(size_t) i].userDeleted = false;
        sort();
    }

    // Drag off: grab the nearest active marker and delete it — leaves a tombstone so
    // detection will not resurrect it.
    void deleteMarkerNear (std::int64_t pos)
    {
        const int i = indexNearActive (pos);
        if (i < 0) return;
        markers_[(size_t) i].userDeleted = true;
        markers_[(size_t) i].locked = false;
    }

    // --- Detection / sensitivity ---------------------------------------------
    // Re-threshold: rebuild the UNLOCKED auto markers from the candidates at/above
    // `threshold`, while (1) keeping every locked marker and (2) never placing one
    // within tolerance of a locked marker or a userDeleted tombstone. Cheap enough
    // to run per sensitivity-drag frame (§2.2) once the onset curve is computed once.
    void applyDetection (const std::vector<Candidate>& candidates, float threshold)
    {
        // Drop the previous auto markers; keep locked + tombstones.
        markers_.erase (std::remove_if (markers_.begin(), markers_.end(),
                        [] (const Marker& m) { return ! m.locked && ! m.userDeleted; }),
                        markers_.end());

        for (const auto& c : candidates)
        {
            if (c.strength < threshold) continue;
            if (blockedAt (c.samplePos)) continue;          // near a locked marker or a tombstone
            markers_.push_back ({ c.samplePos, false, false, c.strength });
        }
        sort();
    }

    // "Reset to detected" (§2.3): discards locked markers AND tombstones — a
    // destructive action the UI must confirm. Applies a fresh detection.
    void resetToDetected (const std::vector<Candidate>& candidates, float threshold)
    {
        markers_.clear();
        applyDetection (candidates, threshold);
    }

    // --- Query ----------------------------------------------------------------
    // Active markers (tombstones excluded), sorted ascending. This is the snapshot the
    // rest of the engine and the renderer consume.
    std::vector<Marker> active() const
    {
        std::vector<Marker> out;
        out.reserve (markers_.size());
        for (const auto& m : markers_) if (! m.userDeleted) out.push_back (m);
        return out;
    }

    int activeCount() const noexcept
    {
        int n = 0; for (const auto& m : markers_) if (! m.userDeleted) ++n; return n;
    }

    int lockedCount() const noexcept
    {
        int n = 0; for (const auto& m : markers_) if (m.locked && ! m.userDeleted) ++n; return n;
    }

    int tombstoneCount() const noexcept
    {
        int n = 0; for (const auto& m : markers_) if (m.userDeleted) ++n; return n;
    }

    // Slice [start,end) boundaries from the active markers over a source of `totalLen`
    // samples. Boundaries can be edited independently later (§3.4); this is the default
    // contiguous slicing (marker N .. marker N+1, last .. end).
    std::vector<std::pair<std::int64_t, std::int64_t>> boundaries (std::int64_t totalLen) const
    {
        std::vector<std::pair<std::int64_t, std::int64_t>> out;
        auto a = active();
        for (size_t i = 0; i < a.size(); ++i)
        {
            const std::int64_t s = a[i].samplePos;
            const std::int64_t e = (i + 1 < a.size()) ? a[i + 1].samplePos : totalLen;
            if (e > s) out.push_back ({ s, e });
        }
        return out;
    }

private:
    void sort()
    {
        std::sort (markers_.begin(), markers_.end(),
                   [] (const Marker& a, const Marker& b) { return a.samplePos < b.samplePos; });
    }

    // Index of any marker (incl. tombstones) within tolerance of pos; -1 if none.
    int indexNear (std::int64_t pos) const
    {
        int best = -1; std::int64_t bestD = mergeTol_ + 1;
        for (int i = 0; i < (int) markers_.size(); ++i)
        {
            const std::int64_t d = std::llabs (markers_[(size_t) i].samplePos - pos);
            if (d <= mergeTol_ && d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    // Index of the nearest ACTIVE marker to pos (no tolerance cap — the user grabbed near it).
    int indexNearActive (std::int64_t pos) const
    {
        int best = -1; std::int64_t bestD = 0;
        for (int i = 0; i < (int) markers_.size(); ++i)
        {
            if (markers_[(size_t) i].userDeleted) continue;
            const std::int64_t d = std::llabs (markers_[(size_t) i].samplePos - pos);
            if (best < 0 || d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    // Is a detected onset at pos blocked by a locked marker or a tombstone nearby?
    bool blockedAt (std::int64_t pos) const
    {
        for (const auto& m : markers_)
            if ((m.locked || m.userDeleted) && std::llabs (m.samplePos - pos) <= mergeTol_)
                return true;
        return false;
    }

    std::vector<Marker> markers_;
    std::int64_t mergeTol_ = 64;   // "same onset" window in samples (~1.3 ms @ 48k); UI sets it
};

} // namespace sattari::dsp
