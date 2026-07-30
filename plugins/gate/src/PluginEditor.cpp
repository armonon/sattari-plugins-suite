#include "PluginEditor.h"

SattariGateAudioProcessorEditor::SattariGateAudioProcessorEditor (SattariGateAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 470);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Gate");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Noise gate + downward expander");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; const char* suffix; };
    const K ks[] = {
        { &threshold,  &thresholdCap,  "threshold",  "THRESHOLD",  " dB" },
        { &range,      &rangeCap,      "range",      "RANGE",      " dB" },
        { &ratio,      &ratioCap,      "ratio",      "RATIO",      ":1"  },
        { &attack,     &attackCap,     "attack",     "ATTACK",     " ms" },
        { &hold,       &holdCap,       "hold",       "HOLD",       " ms" },
        { &release,    &releaseCap,    "release",    "RELEASE",    " ms" },
        { &lookahead,  &lookaheadCap,  "lookahead",  "LOOKAHEAD",  " ms" },
        { &hysteresis, &hysteresisCap, "hysteresis", "HYSTERESIS", " dB" },
        { &scHpf,      &scHpfCap,      "scHpf",      "SC HPF",     " Hz" },
        { &scLpf,      &scLpfCap,      "scLpf",      "SC LPF",     " Hz" },
    };
    int i = 0;
    for (auto& k : ks)
    {
        setupSlider (*k.s, *k.c, k.cap, k.suffix);
        knobAtts[(size_t) i++] = std::make_unique<SliderAttachment> (processor.parameters(), k.id, *k.s);
    }

    scListen.setButtonText ("SC Listen");
    expander.setButtonText ("Expander");
    tempoGate.setButtonText ("Tempo Gate");
    for (auto* b : { &scListen, &expander, &tempoGate })
    {
        b->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (*b);
    }
    scListenAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "scListen", scListen);
    expanderAtt  = std::make_unique<ButtonAttachment> (processor.parameters(), "expander", expander);
    tempoGateAtt = std::make_unique<ButtonAttachment> (processor.parameters(), "tempoGate", tempoGate);

    auto styleCombo = [this] (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (sattari::brand::kPanelRaised));
        c.setColour (juce::ComboBox::textColourId, juce::Colours::white);
        c.setColour (juce::ComboBox::outlineColourId, juce::Colour (sattari::brand::kAccent));
        addAndMakeVisible (c);
    };
    auto styleCap = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (l);
    };

    detMode.addItemList (juce::StringArray { "Peak", "RMS" }, 1);
    styleCombo (detMode);
    detModeAtt = std::make_unique<ComboAttachment> (processor.parameters(), "detMode", detMode);
    styleCap (detModeCap, "DETECTOR");

    tempoDiv.addItemList (juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }, 1);
    styleCombo (tempoDiv);
    tempoDivAtt = std::make_unique<ComboAttachment> (processor.parameters(), "tempoDiv", tempoDiv);
    styleCap (tempoDivCap, "RATE");

    setupSlider (tempoDuty, tempoDutyCap, "DUTY", " %");
    tempoDutyAtt = std::make_unique<SliderAttachment> (processor.parameters(), "tempoDuty", tempoDuty);

    grLabel.setText ("GR 0.0 dB", juce::dontSendNotification);
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setFont (sattari::brand::fontOptions (18.0f, juce::Font::bold));
    grLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (grLabel);

    stateLabel.setJustificationType (juce::Justification::centredRight);
    stateLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (stateLabel);

    startTimerHz (30);
}

SattariGateAudioProcessorEditor::~SattariGateAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariGateAudioProcessorEditor::setupSlider (juce::Slider& s, juce::Label& c,
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
    c.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    c.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    c.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c);
}

void SattariGateAudioProcessorEditor::timerCallback()
{
    const float gr = processor.getGainReductionDb();
    grLabel.setText ("GR " + juce::String (gr, 1) + " dB", juce::dontSendNotification);

    juce::String state;
    if (processor.getTempoGateActive())
        state = "Tempo gate";
    else
        state = processor.getGateOpen() ? "Open" : "Closed";
    stateLabel.setText (state, juce::dontSendNotification);

    grHistory[(size_t) grHistoryPos] = gr;
    grHistoryPos = (grHistoryPos + 1) % grHistorySize;
    repaint (grGraphBounds);
}

void SattariGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    if (! grGraphBounds.isEmpty())
    {
        const auto r = grGraphBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        constexpr float maxGr = 60.0f;   // graph spans 0..60 dB of gating
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

void SattariGateAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (20);

    auto header = b.removeFromTop (52);
    title.setBounds (header.removeFromTop (30));
    auto sub = header;
    subtitle.setBounds (sub.removeFromLeft (300));
    grLabel.setBounds (sub);

    b.removeFromTop (8);

    // Two rows of 5 knobs.
    juce::Slider* sl[] = { &threshold, &range, &ratio, &attack, &hold,
                           &release, &lookahead, &hysteresis, &scHpf, &scLpf };
    juce::Label*  ca[] = { &thresholdCap, &rangeCap, &ratioCap, &attackCap, &holdCap,
                           &releaseCap, &lookaheadCap, &hysteresisCap, &scHpfCap, &scLpfCap };
    auto placeRow = [&] (int first, juce::Rectangle<int> row)
    {
        const int w = row.getWidth() / 5;
        for (int k = 0; k < 5; ++k)
        {
            auto cell = row.removeFromLeft (w);
            ca[first + k]->setBounds (cell.removeFromBottom (16));
            sl[first + k]->setBounds (cell.reduced (6, 0));
        }
    };
    placeRow (0, b.removeFromTop (118));
    b.removeFromTop (4);
    placeRow (5, b.removeFromTop (118));

    b.removeFromTop (10);

    // Toggle + detector + tempo-gate row.
    auto row = b.removeFromTop (30);
    scListen.setBounds (row.removeFromLeft (92));
    expander.setBounds (row.removeFromLeft (92));
    detModeCap.setBounds (row.removeFromLeft (62));
    detMode.setBounds (row.removeFromLeft (78).reduced (0, 3));
    row.removeFromLeft (10);
    tempoGate.setBounds (row.removeFromLeft (104));
    tempoDivCap.setBounds (row.removeFromLeft (36));
    tempoDiv.setBounds (row.removeFromLeft (72).reduced (0, 3));
    tempoDutyCap.setBounds (row.removeFromLeft (34));
    tempoDuty.setBounds (row.removeFromLeft (78));

    b.removeFromTop (6);
    stateLabel.setBounds (b.removeFromTop (18));

    b.removeFromTop (4);
    grGraphBounds = b.reduced (0, 2);
}
