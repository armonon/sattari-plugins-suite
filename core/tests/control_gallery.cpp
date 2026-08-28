// Sattari control gallery — renders the whole control vocabulary to a PNG.
//
//  The suite has one control surface and 49 editors inheriting it, so a change
//  to SattariControlSurface.h is a change to every product at once. Reviewing
//  that by opening plugins in a DAW is slow and partial; this renders every
//  control, in every state, in every product accent, to one sheet.
//
//  It is a REVIEW tool, not an assertion: it fails only if a control paints
//  nothing (a blank tile means a primitive threw away its geometry, which is the
//  failure mode a screenshot review misses because an empty tile looks tidy).
//
//    build/local/SattariControlGallery [out.png]
#include <sattari_audio_core/SattariControlSurface.h>
#include <sattari_audio_core/SattariLookAndFeel.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <vector>

namespace
{
using namespace sattari;

struct Swatch { const char* name; juce::uint32 argb; };
const std::vector<Swatch> kAccents {
    { "crimson", brand::kAccentCrimson },
    { "mint",    brand::kAccent },
    { "violet",  brand::kAccentViolet },
    { "amber",   brand::kAccentAmber },
    { "blue",    brand::kAccentBlue },
};

void caption (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
{
    g.setColour (control::overlayText.withAlpha (0.55f));
    g.setFont (brand::font (10.0f, juce::Font::bold));
    g.drawText (t.toUpperCase(), r, juce::Justification::centredLeft, false);
}

// Real components so the primitives see real state, not hand-passed booleans.
struct Harness
{
    juce::Slider  vert   { juce::Slider::LinearVertical,   juce::Slider::NoTextBox };
    juce::Slider  horiz  { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider  rotary { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::TextButton   button;
    juce::TextButton   latched;
    juce::ToggleButton toggleOff, toggleOn;
    juce::ComboBox     combo;

    Harness()
    {
        for (auto* s : { &vert, &horiz, &rotary }) s->setRange (0.0, 1.0);
        button.setButtonText ("Render");
        latched.setButtonText ("Latched");
        latched.setClickingTogglesState (true);
        latched.setToggleState (true, juce::dontSendNotification);
        toggleOff.setButtonText ("Follow key");
        toggleOn.setButtonText ("Follow key");
        toggleOn.setToggleState (true, juce::dontSendNotification);
        combo.addItem ("Rast", 1);
        combo.setSelectedId (1, juce::dontSendNotification);
    }
};

// Paint one accent's row-block at (x,y); returns the height consumed.
int paintAccentBlock (juce::Graphics& g, Harness& h, int x, int y, int width,
                      const Swatch& sw)
{
    const auto accent = juce::Colour (sw.argb);
    const int top = y;

    caption (g, { x, y, width, 14 }, juce::String (sw.name) + "  —  " + accent.toDisplayString (false));
    y += 20;

    // Vertical sliders at five values, the reference's signature row.
    const int vw = 46, vh = 132;
    const float values[] = { 0.15f, 0.38f, 0.60f, 0.82f, 1.0f };
    int cx = x;
    for (float v : values)
    {
        h.vert.setBounds (0, 0, vw, vh);
        const float pos = (float) (y + 6) + (1.0f - v) * (float) (vh - 12);
        control::drawLinearSlider (g, cx, y, vw, vh, pos,
                                   juce::Slider::LinearVertical,
                                   true, false, false, false, accent);
        cx += vw + 6;
    }

    // Rotaries: off, mid, full, and disabled.
    const int kn = 66;
    const float start = juce::MathConstants<float>::pi * 1.2f;
    const float end   = juce::MathConstants<float>::pi * 2.8f;
    int rx = cx + 16;
    const float rots[] = { 0.0f, 0.45f, 0.9f };
    for (float v : rots)
    {
        control::drawRotarySlider (g, rx, y + 20, kn, kn, v, start, end,
                                   true, false, false, false, accent);
        rx += kn + 4;
    }
    control::drawRotarySlider (g, rx, y + 20, kn, kn, 0.6f, start, end,
                               false, false, false, false, accent);
    rx += kn + 4;

    y += vh + 12;

    // Horizontal sliders, buttons, switches, combo.
    control::drawLinearSlider (g, x, y, 210, 30, (float) x + 6.0f + 0.62f * 198.0f,
                               juce::Slider::LinearHorizontal,
                               true, false, false, false, accent);
    control::drawLinearSlider (g, x + 222, y, 210, 30, (float) (x + 222) + 6.0f + 0.28f * 198.0f,
                               juce::Slider::LinearHorizontal,
                               true, true, true, true, accent);

    int bx = x + 452;
    h.button.setBounds (bx, y, 104, 30);
    { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (h.button.getBounds());
      g.setOrigin (bx, y);
      control::drawButtonBackground (g, h.button, false, false, accent); }
    bx += 112;
    h.latched.setBounds (bx, y, 104, 30);
    { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (h.latched.getBounds());
      g.setOrigin (bx, y);
      control::drawButtonBackground (g, h.latched, false, false, accent); }
    bx += 112;

    h.toggleOff.setBounds (bx, y, 130, 30);
    { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (h.toggleOff.getBounds());
      g.setOrigin (bx, y);
      control::drawToggleButton (g, h.toggleOff, false, false, accent); }
    bx += 138;
    h.toggleOn.setBounds (bx, y, 130, 30);
    { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (h.toggleOn.getBounds());
      g.setOrigin (bx, y);
      control::drawToggleButton (g, h.toggleOn, false, false, accent); }
    bx += 146;

    { juce::Graphics::ScopedSaveState s (g); g.setOrigin (bx, y);
      control::drawComboBox (g, 132, 30, false, false, accent); }

    y += 30 + 22;
    g.setColour (control::overlayText.withAlpha (0.08f));
    g.fillRect (x, y - 11, width, 1);
    return y - top;
}

// A tile is "alive" if it is not a single flat colour.
bool hasContent (const juce::Image& img, juce::Rectangle<int> r)
{
    const auto first = img.getPixelAt (r.getX(), r.getY());
    for (int yy = r.getY(); yy < r.getBottom(); yy += 2)
        for (int xx = r.getX(); xx < r.getRight(); xx += 2)
            if (img.getPixelAt (xx, yy) != first)
                return true;
    return false;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("control-gallery.png");

    const int width = 1480;
    const int margin = 26;
    const int blockH = 260;
    const int height = margin * 2 + 34 + blockH * (int) kAccents.size();

    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics g (image);

    // The canvas the controls actually sit on.
    g.setGradientFill (juce::ColourGradient (
        juce::Colour (0xff26272d), 0.0f, 0.0f,
        juce::Colour (0xff191a1e), 0.0f, (float) height, false));
    g.fillAll();

    g.setColour (control::overlayText);
    g.setFont (brand::font (17.0f, juce::Font::bold));
    g.drawText ("Sattari control surface", margin, margin - 6, 600, 22,
                juce::Justification::centredLeft);

    Harness h;
    int y = margin + 30;
    for (const auto& sw : kAccents)
        y += paintAccentBlock (g, h, margin, y, width - margin * 2, sw);

    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk()) { std::printf ("cannot write %s\n", out.getFullPathName().toRawUTF8()); return 1; }
    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream)) { std::printf ("png write failed\n"); return 1; }

    // Blank-tile guard: every accent block must have painted something.
    int dead = 0;
    int by = margin + 30;
    for (const auto& sw : kAccents)
    {
        if (! hasContent (image, { margin, by, width - margin * 2, blockH - 10 }))
        { std::printf ("  FAIL %s block painted nothing\n", sw.name); ++dead; }
        by += blockH;
    }

    std::printf ("control gallery -> %s (%dx%d)\n",
                 out.getFullPathName().toRawUTF8(), width, height);
    if (dead > 0) { std::printf ("control gallery FAILED (%d dead blocks)\n", dead); return 1; }
    std::printf ("control gallery ok\n");
    return 0;
}
