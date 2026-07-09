#include "PluginEditor.h"

SattariImageAudioProcessorEditor::SattariImageAudioProcessorEditor (SattariImageAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (560, 340);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Image");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Mono-safe stereo imager");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { const char* id; const char* cap; const char* suffix; };
    const K ks[kKnobs] = {
        { "width",      "WIDTH",    {}      },
        { "widthLow",   "LOW",      {}      },
        { "widthMid",   "MID",      {}      },
        { "widthHigh",  "HIGH",     {}      },
        { "xoverLoMid", "LO/MID",   " Hz"   },
        { "xoverMidHi", "MID/HI",   " Hz"   },
        { "haas",       "HAAS",     {}      },
    };
    for (int i = 0; i < kKnobs; ++i)
    {
        styleKnob (knobs[(size_t) i], ks[i].suffix);
        styleCaption (caps[(size_t) i], ks[i].cap);
        addAndMakeVisible (knobs[(size_t) i]);
        addAndMakeVisible (caps[(size_t) i]);
        knobAtts[(size_t) i] = std::make_unique<SliderAttachment> (processor.parameters(), ks[i].id, knobs[(size_t) i]);
    }

    styleKnob (lowMonoFreq, " Hz");
    styleCaption (lowMonoFreqCap, "MONO <");
    addAndMakeVisible (lowMonoFreq);
    addAndMakeVisible (lowMonoFreqCap);
    lowMonoFreqAtt = std::make_unique<SliderAttachment> (processor.parameters(), "lowMonoFreq", lowMonoFreq);

    lowMonoBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    monoAuditionBtn.setColour (juce::ToggleButton::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    addAndMakeVisible (lowMonoBtn);
    addAndMakeVisible (monoAuditionBtn);
    lowMonoAtt      = std::make_unique<ButtonAttachment> (processor.parameters(), "lowMono", lowMonoBtn);
    monoAuditionAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "monoAudition", monoAuditionBtn);

    startTimerHz (20);
}

SattariImageAudioProcessorEditor::~SattariImageAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariImageAudioProcessorEditor::timerCallback()
{
    const float c = processor.getCorrelation();
    shownCorr += 0.3f * (c - shownCorr);   // gentle smoothing
    repaint();
}

void SattariImageAudioProcessorEditor::styleKnob (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    s.setTextValueSuffix (suffix);
}

void SattariImageAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariImageAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Live correlation bar (-1..+1) along the bottom. mirrors plugins/meter.
    const auto accent = juce::Colour (sattari::brand::productAccent ("imager"));
    const auto warn   = juce::Colour (sattari::brand::kWarning);
    const auto danger = juce::Colour (sattari::brand::kDanger);

    auto corrArea = juce::Rectangle<float> (18.0f, (float) getHeight() - 46.0f,
                                            (float) getWidth() - 36.0f, 22.0f);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (corrArea, 6.0f);

    const float mid = corrArea.getCentreX();
    const float x   = mid + juce::jlimit (-1.0f, 1.0f, shownCorr) * (corrArea.getWidth() * 0.5f - 4.0f);
    // Negative correlation = phase risk -> warn/danger colour.
    const juce::Colour barCol = shownCorr < -0.3f ? danger : (shownCorr < 0.0f ? warn : accent);
    g.setColour (barCol);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, x), corrArea.getY() + 3.0f,
                                                    std::fabs (x - mid), corrArea.getHeight() - 6.0f), 4.0f);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("CORRELATION " + juce::String (shownCorr, 2), corrArea.toNearestInt(), juce::Justification::centred);
}

void SattariImageAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (10);

    // Top row: WIDTH / LOW / MID / HIGH.
    auto row1 = b.removeFromTop (110);
    const int w1 = row1.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = row1.removeFromLeft (w1);
        caps[(size_t) i].setBounds (cell.removeFromBottom (16));
        knobs[(size_t) i].setBounds (cell.reduced (6, 0));
    }

    b.removeFromTop (6);

    // Second row: LO/MID xover, MID/HI xover, HAAS, MONO< freq.
    auto row2 = b.removeFromTop (100);
    juce::Slider* r2sl[] = { &knobs[4], &knobs[5], &knobs[6], &lowMonoFreq };
    juce::Label*  r2ca[] = { &caps[4],  &caps[5],  &caps[6],  &lowMonoFreqCap };
    const int w2 = row2.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto cell = row2.removeFromLeft (w2);
        r2ca[i]->setBounds (cell.removeFromBottom (16));
        r2sl[i]->setBounds (cell.reduced (6, 0));
    }

    // Toggle row above the correlation bar.
    auto toggles = juce::Rectangle<int> (18, getHeight() - 74, getWidth() - 36, 22);
    lowMonoBtn.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2).reduced (2, 0));
    monoAuditionBtn.setBounds (toggles.reduced (2, 0));
}
