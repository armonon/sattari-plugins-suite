// Renders the WHOLE perform window offline to a PNG. The component-level renders
// (deckstrip / toolbar / waveform) each look fine in isolation; the problems that
// actually show up on screen -- inconsistent radii, mismatched button weights,
// crowded panels -- only appear in context. This is that gate.
//
//   StemDeckEditorRender [out.png] [W] [H] [channels] [typed] [tab] [loaded] [surface] [recording]
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

    // Optional 4th arg: channel count to render (exercises add/remove live).
    if (argc >= 5)
    {
        const int want = juce::String (argv[4]).getIntValue();
        while (proc.getNumChannels() > want && proc.removeChannel (proc.getNumChannels() - 1)) {}
        while (proc.getNumChannels() < want && proc.addChannel() >= 0) {}
    }

    // Optional 5th arg: also add one Input and one MIDI Instrument channel, to
    // exercise the per-type ChannelStrip rendering.
    if (argc >= 6 && juce::String (argv[5]).getIntValue() != 0)
    {
        proc.addChannel (ChannelType::input);
        proc.addChannel (ChannelType::midiInstrument);
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
        if (auto os = std::unique_ptr<juce::FileOutputStream> (tempWav.createOutputStream()))
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                    wav.createWriterFor (os.get(), sr, 2, 16, {}, 0)))
            { os.release(); w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()); }
        proc.loadDeckFileAsync (0, tempWav);
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
    // DECKS, leaving four top-level navigation destinations visually untested.
    if (argc >= 9)
        if (auto* mpe = dynamic_cast<MainPluginEditor*> (ed.get()))
        {
            const auto surface = juce::String (argv[8]).toLowerCase();
            using S = MainPluginEditor::PrimarySurface;
            mpe->selectPrimarySurface (surface == "library"     ? S::library
                                     : surface == "mixer"       ? S::mixer
                                     : surface == "arranger"    ? S::arranger
                                     : surface == "performance" ? S::performance
                                                                 : S::decks);
        }

    // One message pass so async layout / initial timers settle before we paint.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (tempWav.existsAsFile() ? 2500 : 200);

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
