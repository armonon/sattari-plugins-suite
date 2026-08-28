// Contract for the tap-drum loop (SattariDrumLooper.h).
//
// Locks the looper feel: recorded hits replay once per cycle at their phase, the loop
// boundary wrap drops/doubles nothing, disabled = silent, overdub adds hits, and a full
// pass around the loop reproduces exactly what was played.
#include <sattari_audio_core/SattariDrumLooper.h>

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
using sattari::dsp::DrumLooper;

// Play the loop for one full cycle in `blocks` steps, collecting every fired pad.
std::vector<int> playOneCycle (const DrumLooper& lp, int blocks)
{
    std::vector<int> fired;
    DrumLooper::Hit buf[64];
    double prev = 0.0;
    for (int b = 1; b <= blocks; ++b)
    {
        const double cur = (double) b / (double) blocks;   // 0<..<=1
        const int n = lp.collect (prev, cur, buf, 64);
        for (int i = 0; i < n; ++i) fired.push_back (buf[i].pad);
        prev = cur;
    }
    return fired;
}
} // namespace

int main()
{
    std::printf ("SattariAudioCore drum-looper contract\n");

    // 1) Disabled looper fires nothing.
    {
        DrumLooper lp;
        lp.recordHit (0, 1.0f, 0.25);
        DrumLooper::Hit buf[8];
        check (lp.collect (0.0, 1.0, buf, 8) == 0, "disabled looper is silent");
    }

    // 2) Four hits replay exactly once each per cycle, in phase order across blocks.
    {
        DrumLooper lp;
        lp.recordHit (0, 1.0f, 0.00);   // kick on the 1
        lp.recordHit (2, 0.8f, 0.25);   // hat
        lp.recordHit (1, 0.9f, 0.50);   // snare
        lp.recordHit (2, 0.8f, 0.75);   // hat
        lp.setEnabled (true);
        const auto fired = playOneCycle (lp, 8);   // 8 blocks over the cycle
        check (fired.size() == 4, "all 4 hits fire once per cycle (got " + std::to_string (fired.size()) + ")");
        // Phase order: 0.0(kick,0) 0.25(hat,2) 0.5(snare,1) 0.75(hat,2)
        check (fired.size() == 4 && fired[0] == 0 && fired[1] == 2 && fired[2] == 1 && fired[3] == 2,
               "hits fire in recorded phase order");
    }

    // 3) The loop boundary wrap: a hit near phase 0 fires when the block spans the wrap,
    //    and is not dropped or doubled.
    {
        DrumLooper lp;
        lp.recordHit (0, 1.0f, 0.01);   // just after the downbeat
        lp.recordHit (1, 1.0f, 0.99);   // just before the loop end
        lp.setEnabled (true);
        DrumLooper::Hit buf[8];
        // A block that crosses the boundary (0.95 -> 0.05) must catch BOTH.
        const int n = lp.collect (0.95, 0.05, buf, 8);
        check (n == 2, "wrapping block catches both boundary hits (got " + std::to_string (n) + ")");
        // Over two full cycles each hit fires exactly twice — no drop/double at the seam.
        const auto c1 = playOneCycle (lp, 10);
        const auto c2 = playOneCycle (lp, 10);
        check (c1.size() == 2 && c2.size() == 2, "each cycle reproduces exactly the 2 hits");
    }

    // 4) Overdub: playing while looping adds to the loop.
    {
        DrumLooper lp;
        lp.recordHit (0, 1.0f, 0.0);
        lp.setEnabled (true);
        check (lp.numHits() == 1, "one hit before overdub");
        lp.recordHit (1, 1.0f, 0.5);   // overdubbed live
        check (lp.numHits() == 2, "overdub adds a hit");
        check (playOneCycle (lp, 8).size() == 2, "both hits now loop");
    }

    // 5) Clear wipes the loop.
    {
        DrumLooper lp;
        lp.recordHit (0, 1.0f, 0.3);
        lp.setEnabled (true);
        lp.clear();
        check (lp.numHits() == 0, "clear empties the loop");
        check (playOneCycle (lp, 8).empty(), "cleared loop is silent");
    }

    // 6) Capacity is bounded (no overflow / no allocation).
    {
        DrumLooper lp;
        for (int i = 0; i < DrumLooper::kMaxHits + 50; ++i)
            lp.recordHit (0, 1.0f, (double) i / (double) (DrumLooper::kMaxHits + 50));
        check (lp.numHits() == DrumLooper::kMaxHits, "hit buffer is capacity-bounded");
        check (lp.full(), "reports full at capacity");
    }

    std::printf (failures == 0 ? "drum-looper contract: PASS\n" : "drum-looper contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
