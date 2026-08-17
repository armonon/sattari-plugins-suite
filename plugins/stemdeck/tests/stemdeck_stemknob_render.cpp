// Render the stem gain knob across its range + stem colours to a PNG, to verify the
// 0/100/300 arc + unity-at-top geometry before wiring into the deck.
#include "StemGainKnob.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int cell = 96, cols = 6, rows = 2, pad = 12;
    const int W = cols * cell + pad * 2, H = rows * cell + pad * 2 + 60;
    juce::Image img (juce::Image::ARGB, W, H, true);

    // Stem colours (VOX/DRM/BAS/OTH) from the brief.
    const juce::Colour vox (0xffD4537E), drm (0xff4A9EFF), bas (0xff4AD9C4), oth (0xff888780);

    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour (0xff0B0B0D));

        struct Cell { float pct; juce::Colour col; const char* label; bool muted; };
        const Cell top[] = {
            { 0.0f,   bas, "0",   false }, { 50.0f,  bas, "50",  false },
            { 100.0f, bas, "100", false }, { 150.0f, bas, "150", false },
            { 300.0f, bas, "300", false }, { 100.0f, bas, "MUTE", true } };
        // Second row: one knob per stem colour at a boosted value, to check colour read.
        const Cell bot[] = {
            { 160.0f, vox, "VOX 160", false }, { 130.0f, drm, "DRM 130", false },
            { 100.0f, bas, "BAS 100", false }, { 175.0f, oth, "OTH 175", false },
            { 275.0f, vox, "VOX 275", false }, { 300.0f, drm, "DRM 300", false } };

        auto drawRow = [&] (const Cell* cells, int y)
        {
            for (int i = 0; i < cols; ++i)
            {
                juce::Rectangle<float> area ((float) (pad + i * cell), (float) y, (float) cell, (float) cell - 22.0f);
                stemdeck::drawStemGainKnob (g, area.reduced (14.0f), cells[i].pct, cells[i].col, cells[i].muted);
                g.setColour (juce::Colour (0xffB8B8BE));
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (cells[i].label, (int) area.getX(), y + cell - 22, cell, 16, juce::Justification::centred);
            }
        };
        drawRow (top, pad);
        drawRow (bot, pad + cell);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("stemknob.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
