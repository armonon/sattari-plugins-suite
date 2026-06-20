#include "PluginEditor.h"

SideChainMasterAudioProcessorEditor::SideChainMasterAudioProcessorEditor (SideChainMasterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "side-chain-master")
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 580);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Side Chain Master");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Clean external ducking with musical internal fallback");
    addAndMakeVisible (subtitle);

    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    refreshUserPresets();

    setupCombo (mode, modeLabel, "Mode");
    setupSlider (threshold, thresholdLabel, "Threshold");
    setupSlider (ratio, ratioLabel, "Ratio");
    setupSlider (attack, attackLabel, "Attack");
    setupSlider (release, releaseLabel, "Release");
    setupSlider (depth, depthLabel, "Duck Depth");
    setupSlider (makeup, makeupLabel, "Makeup");
    setupSlider (mix, mixLabel, "Mix");
    setupSlider (sidechainHpf, hpfLabel, "SC HPF");
    setupSlider (lookahead, lookaheadLabel, "Lookahead");
    setupSlider (syncCurve, syncCurveLabel, "Sync Curve");
    setupCombo (syncRate, syncRateLabel, "Sync Rate");
    syncRate.addItemList (juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }, 1);

    external.setButtonText ("Use external sidechain");
    listen.setButtonText ("Solo detector listen");
    syncDuck.setButtonText ("Tempo sync duck");
    for (auto* button : { &external, &listen, &syncDuck })
    {
        button->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (*button);
    }

    grLabel.setText ("GR 0.0 dB", juce::dontSendNotification);
    grLabel.setJustificationType (juce::Justification::centred);
    grLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    grLabel.setColour (juce::Label::textColourId, juce::Colour (0xff7cffd8));
    addAndMakeVisible (grLabel);

    sidechainStatus.setText ("Sidechain: internal detector", juce::dontSendNotification);
    sidechainStatus.setJustificationType (juce::Justification::centredRight);
    sidechainStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (sidechainStatus);

    auto& params = audioProcessor.parameters();
    modeAttach = std::make_unique<ComboAttachment> (params, "mode", mode);
    thresholdAttach = std::make_unique<SliderAttachment> (params, "threshold", threshold);
    ratioAttach = std::make_unique<SliderAttachment> (params, "ratio", ratio);
    attackAttach = std::make_unique<SliderAttachment> (params, "attack", attack);
    releaseAttach = std::make_unique<SliderAttachment> (params, "release", release);
    depthAttach = std::make_unique<SliderAttachment> (params, "depth", depth);
    makeupAttach = std::make_unique<SliderAttachment> (params, "makeup", makeup);
    mixAttach = std::make_unique<SliderAttachment> (params, "mix", mix);
    hpfAttach = std::make_unique<SliderAttachment> (params, "sidechainHpf", sidechainHpf);
    lookaheadAttach = std::make_unique<SliderAttachment> (params, "lookahead", lookahead);
    syncCurveAttach = std::make_unique<SliderAttachment> (params, "syncCurve", syncCurve);
    syncRateAttach = std::make_unique<ComboAttachment> (params, "syncRate", syncRate);
    externalAttach = std::make_unique<ButtonAttachment> (params, "external", external);
    listenAttach = std::make_unique<ButtonAttachment> (params, "listen", listen);
    syncDuckAttach = std::make_unique<ButtonAttachment> (params, "syncDuck", syncDuck);

    startTimerHz (30);
}

SideChainMasterAudioProcessorEditor::~SideChainMasterAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SideChainMasterAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SideChainMasterAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SideChainMasterAudioProcessorEditor::setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 22);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff7cffd8));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff202637));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff11131b));
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (label);
}

void SideChainMasterAudioProcessorEditor::setupCombo (juce::ComboBox& combo, juce::Label& label, const juce::String& text)
{
    combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff171b27));
    combo.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff7cffd8));
    addAndMakeVisible (combo);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (label);
}

void SideChainMasterAudioProcessorEditor::timerCallback()
{
    const float gr = audioProcessor.getLastGainReductionDb();
    grLabel.setText ("GR " + juce::String (gr, 1) + " dB", juce::dontSendNotification);
    sidechainStatus.setText (audioProcessor.getLastExternalSidechainAvailable()
        ? "Sidechain: external bus active"
        : "Sidechain: internal detector", juce::dontSendNotification);

    grHistory[(size_t) grHistoryPos] = gr;
    grHistoryPos = (grHistoryPos + 1) % grHistorySize;
    repaint (grGraphBounds);
}

void SideChainMasterAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Gain-reduction history graph (newest on the right). Scaled to 0..24 dB.
    if (! grGraphBounds.isEmpty())
    {
        const auto r = grGraphBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        constexpr float maxGr = 24.0f;
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

void SideChainMasterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    auto header = area.removeFromTop (70);
    title.setBounds (header.removeFromTop (36));
    subtitle.setBounds (header.removeFromLeft (360));
    sidechainStatus.setBounds (header);

    area.removeFromTop (10);
    auto presetRow = area.removeFromTop (34);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));

    area.removeFromTop (12);
    auto modeRow = area.removeFromTop (52);
    modeLabel.setBounds (modeRow.removeFromLeft (72));
    mode.setBounds (modeRow.removeFromLeft (160));
    modeRow.removeFromLeft (18);
    external.setBounds (modeRow.removeFromLeft (190));
    listen.setBounds (modeRow.removeFromLeft (170));
    grLabel.setBounds (modeRow);

    area.removeFromTop (16);
    auto knobs = area.removeFromTop (150);
    const int cellW = knobs.getWidth() / 9;
    auto placeKnob = [&knobs, cellW] (juce::Slider& slider, juce::Label& label)
    {
        auto cell = knobs.removeFromLeft (cellW);
        label.setBounds (cell.removeFromTop (22));
        slider.setBounds (cell);
    };

    placeKnob (threshold, thresholdLabel);
    placeKnob (ratio, ratioLabel);
    placeKnob (attack, attackLabel);
    placeKnob (release, releaseLabel);
    placeKnob (depth, depthLabel);
    placeKnob (makeup, makeupLabel);
    placeKnob (mix, mixLabel);
    placeKnob (sidechainHpf, hpfLabel);
    placeKnob (lookahead, lookaheadLabel);

    // Tempo-sync row: toggle + rate + curve, with the GR history graph on the right.
    area.removeFromTop (16);
    auto syncRow = area.removeFromTop (54);
    syncDuck.setBounds (syncRow.removeFromLeft (150).withTrimmedTop (16));
    syncRateLabel.setBounds (syncRow.removeFromLeft (74).withTrimmedTop (16));
    syncRate.setBounds (syncRow.removeFromLeft (90).withTrimmedTop (12).withTrimmedBottom (8));
    syncRow.removeFromLeft (14);
    {
        auto cell = syncRow.removeFromLeft (cellW);
        syncCurveLabel.setBounds (cell.removeFromTop (22));
        syncCurve.setBounds (cell);
    }

    area.removeFromTop (10);
    auto graphRow = area.removeFromTop (juce::jmax (60, area.getHeight()));
    grGraphBounds = graphRow.reduced (2);
}
