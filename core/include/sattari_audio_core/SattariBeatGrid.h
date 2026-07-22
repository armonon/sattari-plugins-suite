#pragma once

#include <array>
#include <cstdint>

// =============================================================================
//  SattariBeatGrid — an editable, loopable step grid for the beat machine.
// -----------------------------------------------------------------------------
//  Lanes x steps, each cell carrying on/off + velocity + probability. The active
//  step count IS the loop length (16 = one bar of 16ths, 32 = two bars, ...), and
//  it loops until the user changes it — all editable live. Many lanes so a single
//  instance can stack lots of sounds. Probability lets a cell fire only some of the
//  time (humanised / evolving grooves) via an externally-supplied random draw, so
//  the grid itself stays deterministic and testable.
//
//  Pure-std + fixed capacity (no allocation), so it is contract-tested and safe to
//  read from the audio thread.
// =============================================================================

namespace sattari::dsp
{

class BeatGrid
{
public:
    static constexpr int kMaxLanes = 16;
    static constexpr int kMaxSteps = 64;

    BeatGrid() { clear(); }

    void setLanes (int n) noexcept { lanes_ = clampInt (n, 1, kMaxLanes); }
    void setSteps (int n) noexcept { steps_ = clampInt (n, 1, kMaxSteps); }   // loop length
    int  lanes() const noexcept { return lanes_; }
    int  steps() const noexcept { return steps_; }

    void clear() noexcept
    {
        for (auto& lane : cells_) for (auto& c : lane) c = Cell{};
    }
    void clearLane (int lane) noexcept
    {
        if (validLane (lane)) for (auto& c : cells_[(size_t) lane]) c = Cell{};
    }

    void set (int lane, int step, bool on, float vel = 0.8f, float prob = 1.0f) noexcept
    {
        if (! valid (lane, step)) return;
        Cell& c = cells_[(size_t) lane][(size_t) step];
        c.on = on; c.velocity = clamp01 (vel); c.probability = clamp01 (prob);
    }
    void toggle (int lane, int step) noexcept
    {
        if (! valid (lane, step)) return;
        Cell& c = cells_[(size_t) lane][(size_t) step];
        c.on = ! c.on;
        if (c.on && c.velocity <= 0.0f) c.velocity = 0.8f;   // sensible default on first enable
    }
    void setVelocity   (int lane, int step, float v) noexcept { if (valid (lane, step)) cells_[(size_t) lane][(size_t) step].velocity   = clamp01 (v); }
    void setProbability (int lane, int step, float p) noexcept { if (valid (lane, step)) cells_[(size_t) lane][(size_t) step].probability = clamp01 (p); }

    bool  on          (int lane, int step) const noexcept { return valid (lane, step) && cells_[(size_t) lane][(size_t) step].on; }
    float velocity    (int lane, int step) const noexcept { return valid (lane, step) ? cells_[(size_t) lane][(size_t) step].velocity    : 0.0f; }
    float probability (int lane, int step) const noexcept { return valid (lane, step) ? cells_[(size_t) lane][(size_t) step].probability : 0.0f; }

    // Does this cell fire on this pass? On, and (probability 1) OR (a supplied uniform
    // draw in [0,1) falls under the probability). draw is caller-owned so the grid is
    // deterministic under test and the RNG never lives on the audio thread's hot state.
    bool fires (int lane, int step, float draw) const noexcept
    {
        if (! on (lane, step)) return false;
        const float p = cells_[(size_t) lane][(size_t) step].probability;
        return p >= 1.0f || draw < p;
    }

    // Wrap an absolute step index into the active loop length.
    int wrapStep (int absStep) const noexcept
    {
        int s = absStep % steps_;
        return s < 0 ? s + steps_ : s;
    }

private:
    struct Cell { bool on = false; float velocity = 0.0f; float probability = 1.0f; };

    static int   clampInt (int x, int lo, int hi) noexcept { return x < lo ? lo : (x > hi ? hi : x); }
    static float clamp01  (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    bool validLane (int lane) const noexcept { return lane >= 0 && lane < kMaxLanes; }
    bool valid (int lane, int step) const noexcept { return lane >= 0 && lane < kMaxLanes && step >= 0 && step < kMaxSteps; }

    std::array<std::array<Cell, (size_t) kMaxSteps>, (size_t) kMaxLanes> cells_ {};
    int lanes_ = 8;
    int steps_ = 16;
};

} // namespace sattari::dsp
