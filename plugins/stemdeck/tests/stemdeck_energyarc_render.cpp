// Render the ScenePilot-style energy-arc arranger from a synthetic structure, to
// verify the arc curve + state regions before wiring into the ARRANGER.
#include "EnergyArc.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int W = 760, H = 200;
    juce::Image img (juce::Image::ARGB, W, H, true);

    // Auto Director from a typical structure: intro/build/drop/breakdown/drop/outro.
    std::vector<std::tuple<float, float, juce::String>> structure = {
        { 0.00f, 0.12f, "INTRO" }, { 0.12f, 0.30f, "BUILD" }, { 0.30f, 0.50f, "DROP" },
        { 0.50f, 0.64f, "BREAKDOWN" }, { 0.64f, 0.84f, "DROP" }, { 0.84f, 1.00f, "OUTRO" } };
    auto arc = stemdeck::buildEnergyArc (structure);

    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0B0B0D));
        stemdeck::drawEnergyArc (g, juce::Rectangle<float> (8, 8, W - 16, H - 16), arc, 0.42f, 2);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("energyarc.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
