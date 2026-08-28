#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

namespace { const juce::StringArray modeChoices { "Clean", "Glue", "Console", "Tape" }; }

SattariBusAudioProcessorEditor::SattariBusAudioProcessorEditor (SattariBusAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "bus")
{
    setLookAndFeel (&lookAndFeel);
    setSize (720, 620);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Bus");
    addAndMakeVisible (title);
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Bus / aux channel strip - glue, tone, drive, width, the works");
    addAndMakeVisible (subtitle);

    mode.addItemList (modeChoices, 1);
    addAndMakeVisible (mode);
    modeAttach = std::make_unique<ComboAttachment> (p.parameters(), "mode", mode);

    registerFactoryPresets();
    factoryPresets.setTextWhenNothingSelected ("Factory presets...");
    factoryPresets.addItemList (presetManager.factoryPresetNames(), 1);
    factoryPresets.onChange = [this]
    {
        const int idx = factoryPresets.getSelectedItemIndex();
        if (idx >= 0)
            presetManager.loadFactoryPreset (idx);
    };
    addAndMakeVisible (factoryPresets);

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

    addAndMakeVisible (abRow);

    mute.setButtonText ("Mute");
    bypass.setButtonText ("Bypass");
    msMode.setButtonText ("M/S");
    addAndMakeVisible (mute);
    addAndMakeVisible (bypass);
    addAndMakeVisible (msMode);
    muteAttach   = std::make_unique<ButtonAttachment> (p.parameters(), "mute", mute);
    bypassAttach = std::make_unique<ButtonAttachment> (p.parameters(), "bypass", bypass);
    msModeAttach = std::make_unique<ButtonAttachment> (p.parameters(), "msMode", msMode);

    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] { showMidiLearnPopup(); };

    grLabel.setJustificationType (juce::Justification::centredRight);
    grLabel.setColour (juce::Label::textColourId, lookAndFeel.getAccent());
    grLabel.setFont (sattari::brand::fontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (grLabel);

    addKnob (inputTrim, "inputTrim", "TRIM", " dB");
    addKnob (hpf, "hpf", "HPF", " Hz");
    addKnob (lpf, "lpf", "LPF", " Hz");
    addKnob (low, "low", "LOW", " dB");
    addKnob (mid, "mid", "MID", " dB");
    addKnob (high, "high", "HIGH", " dB");
    addKnob (threshold, "threshold", "THRESH", " dB");
    addKnob (ratio, "ratio", "RATIO", ":1");
    addKnob (attack, "attack", "ATTACK", " ms");
    addKnob (release, "release", "RELEASE", " ms");
    addKnob (makeup, "makeup", "MAKEUP", " dB");
    addKnob (drive, "drive", "DRIVE");
    addKnob (width, "width", "WIDTH");
    addKnob (pan, "pan", "PAN");
    addKnob (mix, "mix", "MIX", " %");
    addKnob (output, "output", "OUTPUT", " dB");
    addKnob (ceiling, "ceiling", "CEILING", " dB");

    startTimerHz (15);
}

SattariBusAudioProcessorEditor::~SattariBusAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariBusAudioProcessorEditor::addKnob (Knob& k, const juce::String& paramId,
                                              const juce::String& caption, const juce::String& suffix)
{
    sattari::ui::SattariLookAndFeel::styleRotary (k.slider, suffix);
    addAndMakeVisible (k.slider);
    k.label.setText (caption, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<SliderAttachment> (processor.parameters(), paramId, k.slider);
}

void SattariBusAudioProcessorEditor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // mode: 0 Clean, 1 Glue, 2 Console, 3 Tape. Only listed params are set.
    presetManager.addFactoryPreset ("Mix Glue", PV {
        {"mode",1},{"threshold",-18},{"ratio",2},{"attack",15},{"release",200},
        {"makeup",2},{"drive",0.12f},{"width",1.0f},{"mix",100},{"ceiling",-0.3f} });
    presetManager.addFactoryPreset ("Drum Bus", PV {
        {"mode",2},{"hpf",35},{"threshold",-16},{"ratio",4},{"attack",5},{"release",120},
        {"makeup",2.5f},{"drive",0.22f},{"low",1.5f},{"high",2},{"width",1.1f},{"mix",100},{"ceiling",-0.3f} });
    presetManager.addFactoryPreset ("Master Polish", PV {
        {"mode",0},{"threshold",-10},{"ratio",1.5f},{"attack",30},{"release",250},
        {"makeup",0.5f},{"drive",0.05f},{"low",0.5f},{"high",1},{"width",1.0f},{"mix",100},{"ceiling",-0.3f} });
    presetManager.addFactoryPreset ("Parallel Crush", PV {
        {"mode",1},{"threshold",-30},{"ratio",8},{"attack",1},{"release",80},
        {"makeup",4},{"drive",0.35f},{"width",1.0f},{"mix",35},{"ceiling",-0.3f} });
    presetManager.addFactoryPreset ("Tape Warm", PV {
        {"mode",3},{"threshold",-20},{"ratio",2},{"attack",20},{"release",220},
        {"makeup",1.5f},{"drive",0.3f},{"low",1.5f},{"high",-1.5f},{"width",1.05f},{"mix",100},{"ceiling",-0.5f} });
    presetManager.addFactoryPreset ("Wide & Open", PV {
        {"mode",0},{"threshold",-14},{"ratio",2},{"attack",20},{"release",180},
        {"makeup",1},{"drive",0.08f},{"low",0.5f},{"high",1.5f},{"width",1.3f},{"mix",100},{"ceiling",-0.3f} });
}

void SattariBusAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariBusAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariBusAudioProcessorEditor::timerCallback()
{
    grLabel.setText ("GR " + juce::String (processor.getGainReductionDb(), 1) + " dB", juce::dontSendNotification);
}

void SattariBusAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariBusAudioProcessorEditor::showMidiLearnPopup()
{
    sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
}

void SattariBusAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);

    title.setBounds (area.removeFromTop (38));
    subtitle.setBounds (area.removeFromTop (22));

    auto presetRow = area.removeFromTop (34);
    factoryPresets.setBounds (presetRow.removeFromLeft (175));
    presetRow.removeFromLeft (8);
    userPresets.setBounds (presetRow.removeFromLeft (140));
    presetRow.removeFromLeft (6);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    grLabel.setBounds (presetRow.removeFromRight (110));

    area.removeFromTop (8);
    auto modeRow = area.removeFromTop (32);
    mode.setBounds (modeRow.removeFromLeft (170));
    modeRow.removeFromLeft (16);
    mute.setBounds (modeRow.removeFromLeft (90));
    bypass.setBounds (modeRow.removeFromLeft (100));
    msMode.setBounds (modeRow.removeFromLeft (80));
    modeRow.removeFromLeft (8);
    midiLearnButton.setBounds (modeRow.removeFromLeft (120));

    area.removeFromTop (12);

    // 17 knobs in a 6-column grid.
    Knob* knobs[] = { &inputTrim, &hpf, &lpf, &low, &mid, &high,
                      &threshold, &ratio, &attack, &release, &makeup, &drive,
                      &width, &pan, &mix, &output, &ceiling };
    const int cols = 6;
    const int cellW = area.getWidth() / cols;
    const int cellH = 124;

    int idx = 0;
    for (auto* k : knobs)
    {
        const int row = idx / cols;
        const int col = idx % cols;
        juce::Rectangle<int> cell (area.getX() + col * cellW, area.getY() + row * cellH, cellW, cellH);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.reduced (4));
        ++idx;
    }
}
