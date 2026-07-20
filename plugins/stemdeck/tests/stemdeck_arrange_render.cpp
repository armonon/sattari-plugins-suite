// Renders the ArrangementView of a captured session to a PNG offline (no live
// window — sidesteps screen-capture). Verifies it draws without crashing and is
// non-blank, and writes an image you can actually look at.
//   StemDeckArrangeRender [out.png]      (default ~/Desktop/StemDeck-demo/arrangement.png)
#include "ArrangementView.h"
#include "SessionCapture.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

using namespace stemdeck::session;

static SessionModel demoSession()
{
    SessionCapture cap; cap.begin (44100.0, 124.0);
    cap.recordDeckLoad (0, { "A", "/demo/opening-track.wav", "ha", 40, 124, 0, 0, false }, 0.0);
    cap.recordParam (0, "fader", 1.0, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordDeckLoad (1, { "B", "/demo/incoming-track.wav", "hb", 40, 124, 0, 0, false }, 32.0);
    cap.recordParam (1, "fader", 0.0, 32.0);
    cap.recordTransport (1, TransportKind::play, 0.0, 32.0);
    for (int k = 0; k <= 8; ++k)   // the transition
    {
        cap.recordParam (0, "fader", 1.0 - k / 8.0, 32.0 + k * 2.0);
        cap.recordParam (1, "fader",       k / 8.0, 32.0 + k * 2.0);
    }
    cap.recordTransport (0, TransportKind::pause, 0.0, 50.0);
    Region tr; tr.kind = "transition"; tr.startBeat = 32.0; tr.endBeat = 48.0;
    cap.session().addRegion (tr);
    cap.recordTransport (1, TransportKind::pause, 0.0, 64.0);
    cap.end();
    return cap.session();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // needed for image/font rendering

    ArrangementView view;
    view.setSession (demoSession());
    view.setPlayheadBeat (40.0);
    const int W = 1400, H = 460;
    view.setSize (W, H);

    juce::Image img (juce::Image::ARGB, W, H, true);
    { juce::Graphics g (img); view.paintEntireComponent (g, false); }

    // Non-blank check: pixels must differ from the background corner.
    const auto bgpx = img.getPixelAt (1, 1);
    int differing = 0;
    for (int yy = 0; yy < H; yy += 7)
        for (int xx = 0; xx < W; xx += 7)
            if (img.getPixelAt (xx, yy) != bgpx) ++differing;
    const bool ok = differing > 200;
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  arrangement drew content (" << differing << " differing sample pixels)\n";

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("arrangement.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
        png.writeImageToStream (img, *os);
    std::cout << "  wrote " << out.getFullPathName() << "\n";

    std::cout << (ok ? "ARRANGE_RENDER_OK" : "ARRANGE_RENDER_FAIL") << "\n";
    return ok ? 0 : 1;
}
