#include "RoyalChainEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>
#include "MacroController.h"

RoyalChainAudioProcessorEditor::RoyalChainAudioProcessorEditor (RoyalChainAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "royal-chain"),
      compass (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (860, 640);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Royal Chain");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Nine-stage vocal chain with one-knob macros and Vocal Crown Compass");
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
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };
    refreshUserPresets();

    setupCombo (presetBox);
    presetBox.addItemList (juce::StringArray {
        "Industry Lead", "Hard Tune Hook", "Silk Lead", "Radio Lead", "Persian Pop Lead",
        "Podcast Clean", "Demo Vocal Fix", "Noisy Room Rescue", "Thin Vocal Repair", "Harsh Mic Smooth", "Phone Recording Polish" }, 1);
    presetBox.setTextWhenNothingSelected ("Load preset...");
    presetBox.onChange = [this] { applyPreset (presetBox.getSelectedItemIndex()); };

    setupCombo (styleBox);
    styleAttachment = std::make_unique<ComboAttachment> (processor.parameters(), "style", styleBox);

    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (0xff62f5c8));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (0xffd8b45a));
    inputMeterLabel.setJustificationType (juce::Justification::centredRight);
    outputMeterLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (outputMeterLabel);

    setupSlider (inputSlider, "Input", " dB");
    setupSlider (outputSlider, "Output", " dB");
    setupSlider (cleanSlider, "Clean");
    setupSlider (tuneSlider, "Tune");
    setupSlider (controlSlider, "Control");
    setupSlider (toneSlider, "Tone");
    setupSlider (airSlider, "Air");
    setupSlider (smoothSlider, "Smooth");
    setupSlider (glowSlider, "Glow");
    setupSlider (widthSlider, "Width");
    setupSlider (spaceSlider, "Space");
    setupSlider (royalSlider, "Royal");

    auto& params = processor.parameters();
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "input", inputSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "output", outputSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "clean", cleanSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "tune", tuneSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "control", controlSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "tone", toneSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "air", airSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "smooth", smoothSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "glow", glowSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "width", widthSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "space", spaceSlider));
    sliderAttachments.push_back (std::make_unique<SliderAttachment> (params, "royal", royalSlider));

    addAndMakeVisible (compass);
    startTimerHz (24);
}

RoyalChainAudioProcessorEditor::~RoyalChainAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RoyalChainAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void RoyalChainAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void RoyalChainAudioProcessorEditor::setupSlider (juce::Slider& slider, const juce::String& name, const juce::String& suffix)
{
    slider.setName (name);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 22);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffd8b45a));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff34394d));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void RoyalChainAudioProcessorEditor::setupCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff171927));
    combo.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xffd8b45a));
    addAndMakeVisible (combo);
}

void RoyalChainAudioProcessorEditor::applyPreset (int presetIndex)
{
    struct Preset
    {
        int style;
        float input, output, clean, tune, control, tone, air, smooth, glow, width, space, royal;
    };

    static constexpr Preset presets[] = {
        { 0, 0.0f, -0.5f, 0.58f, 0.00f, 0.64f, 0.52f, 0.55f, 0.44f, 0.38f, 0.28f, 0.22f, 0.60f },
        { 7, 0.0f, -1.0f, 0.45f, 0.35f, 0.70f, 0.45f, 0.60f, 0.38f, 0.44f, 0.34f, 0.20f, 0.72f },
        { 1, 0.0f, -0.5f, 0.62f, 0.00f, 0.52f, 0.58f, 0.48f, 0.52f, 0.30f, 0.20f, 0.28f, 0.52f },
        { 2, 0.0f, -0.8f, 0.58f, 0.00f, 0.66f, 0.50f, 0.70f, 0.48f, 0.36f, 0.24f, 0.22f, 0.64f },
        { 3, 0.0f, -0.7f, 0.55f, 0.00f, 0.58f, 0.54f, 0.56f, 0.50f, 0.34f, 0.20f, 0.30f, 0.56f },
        { 5, 0.0f, -1.5f, 0.72f, 0.00f, 0.42f, 0.44f, 0.30f, 0.60f, 0.10f, 0.00f, 0.06f, 0.30f },
        { 8, 1.5f, -1.0f, 0.78f, 0.00f, 0.60f, 0.38f, 0.48f, 0.56f, 0.22f, 0.12f, 0.10f, 0.46f },
        { 8, 2.0f, -1.5f, 0.90f, 0.00f, 0.50f, 0.40f, 0.36f, 0.62f, 0.12f, 0.06f, 0.04f, 0.36f },
        { 8, 1.0f, -0.8f, 0.52f, 0.00f, 0.48f, 0.72f, 0.62f, 0.38f, 0.28f, 0.16f, 0.12f, 0.42f },
        { 8, 0.0f, -1.2f, 0.64f, 0.00f, 0.52f, 0.36f, 0.28f, 0.82f, 0.08f, 0.08f, 0.08f, 0.34f },
        { 8, 2.5f, -1.8f, 0.76f, 0.00f, 0.55f, 0.42f, 0.40f, 0.70f, 0.18f, 0.10f, 0.05f, 0.40f }
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& p = presets[presetIndex];
    auto& state = processor.parameters();
    MacroController::setParameterValue (state, "style", (float) p.style);
    MacroController::setParameterValue (state, "input", p.input);
    MacroController::setParameterValue (state, "output", p.output);
    MacroController::setParameterValue (state, "clean", p.clean);
    MacroController::setParameterValue (state, "tune", p.tune);
    MacroController::setParameterValue (state, "control", p.control);
    MacroController::setParameterValue (state, "tone", p.tone);
    MacroController::setParameterValue (state, "air", p.air);
    MacroController::setParameterValue (state, "smooth", p.smooth);
    MacroController::setParameterValue (state, "glow", p.glow);
    MacroController::setParameterValue (state, "width", p.width);
    MacroController::setParameterValue (state, "space", p.space);
    MacroController::setParameterValue (state, "royal", p.royal);
}

void RoyalChainAudioProcessorEditor::timerCallback()
{
    inputMeterLabel.setText ("IN " + juce::String (juce::Decibels::gainToDecibels (processor.getInputMeter() + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    outputMeterLabel.setText ("OUT " + juce::String (juce::Decibels::gainToDecibels (processor.getOutputMeter() + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    compass.repaint();
}

void RoyalChainAudioProcessorEditor::drawChainFlow (juce::Graphics& g, juce::Rectangle<int> area)
{
    static const char* names[] = { "Clean", "Tune", "Control", "Tone", "De-Ess", "Color", "Width", "Space", "Finish" };
    const int gap = 6;
    const int blockW = (area.getWidth() - gap * 8) / 9;
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    for (int i = 0; i < 9; ++i)
    {
        auto block = area.removeFromLeft (blockW);
        g.setColour (i == 1 ? juce::Colour (0xff3b3145) : juce::Colour (0xff1c2232));
        g.fillRoundedRectangle (block.toFloat(), 8.0f);
        g.setColour (i == 1 ? juce::Colour (0xffa78bfa) : juce::Colour (0xffd8b45a));
        g.drawRoundedRectangle (block.toFloat(), 8.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.drawText (names[i], block, juce::Justification::centred);
        area.removeFromLeft (gap);
    }
}

void RoyalChainAudioProcessorEditor::drawSliderLabel (juce::Graphics& g, const juce::Slider& slider)
{
    g.setColour (slider.getName() == "Tune" ? juce::Colour (0xffa78bfa) : juce::Colours::white.withAlpha (0.88f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    auto b = slider.getBounds();
    g.drawText (slider.getName(), b.withHeight (18).translated (0, -14), juce::Justification::centred);
}

void RoyalChainAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    drawChainFlow (g, getLocalBounds().reduced (28).withY (366).withHeight (42));

    juce::Slider* sliders[] = { &inputSlider, &outputSlider, &cleanSlider, &tuneSlider, &controlSlider, &toneSlider, &airSlider, &smoothSlider, &glowSlider, &widthSlider, &spaceSlider, &royalSlider };
    for (auto* slider : sliders)
        drawSliderLabel (g, *slider);
}

void RoyalChainAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    auto top = area.removeFromTop (82);
    auto leftTitle = top.removeFromLeft (360);
    title.setBounds (leftTitle.removeFromTop (38));
    subtitle.setBounds (leftTitle.removeFromTop (28));

    presetBox.setBounds (top.removeFromLeft (190).reduced (0, 18));
    top.removeFromLeft (12);
    styleBox.setBounds (top.removeFromLeft (180).reduced (0, 18));
    top.removeFromLeft (10);
    inputMeterLabel.setBounds (top.removeFromTop (28));
    outputMeterLabel.setBounds (top.removeFromTop (28));

    auto presetRow = area.removeFromTop (34);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (120));
    area.removeFromTop (10);

    auto center = area.removeFromTop (230);
    auto trim = center.removeFromLeft (92);
    inputSlider.setBounds (trim.removeFromTop (108).reduced (2, 10));
    outputSlider.setBounds (trim.removeFromTop (108).reduced (2, 10));
    center.removeFromLeft (14);
    compass.setBounds (center.reduced (0, 4));

    area.removeFromTop (70);
    auto macros = area.removeFromTop (132);
    juce::Slider* macroSliders[] = { &cleanSlider, &tuneSlider, &controlSlider, &toneSlider, &airSlider, &smoothSlider, &glowSlider, &widthSlider, &spaceSlider, &royalSlider };
    const int cellW = macros.getWidth() / 10;
    for (auto* slider : macroSliders)
        slider->setBounds (macros.removeFromLeft (cellW).reduced (3, 8));
}
