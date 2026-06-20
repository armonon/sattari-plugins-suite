#include "PluginEditor.h"

SubConjurerAudioProcessorEditor::SubConjurerAudioProcessorEditor (SubConjurerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      presetManager (p.parameters(), "sub-conjurer")
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 470);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sub Conjurer");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Turn any track into a clear, powerful mono-safe sub layer");
    addAndMakeVisible (subtitle);

    setupCombo (preset);
    preset.addItemList (juce::StringArray {
        "Clean 808",
        "Heavy Trap Sub",
        "Bass Guitar Weight",
        "Kick Reinforcer",
        "Club Low-End",
        "Warm R&B Bass",
        "Subtle Mix Weight",
        "Deep Synth Bass",
        "Tight Low-End",
        "Cinematic Sub" }, 1);
    preset.setTextWhenNothingSelected ("Factory presets...");
    preset.onChange = [this] { applyPreset (preset.getSelectedItemIndex()); };

    // Shared cross-plugin user presets (saved under the suite folder).
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

    refreshUserPresets();

    setupCombo (mode);
    setupCombo (tuning);
    setupCombo (note);

    setupSlider (octave);
    setupSlider (amount);
    setupSlider (dry);
    setupSlider (richness);
    setupSlider (glide);
    setupSlider (saturation);
    setupSlider (attack, " s");
    setupSlider (release, " s");
    setupSlider (limiter);

    subOnly.setButtonText ("Sub only output");
    subOnly.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (subOnly);

    auto& params = processor.parameters();
    modeAttach = std::make_unique<ComboAttachment> (params, "mode", mode);
    tuningAttach = std::make_unique<ComboAttachment> (params, "tuning", tuning);
    noteAttach = std::make_unique<ComboAttachment> (params, "note", note);
    octaveAttach = std::make_unique<SliderAttachment> (params, "octave", octave);
    amountAttach = std::make_unique<SliderAttachment> (params, "amount", amount);
    dryAttach = std::make_unique<SliderAttachment> (params, "dry", dry);
    richnessAttach = std::make_unique<SliderAttachment> (params, "richness", richness);
    glideAttach = std::make_unique<SliderAttachment> (params, "glide", glide);
    saturationAttach = std::make_unique<SliderAttachment> (params, "saturation", saturation);
    attackAttach = std::make_unique<SliderAttachment> (params, "attack", attack);
    releaseAttach = std::make_unique<SliderAttachment> (params, "release", release);
    limiterAttach = std::make_unique<SliderAttachment> (params, "limiter", limiter);
    subOnlyAttach = std::make_unique<ButtonAttachment> (params, "subonly", subOnly);
}

SubConjurerAudioProcessorEditor::~SubConjurerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
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
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SubConjurerAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff62f5c8));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    addAndMakeVisible (s);
}

void SubConjurerAudioProcessorEditor::setupCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff181820));
    c.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    c.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff62f5c8));
    addAndMakeVisible (c);
}

void SubConjurerAudioProcessorEditor::setParameterValue (const juce::String& parameterID, float value)
{
    if (auto* parameter = processor.parameters().getParameter (parameterID))
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
        int mode;
        int tuning;
        int note;
        int octave;
        float amount;
        float dry;
        float richness;
        float glide;
        float saturation;
        float attack;
        float release;
        float limiter;
    };

    static constexpr Preset presets[] = {
        { 3, 1, 9, 1, 0.78f, 0.28f, 0.20f, 0.00f, 0.20f, 0.006f, 0.160f, 0.82f }, // Clean 808
        { 3, 1, 9, 1, 1.25f, 0.22f, 0.58f, 0.06f, 0.65f, 0.004f, 0.260f, 0.78f }, // Heavy Trap Sub
        { 1, 0, 7, 1, 0.72f, 0.52f, 0.34f, 0.15f, 0.25f, 0.012f, 0.220f, 0.84f }, // Bass Guitar Weight
        { 4, 0, 9, 1, 1.05f, 0.40f, 0.26f, 0.00f, 0.30f, 0.002f, 0.120f, 0.80f }, // Kick Reinforcer
        { 1, 1, 7, 1, 1.18f, 0.32f, 0.45f, 0.04f, 0.45f, 0.007f, 0.280f, 0.78f }, // Club Low-End
        { 0, 1, 9, 1, 0.62f, 0.55f, 0.24f, 0.10f, 0.15f, 0.018f, 0.340f, 0.86f }, // Warm R&B Bass
        { 0, 0, 9, 1, 0.35f, 0.70f, 0.10f, 0.05f, 0.05f, 0.020f, 0.200f, 0.88f }, // Subtle Mix Weight
        { 2, 1, 7, 1, 1.05f, 0.20f, 0.36f, 0.25f, 0.35f, 0.015f, 0.460f, 0.76f }, // Deep Synth Bass
        { 1, 1, 9, 1, 0.82f, 0.36f, 0.18f, 0.00f, 0.20f, 0.003f, 0.080f, 0.84f }, // Tight Low-End
        { 2, 1, 0, 1, 1.32f, 0.18f, 0.50f, 0.30f, 0.40f, 0.030f, 0.620f, 0.74f }  // Cinematic Sub
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& p = presets[presetIndex];
    setParameterValue ("mode", (float) p.mode);
    setParameterValue ("tuning", (float) p.tuning);
    setParameterValue ("note", (float) p.note);
    setParameterValue ("octave", (float) p.octave);
    setParameterValue ("amount", p.amount);
    setParameterValue ("dry", p.dry);
    setParameterValue ("richness", p.richness);
    setParameterValue ("glide", p.glide);
    setParameterValue ("saturation", p.saturation);
    setParameterValue ("attack", p.attack);
    setParameterValue ("release", p.release);
    setParameterValue ("limiter", p.limiter);
}

void SubConjurerAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Shared suite backdrop — identical across every Sattari plugin editor.
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SubConjurerAudioProcessorEditor::resized()
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

    area.removeFromTop (12);
    auto top = area.removeFromTop (44);
    mode.setBounds (top.removeFromLeft (125));
    top.removeFromLeft (10);
    tuning.setBounds (top.removeFromLeft (125));
    top.removeFromLeft (10);
    note.setBounds (top.removeFromLeft (82));
    top.removeFromLeft (10);
    subOnly.setBounds (top.removeFromLeft (130));

    area.removeFromTop (18);
    auto row1 = area.removeFromTop (138);
    const int kw = row1.getWidth() / 9;
    octave.setBounds (row1.removeFromLeft (kw));
    amount.setBounds (row1.removeFromLeft (kw));
    dry.setBounds (row1.removeFromLeft (kw));
    richness.setBounds (row1.removeFromLeft (kw));
    glide.setBounds (row1.removeFromLeft (kw));
    saturation.setBounds (row1.removeFromLeft (kw));
    attack.setBounds (row1.removeFromLeft (kw));
    release.setBounds (row1.removeFromLeft (kw));
    limiter.setBounds (row1.removeFromLeft (kw));
}
