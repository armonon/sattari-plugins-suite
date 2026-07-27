// Renders the DeckStrip in its key states to a PNG, offline (no live window), to
// verify layout + tiers + colour against the spec before wiring into the editor.
//   StemDeckDeckStripRender [out.png]   (default ~/Desktop/StemDeck-demo/deckstrip.png)
#include "DeckStrip.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

using stemdeck::DeckStrip;

static void blit (juce::Image& dst, DeckStrip& s, int x, int y, int w, int h)
{
    s.setSize (w, h);
    juce::Image sub (juce::Image::ARGB, w, h, true);
    { juce::Graphics g (sub); s.paintEntireComponent (g, false); }
    juce::Graphics gd (dst);
    gd.drawImageAt (sub, x, y);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const int W = 1180, pad = 16;
    const int collapsed = DeckStrip::kCollapsedHeight;
    const int expanded  = DeckStrip::kExpandedHeight;
    // 4 collapsed strips + one expanded example + a narrow (per-column) example below.
    const int H = pad * 3 + collapsed * 4 + 3 * 3 + expanded + pad + collapsed + 40;
    juce::Image img (juce::Image::ARGB, W, H, true);
    { juce::Graphics g (img); g.fillAll (juce::Colour (0xff0a0b10)); }

    int y = pad;
    const int stripW = W - pad * 2;

    // Deck 1 — focused, playing, loop active (4 beats), sync locked, key set +2.
    {
        DeckStrip d (0);
        DeckStrip::State s;
        s.hasTrack = true; s.focused = true; s.playing = true;
        s.cueSet = true;
        s.loopActive = true; s.loopBeats = 4.0;
        s.beatJumpBars = 4;
        s.syncOn = true; s.syncLocked = true; s.phaseOffset = 0.0f;
        s.keyCamelot = "8A"; s.keySemis = 2;
        d.setState (s);
        blit (img, d, pad, y, stripW, collapsed);
    }
    y += collapsed + 3;

    // Deck 2 — has track, stopped, loop armed (1/2 beat), sync drifting, low-conf key.
    {
        DeckStrip d (1);
        DeckStrip::State s;
        s.hasTrack = true; s.playing = false;
        s.cueSet = true;
        s.loopArmed = true; s.loopBeats = 0.5;
        s.beatJumpBars = 8;
        s.syncOn = true; s.syncLocked = false; s.phaseOffset = 0.35f;   // drifting → warn ring
        s.keyCamelot = "5A"; s.keySemis = 0; s.keyLowConf = true;
        d.setState (s);
        blit (img, d, pad, y, stripW, collapsed);
    }
    y += collapsed + 3;

    // Deck 3 — has track, cue held (playing from cue), loop off, 16-beat jump, no sync.
    {
        DeckStrip d (2);
        DeckStrip::State s;
        s.hasTrack = true; s.playing = true; s.cueSet = true; s.cueHeld = true;
        s.loopBeats = 16.0;
        s.beatJumpBars = 16;
        s.syncOn = false;
        s.keyCamelot = "11B"; s.keySemis = -1;
        d.setState (s);
        blit (img, d, pad, y, stripW, collapsed);
    }
    y += collapsed + 3;

    // Deck 4 — EMPTY (no track): primary controls disabled.
    {
        DeckStrip d (3);
        DeckStrip::State s;    // hasTrack=false
        s.keyCamelot = "--";
        d.setState (s);
        blit (img, d, pad, y, stripW, collapsed);
    }
    y += collapsed + pad;

    // Expanded example — deck 1, drawer open: stems (VOX solo, BAS muted), QUANT 1/4,
    // SLIP on, FX A+C on.
    {
        DeckStrip d (0);
        DeckStrip::State s;
        s.hasTrack = true; s.focused = true; s.playing = true;
        s.cueSet = true; s.loopActive = true; s.loopBeats = 8.0;
        s.beatJumpBars = 4; s.syncOn = true; s.syncLocked = true;
        s.keyCamelot = "8A"; s.keySemis = 2;
        s.stem = { { 0, 1, 0, 2 } };   // drums active, bass muted, music active, vocals solo
        s.quantIndex = 3; s.slipOn = true; s.fx = { { true, false, true } };
        d.setState (s);
        d.setExpanded (true, false);   // snap open for the still render
        blit (img, d, pad, y, stripW, expanded);
    }
    y += expanded + pad;

    // Narrow per-column width (~380px) — compact mode: icon-only SYNC/TAKE, tight.
    {
        DeckStrip d (2);
        DeckStrip::State s;
        s.hasTrack = true; s.focused = true; s.playing = true; s.cueSet = true;
        s.loopActive = true; s.loopBeats = 4.0; s.beatJumpBars = 4;
        s.syncOn = true; s.syncLocked = true;
        s.keyCamelot = "8A"; s.keySemis = 2;
        d.setState (s);
        blit (img, d, pad, y, 380, collapsed);
    }

    const juce::File out = argc >= 2 ? juce::File (juce::String (argv[1]))
                                     : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("deckstrip.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream())) png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << "\n";
    return 0;
}
