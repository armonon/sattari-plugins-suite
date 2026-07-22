// Contract for the editable step grid (SattariBeatGrid.h).
//
// Locks the sequencer behaviours: cells hold on/velocity/probability, the step count is
// the loop length and wraps, probability gates firing deterministically from a supplied
// draw, edits are bounds-safe, and clear works per-lane and whole-grid.
#include <sattari_audio_core/SattariBeatGrid.h>

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
    using sattari::dsp::BeatGrid;
    std::printf ("SattariAudioCore beat-grid contract\n");

    // 1) Edit + query a cell.
    {
        BeatGrid g;
        g.set (0, 0, true, 1.0f, 1.0f);
        g.set (1, 4, true, 0.5f, 0.75f);
        check (g.on (0, 0),                    "cell (0,0) is on");
        check (! g.on (0, 1),                  "cell (0,1) is off");
        check (g.velocity (1, 4) == 0.5f,      "cell velocity stored");
        check (g.probability (1, 4) == 0.75f,  "cell probability stored");
        g.toggle (0, 0);
        check (! g.on (0, 0), "toggle turns a cell off");
        g.toggle (2, 2);
        check (g.on (2, 2) && g.velocity (2, 2) > 0.0f, "toggle-on gives a sensible default velocity");
    }

    // 2) Step count is the loop length and wraps.
    {
        BeatGrid g;
        g.setSteps (32);
        check (g.steps() == 32, "loop length settable (32 = two bars)");
        check (g.wrapStep (33) == 1, "absolute step wraps into the loop");
        check (g.wrapStep (-1) == 31, "negative step wraps");
        g.setSteps (999);
        check (g.steps() == BeatGrid::kMaxSteps, "loop length clamps to capacity");
        g.setSteps (0);
        check (g.steps() == 1, "loop length has a floor of 1");
    }

    // 3) Probability gates firing deterministically from the supplied draw.
    {
        BeatGrid g;
        g.set (0, 0, true, 1.0f, 1.0f);   // always
        g.set (0, 1, true, 1.0f, 0.5f);   // 50%
        g.set (0, 2, true, 1.0f, 0.0f);   // never (prob 0)
        check (g.fires (0, 0, 0.99f), "prob 1.0 fires for any draw");
        check (g.fires (0, 1, 0.30f), "prob 0.5 fires when draw < 0.5");
        check (! g.fires (0, 1, 0.70f), "prob 0.5 does not fire when draw >= 0.5");
        check (! g.fires (0, 2, 0.0f), "prob 0.0 never fires");
        check (! g.fires (0, 5, 0.0f), "an off cell never fires");
    }

    // 4) Many lanes for stacking lots of sounds.
    {
        BeatGrid g;
        g.setLanes (16);
        check (g.lanes() == 16, "up to 16 lanes for stacked sounds");
        g.setLanes (100);
        check (g.lanes() == BeatGrid::kMaxLanes, "lane count clamps to capacity");
    }

    // 5) Bounds safety: out-of-range edits/queries never crash or report on.
    {
        BeatGrid g;
        g.set (-1, 0, true);
        g.set (0, -1, true);
        g.set (999, 999, true);
        check (! g.on (-1, 0) && ! g.on (0, -1) && ! g.on (999, 999), "out-of-range cells stay off");
        check (g.velocity (999, 0) == 0.0f, "out-of-range query is safe");
    }

    // 6) Clear per-lane and whole-grid.
    {
        BeatGrid g;
        g.set (0, 0, true); g.set (0, 8, true); g.set (1, 0, true);
        g.clearLane (0);
        check (! g.on (0, 0) && ! g.on (0, 8), "clearLane empties one lane");
        check (g.on (1, 0), "clearLane leaves other lanes");
        g.clear();
        check (! g.on (1, 0), "clear empties the whole grid");
    }

    std::printf (failures == 0 ? "beat-grid contract: PASS\n" : "beat-grid contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
