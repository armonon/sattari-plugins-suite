#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

#include <cmath>

SubConjurerAudioProcessorEditor::SubConjurerAudioProcessorEditor (
    SubConjurerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      presetManager (p.parameters(), "sub-conjurer")
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (820, 560, 1400, 880);
    setSize (1020, 650);
    setTitle ("Sub Conjurer");
    setDescription ("Pitch-aware low-end generation and reinforcement");

    addAndMakeVisible (header);

    setupCombo (preset, "Factory presets");
    preset.addItemList (juce::StringArray {
        "Clean 808", "Heavy Trap Sub", "Bass Guitar Weight", "Kick Reinforcer",
        "Club Low-End", "Warm R&B Bass", "Subtle Mix Weight", "Deep Synth Bass",
        "Tight Low-End", "Cinematic Sub"
    }, 1);
    preset.setTextWhenNothingSelected ("Factory presets");
    preset.onChange = [this] { applyPreset (preset.getSelectedItemIndex()); };

    setupCombo (userPresets, "User presets");
    userPresets.setTextWhenNothingSelected ("User presets");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty() && ! presetManager.loadUserPreset (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Could not load preset", "The preset could not be read. Your settings are unchanged.");
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    refreshUserPresets();

    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (
            audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };
    addAndMakeVisible (midiLearnButton);

    setupCaption (modeLabel, "Character");
    setupCombo (mode, "Character");
    mode.addItemList (juce::StringArray {
        "Clean", "Power", "Deep", "808", "Perc-to-Sub"
    }, 1);
    setupCaption (tuningLabel, "Tracking");
    setupCombo (tuning, "Tracking");
    tuning.addItemList (juce::StringArray {
        "Follow Pitch", "Lock Note", "MIDI Note"
    }, 1);
    setupCaption (noteLabel, "Root");
    setupCombo (note, "Root note");
    note.addItemList (juce::StringArray {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    }, 1);

    for (auto* macro : {
            &amount, &dry, &richness, &saturation, &octave,
            &glide, &attack, &release, &limiter })
        addAndMakeVisible (*macro);

    addAndMakeVisible (subOnly);
    addAndMakeVisible (followKey);
    addAndMakeVisible (pitchMetric);
    addAndMakeVisible (inputMetric);
    addAndMakeVisible (outputMetric);

    auto& params = audioProcessor.parameters();
    modeAttach = std::make_unique<ComboAttachment> (params, "mode", mode);
    tuningAttach = std::make_unique<ComboAttachment> (params, "tuning", tuning);
    noteAttach = std::make_unique<ComboAttachment> (params, "note", note);
    octaveAttach = std::make_unique<SliderAttachment> (
        params, "octave", octave.slider());
    amountAttach = std::make_unique<SliderAttachment> (
        params, "amount", amount.slider());
    dryAttach = std::make_unique<SliderAttachment> (
        params, "dry", dry.slider());
    richnessAttach = std::make_unique<SliderAttachment> (
        params, "richness", richness.slider());
    glideAttach = std::make_unique<SliderAttachment> (
        params, "glide", glide.slider());
    saturationAttach = std::make_unique<SliderAttachment> (
        params, "saturation", saturation.slider());
    attackAttach = std::make_unique<SliderAttachment> (
        params, "attack", attack.slider());
    releaseAttach = std::make_unique<SliderAttachment> (
        params, "release", release.slider());
    limiterAttach = std::make_unique<SliderAttachment> (
        params, "limiter", limiter.slider());
    subOnlyAttach = std::make_unique<ButtonAttachment> (
        params, "subonly", subOnly);
    followKeyAttach = std::make_unique<ButtonAttachment> (
        params, "followKey", followKey);

    for (auto* macro : { &amount, &dry, &richness, &saturation, &glide })
    {
        auto& slider = macro->slider();
        slider.setTextValueSuffix ("%");
        slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)); };
        slider.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100.0; };
        slider.updateText();
    }
    for (auto* macro : { &attack, &release })
    {
        auto& slider = macro->slider();
        slider.setTextValueSuffix (" ms");
        slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 1000)); };
        slider.valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 1000.0; };
        slider.updateText();
    }
    limiter.slider().setTextValueSuffix (" dB");
    limiter.slider().textFromValueFunction = [] (double v) { return juce::String (juce::Decibels::gainToDecibels (v), 1); };
    limiter.slider().valueFromTextFunction = [] (const juce::String& value) { return juce::Decibels::decibelsToGain (value.getDoubleValue()); };
    limiter.slider().updateText();

    auto syncNoteEnabled = [this]
    {
        note.setEnabled (! followKey.getToggleState());
    };
    followKey.onStateChange = syncNoteEnabled;
    syncNoteEnabled();

    startTimerHz (24);
    timerCallback();
}

SubConjurerAudioProcessorEditor::~SubConjurerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SubConjurerAudioProcessorEditor::setupCombo (
    juce::ComboBox& combo, const juce::String& title)
{
    combo.setTitle (title);
    combo.setDescription ("Choose " + title.toLowerCase());
    addAndMakeVisible (combo);
}

void SubConjurerAudioProcessorEditor::setupCaption (
    juce::Label& label, const juce::String& text)
{
    sattari::releaseui::caption (label, text);
    addAndMakeVisible (label);
}

void SubConjurerAudioProcessorEditor::timerCallback()
{
    uiState = audioProcessor.currentUiState();
    const bool live = uiState.inputPeak > 0.002f || uiState.envelope > 0.002f;
    header.setStatus (live ? "GENERATING SUB" : "READY", live);
    pitchMetric.setValue (
        uiState.frequencyHz > 0.0f ? juce::String (uiState.frequencyHz, 1) : "--",
        uiState.followingKey ? "Hz / Compass" : "Hz");
    inputMetric.setValue (
        juce::String (juce::Decibels::gainToDecibels (
            juce::jmax (uiState.inputPeak, 1.0e-6f)), 1),
        "dBFS");
    outputMetric.setValue (
        juce::String (juce::Decibels::gainToDecibels (
            juce::jmax (uiState.outputPeak, 1.0e-6f)), 1),
        uiState.limiterGain < 0.999f ? "dBFS / limiting" : "dBFS");
    repaint (displayArea);
}

void SubConjurerAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SubConjurerAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = presetManager.newUserPresetName();
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
    else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
        "Could not save preset", "The preset folder could not be written. Your settings remain active.");
}

void SubConjurerAudioProcessorEditor::setParameterValue (
    const juce::String& parameterID, float value)
{
    if (auto* parameter = audioProcessor.parameters().getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void SubConjurerAudioProcessorEditor::applyPreset (int presetIndex)
{
    struct Preset
    {
        int mode, tuning, note, octave;
        float amount, dry, richness, glide, saturation, attack, release, limiter;
    };

    static constexpr Preset presets[] = {
        { 3, 1, 9, 1, 0.78f, 0.28f, 0.20f, 0.00f, 0.20f, 0.006f, 0.160f, 0.82f },
        { 3, 1, 9, 1, 1.25f, 0.22f, 0.58f, 0.06f, 0.65f, 0.004f, 0.260f, 0.78f },
        { 1, 0, 7, 1, 0.72f, 0.52f, 0.34f, 0.15f, 0.25f, 0.012f, 0.220f, 0.84f },
        { 4, 0, 9, 1, 1.05f, 0.40f, 0.26f, 0.00f, 0.30f, 0.002f, 0.120f, 0.80f },
        { 1, 1, 7, 1, 1.18f, 0.32f, 0.45f, 0.04f, 0.45f, 0.007f, 0.280f, 0.78f },
        { 0, 1, 9, 1, 0.62f, 0.55f, 0.24f, 0.10f, 0.15f, 0.018f, 0.340f, 0.86f },
        { 0, 0, 9, 1, 0.35f, 0.70f, 0.10f, 0.05f, 0.05f, 0.020f, 0.200f, 0.88f },
        { 2, 1, 7, 1, 1.05f, 0.20f, 0.36f, 0.25f, 0.35f, 0.015f, 0.460f, 0.76f },
        { 1, 1, 9, 1, 0.82f, 0.36f, 0.18f, 0.00f, 0.20f, 0.003f, 0.080f, 0.84f },
        { 2, 1, 0, 1, 1.32f, 0.18f, 0.50f, 0.30f, 0.40f, 0.030f, 0.620f, 0.74f }
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& value = presets[presetIndex];
    setParameterValue ("mode", (float) value.mode);
    setParameterValue ("tuning", (float) value.tuning);
    setParameterValue ("note", (float) value.note);
    setParameterValue ("octave", (float) value.octave);
    setParameterValue ("amount", value.amount);
    setParameterValue ("dry", value.dry);
    setParameterValue ("richness", value.richness);
    setParameterValue ("glide", value.glide);
    setParameterValue ("saturation", value.saturation);
    setParameterValue ("attack", value.attack);
    setParameterValue ("release", value.release);
    setParameterValue ("limiter", value.limiter);
}

void SubConjurerAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace sattari::releaseui;
    g.fillAll (canvas);
    g.setColour (surface); g.fillRect (detailArea);
    g.setColour (line);
    g.drawHorizontalLine (124, 0, (float) getWidth());
    g.drawHorizontalLine (detailArea.getY(), 0, (float) getWidth());
    g.drawVerticalLine (primaryArea.getX() - 12, (float) workspaceArea.getY() + 18, (float) workspaceArea.getBottom() - 18);
    paintSubDisplay (g);
    section (g, primaryArea.withHeight (24), "Tone");
    section (g, detailArea.reduced (22, 8).removeFromTop (22), "Tracking & envelope");
}

void SubConjurerAudioProcessorEditor::paintSubDisplay (juce::Graphics& g)
{
    auto display = displayArea;
    sattari::releaseui::section (
        g, display.removeFromTop (20),
        "Oscillator contour", uiState.limiterGain < 0.999f ? "Limiting" : "");

    display.removeFromBottom (70);
    auto meters = display.removeFromRight (42).reduced (4, 8);
    display.removeFromRight (14);
    auto plot = display.reduced (0, 8).toFloat();
    const float centreY = plot.getCentreY();
    g.setColour (sattari::releaseui::line.withAlpha (0.65f));
    for (int i = 1; i < 4; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
    }
    g.drawHorizontalLine ((int) centreY, plot.getX(), plot.getRight());

    juce::Path waveform;
    const float visibleCycles = juce::jlimit (
        1.2f, 6.0f, uiState.frequencyHz / 28.0f);
    const float envelope = juce::jlimit (
        0.0f, 1.0f, uiState.envelope * 2.8f);
    for (int x = 0; x <= (int) plot.getWidth(); ++x)
    {
        const float t = (float) x / juce::jmax (1.0f, plot.getWidth());
        const float taper = std::sin (t * juce::MathConstants<float>::pi);
        const float sample = std::sin (
            t * visibleCycles * juce::MathConstants<float>::twoPi);
        const float y = centreY - sample * taper * envelope * plot.getHeight() * 0.35f;
        if (x == 0) waveform.startNewSubPath (plot.getX(), y);
        else waveform.lineTo (plot.getX() + (float) x, y);
    }

    g.setColour (accent);
    g.strokePath (waveform, juce::PathStrokeType (
        1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (float peak : { uiState.inputPeak, uiState.outputPeak })
    {
        auto meter = meters.removeFromLeft (9).toFloat(); meters.removeFromLeft (9);
        g.setColour (sattari::releaseui::raised); g.fillRect (meter);
        const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (peak, -60.0f) + 60) / 60);
        g.setColour (peak > 0.98f ? sattari::releaseui::red : accent);
        g.fillRect (meter.removeFromBottom (meter.getHeight() * level));
    }
}

void SubConjurerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    header.setBounds (area.removeFromTop (72));
    auto command = area.removeFromTop (52).reduced (22, 9);
    preset.setBounds (command.removeFromLeft (190));
    command.removeFromLeft (8);
    userPresets.setBounds (command.removeFromLeft (170));
    command.removeFromLeft (8);
    savePresetButton.setBounds (command.removeFromLeft (34));
    command.removeFromLeft (8);
    abRow.setBounds (command.removeFromLeft (108));
    midiLearnButton.setBounds (command.removeFromRight (100));
    detailArea = area.removeFromBottom (222);
    workspaceArea = area;
    auto workspace = workspaceArea.reduced (22, 16);
    primaryArea = workspace.removeFromRight (juce::jmax (340, workspace.getWidth() / 2));
    workspace.removeFromRight (24);
    displayArea = workspace;

    auto metrics = displayArea.withHeight (displayArea.getHeight()).removeFromBottom (66);
    const int metricGap = 8;
    const int metricWidth = (metrics.getWidth() - metricGap * 2) / 3;
    pitchMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    inputMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    outputMetric.setBounds (metrics);

    auto primary = primaryArea.withTrimmedTop (30);
    primary = primary.withSizeKeepingCentre (primary.getWidth(), juce::jmin (170, primary.getHeight()));
    const int macroGap = 8;
    const int macroWidth = (primary.getWidth() - macroGap * 3) / 4;
    for (auto* macro : { &amount, &dry, &richness, &saturation })
    {
        macro->setBounds (primary.removeFromLeft (macroWidth));
        primary.removeFromLeft (macroGap);
    }

    auto detail = detailArea.reduced (22, 10);
    detail.removeFromTop (26);
    auto fields = detail.removeFromTop (48);
    const int fieldGap = 8;
    auto modeCell = fields.removeFromLeft (150);
    fields.removeFromLeft (fieldGap);
    auto tuningCell = fields.removeFromLeft (150);
    fields.removeFromLeft (fieldGap);
    auto noteCell = fields.removeFromLeft (82);
    modeLabel.setBounds (modeCell.removeFromTop (18));
    mode.setBounds (modeCell);
    tuningLabel.setBounds (tuningCell.removeFromTop (18));
    tuning.setBounds (tuningCell);
    noteLabel.setBounds (noteCell.removeFromTop (18));
    note.setBounds (noteCell);
    fields.removeFromLeft (12);
    followKey.setBounds (fields.removeFromLeft (152).withSizeKeepingCentre (152, 28));
    subOnly.setBounds (fields.removeFromLeft (112).withSizeKeepingCentre (112, 28));

    detail.removeFromTop (8);
    const int detailGap = 8;
    const int detailWidth = (detail.getWidth() - detailGap * 4) / 5;
    for (auto* macro : { &octave, &glide, &attack, &release, &limiter })
    {
        macro->setBounds (detail.removeFromLeft (detailWidth));
        detail.removeFromLeft (detailGap);
    }
}
