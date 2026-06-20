#include "PluginEditor.h"

SattariAlignAudioProcessorEditor::SattariAlignAudioProcessorEditor (SattariAlignAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "align")
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 460);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Align");
    addAndMakeVisible (title);
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle,
        "Time-aligns Main to the Guide sidechain via onset correlation. Works on any transient material.");
    addAndMakeVisible (subtitle);

    styleKnob (searchRange);
    searchRange.setTextValueSuffix (" ms");
    styleKnob (sensitivity);
    sensitivity.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    styleKnob (smoothing);
    smoothing.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    styleKnob (manualOffset, true);
    manualOffset.setTextValueSuffix (" ms");

    styleCaption (searchCaption,     "SEARCH RANGE");
    styleCaption (sensitivityCaption,"SENSITIVITY");
    styleCaption (smoothingCaption,  "SMOOTHING");
    styleCaption (manualCaption,     "MANUAL OFFSET");

    for (auto* s : { &searchRange, &sensitivity, &smoothing, &manualOffset })
        addAndMakeVisible (*s);
    for (auto* l : { &searchCaption, &sensitivityCaption, &smoothingCaption, &manualCaption })
        addAndMakeVisible (*l);

    autoAlign.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    lock.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    bypass.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (autoAlign);
    addAndMakeVisible (lock);
    addAndMakeVisible (bypass);

    offsetReadout.setFont (juce::FontOptions (44.0f, juce::Font::plain));
    offsetReadout.setJustificationType (juce::Justification::centred);
    offsetReadout.setColour (juce::Label::textColourId,
                              juce::Colour (sattari::brand::productAccent ("align")));
    addAndMakeVisible (offsetReadout);

    confidenceLabel.setFont (juce::FontOptions (12.0f));
    confidenceLabel.setJustificationType (juce::Justification::centred);
    confidenceLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (confidenceLabel);

    guideStatus.setFont (juce::FontOptions (11.0f));
    guideStatus.setJustificationType (juce::Justification::centredRight);
    guideStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (guideStatus);

    refreshUserPresets();
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] {
        const auto n = userPresets.getText();
        if (n.isNotEmpty()) presetManager.loadUserPreset (n);
    };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);

    auto& params = audioProcessor.parameters();
    searchAttach = std::make_unique<SliderAttachment> (params, "searchRangeMs", searchRange);
    sensAttach   = std::make_unique<SliderAttachment> (params, "sensitivity", sensitivity);
    smoothAttach = std::make_unique<SliderAttachment> (params, "smoothing", smoothing);
    manualAttach = std::make_unique<SliderAttachment> (params, "manualOffsetMs", manualOffset);
    autoAttach   = std::make_unique<ButtonAttachment> (params, "autoAlign", autoAlign);
    lockAttach   = std::make_unique<ButtonAttachment> (params, "lock", lock);
    bypassAttach = std::make_unique<ButtonAttachment> (params, "bypass", bypass);

    startTimerHz (20);
}

SattariAlignAudioProcessorEditor::~SattariAlignAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariAlignAudioProcessorEditor::styleKnob (juce::Slider& s, bool feature)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, feature ? 96 : 78, feature ? 22 : 18);
}

void SattariAlignAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariAlignAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : presetManager.userPresetNames())
        userPresets.addItem (n, id++);
}

void SattariAlignAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariAlignAudioProcessorEditor::timerCallback()
{
    const auto s = audioProcessor.getLiveSnapshot();
    const juce::String sign = (s.detectedOffsetMs > 0.05f) ? "+" : (s.detectedOffsetMs < -0.05f ? "-" : "");
    const float abs_ms = std::fabs (s.detectedOffsetMs);
    offsetReadout.setText (sign + juce::String (abs_ms, 1) + " ms", juce::dontSendNotification);
    confidenceLabel.setText ("Correlation " + juce::String ((int) std::round (s.confidence * 100.0f)) + "%"
                              + (s.locked ? "  *  Locked" : juce::String()),
                              juce::dontSendNotification);
    guideStatus.setText (s.guideActive ? "Guide: active" : "Guide: silent / not connected",
                         juce::dontSendNotification);
}

void SattariAlignAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariAlignAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    title.setBounds (area.removeFromTop (38));
    subtitle.setBounds (area.removeFromTop (22));

    auto presetRow = area.removeFromTop (32);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));
    guideStatus.setBounds (presetRow.removeFromRight (220));

    area.removeFromTop (8);

    auto readoutArea = area.removeFromTop (110);
    offsetReadout.setBounds (readoutArea.removeFromTop (66));
    confidenceLabel.setBounds (readoutArea);

    area.removeFromTop (10);
    auto knobsRow = area.removeFromTop (150);
    const int kw = knobsRow.getWidth() / 4;
    auto srCell = knobsRow.removeFromLeft (kw);
    searchCaption.setBounds (srCell.removeFromTop (14));
    searchRange.setBounds (srCell);
    auto seCell = knobsRow.removeFromLeft (kw);
    sensitivityCaption.setBounds (seCell.removeFromTop (14));
    sensitivity.setBounds (seCell);
    auto smCell = knobsRow.removeFromLeft (kw);
    smoothingCaption.setBounds (smCell.removeFromTop (14));
    smoothing.setBounds (smCell);
    auto moCell = knobsRow;
    manualCaption.setBounds (moCell.removeFromTop (14));
    manualOffset.setBounds (moCell);

    area.removeFromTop (16);
    auto toggleRow = area.removeFromTop (28);
    autoAlign.setBounds (toggleRow.removeFromLeft (140));
    lock.setBounds (toggleRow.removeFromLeft (100));
    bypass.setBounds (toggleRow.removeFromLeft (100));
}
