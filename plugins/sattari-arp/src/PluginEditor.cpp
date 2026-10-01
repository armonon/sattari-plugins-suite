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
        if (name.isNotEmpty() && ! presetManager.loadUserPreset (name))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                "Could not load preset", "The preset could not be read. Your settings are unchanged.");
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
    keyConfig.compact = true;
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

    for (auto* slider : { &gate.slider(), &swing.slider() })
    {
        slider->textFromValueFunction = [] (double value) { return juce::String (juce::roundToInt (value * 100)) + "%"; };
        slider->valueFromTextFunction = [] (const juce::String& value) { return value.getDoubleValue() / 100; };
        slider->updateText();
    }

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
    sattari::releaseui::caption (label, text);
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
        juce::String (uiState.bpm, 1), hostSync.getToggleState() ? "BPM / host" : "BPM / free");
    heldMetric.setValue (
        juce::String (uiState.heldNotes),
        uiState.running ? "active" : "idle");
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
    const auto name = presetManager.newUserPresetName();
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
    else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
        "Could not save preset", "The preset folder could not be written. Your settings remain active.");
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
    using namespace sattari::releaseui;
    g.fillAll (canvas);
    g.setColour (surface); g.fillRect (controlsArea);
    g.setColour (line);
    g.drawHorizontalLine (124, 0, (float) getWidth());
    g.drawHorizontalLine (controlsArea.getY(), 0, (float) getWidth());
    g.drawVerticalLine (keyArea.getX() - 17, (float) workspaceArea.getY(), (float) workspaceArea.getBottom());
    paintPatternWorkspace (g);
    section (g, controlsArea.reduced (22, 8).removeFromTop (22), "Performance");
}

void SattariArpAudioProcessorEditor::paintPatternWorkspace (juce::Graphics& g)
{
    auto titledSequence = sequenceArea;
    sattari::releaseui::section (
        g, titledSequence.removeFromTop (22),
        "Pattern contour", "16 steps");

    auto grid = sequenceArea.withTrimmedTop (28).toFloat();
    const int patternIndex = (int) audioProcessor.parameters()
                                 .getRawParameterValue ("pattern")->load();
    const float gateValue = audioProcessor.parameters()
                                .getRawParameterValue ("gate")->load();
    constexpr int steps = 16;
    const float width = grid.getWidth() / (float) steps;
    const auto wholeGrid = grid;
    g.setColour (sattari::releaseui::line.withAlpha (0.55f));
    for (int row = 0; row < 5; ++row)
        g.drawHorizontalLine ((int) (grid.getY() + 20 + (grid.getHeight() - 30) * row / 4), grid.getX(), grid.getRight());
    for (int step = 0; step < steps; ++step)
    {
        auto cell = grid.removeFromLeft (width);
        const bool active = uiState.running && step == uiState.step;
        if (active) { g.setColour (accent.withAlpha (0.09f)); g.fillRect (cell); }
        g.setColour (sattari::releaseui::line.withAlpha (step % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine ((int) cell.getX(), cell.getY() + 20, cell.getBottom());

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

        auto inner = cell.reduced (3.0f).withTrimmedTop (28);
        auto note = juce::Rectangle<float> (
            inner.getX(), inner.getBottom() - 12 - phase * (inner.getHeight() - 20),
            juce::jmax (3.0f, inner.getWidth() * gateValue), 7.0f);
        g.setColour (active ? accent
                            : accent.withAlpha (0.5f));
        g.fillRoundedRectangle (note, 2.0f);
        g.setColour (active ? accent : sattari::releaseui::muted);
        g.setFont (sattari::brand::fontOptions (10.0f));
        g.drawText (juce::String (step + 1),
                    cell.withTrimmedBottom (cell.getHeight() - 16.0f).toNearestInt(),
                    juce::Justification::centred, false);
    }
    g.setColour (sattari::releaseui::line);
    g.drawHorizontalLine ((int) wholeGrid.getBottom(), wholeGrid.getX(), wholeGrid.getRight());
}

void SattariArpAudioProcessorEditor::resized()
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
    auto toggles = area.removeFromBottom (48).reduced (22, 8);
    latch.setBounds (toggles.removeFromLeft (100));
    inputThru.setBounds (toggles.removeFromLeft (110));
    hostSync.setBounds (toggles.removeFromLeft (110));
    followSwing.setBounds (toggles.removeFromLeft (145));
    followEnergy.setBounds (toggles.removeFromLeft (145));
    controlsArea = area.removeFromBottom (186);
    workspaceArea = area.reduced (22, 18);
    auto workspace = workspaceArea;
    auto right = workspace.removeFromRight (222);
    workspace.removeFromRight (32);
    keyArea = right.removeFromTop (72);
    keyButton.setBounds (keyArea);
    right.removeFromTop (10);
    auto metrics = right.removeFromTop (66);
    tempoMetric.setBounds (metrics.removeFromLeft (116)); heldMetric.setBounds (metrics);
    right.removeFromTop (6); compassMetric.setBounds (right.removeFromTop (66));
    auto modeRow = workspace.removeFromTop (52);
    auto rateCell = modeRow.removeFromLeft (92); modeRow.removeFromLeft (10);
    auto patternCell = modeRow.removeFromLeft (juce::jmax (145, modeRow.getWidth() * 3 / 5)); modeRow.removeFromLeft (10);
    auto velocityCell = modeRow;
    rateLabel.setBounds (rateCell.removeFromTop (18));
    rate.setBounds (rateCell);
    patternLabel.setBounds (patternCell.removeFromTop (18));
    pattern.setBounds (patternCell);
    velocityLabel.setBounds (velocityCell.removeFromTop (18));
    velocityMode.setBounds (velocityCell);

    workspace.removeFromTop (14); sequenceArea = workspace;
    auto controls = controlsArea.reduced (22, 10);
    controls.removeFromTop (28);
    const int macroGap = 6;
    const int macroWidth = (controls.getWidth() - macroGap * 6) / 7;
    for (auto* macro : {
            &octaves, &ratchet, &gate, &swing, &fixedVelocity, &humanize, &transpose })
    {
        macro->setBounds (controls.removeFromLeft (macroWidth));
        controls.removeFromLeft (macroGap);
    }
}
