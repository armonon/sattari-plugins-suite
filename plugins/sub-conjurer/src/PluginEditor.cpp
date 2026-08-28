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
        if (name.isNotEmpty()) presetManager.loadUserPreset (name);
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
    sattari::studio::styleFieldLabel (label, text);
    addAndMakeVisible (label);
}

void SubConjurerAudioProcessorEditor::timerCallback()
{
    uiState = audioProcessor.currentUiState();
    const bool live = uiState.inputPeak > 0.002f || uiState.envelope > 0.002f;
    header.setStatus (live ? "GENERATING SUB" : "READY", live);
    pitchMetric.setValue (
        uiState.frequencyHz > 0.0f ? juce::String (uiState.frequencyHz, 1) : "--",
        uiState.followingKey ? "Hz · following Brain" : "Hz");
    inputMetric.setValue (
        juce::String (juce::Decibels::gainToDecibels (
            juce::jmax (uiState.inputPeak, 1.0e-6f)), 1),
        "dBFS");
    outputMetric.setValue (
        juce::String (juce::Decibels::gainToDecibels (
            juce::jmax (uiState.outputPeak, 1.0e-6f)), 1),
        uiState.limiterGain < 0.999f ? "dBFS · limiting" : "dBFS");
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
    const auto name = "User "
        + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
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
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (g, workspaceArea.toFloat(), true);
    sattari::studio::paintSurface (g, primaryArea.toFloat());
    sattari::studio::paintSurface (g, detailArea.toFloat());
    paintSubDisplay (g);
    sattari::studio::paintSectionTitle (
        g, primaryArea.reduced (16).removeFromTop (18),
        "Tone architecture", "four controls define the sound");
    sattari::studio::paintSectionTitle (
        g, detailArea.reduced (16).removeFromTop (18),
        "Tracking & response", "advanced controls");
}

void SubConjurerAudioProcessorEditor::paintSubDisplay (juce::Graphics& g)
{
    auto display = displayArea.reduced (18);
    sattari::studio::paintSectionTitle (
        g, display.removeFromTop (20),
        "Sub signal", uiState.envelope > 0.002f ? "tracking input" : "waiting for signal");

    auto plot = display.removeFromTop (juce::jmax (92, display.getHeight() - 66)).toFloat();
    const float centreY = plot.getCentreY();
    g.setColour (sattari::studio::colour::separator.withAlpha (0.8f));
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
        0.08f, 1.0f, uiState.envelope * 2.8f);
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

    g.setColour (accent.withAlpha (0.14f));
    g.strokePath (waveform, juce::PathStrokeType (
        8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (waveform, juce::PathStrokeType (
        2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SubConjurerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced ((int) sattari::studio::metric::outerMargin);
    header.setBounds (area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (38);
    preset.setBounds (command.removeFromLeft (180));
    command.removeFromLeft (8);
    userPresets.setBounds (command.removeFromLeft (170));
    command.removeFromLeft (8);
    savePresetButton.setBounds (command.removeFromLeft (70));
    command.removeFromLeft (8);
    abRow.setBounds (command.removeFromLeft (94));
    midiLearnButton.setBounds (command.removeFromRight (104));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    workspaceArea = area.removeFromTop (juce::jmax (210, (int) (area.getHeight() * 0.46f)));
    area.removeFromTop ((int) sattari::studio::metric::gap);
    detailArea = area;

    auto workspace = workspaceArea.reduced (12);
    primaryArea = workspace.removeFromRight (
        juce::jmax (340, (int) (workspace.getWidth() * 0.43f)));
    workspace.removeFromRight (12);
    displayArea = workspace;

    auto metrics = displayArea.reduced (18).removeFromBottom (58);
    const int metricGap = 8;
    const int metricWidth = (metrics.getWidth() - metricGap * 2) / 3;
    pitchMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    inputMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    outputMetric.setBounds (metrics);

    auto primary = primaryArea.reduced (16);
    primary.removeFromTop (27);
    const int macroGap = 8;
    const int macroWidth = (primary.getWidth() - macroGap * 3) / 4;
    for (auto* macro : { &amount, &dry, &richness, &saturation })
    {
        macro->setBounds (primary.removeFromLeft (macroWidth));
        primary.removeFromLeft (macroGap);
    }

    auto detail = detailArea.reduced (16);
    detail.removeFromTop (26);
    auto fields = detail.removeFromTop (52);
    const int fieldGap = 8;
    auto modeCell = fields.removeFromLeft (150);
    fields.removeFromLeft (fieldGap);
    auto tuningCell = fields.removeFromLeft (150);
    fields.removeFromLeft (fieldGap);
    auto noteCell = fields.removeFromLeft (82);
    modeLabel.setBounds (modeCell.removeFromTop (16));
    mode.setBounds (modeCell);
    tuningLabel.setBounds (tuningCell.removeFromTop (16));
    tuning.setBounds (tuningCell);
    noteLabel.setBounds (noteCell.removeFromTop (16));
    note.setBounds (noteCell);
    fields.removeFromLeft (12);
    followKey.setBounds (fields.removeFromLeft (132).withSizeKeepingCentre (132, 28));
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
