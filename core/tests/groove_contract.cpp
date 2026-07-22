// Contract for the generative "machine" (SattariGroove.h).
//
// Locks the euclidean distribution, that generation produces a musical, style-correct
// groove (four-on-floor kick, backbeat snare), that Density genuinely thins/fills, that
// generation is deterministic, that mutate evolves without destroying the groove, and
// that regenerate preserves hand-placed sounds on higher lanes.
#include <sattari_audio_core/SattariGroove.h>

#include <cstdio>
#include <string>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}
using namespace sattari::dsp;

int laneHits (const BeatGrid& g, int lane)
{
    int n = 0; for (int s = 0; s < g.steps(); ++s) if (g.on (lane, s)) ++n; return n;
}
}

int main()
{
    std::printf ("SattariAudioCore groove contract\n");

    // 1) Euclidean distribution.
    {
        check (popcount64 (euclid (4, 16)) == 4, "euclid(4,16) has 4 pulses");
        check (euclid (4, 16) == (std::uint64_t) ((1<<0)|(1<<4)|(1<<8)|(1<<12)), "euclid(4,16) is on the quarter notes");
        check (popcount64 (euclid (3, 8)) == 3, "euclid(3,8) has 3 pulses");
        check (euclid (3, 8) == (std::uint64_t) ((1<<0)|(1<<3)|(1<<6)), "euclid(3,8) is the tresillo x..x..x.");
        check (euclid (0, 16) == 0, "0 pulses is empty");
        check (popcount64 (euclid (16, 16)) == 16, "n pulses over n steps is full");
        check (popcount64 (euclid (5, 16)) == 5, "euclid(5,16) has 5 pulses");
    }

    // 2) Generation is musical: four-on-the-floor kick, backbeat snare.
    {
        BeatGrid g; g.setLanes (8); g.setSteps (16);
        generate (g, GrooveStyle::FourFloor, 0.6f, 1234u);
        check (laneHits (g, 0) == 4, "four-on-floor: 4 kicks in a bar");
        check (g.on (0, 0) && g.on (0, 4) && g.on (0, 8) && g.on (0, 12), "kicks on every beat");
        check (g.on (1, 4) && g.on (1, 12), "snare on the backbeat (2 & 4)");
        check (laneHits (g, 2) > 0, "hats present");
    }

    // 3) Density thins/fills the groove.
    {
        BeatGrid a; a.setLanes (8); a.setSteps (16); generate (a, GrooveStyle::House, 0.1f, 7u);
        BeatGrid b; b.setLanes (8); b.setSteps (16); generate (b, GrooveStyle::House, 0.95f, 7u);
        const int hatsLow  = laneHits (a, 2);
        const int hatsHigh = laneHits (b, 2);
        check (hatsHigh > hatsLow, "higher Density -> more hats (" + std::to_string (hatsLow) + " -> " + std::to_string (hatsHigh) + ")");
    }

    // 4) Deterministic: same style/density/seed -> identical grid.
    {
        BeatGrid a; a.setLanes (8); a.setSteps (16); generate (a, GrooveStyle::Trap, 0.5f, 42u);
        BeatGrid b; b.setLanes (8); b.setSteps (16); generate (b, GrooveStyle::Trap, 0.5f, 42u);
        bool same = true;
        for (int l = 0; l < 4; ++l) for (int s = 0; s < 16; ++s) if (a.on (l, s) != b.on (l, s)) same = false;
        check (same, "same seed -> identical groove");
    }

    // 5) Mutate evolves without razing the groove.
    {
        BeatGrid g; g.setLanes (8); g.setSteps (16); generate (g, GrooveStyle::BoomBap, 0.5f, 3u);
        const int before = laneHits (g, 0) + laneHits (g, 1) + laneHits (g, 2);
        BeatGrid m = g;
        mutate (m, 0.5f, 99u);
        int changed = 0, total = 0;
        for (int l = 0; l < 4; ++l) for (int s = 0; s < 16; ++s) { ++total; if (g.on (l, s) != m.on (l, s)) ++changed; }
        check (changed > 0, "mutate changes something");
        check (changed < total, "mutate does not change everything (groove survives)");
        const int after = laneHits (m, 0) + laneHits (m, 1) + laneHits (m, 2);
        check (after > before / 4, "mutate keeps a recognisable amount of the groove");
    }

    // 6) Regenerate preserves hand-placed sounds on higher lanes (stacks survive).
    {
        BeatGrid g; g.setLanes (8); g.setSteps (16);
        g.set (5, 3, true, 1.0f, 1.0f);           // a hand-placed sound on lane 5
        generate (g, GrooveStyle::House, 0.7f, 1u);
        check (g.on (5, 3), "regenerate leaves lanes 4+ (stacked/hand-placed sounds) intact");
    }

    // 7) addFill adds hits at the end of the loop.
    {
        BeatGrid g; g.setLanes (8); g.setSteps (16);
        addFill (g, 1, 4, 1.0f, 5u);
        int tail = 0; for (int s = 12; s < 16; ++s) if (g.on (1, s)) ++tail;
        check (tail > 0, "addFill puts hits in the last region");
    }

    std::printf (failures == 0 ? "groove contract: PASS\n" : "groove contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
