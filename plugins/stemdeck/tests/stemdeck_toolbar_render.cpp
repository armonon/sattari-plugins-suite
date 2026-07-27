// Render the top-toolbar buttons (PLAY ALL / REC / AUTO) to a PNG to verify the
// icons + pill styling before wiring into the editor.
#include "ToolbarIcons.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int W = 520, H = 60;
    juce::Image img (juce::Image::ARGB, W, H, true);
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0a0b10));
        const auto cyan = juce::Colour (0xff2fd8f0), red = juce::Colour (0xffff3b47), amber = juce::Colour (0xffe8c25c);
        // PLAY ALL (playing=false -> play icon), STOP variant, REC idle, REC active, AUTO
        stemdeck::drawToolButton (g, { 12, 14, 118, 34 }, stemdeck::ToolIcon::play, "PLAY ALL", cyan, false);
        stemdeck::drawToolButton (g, { 140, 14, 78, 34 }, stemdeck::ToolIcon::rec, "REC", red, false);
        stemdeck::drawToolButton (g, { 228, 14, 78, 34 }, stemdeck::ToolIcon::rec, "REC", red, true);   // recording
        stemdeck::drawToolButton (g, { 316, 14, 84, 34 }, stemdeck::ToolIcon::magic, "AUTO", amber, false);
        stemdeck::drawToolButton (g, { 410, 14, 84, 34 }, stemdeck::ToolIcon::magic, "AUTO", amber, false, true); // hover
    }
    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("toolbar.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
