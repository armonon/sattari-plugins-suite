#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

void VoxKeyOrb::setState (const VoxKeyVoiceMidiMapper::State& newState)
{
    state = newState;
    repaint();
}

void VoxKeyOrb::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (juce::Colour (0xff080913));
    g.fillRoundedRectangle (area, 22.0f);

    const auto glow = state.midiActive ? juce::Colour (0xff7cffd4) : (state.voiced ? juce::Colour (0xffffb45e) : juce::Colour (0xff4b4f66));
    g.setColour (glow.withAlpha (0.32f));
    g.fillEllipse (area.reduced (36.0f));
    g.setColour (glow);
    g.drawEllipse (area.reduced (36.0f), 3.0f);

    g.setFont (juce::FontOptions (48.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawFittedText (state.outputName, area.toNearestInt().withTrimmedBottom (60), juce::Justification::centred, 1);

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colours::lightgrey);
    g.drawFittedText ("detected " + state.detectedName + "  •  confidence " + juce::String ((int) std::round (state.confidence * 100.0f)) + "%",
                      area.toNearestInt().removeFromBottom (42), juce::Justification::centred, 2);

    const float level = juce::jlimit (0.0f, 1.0f, state.inputRms * 18.0f);
    auto meter = area.reduced (18.0f).removeFromBottom (8.0f);
    g.setColour (juce::Colour (0xff202439));
    g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (glow);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * level), 4.0f);
}

SattariVoxKeyAudioProcessorEditor::SattariVoxKeyAudioProcessorEditor (SattariVoxKeyAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "voxkey")
{
    setLookAndFeel (&lookAndFeel);
    setSize (860, 520);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari VoxKey");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Voice-to-MIDI instrument with dry vocal pass-through");
    addAndMakeVisible (subtitle);

    setupCombo (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };
    refreshUserPresets();

    setupCombo (mode);
    mode.addItemList ({ "Melody", "Bass Follow", "Harmony", "Drum Mouth", "Pad/Soul Layer" }, 1);
    setupCombo (key);
    key.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    setupCombo (scale);
    scale.addItemList ({ "Chromatic", "Major", "Minor", "Pentatonic" }, 1);

    setupSlider (sensitivity);
    setupSlider (smoothness);
    setupSlider (confidence);
    setupSlider (octave);
    setupSlider (hold, " ms");
    setupSlider (release, " ms");
    setupSlider (velocity);
    setupSlider (pitchBendRange, " st");

    sensitivity.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    smoothness.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    confidence.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    velocity.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };

    setupToggle (pitchBend);
    pitchBend.setButtonText ("Pitch Bend");
    setupToggle (glide);
    glide.setButtonText ("Glide / Legato");
    setupToggle (bypass);
    bypass.setButtonText ("Bypass");
    setupToggle (paraphonic);
    paraphonic.setButtonText ("Paraphonic (alpha)");

    panicButton.onClick = [this] { audioProcessor.panic(); };
    panicButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1320));
    panicButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible (panicButton);

    setupLabel (inputLabel, "Performance");
    setupLabel (routingLabel, "Ableton routing: Audio track has VoxKey; MIDI track receives MIDI From VoxKey, Monitor In/Auto.");
    setupLabel (detectedLabel, "Detected: --");
    setupLabel (outputLabel, "MIDI Out: --");
    setupLabel (confidenceLabel, "MIDI activity: idle");
    addAndMakeVisible (orb);

    auto& params = audioProcessor.parameters();
    modeAttach = std::make_unique<ComboAttachment> (params, "mode", mode);
    keyAttach = std::make_unique<ComboAttachment> (params, "key", key);
    scaleAttach = std::make_unique<ComboAttachment> (params, "scale", scale);
    sensitivityAttach = std::make_unique<SliderAttachment> (params, "sensitivity", sensitivity);
    smoothnessAttach = std::make_unique<SliderAttachment> (params, "smoothness", smoothness);
    confidenceAttach = std::make_unique<SliderAttachment> (params, "confidence", confidence);
    octaveAttach = std::make_unique<SliderAttachment> (params, "octave", octave);
    holdAttach = std::make_unique<SliderAttachment> (params, "hold", hold);
    releaseAttach = std::make_unique<SliderAttachment> (params, "release", release);
    velocityAttach = std::make_unique<SliderAttachment> (params, "velocity", velocity);
    pitchBendRangeAttach = std::make_unique<SliderAttachment> (params, "pitchBendRange", pitchBendRange);
    pitchBendAttach = std::make_unique<ButtonAttachment> (params, "pitchBend", pitchBend);
    glideAttach = std::make_unique<ButtonAttachment> (params, "glide", glide);
    bypassAttach = std::make_unique<ButtonAttachment> (params, "bypass", bypass);
    paraphonicAttach = std::make_unique<ButtonAttachment> (params, "paraphonic", paraphonic);

    startTimerHz (20);
}

SattariVoxKeyAudioProcessorEditor::~SattariVoxKeyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariVoxKeyAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariVoxKeyAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariVoxKeyAudioProcessorEditor::setupSlider (juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff7cffd4));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff272c42));
    addAndMakeVisible (slider);
}

void SattariVoxKeyAudioProcessorEditor::setupCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111525));
    combo.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff7cffd4));
    addAndMakeVisible (combo);
}

void SattariVoxKeyAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& text, float size)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (size, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colour (0xffffd3a1));
    label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (label);
}

void SattariVoxKeyAudioProcessorEditor::setupToggle (juce::ToggleButton& button)
{
    button.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    button.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff7cffd4));
    addAndMakeVisible (button);
}

void SattariVoxKeyAudioProcessorEditor::timerCallback()
{
    updateMeters();
}

void SattariVoxKeyAudioProcessorEditor::updateMeters()
{
    const auto state = audioProcessor.getDisplayState();
    orb.setState (state);
    detectedLabel.setText ("Detected: " + state.detectedName, juce::dontSendNotification);
    outputLabel.setText ("MIDI Out: " + state.outputName, juce::dontSendNotification);
    confidenceLabel.setText (state.midiActive ? "MIDI activity: sending" : (state.voiced ? "MIDI activity: tracking" : "MIDI activity: idle"), juce::dontSendNotification);
}

void SattariVoxKeyAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariVoxKeyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    title.setBounds (area.removeFromTop (40));
    subtitle.setBounds (area.removeFromTop (24));

    auto presetRow = area.removeFromTop (34);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (120));

    area.removeFromTop (10);

    auto left = area.removeFromLeft (500);
    area.removeFromLeft (18);
    auto right = area;

    inputLabel.setBounds (left.removeFromTop (24));
    auto combos = left.removeFromTop (40);
    mode.setBounds (combos.removeFromLeft (160));
    combos.removeFromLeft (8);
    key.setBounds (combos.removeFromLeft (80));
    combos.removeFromLeft (8);
    scale.setBounds (combos.removeFromLeft (130));

    left.removeFromTop (14);
    auto row1 = left.removeFromTop (124);
    sensitivity.setBounds (row1.removeFromLeft (100));
    smoothness.setBounds (row1.removeFromLeft (100));
    confidence.setBounds (row1.removeFromLeft (100));
    octave.setBounds (row1.removeFromLeft (100));

    left.removeFromTop (12);
    auto row2 = left.removeFromTop (124);
    hold.setBounds (row2.removeFromLeft (100));
    release.setBounds (row2.removeFromLeft (100));
    velocity.setBounds (row2.removeFromLeft (100));
    pitchBendRange.setBounds (row2.removeFromLeft (100));

    left.removeFromTop (8);
    auto toggleRow = left.removeFromTop (32);
    pitchBend.setBounds (toggleRow.removeFromLeft (120));
    glide.setBounds (toggleRow.removeFromLeft (140));
    bypass.setBounds (toggleRow.removeFromLeft (100));
    panicButton.setBounds (toggleRow.removeFromLeft (84));
    paraphonic.setBounds (toggleRow.removeFromLeft (160));

    left.removeFromTop (10);
    routingLabel.setBounds (left.removeFromTop (48));

    orb.setBounds (right.removeFromTop (290));
    right.removeFromTop (12);
    detectedLabel.setBounds (right.removeFromTop (30));
    outputLabel.setBounds (right.removeFromTop (30));
    confidenceLabel.setBounds (right.removeFromTop (30));
}
