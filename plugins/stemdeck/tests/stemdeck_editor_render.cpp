// Renders the WHOLE perform window offline to a PNG. The component-level renders
// (deckstrip / toolbar / waveform) each look fine in isolation; the problems that
// actually show up on screen -- inconsistent radii, mismatched button weights,
// crowded panels -- only appear in context. This is that gate.
//
//   StemDeckEditorRender [out.png] [W] [H] [channels] [typed] [tab] [loaded] [surface] [recording] [scenario]
//     default ~/Desktop/StemDeck-demo/editor.png at 1680x1050
#include "MainPluginProcessor.h"
#include "MainPluginEditor.h"
#include <juce_graphics/juce_graphics.h>
#include <iostream>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    const int W = argc >= 3 ? juce::String (argv[2]).getIntValue() : 1680;
    const int H = argc >= 4 ? juce::String (argv[3]).getIntValue() : 1050;

    MainPluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    const auto scenario = argc >= 11 ? juce::String (argv[10]).toLowerCase()
                                     : juce::String();

    // Optional 4th arg: channel count to render (exercises add/remove live).
    if (argc >= 5)
    {
        const int want = juce::String (argv[4]).getIntValue();
        while (proc.getNumChannels() > want && proc.removeChannel (proc.getNumChannels() - 1)) {}
        while (proc.getNumChannels() < want && proc.addChannel() >= 0) {}
    }

    // Optional 5th arg: 1 adds typed channels after the decks. 2 deliberately
    // interleaves a removed/re-added TrackDeck with typed channels, reproducing
    // restored sessions where list index no longer equals permanent deck slot.
    const int typedMode = argc >= 6 ? juce::String (argv[5]).getIntValue() : 0;
    if (typedMode == 1)
    {
        proc.addChannel (ChannelType::input);
        proc.addChannel (ChannelType::midiInstrument);
    }
    else if (typedMode == 2)
    {
        if (proc.getNumChannels() > 2)
            proc.removeChannel (2);
        proc.addChannel (ChannelType::input);
        proc.addChannel (ChannelType::trackDeck);
        proc.addChannel (ChannelType::midiInstrument);
    }

    if (scenario == "focus-source-tone" || scenario == "focus-source-fx")
        for (int channel = 0; channel < proc.getNumChannels(); ++channel)
            if (proc.getChannelType (channel) != ChannelType::trackDeck)
            {
                proc.setChannelEditorPage (channel,
                    scenario == "focus-source-tone" ? 1 : 2);
                break;
            }

    // Optional 7th arg: write a short WAV and load it into deck 0, so the gate covers
    // the LOADED deck (WaveformRegion's vertical stem columns). Every render before
    // this one photographed an EMPTY deck, so the entire loaded layout — the part the
    // reference's §2 is about — was never actually looked at.
    juce::File tempWav;
    if (argc >= 8 && juce::String (argv[7]).getIntValue() != 0)
    {
        tempWav = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("stemdeck-render-src.wav");
        const int sr = 44100, secs = 8;
        juce::AudioBuffer<float> buf (2, sr * secs);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) sr;
                // Deterministic: a 2 Hz amplitude pulse over a tone, so the drawn
                // envelope has visible structure without needing Random.
                const float env = 0.35f + 0.45f * std::abs (std::sin (t * juce::MathConstants<float>::pi * 2.0f));
                buf.setSample (ch, i, env * std::sin (t * 220.0f * juce::MathConstants<float>::twoPi));
            }
        tempWav.deleteFile();
        juce::WavAudioFormat wav;
        if (std::unique_ptr<juce::OutputStream> os = tempWav.createOutputStream())
        {
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate (sr)
                                     .withNumChannels (2)
                                     .withBitsPerSample (16);
            if (auto writer = wav.createWriterFor (os, options))
                writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
        proc.loadDeckFileAsync (0, tempWav);
        if (scenario == "focus-c")
        {
            // Regression for the live bug where widening C left parent-painted
            // deck frames at every intermediate position. Three loaded channels
            // plus five idle targets reproduces the exact width allocation.
            proc.loadDeckFileAsync (1, tempWav);
            proc.loadDeckFileAsync (2, tempWav);
        }
    }

    // Optional 9th arg: put the real recorder/dispatcher into the rolling state so
    // the persistent red HUD and writing badges can be visually gated.
    juce::File tempRecording;
    if (argc >= 10 && juce::String (argv[9]).getIntValue() != 0)
    {
        tempRecording = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("stemdeck-editor-render-take.wav");
        tempRecording.deleteFile();
        proc.beginMasterRecording (tempRecording);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (ed == nullptr) { std::cerr << "no editor\n"; return 1; }
    ed->setSize (W, H);

    // The default render gates the simple performance surface. "tools" also
    // opens progressive disclosure so the professional controls cannot silently
    // become unreachable while simplifying the primary UI.
    if (scenario == "tools")
        for (int child = 0; child < ed->getNumChildComponents(); ++child)
            if (auto* button = dynamic_cast<juce::Button*> (ed->getChildComponent (child));
                button != nullptr && button->getButtonText() == "More")
            {
                button->triggerClick();
                break;
            }

    if (scenario == "master-output")
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
            mpe->setMasterDetailVisible (true, true);

    // Optional 6th arg: select this segmented tab (0=CUE 1=LOOP 2=FX 3=SYNC 4=SRC) on
    // every deck, so each tab's panel can be photographed. Without this the gate only
    // ever sees CUE and a cropped LOOP/SYNC panel would ship unnoticed.
    if (argc >= 7)
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            const int tab = juce::String (argv[6]).getIntValue();
            for (int d = 0; d < proc.getNumTrackDecks(); ++d)
                mpe->selectDeckToolTab (d, tab);
        }

    // Optional 8th arg: primary surface name. The prior gate photographed only
    // PERFORM, leaving the other top-level destinations visually untested.
    if (argc >= 9)
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            const auto surface = juce::String (argv[8]).toLowerCase();
            using S = MainPluginEditor::PrimarySurface;
            mpe->selectPrimarySurface (surface == "library"  ? S::library
                                     : surface == "mixer"    ? S::mixer
                                     : surface == "arranger" ? S::arranger
                                     : surface == "files"    ? S::files
                                                              : S::perform);
            if (scenario == "surface-stress")
            {
                // Regression for opposing fade/layout callbacks: rapid real-world
                // navigation must still end with one uncontested primary surface.
                mpe->selectPrimarySurface (S::perform);
                mpe->selectPrimarySurface (S::arranger);
                mpe->selectPrimarySurface (S::perform);
                mpe->selectPrimarySurface (S::arranger);
            }
            if (surface == "focus" || surface == "focus-exit")
                mpe->keyPressed (juce::KeyPress ('F'));
        }

    // One message pass so async layout / initial timers settle before we paint.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (tempWav.existsAsFile() ? 2500 : 200);

    if (scenario == "focus-c")
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            int stripIndex = 0;
            for (int child = 0; child < mpe->getNumChildComponents(); ++child)
                if (auto* strip = dynamic_cast<stemdeck::DeckStrip*> (mpe->getChildComponent (child)))
                {
                    if (stripIndex++ == 2 && strip->callbacks.onRequestFocus)
                    {
                        strip->callbacks.onRequestFocus();
                        break;
                    }
                }
            // Exercise every eased geometry frame, including the final snap.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        }

    if (scenario.startsWith ("focus-source"))
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            // Typed channels normally enter as compact monitors while an existing
            // deck retains focus. Promote the first one so CI photographs the real
            // source editor (workflow tabs, meter, routing and FX affordance), not
            // merely its collapsed strip.
            for (int child = 0; child < mpe->getNumChildComponents(); ++child)
                if (auto* strip = dynamic_cast<stemdeck::ChannelStrip*> (
                        mpe->getChildComponent (child)); strip != nullptr)
                {
                    if (strip->onRequestFocus)
                        strip->onRequestFocus();
                    break;
                }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        }

    // Callouts dismiss themselves when this headless renderer has no foreground
    // window, so open them only after slower audio analysis has settled and paint
    // them before their foreground-check timer runs.
    if (argc >= 9)
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            const auto surface = juce::String (argv[8]).toLowerCase();
            if (surface == "palette")
                mpe->keyPressed (juce::KeyPress ('K', juce::ModifierKeys::commandModifier, 0));
            else if (surface == "inspector")
                mpe->keyPressed (juce::KeyPress ('I', juce::ModifierKeys::commandModifier, 0));
        }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    if (argc >= 9 && juce::String (argv[8]).equalsIgnoreCase ("focus-exit"))
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            mpe->keyPressed (juce::KeyPress ('F'));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        }

    juce::Image img (juce::Image::ARGB, W, H, true);
    { juce::Graphics g (img); ed->paintEntireComponent (g, false); }

    const juce::File out = argc >= 2
        ? juce::File (juce::String (argv[1]))
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Desktop").getChildFile ("StemDeck-demo").getChildFile ("editor.png");
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
        png.writeImageToStream (img, *os);
    std::cout << "wrote " << out.getFullPathName() << " (" << W << "x" << H << ")\n";

    // The PNG is the whole product of this tool. Tracktion Engine keeps background
    // threads alive that don't join on a plain return, so leave immediately rather
    // than hanging the build gate on an orderly teardown we don't need.
    std::cout.flush();
    std::_Exit (0);
}
