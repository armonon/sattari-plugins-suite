// SattariArrangement — song-position contract.
//
//  The feature this enables is the one no other step sequencer has: filling
//  BEFORE the chorus rather than after it. That only works if the fill window is
//  derived from position, so these tests are about position arithmetic and the
//  edge-triggering that keeps a fill from re-arming every block.
#include <sattari_audio_core/SattariArrangement.h>

#include <cstdio>
#include <vector>

namespace
{
int fails = 0;
void check (bool ok, const char* what)
{
    if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; }
}
} // namespace

int main()
{
    std::printf ("Arrangement contract\n");
    using namespace sattari::dsp;

    // --- 1. The fill window is the LAST bar of an 8-bar phrase ---------------
    {
        ArrangementWatcher w; w.reset();
        // 8 bars of 4 beats = 32 beats. Fill = final bar => beats 28..31.
        int firstFill = -1, lastFill = -1, count = 0;
        for (int beat = 0; beat < 32; ++beat)
        {
            w.update ((double) beat, 4.0, 8.0, 4.0, 0);
            if (w.inFillWindow())
            {
                if (firstFill < 0) firstFill = beat;
                lastFill = beat; ++count;
            }
        }
        std::printf ("  8-bar phrase, 1-bar fill: beats %d..%d (%d of 32)\n",
                     firstFill, lastFill, count);
        check (firstFill == 28, "fill window did not start at the last bar");
        check (lastFill == 31,  "fill window did not run to the phrase end");
        check (count == 4,      "fill window was not exactly one bar long");
    }

    // --- 2. It repeats every phrase, not just the first ----------------------
    {
        ArrangementWatcher w; w.reset();
        int fillsInPhrase[4] = { 0, 0, 0, 0 };
        for (int beat = 0; beat < 128; ++beat)   // 4 phrases of 32 beats
        {
            w.update ((double) beat, 4.0, 8.0, 4.0, 0);
            if (w.inFillWindow()) ++fillsInPhrase[beat / 32];
        }
        bool every = true;
        for (int p = 0; p < 4; ++p) every = every && fillsInPhrase[p] == 4;
        check (every, "fill did not recur identically in every phrase");
    }

    // --- 3. Phrase change is EDGE-triggered ---------------------------------
    //  If this reported true for the whole first block of a phrase, a caller
    //  would regenerate the pattern repeatedly and the beat would never settle.
    {
        ArrangementWatcher w; w.reset();
        int edges = 0;
        for (int beat = 0; beat < 128; ++beat)
        {
            w.update ((double) beat, 4.0, 8.0, 4.0, 0);
            if (w.phraseJustChanged()) ++edges;
        }
        std::printf ("  128 beats / 32-beat phrases: %d phrase edges\n", edges);
        check (edges == 3, "phrase change should fire once per boundary");
    }

    // --- 4. Section change is edge-triggered too, and independent of phrase --
    {
        ArrangementWatcher w; w.reset();
        int edges = 0;
        for (int beat = 0; beat < 64; ++beat)
        {
            const int section = beat < 20 ? 2 : (beat < 40 ? 4 : 5);  // verse, chorus, bridge
            w.update ((double) beat, 4.0, 8.0, 4.0, section);
            if (w.sectionJustChanged()) ++edges;
        }
        check (edges == 2, "section change should fire once per change");
    }

    // --- 5. No section published (id 0) must not look like a change ---------
    {
        ArrangementWatcher w; w.reset();
        int edges = 0;
        for (int beat = 0; beat < 64; ++beat)
        {
            w.update ((double) beat, 4.0, 8.0, 4.0, 0);
            if (w.sectionJustChanged()) ++edges;
        }
        check (edges == 0, "a constant section id reported changes");
    }

    // --- 6. The seed is stable within a phrase, different between phrases ----
    //  This is what makes a generated fill identical on replay but not identical
    //  to the fill before it — the reason the generative core is seeded at all.
    {
        ArrangementWatcher w; w.reset();
        std::vector<std::uint32_t> seedsPerPhrase;
        std::uint32_t current = 0;
        bool stable = true;
        for (int beat = 0; beat < 128; ++beat)
        {
            w.update ((double) beat, 4.0, 8.0, 4.0, 0);
            const auto seed = w.phraseSeed();
            if (beat % 32 == 0) { current = seed; seedsPerPhrase.push_back (seed); }
            else if (seed != current) stable = false;
        }
        check (stable, "seed changed within a phrase");
        bool distinct = true;
        for (size_t i = 1; i < seedsPerPhrase.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                if (seedsPerPhrase[i] == seedsPerPhrase[j]) distinct = false;
        check (distinct, "consecutive phrases produced the same seed");
        check (seedsPerPhrase.size() == 4, "wrong number of phrases seen");
    }

    // --- 7. Replaying the same position gives the same answer ---------------
    //  Loop a section and it must behave identically, which is only true because
    //  everything here is a function of position.
    {
        ArrangementWatcher a, b; a.reset(); b.reset();
        for (int beat = 0; beat < 200; ++beat) a.update ((double) beat, 4.0, 8.0, 4.0, 0);
        a.update (28.0, 4.0, 8.0, 4.0, 0);
        b.update (0.0,  4.0, 8.0, 4.0, 0);
        b.update (28.0, 4.0, 8.0, 4.0, 0);
        check (a.inFillWindow() == b.inFillWindow(),
               "the same position gave different fill answers after replay");
        check (a.phraseSeed() == b.phraseSeed(),
               "the same position gave a different seed after replay");
    }

    // --- 8. A silly fill length cannot swallow the phrase -------------------
    {
        ArrangementWatcher w; w.reset();
        int count = 0;
        for (int beat = 0; beat < 32; ++beat)
        {
            w.update ((double) beat, 4.0, 8.0, /*fillBeats*/ 999.0, 0);
            if (w.inFillWindow()) ++count;
        }
        check (count <= 16, "an oversized fill window took over the whole phrase");
        check (count > 0,   "an oversized fill window disabled fills entirely");
    }

    if (fails == 0) { std::printf ("Arrangement contract ok\n"); return 0; }
    std::printf ("Arrangement contract FAILED (%d)\n", fails);
    return 1;
}
