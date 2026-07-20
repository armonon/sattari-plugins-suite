// StemDeck Arrange — a standalone app to TRY the arrangement view on real tracks.
// Load two songs; it analyses them (BPM/key/structure), lays them out as a mix
// (A in, crossfade to B over a transition), and shows the premium ArrangementView
// with real stem-less waveforms + structure + an animated playhead. Doubles as the
// integration proof for the view before it goes into the main app's ARRANGER tab.
#include <JuceHeader.h>
#include "ArrangementView.h"
#include "SessionCapture.h"
#include "TrackAnalysis.h"
#include "StructureAnalysis.h"
#include <map>
#include <memory>

using namespace stemdeck::session;

class ArrangePanel : public juce::Component, private juce::Timer
{
public:
    ArrangePanel()
    {
        formats.registerBasicFormats();
        capture.begin (44100.0, tempo);

        addAndMakeVisible (view);
        for (auto* b : { &loadA, &loadB, &playBtn })
        {
            addAndMakeVisible (*b);
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1b1f2a));
            b->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcfd6e4));
        }
        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setColour (juce::Label::textColourId, juce::Colour (0xff6b7280));
        hint.setText ("Load two tracks to see them arranged.", juce::dontSendNotification);
        addAndMakeVisible (hint);

        loadA.onClick = [this] { pick (0); };
        loadB.onClick = [this] { pick (1); };
        playBtn.onClick = [this] { playing = ! playing; playBtn.setButtonText (playing ? "PAUSE" : "PLAY"); };

        view.setAudioProvider ([this] (const juce::String& id, double& rate) -> const juce::AudioBuffer<float>*
        { auto it = audio.find (id); if (it == audio.end()) return nullptr; rate = rates[id]; return it->second.get(); });
        view.setStructureProvider ([this] (const juce::String& id) { auto it = structure.find (id); return it != structure.end() ? it->second : std::vector<stemdeck::Section>{}; });

        startTimerHz (30);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (44).reduced (10, 8);
        loadA.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (6);
        loadB.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (12);
        playBtn.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (12);
        hint.setBounds (bar);
        view.setBounds (r.reduced (8, 4));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0a0b10)); }

private:
    void timerCallback() override
    {
        if (playing && loaded > 0)
        {
            playhead += tempo / 60.0 / 30.0;   // beats per frame
            const double tot = 72.0;
            if (playhead > tot) playhead = 0.0;
            view.setPlayheadBeat (playhead);
        }
    }

    void pick (int deck)
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a track", juce::File(), formats.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, deck] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File()) return;
                std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
                if (rd == nullptr) return;

                const int n = (int) rd->lengthInSamples;
                auto buf = std::make_shared<juce::AudioBuffer<float>> ((int) juce::jmin<juce::int64> (2, rd->numChannels), n);
                rd->read (buf.get(), 0, n, 0, true, true);

                const juce::String id = deck == 0 ? "A" : "B";
                audio[id] = buf; rates[id] = rd->sampleRate;
                const auto analysis = TrackAnalyzer::analyze (*buf, rd->sampleRate);
                structure[id] = stemdeck::segmentStructure (*buf, rd->sampleRate, analysis.detectedBpm > 1 ? analysis.detectedBpm : tempo);

                TrackRef t; t.id = id; t.path = f.getFullPathName(); t.durationSec = n / rd->sampleRate;
                t.bpm = analysis.detectedBpm; t.keyPitchClass = analysis.keyPitchClass; t.keyIsMinor = analysis.keyIsMinor;

                // Lay it out: A enters at 0, B at bar 8 (beat 32) with a crossfade to it.
                const double at = deck == 0 ? 0.0 : 32.0;
                capture.recordDeckLoad (deck, t, at);
                capture.recordParam (deck, "cfSide", deck == 0 ? 0.0 : 2.0, at);
                capture.recordParam (deck, "fader", 1.0, at);
                capture.recordTransport (deck, TransportKind::play, 0.0, at);
                if (deck == 1)
                {
                    for (int k = 0; k <= 8; ++k) { const double b = 32.0 + k * 2.0; capture.recordParam (0, "crossfader", k / 8.0, b); capture.recordParam (0, "low", -12.0 * k / 8.0, b); }
                    Region tr; tr.kind = "transition"; tr.startBeat = 32.0; tr.endBeat = 48.0; capture.session().addRegion (tr);
                    capture.recordTransport (0, TransportKind::pause, 0.0, 52.0);
                }
                ++loaded;
                hint.setText (loaded >= 2 ? juce::String ("Mix arranged — press PLAY to scrub. " + juce::String (t.bpm, 0) + " BPM")
                                          : juce::String ("Loaded track " + id + " (" + juce::String (t.bpm, 0) + " BPM). Load one more."),
                              juce::dontSendNotification);
                view.setSession (capture.session());
            });
    }

    ArrangementView view;
    juce::TextButton loadA { "LOAD A" }, loadB { "LOAD B" }, playBtn { "PLAY" };
    juce::Label hint;
    SessionCapture capture;
    juce::AudioFormatManager formats;
    std::map<juce::String, std::shared_ptr<juce::AudioBuffer<float>>> audio;
    std::map<juce::String, double> rates;
    std::map<juce::String, std::vector<stemdeck::Section>> structure;
    std::unique_ptr<juce::FileChooser> chooser;
    double tempo = 124.0, playhead = 0.0;
    bool playing = false;
    int loaded = 0;
};

class ArrangeWindow : public juce::DocumentWindow
{
public:
    ArrangeWindow() : DocumentWindow ("StemDeck Arrange", juce::Colour (0xff0a0b10), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ArrangePanel(), false);
        setResizable (true, true);
        centreWithSize (1400, 520);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class StemDeckArrangeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "StemDeck Arrange"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    void initialise (const juce::String&) override { window = std::make_unique<ArrangeWindow>(); }
    void shutdown() override { window = nullptr; }
private:
    std::unique_ptr<ArrangeWindow> window;
};

START_JUCE_APPLICATION (StemDeckArrangeApp)
