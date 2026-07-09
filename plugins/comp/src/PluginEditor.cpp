#include "PluginEditor.h"

SattariCompAudioProcessorEditor::SattariCompAudioProcessorEditor (SattariCompAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (720, 430);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Comp");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Vocal-voiced compressor");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; const char* suffix; };
    const K ks[] = {
        { &threshold, &thresholdCap, "threshold", "THRESHOLD", " dB" },
        { &ratio,     &ratioCap,     "ratio",     "RATIO",     ":1" },
        { &knee,      &kneeCap,      "knee",      "KNEE",      " dB" },
        { &attack,    &attackCap,    "attack",    "ATTACK",    " ms" },
        { &release,   &releaseCap,   "release",   "RELEASE",   " ms" },
        { &makeup,    &makeupCap,    "makeup",    "MAKEUP",    " dB" },
        { &mix,       &mixCap,       "mix",       "MIX",       " %" },
        { &scHpf,     &scHpfCap,     "scHpf",     "SC HPF",    " Hz" },
    };
    int i = 0;
    for (auto& k : ks)
    {
        setupSlider (*k.s, *k.c, k.cap, k.suffix);
        knobAtts[(size_t) i++] = std::make_unique<SliderAttachment> (processor.parameters(), k.id, *k.s);
    }

    autoRelease.setButtonText ("Auto Release");
    autoGain.setButtonText ("Auto Gain");
    scListen.setButtonText ("SC Listen");
    tempoRelease.setButtonText ("Tempo Release");
    for (auto* b : { &autoRelease, &autoGain, &scListen, &tempoRelease })
    {
        b->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (*b);
    }
    autoRelAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "autoRelease", autoRelease);
    autoGainAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "autoGain", autoGain);
    scListenAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "scListen", scListen);
    tempoRelAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "tempoRelease", tempoRelease);

    releaseDiv.addItemList (juce::StringArray { "1/16", "1/8T", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2" }, 1);
    releaseDiv.setColour (juce::ComboBox::backgroundColourId, juce::Colour (sattari::brand::kPanelRaised));
    releaseDiv.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    releaseDiv.setColour (juce::ComboBox::outlineColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (releaseDiv);
    releaseDivAtt = std::make_unique<ComboAttachment> (processor.parameters(), "releaseDiv", releaseDiv);

    releaseDivCap.setText ("REL DIV", juce::dontSendNotification);
    releaseDivCap.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    releaseDivCap.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    releaseDivCap.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (releaseDivCap);

    grLabel.setText ("GR 0.0 dB", juce::dontSendNotification);
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    grLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (grLabel);

    releaseInfo.setJustificationType (juce::Justification::centredRight);
    releaseInfo.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (releaseInfo);

    startTimerHz (30);
}

SattariCompAudioProcessorEditor::~SattariCompAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariCompAudioProcessorEditor::setupSlider (juce::Slider& s, juce::Label& c,
                                                   const juce::String& text, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (sattari::brand::kAccent));
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (sattari::brand::kHairline));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (sattari::brand::kPanelDeep));
    if (suffix.isNotEmpty()) s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);

    c.setText (text, juce::dontSendNotification);
    c.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    c.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
}

void SattariCompAudioProcessorEditor::timerCallback()
{
    const float gr = processor.getGainReductionDb();
    grLabel.setText ("GR " + juce::String (gr, 1) + " dB", juce::dontSendNotification);

    if (processor.getTempoReleaseActive())
        releaseInfo.setText ("Tempo release: " + juce::String (processor.getEffectiveReleaseMs(), 0) + " ms",
                             juce::dontSendNotification);
    else
        releaseInfo.setText ("Release: " + juce::String (processor.getEffectiveReleaseMs(), 0) + " ms",
                             juce::dontSendNotification);

    grHistory[(size_t) grHistoryPos] = gr;
    grHistoryPos = (grHistoryPos + 1) % grHistorySize;
    repaint (grGraphBounds);
}

void SattariCompAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    if (! grGraphBounds.isEmpty())
    {
        const auto r = grGraphBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        constexpr float maxGr = 24.0f;   // graph spans 0..24 dB of reduction
        juce::Path p;
        for (int i = 0; i < grHistorySize; ++i)
        {
            const int idx = (grHistoryPos + i) % grHistorySize;
            const float norm = juce::jlimit (0.0f, 1.0f, grHistory[(size_t) idx] / maxGr);
            const float x = r.getX() + r.getWidth() * (float) i / (float) (grHistorySize - 1);
            const float y = r.getY() + norm * r.getHeight();   // more GR -> lower
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        g.setColour (lookAndFeel.getAccent());
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
}

void SattariCompAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (20);

    auto header = b.removeFromTop (52);
    title.setBounds (header.removeFromTop (30));
    auto sub = header;
    subtitle.setBounds (sub.removeFromLeft (280));
    grLabel.setBounds (sub);

    b.removeFromTop (8);

    // Two rows of 4 knobs.
    juce::Slider* sl[] = { &threshold, &ratio, &knee, &attack, &release, &makeup, &mix, &scHpf };
    juce::Label*  ca[] = { &thresholdCap, &ratioCap, &kneeCap, &attackCap, &releaseCap, &makeupCap, &mixCap, &scHpfCap };
    auto placeRow = [&] (int first, juce::Rectangle<int> row)
    {
        const int w = row.getWidth() / 4;
        for (int k = 0; k < 4; ++k)
        {
            auto cell = row.removeFromLeft (w);
            ca[first + k]->setBounds (cell.removeFromBottom (16));
            sl[first + k]->setBounds (cell.reduced (8, 0));
        }
    };
    placeRow (0, b.removeFromTop (120));
    b.removeFromTop (6);
    placeRow (4, b.removeFromTop (120));

    b.removeFromTop (10);

    // Toggle + tempo-release row.
    auto toggles = b.removeFromTop (30);
    autoRelease.setBounds (toggles.removeFromLeft (120));
    autoGain.setBounds (toggles.removeFromLeft (110));
    scListen.setBounds (toggles.removeFromLeft (100));
    tempoRelease.setBounds (toggles.removeFromLeft (130));
    releaseDivCap.setBounds (toggles.removeFromLeft (56));
    releaseDiv.setBounds (toggles.removeFromLeft (90));

    b.removeFromTop (6);
    releaseInfo.setBounds (b.removeFromTop (18));

    b.removeFromTop (4);
    grGraphBounds = b.reduced (0, 2);
}
