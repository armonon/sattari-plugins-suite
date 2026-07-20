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

static juce::AudioBuffer<float> makeTrack (double sr, double bpm, double hz, double secs)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const double beat = sr * 60.0 / bpm;
    for (double t = 0; t < n; t += beat)
        for (int i = 0; i < (int) (sr * 0.1) && (int) t + i < n; ++i)
        { const float v = 0.7f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * i / (float) sr) * std::exp (-(float) i / (float) (sr * 0.03)); b.addSample (0, (int) t + i, v); b.addSample (1, (int) t + i, v); }
    for (int i = 0; i < n; ++i) { const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) hz * i / (float) sr); b.addSample (0, i, v); b.addSample (1, i, v); }
    return b;
}

static SessionModel demoSession()
{
    SessionCapture cap; cap.begin (44100.0, 124.0);
    cap.recordDeckLoad (0, { "A", "/demo/opening-track.wav", "ha", 40, 124, 0, 0, false }, 0.0);
    cap.recordParam (0, "cfSide", 0.0, 0.0);
    cap.recordParam (0, "fader", 1.0, 0.0);
    cap.recordParam (0, "crossfader", 0.0, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordDeckLoad (1, { "B", "/demo/incoming-track.wav", "hb", 40, 124, 0, 0, false }, 32.0);
    cap.recordParam (1, "cfSide", 2.0, 32.0);
    cap.recordParam (1, "fader", 1.0, 32.0);
    cap.recordTransport (1, TransportKind::play, 0.0, 32.0);
    for (int k = 0; k <= 8; ++k)   // the transition: crossfade A->B, roll off A's lows
    {
        const double beat = 32.0 + k * 2.0;
        cap.recordParam (0, "crossfader", k / 8.0, beat);
        cap.recordParam (0, "low", -12.0 * k / 8.0, beat);
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
    auto trackA = makeTrack (44100.0, 124.0, 220.0, 40.0);
    auto trackB = makeTrack (44100.0, 124.0, 330.0, 40.0);
    view.setAudioProvider ([&] (const juce::String& id, double& rate) { rate = 44100.0; return id == "A" ? &trackA : &trackB; });
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
