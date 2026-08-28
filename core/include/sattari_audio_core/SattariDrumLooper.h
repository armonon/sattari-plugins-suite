#pragma once

#include <array>
#include <cstddef>   // std::size_t (libstdc++ does not leak ::size_t)
#include <cstdint>

// =============================================================================
//  SattariDrumLooper — a live, feel-preserving loop for the tap-drum pads.
// -----------------------------------------------------------------------------
//  While the user plays pads, every hit is recorded at its position within the
//  loop (a phase in [0, 1), so it is tempo-independent and locks to the host).
//  Hitting Loop replays those hits every cycle — the last set bars repeat, a
//  looper feel. Playing while looping OVERDUBS (new hits join the loop); Clear
//  wipes it. Unlike the step grid this keeps the actual timing/groove, not a
//  quantised grid.
//
//  The processor drives it per block: recordHit() on a live trigger, then
//  collect() over the block's phase span to fire the recorded hits. collect()
//  handles the wrap at the loop boundary so nothing is dropped or doubled.
//
//  Pure-std + allocation-free (fixed hit capacity), so it is contract-tested.
// =============================================================================

namespace sattari::dsp
{

class DrumLooper
{
public:
    static constexpr int kMaxHits = 1024;

    struct Hit { int pad; float velocity; double phase; };

    void reset() noexcept { count_ = 0; enabled_ = false; }
    void clear() noexcept { count_ = 0; }

    void setEnabled (bool on) noexcept { enabled_ = on; }
    bool enabled() const noexcept { return enabled_; }
    int  numHits() const noexcept { return count_; }
    bool full() const noexcept { return count_ >= kMaxHits; }

    // Record a live pad hit at the current loop phase (wrapped into [0,1)).
    void recordHit (int pad, float velocity, double phase) noexcept
    {
        if (count_ >= kMaxHits) return;
        hits_[(std::size_t) count_++] = { pad, velocity, wrap01 (phase) };
    }

    // Collect recorded hits whose phase lies in the half-open span [from, to), i.e. hits
    // the playhead CROSSES advancing from `from` to `to`. Half-open at `from` so a hit on
    // the downbeat (phase 0) fires at the START of the cycle, not the end. Handles the
    // wrap when to <= from (block crossed the loop boundary; to == 0 means "up to the loop
    // end"). Writes up to maxOut hits into out; returns the count.
    int collect (double from, double to, Hit* out, int maxOut) const noexcept
    {
        if (! enabled_ || count_ == 0 || maxOut <= 0) return 0;
        from = wrap01 (from);
        to   = wrap01 (to);
        int n = 0;
        for (int i = 0; i < count_ && n < maxOut; ++i)
        {
            const double p = hits_[(std::size_t) i].phase;
            const bool due = (from < to) ? (p >= from && p < to)          // no wrap
                                         : (p >= from || p < to);         // wrapped across 0
            if (due) out[n++] = hits_[(std::size_t) i];
        }
        return n;
    }

private:
    static double wrap01 (double p) noexcept
    {
        p -= (double) (long long) p;      // fractional part
        if (p < 0.0) p += 1.0;
        return p;
    }

    std::array<Hit, (std::size_t) kMaxHits> hits_ {};
    int  count_ = 0;
    bool enabled_ = false;
};

} // namespace sattari::dsp
