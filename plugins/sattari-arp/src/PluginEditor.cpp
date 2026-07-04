#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

SattariArpAudioProcessorEditor::SattariArpAudioProcessorEditor (SattariArpAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "sattari-arp")
{
    setLookAndFeel (&lookAndFeel);
    setSize (940, 540);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Arp");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "MIDI arpeggiator: place before any synth to turn held notes into patterns");
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

    setupCombo (preset);
    preset.addItemList (juce::StringArray {
        "Classic 1/16 Up",
        "Wide Octave Bounce",
        "Trap 1/32 Repeat",
        "R&B Chord Pulse",
        "Random Sparkle",
        "Slow Quarter Pulse" }, 1);
    preset.setTextWhenNothingSelected ("Load preset...");
    preset.onChange = [this] { applyPreset (preset.getSelectedItemIndex()); };

    // Populate the choice combos. ComboBoxAttachment maps item IDs (1-based) to
    // each param's choice indices, so order MUST match the StringArrays in
    // PluginProcessor.cpp. Without addItemList the boxes render blank/unusable.
    setupCombo (rate);
    rate.addItemList (juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 1);
    setupCombo (pattern);
    pattern.addItemList (juce::StringArray { "Up", "Down", "Up-Down", "Down-Up", "As Played", "Random", "Chord Pulse" }, 1);
    setupCombo (velocityMode);
    velocityMode.addItemList (juce::StringArray { "Input", "Fixed", "Accent" }, 1);
    setupCombo (scaleRoot);
    scaleRoot.addItemList (juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    setupCombo (scale);
    scale.addItemList (juce::StringArray { "Off","Major","Minor","Penta Maj","Penta Min","Dorian","Mixolydian","Harm Minor" }, 1);
    setupSlider (octaves);
    setupSlider (ratchet);
    setupSlider (gate, "%");
    setupSlider (swing, "%");
    setupSlider (fixedVelocity);
    setupSlider (humanize);
    setupSlider (transpose, " st");

    latch.setButtonText ("Latch held notes");
    latch.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (latch);

    inputThru.setButtonText ("Input thru");
    inputThru.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (inputThru);

    hostSync.setButtonText ("Host sync");
    hostSync.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (hostSync);

    followKey.setButtonText ("Follow Song Key");
    followKey.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (followKey);

    setupLabel (rateLabel, "Time");
    setupLabel (patternLabel, "Pattern");
    setupLabel (velocityLabel, "Velocity");
    setupLabel (performanceLabel, "Performance");
    setupLabel (scaleLabel, "Scale");

    // Per-knob name captions (smaller than the section headers, centred).
    for (auto* l : { &octavesLabel, &ratchetLabel, &gateLabel, &swingLabel,
                     &fixedVelocityLabel, &humanizeLabel, &transposeLabel })
        l->setJustificationType (juce::Justification::centred);
    setupLabel (octavesLabel, "Octaves", 11.0f);
    setupLabel (ratchetLabel, "Ratchet", 11.0f);
    setupLabel (gateLabel, "Gate", 11.0f);
    setupLabel (swingLabel, "Swing", 11.0f);
    setupLabel (fixedVelocityLabel, "Fixed Vel", 11.0f);
    setupLabel (humanizeLabel, "Humanize", 11.0f);
    setupLabel (transposeLabel, "Transpose", 11.0f);

    auto& params = audioProcessor.parameters();
    rateAttach = std::make_unique<ComboAttachment> (params, "rate", rate);
    patternAttach = std::make_unique<ComboAttachment> (params, "pattern", pattern);
    velocityModeAttach = std::make_unique<ComboAttachment> (params, "velocityMode", velocityMode);
    scaleRootAttach = std::make_unique<ComboAttachment> (params, "scaleRoot", scaleRoot);
    scaleAttach = std::make_unique<ComboAttachment> (params, "scale", scale);
    octavesAttach = std::make_unique<SliderAttachment> (params, "octaves", octaves);
    ratchetAttach = std::make_unique<SliderAttachment> (params, "ratchet", ratchet);
    gateAttach = std::make_unique<SliderAttachment> (params, "gate", gate);
    swingAttach = std::make_unique<SliderAttachment> (params, "swing", swing);
    fixedVelocityAttach = std::make_unique<SliderAttachment> (params, "fixedVelocity", fixedVelocity);
    humanizeAttach = std::make_unique<SliderAttachment> (params, "humanize", humanize);
    transposeAttach = std::make_unique<SliderAttachment> (params, "transpose", transpose);
    latchAttach = std::make_unique<ButtonAttachment> (params, "latch", latch);
    inputThruAttach = std::make_unique<ButtonAttachment> (params, "inputThru", inputThru);
    hostSyncAttach = std::make_unique<ButtonAttachment> (params, "hostSync", hostSync);
    followKeyAttach = std::make_unique<ButtonAttachment> (params, "followKey", followKey);

    // Grey the manual Root menu while Follow Song Key is engaged so the bus
    // override is never silent. Reflect the current state now and on every toggle.
    auto syncRootEnabled = [this] { scaleRoot.setEnabled (! followKey.getToggleState()); };
    followKey.onStateChange = syncRootEnabled;
    syncRootEnabled();

    gate.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };
    swing.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };
}

SattariArpAudioProcessorEditor::~SattariArpAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
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
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariArpAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 22);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (sattari::brand::kAccent));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    addAndMakeVisible (s);
}

void SattariArpAudioProcessorEditor::setupCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff171821));
    c.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    c.setColour (juce::ComboBox::outlineColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (c);
}

void SattariArpAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& text, float size)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (size, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colour (0xffffc7a8));
    addAndMakeVisible (label);
}

void SattariArpAudioProcessorEditor::setParameterValue (const juce::String& parameterID, float value)
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
        int rate;
        int pattern;
        int octaves;
        float gate;
        float swing;
        int velocityMode;
        int fixedVelocity;
        int humanize;
        int transpose;
        bool latch;
        bool inputThru;
    };

    static constexpr Preset presets[] = {
        { 3, 0, 1, 0.72f, 0.00f, 0, 96, 0, 0, false, false }, // Classic 1/16 Up
        { 3, 2, 3, 0.64f, 0.10f, 2, 96, 3, 0, false, false }, // Wide Octave Bounce
        { 5, 0, 1, 0.48f, 0.00f, 1, 104, 2, -12, false, false }, // Trap 1/32 Repeat
        { 1, 6, 1, 0.82f, 0.08f, 0, 96, 4, 0, true, false }, // R&B Chord Pulse
        { 3, 5, 2, 0.58f, 0.00f, 0, 96, 8, 12, false, false }, // Random Sparkle
        { 0, 6, 1, 0.90f, 0.00f, 1, 92, 0, 0, true, false }  // Slow Quarter Pulse
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
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariArpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    title.setBounds (area.removeFromTop (38));
    subtitle.setBounds (area.removeFromTop (26));

    auto presetRow = area.removeFromTop (34);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (120));

    area.removeFromTop (12);

    auto top = area.removeFromTop (40);
    preset.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (12);
    rate.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (12);
    pattern.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (12);
    velocityMode.setBounds (top.removeFromLeft (110));

    area.removeFromTop (10);
    auto scaleRow = area.removeFromTop (32);
    scaleLabel.setBounds (scaleRow.removeFromLeft (52));
    scaleRoot.setBounds (scaleRow.removeFromLeft (80));
    scaleRow.removeFromLeft (10);
    scale.setBounds (scaleRow.removeFromLeft (150));
    scaleRow.removeFromLeft (20);
    latch.setBounds (scaleRow.removeFromLeft (150));
    inputThru.setBounds (scaleRow.removeFromLeft (120));
    hostSync.setBounds (scaleRow.removeFromLeft (110));
    followKey.setBounds (scaleRow.removeFromLeft (150));

    area.removeFromTop (18);
    auto labelRow = area.removeFromTop (22);
    rateLabel.setBounds (labelRow.removeFromLeft (200));
    patternLabel.setBounds (labelRow.removeFromLeft (200));
    velocityLabel.setBounds (labelRow.removeFromLeft (210));
    performanceLabel.setBounds (labelRow);

    const int kw = 102;
    // Per-knob caption strip directly above the knobs.
    auto knobCaptions = area.removeFromTop (15);
    octavesLabel.setBounds (knobCaptions.removeFromLeft (kw));
    ratchetLabel.setBounds (knobCaptions.removeFromLeft (kw));
    gateLabel.setBounds (knobCaptions.removeFromLeft (kw));
    swingLabel.setBounds (knobCaptions.removeFromLeft (kw));
    fixedVelocityLabel.setBounds (knobCaptions.removeFromLeft (kw));
    humanizeLabel.setBounds (knobCaptions.removeFromLeft (kw));
    transposeLabel.setBounds (knobCaptions.removeFromLeft (kw));

    auto row = area.removeFromTop (142);
    octaves.setBounds (row.removeFromLeft (kw));
    ratchet.setBounds (row.removeFromLeft (kw));
    gate.setBounds (row.removeFromLeft (kw));
    swing.setBounds (row.removeFromLeft (kw));
    fixedVelocity.setBounds (row.removeFromLeft (kw));
    humanize.setBounds (row.removeFromLeft (kw));
    transpose.setBounds (row.removeFromLeft (kw));
}
