#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

#include <cmath>

SattariArpAudioProcessorEditor::SattariArpAudioProcessorEditor (
    SattariArpAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      presetManager (p.parameters(), "sattari-arp")
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (820, 640, 1440, 900);
    setSize (1060, 660);
    setTitle ("Sattari Arp");
    setDescription ("Generative MIDI arpeggiator and performance workspace");

    addAndMakeVisible (header);

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

    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (
            audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };
    addAndMakeVisible (midiLearnButton);
    refreshUserPresets();

    setupFieldLabel (presetLabel, "Factory");
    setupCombo (preset, "Factory presets");
    preset.addItemList (juce::StringArray {
        "Classic 1/16 Up", "Wide Octave Bounce", "Trap 1/32 Repeat",
        "R&B Chord Pulse", "Random Sparkle", "Slow Quarter Pulse"
    }, 1);
    preset.setTextWhenNothingSelected ("Factory presets");
    preset.onChange = [this] { applyPreset (preset.getSelectedItemIndex()); };

    setupFieldLabel (rateLabel, "Rate");
    setupCombo (rate, "Rate");
    rate.addItemList (juce::StringArray {
        "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"
    }, 1);
    setupFieldLabel (patternLabel, "Pattern");
    setupCombo (pattern, "Pattern");
    pattern.addItemList (juce::StringArray {
        "Up", "Down", "Up-Down", "Down-Up", "As Played", "Random", "Chord Pulse"
    }, 1);
    setupFieldLabel (velocityLabel, "Velocity");
    setupCombo (velocityMode, "Velocity mode");
    velocityMode.addItemList (juce::StringArray { "Input", "Fixed", "Accent" }, 1);

    sattari::ui::KeyButton::Config keyConfig;
    keyConfig.accentId = "sattari-arp";
    keyConfig.showFollow = true;
    keyConfig.showAutoKey = false;
    keyConfig.rootNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    keyConfig.scaleNames = {
        "Off", "Major", "Minor", "Penta Maj", "Penta Min",
        "Dorian", "Mixolydian", "Harm Minor"
    };
    keyConfig.hint = "click to follow Compass or set a musical constraint";
    keyButton.setConfig (keyConfig);
    keyButton.onLinkFollow = [this] { setParameterValue ("followKey", 1.0f); };
    keyButton.onManualKey = [this] (int root, int scale)
    {
        setParameterValue ("followKey", 0.0f);
        setParameterValue ("scaleRoot", (float) root);
        setParameterValue ("scale", (float) scale);
    };
    addAndMakeVisible (keyButton);

    for (auto* macro : {
            &octaves, &ratchet, &gate, &swing, &fixedVelocity, &humanize, &transpose })
        addAndMakeVisible (*macro);

    gate.slider().textFromValueFunction = [] (double value)
    {
        return juce::String ((int) std::round (value * 100.0)) + "%";
    };
    swing.slider().textFromValueFunction = [] (double value)
    {
        return juce::String ((int) std::round (value * 100.0)) + "%";
    };

    addAndMakeVisible (latch);
    addAndMakeVisible (inputThru);
    addAndMakeVisible (hostSync);
    followSwing.setTitle ("Follow Compass groove");
    followSwing.setDescription (
        "Use the session swing published by Sattari Compass");
    followEnergy.setTitle ("Follow Compass energy");
    followEnergy.setDescription (
        "Shape velocity and octave range from the published session energy");
    addAndMakeVisible (followSwing);
    addAndMakeVisible (followEnergy);
    addAndMakeVisible (tempoMetric);
    addAndMakeVisible (heldMetric);
    addAndMakeVisible (compassMetric);

    auto& params = audioProcessor.parameters();
    rateAttach = std::make_unique<ComboAttachment> (params, "rate", rate);
    patternAttach = std::make_unique<ComboAttachment> (params, "pattern", pattern);
    velocityModeAttach = std::make_unique<ComboAttachment> (
        params, "velocityMode", velocityMode);
    octavesAttach = std::make_unique<SliderAttachment> (
        params, "octaves", octaves.slider());
    ratchetAttach = std::make_unique<SliderAttachment> (
        params, "ratchet", ratchet.slider());
    gateAttach = std::make_unique<SliderAttachment> (
        params, "gate", gate.slider());
    swingAttach = std::make_unique<SliderAttachment> (
        params, "swing", swing.slider());
    fixedVelocityAttach = std::make_unique<SliderAttachment> (
        params, "fixedVelocity", fixedVelocity.slider());
    humanizeAttach = std::make_unique<SliderAttachment> (
        params, "humanize", humanize.slider());
    transposeAttach = std::make_unique<SliderAttachment> (
        params, "transpose", transpose.slider());
    latchAttach = std::make_unique<ButtonAttachment> (params, "latch", latch);
    inputThruAttach = std::make_unique<ButtonAttachment> (
        params, "inputThru", inputThru);
    hostSyncAttach = std::make_unique<ButtonAttachment> (
        params, "hostSync", hostSync);
    followSwingAttach = std::make_unique<ButtonAttachment> (
        params, "followSwing", followSwing);
    followEnergyAttach = std::make_unique<ButtonAttachment> (
        params, "followEnergy", followEnergy);

    startTimerHz (24);
    timerCallback();
}

SattariArpAudioProcessorEditor::~SattariArpAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariArpAudioProcessorEditor::setupCombo (
    juce::ComboBox& combo, const juce::String& title)
{
    combo.setTitle (title);
    combo.setDescription ("Choose " + title.toLowerCase());
    addAndMakeVisible (combo);
}

void SattariArpAudioProcessorEditor::setupFieldLabel (
    juce::Label& label, const juce::String& text)
{
    sattari::studio::styleFieldLabel (label, text);
    addAndMakeVisible (label);
}

void SattariArpAudioProcessorEditor::timerCallback()
{
    const auto key = audioProcessor.currentKeyState();
    keyButton.setState (
        key.following ? sattari::ui::KeyButton::Mode::Follow
                      : sattari::ui::KeyButton::Mode::Manual,
        key.root, key.scale);

    uiState = audioProcessor.currentUiState();
    const bool followsCompass =
        uiState.followingSwing || uiState.followingEnergy;
    header.setStatus (
        followsCompass && uiState.compassLive
            ? "COMPASS / "
                + sattari::compass::sectionName (uiState.sectionId).toUpperCase()
            : uiState.running ? "GENERATING" : "READY",
        uiState.running || (followsCompass && uiState.compassLive));
    tempoMetric.setValue (
        juce::String (uiState.bpm, 1), hostSync.getToggleState() ? "BPM · host" : "BPM · free");
    heldMetric.setValue (
        juce::String (uiState.heldNotes),
        uiState.running ? "pattern active" : "play a chord");
    compassMetric.setValue (
        uiState.compassLive
            ? sattari::compass::sectionName (uiState.sectionId).toUpperCase()
            : "LOCAL",
        uiState.compassLive
            ? juce::String ((int) std::round (uiState.sessionEnergy * 100.0f))
                + "% energy / "
                + juce::String ((int) std::round (uiState.sessionSwing * 100.0f))
                + "% swing"
            : "no session context");
    swing.setEnabled (! uiState.followingSwing || ! uiState.compassLive);
    repaint (sequenceArea);
}

void SattariArpAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariArpAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User "
        + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariArpAudioProcessorEditor::setParameterValue (
    const juce::String& parameterID, float value)
{
    if (auto* parameter = audioProcessor.parameters().getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void SattariArpAudioProcessorEditor::applyPreset (int presetIndex)
{
    struct Preset
    {
        int rate, pattern, octaves;
        float gate, swing;
        int velocityMode, fixedVelocity, humanize, transpose;
        bool latch, inputThru;
    };

    static constexpr Preset presets[] = {
        { 3, 0, 1, 0.72f, 0.00f, 0, 96, 0, 0, false, false },
        { 3, 2, 3, 0.64f, 0.10f, 2, 96, 3, 0, false, false },
        { 5, 0, 1, 0.48f, 0.00f, 1, 104, 2, -12, false, false },
        { 1, 6, 1, 0.82f, 0.08f, 0, 96, 4, 0, true, false },
        { 3, 5, 2, 0.58f, 0.00f, 0, 96, 8, 12, false, false },
        { 0, 6, 1, 0.90f, 0.00f, 1, 92, 0, 0, true, false }
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& p = presets[presetIndex];
    setParameterValue ("rate", (float) p.rate);
    setParameterValue ("pattern", (float) p.pattern);
    setParameterValue ("octaves", (float) p.octaves);
    setParameterValue ("gate", p.gate);
    setParameterValue ("swing", p.swing);
    setParameterValue ("velocityMode", (float) p.velocityMode);
    setParameterValue ("fixedVelocity", (float) p.fixedVelocity);
    setParameterValue ("humanize", (float) p.humanize);
    setParameterValue ("transpose", (float) p.transpose);
    setParameterValue ("latch", p.latch ? 1.0f : 0.0f);
    setParameterValue ("inputThru", p.inputThru ? 1.0f : 0.0f);
}

void SattariArpAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (g, workspaceArea.toFloat(), true);
    sattari::studio::paintSurface (g, controlsArea.toFloat());
    paintPatternWorkspace (g);
    sattari::studio::paintSectionTitle (
        g, controlsArea.reduced (16).removeFromTop (18),
        "Performance shaping", "automation-ready macros");
}

void SattariArpAudioProcessorEditor::paintPatternWorkspace (juce::Graphics& g)
{
    auto titledSequence = sequenceArea;
    sattari::studio::paintSectionTitle (
        g, titledSequence.removeFromTop (22),
        "Generated pattern", uiState.running ? "live sequence" : "preview");

    auto grid = sequenceArea.withTrimmedTop (28).toFloat();
    const int patternIndex = (int) audioProcessor.parameters()
                                 .getRawParameterValue ("pattern")->load();
    const float gateValue = audioProcessor.parameters()
                                .getRawParameterValue ("gate")->load();
    const int velocity = uiState.running
        ? uiState.velocity
        : (int) audioProcessor.parameters()
              .getRawParameterValue ("fixedVelocity")->load();

    constexpr int steps = 16;
    const float gap = 5.0f;
    const float width = (grid.getWidth() - gap * (steps - 1)) / (float) steps;
    for (int step = 0; step < steps; ++step)
    {
        auto cell = grid.removeFromLeft (width);
        if (step + 1 < steps) grid.removeFromLeft (gap);

        const bool active = uiState.running && step == uiState.step;
        g.setColour (active ? accent.withAlpha (0.24f)
                            : sattari::studio::colour::surfaceHigh);
        g.fillRoundedRectangle (cell, 7.0f);
        g.setColour (active ? accent
                            : sattari::studio::colour::separator);
        g.drawRoundedRectangle (cell.reduced (0.5f), 7.0f, active ? 1.5f : 1.0f);

        float phase = (float) (step % 8) / 7.0f;
        if (patternIndex == 1) phase = 1.0f - phase;
        else if (patternIndex == 2 || patternIndex == 3)
        {
            phase = step < 8 ? (float) step / 7.0f
                             : (float) (15 - step) / 7.0f;
            if (patternIndex == 3) phase = 1.0f - phase;
        }
        else if (patternIndex == 5)
            phase = (float) ((step * 7 + 3) % 11) / 10.0f;
        else if (patternIndex == 6)
            phase = 0.55f;

        auto inner = cell.reduced (5.0f);
        const float noteHeight = 5.0f + phase * (inner.getHeight() * 0.56f);
        auto note = juce::Rectangle<float> (
            inner.getX(), inner.getBottom() - noteHeight,
            inner.getWidth(), juce::jmax (4.0f, noteHeight * gateValue));
        g.setColour (active ? accent
                            : accent.withAlpha (0.42f + phase * 0.24f));
        g.fillRoundedRectangle (note, 3.0f);

        const float velocityHeight = juce::jlimit (
            2.0f, 10.0f, (float) velocity / 127.0f * 10.0f);
        g.setColour (sattari::studio::colour::text.withAlpha (active ? 0.9f : 0.28f));
        g.fillRoundedRectangle (
            inner.getX(), inner.getBottom() - velocityHeight,
            inner.getWidth(), velocityHeight, 2.0f);

        g.setColour (active ? accent : sattari::studio::colour::tertiary);
        g.setFont (juce::FontOptions (8.5f, active ? juce::Font::bold : juce::Font::plain));
        g.drawText (juce::String (step + 1),
                    cell.withTrimmedBottom (cell.getHeight() - 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }
}

void SattariArpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced ((int) sattari::studio::metric::outerMargin);
    header.setBounds (area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (54);
    auto factoryCell = command.removeFromLeft (190);
    presetLabel.setBounds (factoryCell.removeFromTop (16));
    preset.setBounds (factoryCell.removeFromTop (34));
    command.removeFromLeft (8);
    userPresets.setBounds (command.removeFromLeft (170).withSizeKeepingCentre (170, 34));
    command.removeFromLeft (8);
    savePresetButton.setBounds (command.removeFromLeft (70).withSizeKeepingCentre (70, 34));
    command.removeFromLeft (8);
    abRow.setBounds (command.removeFromLeft (94).withSizeKeepingCentre (94, 34));
    midiLearnButton.setBounds (command.removeFromRight (104).withSizeKeepingCentre (104, 34));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    workspaceArea = area.removeFromTop (juce::jmax (230, (int) (area.getHeight() * 0.48f)));
    area.removeFromTop ((int) sattari::studio::metric::gap);
    controlsArea = area;

    auto workspace = workspaceArea.reduced (16);
    auto right = workspace.removeFromRight (300);
    workspace.removeFromRight (14);
    sequenceArea = workspace;
    keyArea = right.removeFromTop (90);
    keyButton.setBounds (keyArea);
    right.removeFromTop (6);
    auto modeRow = right.removeFromTop (46);
    const int fieldGap = 8;
    auto rateCell = modeRow.removeFromLeft ((modeRow.getWidth() - fieldGap * 2) / 3);
    modeRow.removeFromLeft (fieldGap);
    auto patternCell = modeRow.removeFromLeft ((modeRow.getWidth() - fieldGap) / 2);
    modeRow.removeFromLeft (fieldGap);
    auto velocityCell = modeRow;
    rateLabel.setBounds (rateCell.removeFromTop (16));
    rate.setBounds (rateCell);
    patternLabel.setBounds (patternCell.removeFromTop (16));
    pattern.setBounds (patternCell);
    velocityLabel.setBounds (velocityCell.removeFromTop (16));
    velocityMode.setBounds (velocityCell);

    right.removeFromTop (6);
    auto metrics = right.removeFromTop (50);
    const int metricGap = 6;
    const int metricWidth = (metrics.getWidth() - metricGap * 2) / 3;
    tempoMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    heldMetric.setBounds (metrics.removeFromLeft (metricWidth));
    metrics.removeFromLeft (metricGap);
    compassMetric.setBounds (metrics);

    auto controls = controlsArea.reduced (16);
    controls.removeFromTop (28);
    auto toggles = controls.removeFromBottom (30);
    latch.setBounds (toggles.removeFromLeft (110));
    inputThru.setBounds (toggles.removeFromLeft (130));
    hostSync.setBounds (toggles.removeFromLeft (120));
    followSwing.setBounds (toggles.removeFromLeft (142));
    followEnergy.setBounds (toggles.removeFromLeft (142));
    controls.removeFromBottom (8);

    const int macroGap = 6;
    const int macroWidth = (controls.getWidth() - macroGap * 6) / 7;
    for (auto* macro : {
            &octaves, &ratchet, &gate, &swing, &fixedVelocity, &humanize, &transpose })
    {
        macro->setBounds (controls.removeFromLeft (macroWidth));
        controls.removeFromLeft (macroGap);
    }
}
