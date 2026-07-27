// Multi-instance: a DAW can host more than one StemDeck in a single process.
//
// Found while measuring A3: constructing a second MainPluginProcessor after the first
// was destroyed SIGSEGVs. This isolates which of the three shapes actually breaks, so
// the fix targets the real one instead of guessing:
//
//   1. sequential  — construct, destroy, construct again
//   2. concurrent  — two alive at once
//   3. prepared    — two alive and both prepareToPlay'd
//
// Prints a marker before and after each step so a crash names its own step.
#include "MainPluginProcessor.h"
#include <iostream>
#include <memory>

namespace { void step (const char* s) { std::cout << "  ... " << s << std::endl; } }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "Multi-instance probe" << std::endl;

    step ("1a: construct first processor");
    { auto a = std::make_unique<MainPluginProcessor>(); step ("1b: destroy first"); }
    step ("1c: first destroyed cleanly");

    step ("1d: construct second AFTER first destroyed");
    { auto b = std::make_unique<MainPluginProcessor>(); step ("1e: second constructed"); }
    step ("1f: SEQUENTIAL OK");

    step ("2a: construct two CONCURRENTLY");
    {
        auto a = std::make_unique<MainPluginProcessor>();
        step ("2b: first up");
        auto b = std::make_unique<MainPluginProcessor>();
        step ("2c: CONCURRENT OK");

        step ("3a: prepare both");
        a->prepareToPlay (48000.0, 512);
        b->prepareToPlay (48000.0, 512);
        step ("3b: PREPARED OK");

        // Run a few blocks through both — the graph snapshot and any process-wide
        // singleton are what a real DAW would exercise.
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 8; ++i)
        {
            buf.clear(); a->processBlock (buf, midi);
            buf.clear(); b->processBlock (buf, midi);
        }
        step ("3c: BOTH PROCESSED OK");

        // ── Does Tracktion contribute ANY audio? ─────────────────────────────
        // Instance `a` owns the engine; `b` was refused it. Static analysis says the
        // Edit is never populated (createSingleTrackEdit, no clip/track inserts) and
        // usingTeMidi is never set true — but that is inference. Feed both identical
        // input and compare: if the engine contributed anything, the outputs differ.
        step ("5a: compare engine-owner vs engine-less output");
        juce::AudioBuffer<float> ba (2, 512), bb (2, 512);
        double maxDiff = 0.0;
        for (int blk = 0; blk < 64; ++blk)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.25f * std::sin ((float) (blk * 512 + i) * 0.01f);
                    ba.setSample (ch, i, v);
                    bb.setSample (ch, i, v);
                }
            juce::MidiBuffer ma, mb;
            a->processBlock (ba, ma);
            b->processBlock (bb, mb);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    maxDiff = juce::jmax (maxDiff, (double) std::abs (ba.getSample (ch, i) - bb.getSample (ch, i)));
        }
        std::cout << "    max |withEngine - withoutEngine| = " << maxDiff << std::endl;
        std::cout << (maxDiff == 0.0
                        ? "    -> Tracktion contributes NOTHING to the mix"
                        : "    -> Tracktion DOES contribute audio; it is load-bearing")
                  << std::endl;
    }
    step ("4: both destroyed");

    std::cout << "MULTIINSTANCE_OK" << std::endl;
    std::cout.flush();
    std::_Exit (0);
}
