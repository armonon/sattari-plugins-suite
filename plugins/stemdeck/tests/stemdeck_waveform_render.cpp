// Render the two-zone waveform region (overview + vertical stem columns) with
// synthetic peak data, to verify the §2 layout before wiring to live audio.
#include "WaveformRegion.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>
#include <cmath>

static std::vector<float> synth (int n, int kind)
{
    std::vector<float> p ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) n;
        float v = 0.0f;
        switch (kind)
        {
            case 0: v = (0.25f + 0.7f * t) * (0.4f + 0.6f * std::abs (std::sin (t * 40.0f))); break;    // overview: build
            case 1: v = (std::fmod (t * 64.0f, 1.0f) < 0.15f) ? 0.9f : 0.15f; break;                    // DRM: pulses
            case 2: v = 0.55f + 0.25f * std::sin (t * 12.0f); break;                                    // BAS: steady
            case 3: v = (std::sin (t * 6.0f) > 0.4f) ? 0.7f * std::abs (std::sin (t * 80.0f)) : 0.05f; break; // VOX: phrases
            case 4: v = 0.35f + 0.3f * std::abs (std::sin (t * 3.0f)); break;                           // OTH: pads
        }
        p[(size_t) i] = juce::jlimit (0.0f, 1.0f, v);
    }
    return p;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    using WR = stemdeck::WaveformRegion;

    const int W = 520, H = 420;
    juce::Image img (juce::Image::ARGB, W, H, true);

    WR region;
    region.setBounds (0, 0, W, H);
    region.setBpm (124.0);
    region.setTotalSeconds (210.0);
    region.setZoomBars (8);
    region.setPlayheadNorm (0.42f);
    const int N = 2048;
    region.setOverviewPeaks (synth (N, 0));
    region.setStemPeaks (0, synth (N, 3));   // VOX
    region.setStemPeaks (1, synth (N, 1));   // DRM
    region.setStemPeaks (2, synth (N, 2));   // BAS
    region.setStemPeaks (3, synth (N, 4));   // OTH
    region.setStemMuted (3, true);            // OTH muted -> dims
    region.setKeyBands ({
        { 0.00f, 0.58f, "8A", juce::Colour::fromHSV (7 / 12.0f, 0.45f, 0.7f, 1.0f) },   // A minor
        { 0.58f, 1.00f, "9A", juce::Colour::fromHSV (8 / 12.0f, 0.45f, 0.7f, 1.0f) } }); // modulates to B minor
    region.setStructure ({
        { 0.00f, 0.12f, "intro",     juce::Colour (0xff4A9EFF) },
        { 0.12f, 0.34f, "build",     juce::Colour (0xff4AD9C4) },
        { 0.34f, 0.58f, "drop",      juce::Colour (0xffD4537E) },
        { 0.58f, 0.74f, "breakdown", juce::Colour (0xffB47AFF) },
        { 0.74f, 1.00f, "outro",     juce::Colour (0xffFFB84A) } });

    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0B0B0D));
        region.paintEntireComponent (g, false);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("waveform.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
