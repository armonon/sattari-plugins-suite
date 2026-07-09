#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>
#include <sattari_audio_core/SattariPitchDetector.h>

Sattari808AudioProcessorEditor::Sattari808AudioProcessorEditor (Sattari808AudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      presetManager (p.parameters(), "eightoheight")
{
    setLookAndFeel (&lookAndFeel);
    setSize (820, 430);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari 808");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle,
        "Track any bass or vocal and re-synthesize a tuned, key-snapped 808 under it");
    addAndMakeVisible (subtitle);

    setupCombo (preset);
    preset.addItemList (juce::StringArray {
        "Clean Sine 808",
        "Trap Growl",
        "Distorted Sub",
        "Tight Kick 808",
        "Deep & Long",
        "Key-Locked Bass",
        "Punchy Triangle",
        "Cinematic Drop" }, 1);
    preset.setTextWhenNothingSelected ("Factory presets...");
    preset.onChange = [this] { applyPreset (preset.getSelectedItemIndex()); };

    setupCombo (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };

    savePresetButton.setButtonText ("Save Preset");
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);

    refreshUserPresets();

    // Choice combo — item order MUST match the param StringArray in the processor.
    setupCombo (wave);
    wave.addItemList (juce::StringArray { "Sine", "Triangle" }, 1);

    setupSlider (glide);
    setupSlider (decay, " s");
    setupSlider (drive);
    setupSlider (subfold);
    setupSlider (blend);
    setupSlider (output);

    styleCaption (waveLabel, "BODY WAVE");
    styleCaption (glideLabel, "GLIDE");
    styleCaption (decayLabel, "DECAY");
    styleCaption (driveLabel, "DRIVE / CLICK");
    styleCaption (subfoldLabel, "SUB FOLD");
    styleCaption (blendLabel, "BLEND");
    styleCaption (outputLabel, "OUTPUT");

    snap.setButtonText ("Tune to Key");
    snap.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (snap);

    bodyReadout.setJustificationType (juce::Justification::centredLeft);
    bodyReadout.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    bodyReadout.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::productAccent ("eightoheight")));
    addAndMakeVisible (bodyReadout);

    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };

    auto& params = processor.parameters();
    waveAttach = std::make_unique<ComboAttachment> (params, "wave", wave);
    glideAttach = std::make_unique<SliderAttachment> (params, "glide", glide);
    decayAttach = std::make_unique<SliderAttachment> (params, "decay", decay);
    driveAttach = std::make_unique<SliderAttachment> (params, "drive", drive);
    subfoldAttach = std::make_unique<SliderAttachment> (params, "subfold", subfold);
    blendAttach = std::make_unique<SliderAttachment> (params, "blend", blend);
    outputAttach = std::make_unique<SliderAttachment> (params, "output", output);
    snapAttach = std::make_unique<ButtonAttachment> (params, "snap", snap);

    startTimerHz (15);
}

Sattari808AudioProcessorEditor::~Sattari808AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Sattari808AudioProcessorEditor::timerCallback()
{
    const float hz = processor.currentBodyHz();
    const int midi = sattari::dsp::nearestMidiNote (hz);
    juce::String note = "--";
    if (midi >= 0)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        note = juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    }
    bodyReadout.setText ("Body: " + juce::String (hz, 1) + " Hz  (" + note + ")",
                         juce::dontSendNotification);
}

void Sattari808AudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void Sattari808AudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void Sattari808AudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::rotarySliderFillColourId,
                 juce::Colour (sattari::brand::productAccent ("eightoheight")));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    addAndMakeVisible (s);
}

void Sattari808AudioProcessorEditor::setupCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff181820));
    c.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    c.setColour (juce::ComboBox::outlineColourId,
                 juce::Colour (sattari::brand::productAccent ("eightoheight")));
    addAndMakeVisible (c);
}

void Sattari808AudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (l);
}

void Sattari808AudioProcessorEditor::setParameterValue (const juce::String& parameterID, float value)
{
    if (auto* parameter = processor.parameters().getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

void Sattari808AudioProcessorEditor::applyPreset (int presetIndex)
{
    struct Preset
    {
        int wave;
        bool snap;
        float glide;
        float decay;
        float drive;
        float subfold;
        float blend;
        float output;
    };

    static constexpr Preset presets[] = {
        { 0, false, 0.10f, 0.55f, 0.15f, 0.15f, 0.75f, 1.00f }, // Clean Sine 808
        { 0, false, 0.20f, 0.70f, 0.55f, 0.60f, 0.85f, 0.95f }, // Trap Growl
        { 1, false, 0.12f, 0.60f, 0.80f, 0.70f, 0.90f, 0.90f }, // Distorted Sub
        { 0, false, 0.02f, 0.30f, 0.45f, 0.20f, 0.80f, 1.00f }, // Tight Kick 808
        { 0, false, 0.30f, 1.60f, 0.20f, 0.35f, 0.80f, 1.00f }, // Deep & Long
        { 0, true,  0.18f, 0.65f, 0.25f, 0.25f, 0.80f, 1.00f }, // Key-Locked Bass
        { 1, false, 0.06f, 0.45f, 0.35f, 0.30f, 0.82f, 1.00f }, // Punchy Triangle
        { 0, false, 0.35f, 2.00f, 0.50f, 0.55f, 0.90f, 0.90f }  // Cinematic Drop
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& pr = presets[presetIndex];
    setParameterValue ("wave", (float) pr.wave);
    setParameterValue ("snap", pr.snap ? 1.0f : 0.0f);
    setParameterValue ("glide", pr.glide);
    setParameterValue ("decay", pr.decay);
    setParameterValue ("drive", pr.drive);
    setParameterValue ("subfold", pr.subfold);
    setParameterValue ("blend", pr.blend);
    setParameterValue ("output", pr.output);
}

void Sattari808AudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void Sattari808AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    title.setBounds (area.removeFromTop (36));
    subtitle.setBounds (area.removeFromTop (28));
    area.removeFromTop (14);

    auto presetRow = area.removeFromTop (40);
    preset.setBounds (presetRow.removeFromLeft (170));
    presetRow.removeFromLeft (10);
    userPresets.setBounds (presetRow.removeFromLeft (170));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));

    area.removeFromTop (12);

    // Top control strip: body-wave menu, tune-to-key, live readout, MIDI learn.
    auto comboCaptions = area.removeFromTop (14);
    waveLabel.setBounds (comboCaptions.removeFromLeft (125));

    auto top = area.removeFromTop (44);
    wave.setBounds (top.removeFromLeft (125));
    top.removeFromLeft (14);
    snap.setBounds (top.removeFromLeft (130));
    top.removeFromLeft (10);
    bodyReadout.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (10);
    midiLearnButton.setBounds (top.removeFromRight (120));

    area.removeFromTop (18);

    // Knob row: caption strip on top, knob below.
    auto row = area.removeFromTop (154);
    const int kw = row.getWidth() / 6;
    auto placeKnob = [&row, kw] (juce::Label& cap, juce::Slider& knob)
    {
        auto cell = row.removeFromLeft (kw);
        cap.setBounds (cell.removeFromTop (14));
        knob.setBounds (cell);
    };
    placeKnob (glideLabel, glide);
    placeKnob (decayLabel, decay);
    placeKnob (driveLabel, drive);
    placeKnob (subfoldLabel, subfold);
    placeKnob (blendLabel, blend);
    placeKnob (outputLabel, output);
}
