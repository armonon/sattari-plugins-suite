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

// A track's four separated stems + the summed mix, with a real energy arc
// (intro -> build -> drop -> breakdown -> outro) so structure detection has
// something to find and the stem waveforms differ.
struct TrackAudio { std::array<juce::AudioBuffer<float>, 4> stems; juce::AudioBuffer<float> full; };

static float sectionGain (double frac)   // frac = position 0..1 through the track
{
    if (frac < 0.20) return 0.30f;                                   // intro
    if (frac < 0.40) return 0.30f + (float) ((frac - 0.20) / 0.20) * 0.70f;   // build
    if (frac < 0.70) return 1.00f;                                   // drop
    if (frac < 0.85) return 0.32f;                                   // breakdown
    return 0.32f * (1.0f - (float) ((frac - 0.85) / 0.15));          // outro fade
}

static TrackAudio makeTrackAudio (double sr, double bpm, double baseHz, double secs)
{
    const int n = (int) (sr * secs);
    TrackAudio ta;
    for (auto& s : ta.stems) { s.setSize (2, n); s.clear(); }
    ta.full.setSize (2, n); ta.full.clear();
    const double beat = sr * 60.0 / bpm;

    auto add = [&] (juce::AudioBuffer<float>& b, int i, float v) { b.addSample (0, i, v); b.addSample (1, i, v); };

    // drums (0): kicks, scaled by section energy.
    for (double t = 0; t < n; t += beat)
        for (int i = 0; i < (int) (sr * 0.1) && (int) t + i < n; ++i)
        {
            const int idx = (int) t + i; const float g = sectionGain ((double) idx / n);
            add (ta.stems[0], idx, 0.8f * g * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * i / (float) sr) * std::exp (-(float) i / (float) (sr * 0.03)));
        }
    for (int i = 0; i < n; ++i)
    {
        const float g = sectionGain ((double) i / n);
        add (ta.stems[1], i, 0.30f * (0.4f + 0.6f * g) * std::sin (2.0f * juce::MathConstants<float>::pi * 82.0f * i / (float) sr));   // bass
        add (ta.stems[2], i, 0.18f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) baseHz * i / (float) sr));               // music pad
        const float vg = g > 0.85f ? 0.22f : 0.0f;                                                                                      // vocals only in the drop
        add (ta.stems[3], i, vg * std::sin (2.0f * juce::MathConstants<float>::pi * (float) baseHz * 2.0f * i / (float) sr));
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        { float sum = 0; for (auto& s : ta.stems) sum += s.getSample (c, i); ta.full.setSample (c, i, sum); }
    return ta;
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
    auto trackA = makeTrackAudio (44100.0, 124.0, 220.0, 40.0);
    auto trackB = makeTrackAudio (44100.0, 124.0, 330.0, 40.0);
    auto pick = [&] (const juce::String& id) -> TrackAudio& { return id == "A" ? trackA : trackB; };
    view.setStemProvider ([&] (const juce::String& id, double& rate)
    {
        rate = 44100.0; auto& t = pick (id);
        return std::array<const juce::AudioBuffer<float>*, 4> { &t.stems[0], &t.stems[1], &t.stems[2], &t.stems[3] };
    });
    view.setStructureProvider ([&] (const juce::String& id)
    { return stemdeck::segmentStructure (pick (id).full, 44100.0, 124.0); });
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
