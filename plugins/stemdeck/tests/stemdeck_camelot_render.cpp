// Render the Camelot wheel + assert the Camelot math round-trips, before wiring the
// interactive control into the deck.
#include "CamelotWheel.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    using namespace stemdeck;

    // ── Math checks ─────────────────────────────────────────────────────────
    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    { std::cout << (ok ? "  OK    " : "  FAIL  ") << what << "\n"; if (! ok) ++fails; };

    // camelotNumber <-> pitchClass must round-trip for every slot.
    bool rt = true;
    for (int minor = 0; minor <= 1; ++minor)
        for (int n = 1; n <= 12; ++n)
        {
            const int pc = pitchClassForCamelot (n, minor == 1);
            const int n2 = camelotNumberFor (pc, minor == 1);
            if (n2 != n) rt = false;
        }
    check (rt, "camelot number <-> pitch class round-trips for all 24 slots");
    // A minor (pc 9, minor) is 8A; C major (pc 0) is 8B.
    check (camelotNumberFor (9, true) == 8,  "A minor is 8A");
    check (camelotNumberFor (0, false) == 8, "C major is 8B");
    // 8A's compatible set: 8A, 8B (relative), 7A, 9A.
    check (slotsCompatible ({8,true}, {8,false}), "8A compatible with relative 8B");
    check (slotsCompatible ({8,true}, {7,true}),  "8A compatible with 7A");
    check (slotsCompatible ({8,true}, {9,true}),  "8A compatible with 9A");
    check (! slotsCompatible ({8,true}, {2,true}),"8A clashes with 2A (across the wheel)");

    // ── Render ───────────────────────────────────────────────────────────────
    const int W = 360, H = 360;
    juce::Image img (juce::Image::ARGB, W, H, true);
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0B0B0D));
        std::vector<CamelotDeckMark> others {
            { {6, false}, juce::Colour (0xff4A9EFF), 1, true },   // deck B at 6B
            { {9, true},  juce::Colour (0xffB47AFF), 2, true },   // deck C at 9A
        };
        drawCamelotWheel (g, juce::Rectangle<float> (10, 10, W - 20, H - 20),
                          { 8, true }, true, others, {}, false);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("camelot.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    std::cout << (fails == 0 ? "CAMELOT_OK" : "CAMELOT_FAIL") << "\n";
    return fails == 0 ? 0 : 1;
}
