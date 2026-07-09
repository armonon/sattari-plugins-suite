#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

namespace { const juce::StringArray modelChoices { "Tube", "Tape", "Transformer", "Transistor" }; }

SattariHeatAudioProcessorEditor::SattariHeatAudioProcessorEditor (SattariHeatAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "heat")
{
    setLookAndFeel (&lookAndFeel);
    setSize (560, 430);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Heat");
    addAndMakeVisible (title);
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle,
        "Saturation / tone box - tube, tape, transformer, transistor");
    addAndMakeVisible (subtitle);

    model.addItemList (modelChoices, 1);
    addAndMakeVisible (model);
    modelAttach = std::make_unique<ComboAttachment> (p.parameters(), "model", model);

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

    bypass.setButtonText ("Bypass");
    addAndMakeVisible (bypass);
    bypassAttach = std::make_unique<ButtonAttachment> (p.parameters(), "bypass", bypass);

    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] { showMidiLearnPopup(); };

    heatLabel.setJustificationType (juce::Justification::centredRight);
    heatLabel.setColour (juce::Label::textColourId, lookAndFeel.getAccent());
    heatLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (heatLabel);

    addKnob (drive,  "drive",  "DRIVE",  " dB");
    addKnob (tone,   "tone",   "TONE");
    addKnob (bias,   "bias",   "BIAS");
    addKnob (mix,    "mix",    "MIX");
    addKnob (output, "output", "OUTPUT", " dB");

    startTimerHz (15);
}

SattariHeatAudioProcessorEditor::~SattariHeatAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariHeatAudioProcessorEditor::addKnob (Knob& k, const juce::String& paramId,
                                               const juce::String& caption, const juce::String& suffix)
{
    sattari::ui::SattariLookAndFeel::styleRotary (k.slider, suffix);
    addAndMakeVisible (k.slider);
    k.label.setText (caption, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<SliderAttachment> (processor.parameters(), paramId, k.slider);
}

void SattariHeatAudioProcessorEditor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // model: 0 Tube, 1 Tape, 2 Transformer, 3 Transistor. Only listed set.
    presetManager.addFactoryPreset ("Tube Warmth", PV {
        {"model",0},{"drive",8},{"output",-1.5f},{"tone",0.15f},{"mix",1.0f},{"bias",0.25f} });
    presetManager.addFactoryPreset ("Tape Glue", PV {
        {"model",1},{"drive",10},{"output",-2.0f},{"tone",-0.2f},{"mix",1.0f},{"bias",0.1f} });
    presetManager.addFactoryPreset ("Iron Transformer", PV {
        {"model",2},{"drive",14},{"output",-3.0f},{"tone",0.0f},{"mix",1.0f},{"bias",0.35f} });
    presetManager.addFactoryPreset ("Parallel Grit", PV {
        {"model",3},{"drive",22},{"output",-1.0f},{"tone",0.3f},{"mix",0.4f},{"bias",0.0f} });
    presetManager.addFactoryPreset ("Bright Bite", PV {
        {"model",3},{"drive",16},{"output",-2.5f},{"tone",0.5f},{"mix",0.7f},{"bias",-0.2f} });
    presetManager.addFactoryPreset ("Clean / Off", PV {
        {"model",0},{"drive",0},{"output",0.0f},{"tone",0.0f},{"mix",1.0f},{"bias",0.0f} });
}

void SattariHeatAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariHeatAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariHeatAudioProcessorEditor::timerCallback()
{
    const int pct = juce::roundToInt (processor.getDriveMeter() * 100.0f);
    heatLabel.setText ("HEAT " + juce::String (pct) + "%", juce::dontSendNotification);
}

void SattariHeatAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariHeatAudioProcessorEditor::showMidiLearnPopup()
{
    sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
}

void SattariHeatAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);

    title.setBounds (area.removeFromTop (36));
    subtitle.setBounds (area.removeFromTop (20));

    auto presetRow = area.removeFromTop (32);
    factoryPresets.setBounds (presetRow.removeFromLeft (165));
    presetRow.removeFromLeft (6);
    userPresets.setBounds (presetRow.removeFromLeft (120));
    presetRow.removeFromLeft (6);
    abRow.setBounds (presetRow.removeFromLeft (90));
    presetRow.removeFromLeft (6);
    savePresetButton.setBounds (presetRow.removeFromLeft (96));
    heatLabel.setBounds (presetRow.removeFromRight (96));

    area.removeFromTop (8);
    auto modeRow = area.removeFromTop (30);
    model.setBounds (modeRow.removeFromLeft (170));
    modeRow.removeFromLeft (14);
    bypass.setBounds (modeRow.removeFromLeft (90));
    modeRow.removeFromLeft (8);
    midiLearnButton.setBounds (modeRow.removeFromLeft (120));

    area.removeFromTop (14);

    // 5 knobs in one row.
    Knob* knobs[] = { &drive, &tone, &bias, &mix, &output };
    const int cols = 5;
    const int cellW = area.getWidth() / cols;
    const int cellH = juce::jmin (150, area.getHeight());

    int idx = 0;
    for (auto* k : knobs)
    {
        juce::Rectangle<int> cell (area.getX() + idx * cellW, area.getY(), cellW, cellH);
        k->label.setBounds (cell.removeFromTop (16));
        k->slider.setBounds (cell.reduced (4));
        ++idx;
    }
}
