// Render the §5 tempo panel (track BPM + playing-at + tempo map / retroactive lane)
// with synthetic data to verify layout before wiring.
#include "TempoPanel.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int W = 720, H = 260;
    juce::Image img (juce::Image::ARGB, W, H, true);

    stemdeck::TempoPanelModel m;
    m.trackBpm = 78.0; m.nativeBpm = 78.0; m.liveBpm = 80.0;   // pitched +2.6%
    m.confidence = 0.58f; m.source = "neural";                 // low -> amber + octave nudge
    m.dynamic = true; m.pitchRange = 8;

    for (int i = 0; i <= 40; ++i)
    {
        const float t = (float) i / 40.0f;
        m.map.push_back ({ t, 77.7 + 2.1 * (0.5 + 0.5 * std::sin (t * 9.0f)) });   // drift 77.7..79.8
    }
    m.lane = { { 0.0f, 78.0 }, { 0.45f, 78.0 }, { 0.62f, 82.0 }, { 1.0f, 82.0 } };  // drawn up across a transition
    m.playheadNorm = 0.5f;

    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0B0B0D));
        stemdeck::drawTempoPanel (g, juce::Rectangle<float> (8, 8, W - 16, H - 16), m);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("tempo.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
