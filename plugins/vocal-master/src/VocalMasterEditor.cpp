#include "VocalMasterEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

SattariVocalMasterAudioProcessorEditor::SattariVocalMasterAudioProcessorEditor (SattariVocalMasterAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "vocal-master")
{
    setLookAndFeel (&lookAndFeel);
    setSize (820, 470);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Vocal Master");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Clean the vocal out of the mix - isolate, de-noise, de-ess, tone, level");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    inputMeterLabel.setJustificationType (juce::Justification::centredRight);
    outputMeterLabel.setJustificationType (juce::Justification::centredRight);
    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (outputMeterLabel);

    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] { const auto n = userPresets.getText(); if (n.isNotEmpty()) presetManager.loadUserPreset (n); };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] { sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters()); };
    refreshUserPresets();

    auto& params = processor.parameters();
    auto add = [&] (juce::Slider& s, const juce::String& name, const juce::String& id, const juce::String& suffix = {})
    {
        setupSlider (s, name, suffix);
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, id, s));
    };
    add (inputSlider,   "Input",    "input", " dB");
    add (isolateSlider, "Isolate",  "isolate");
    add (denoiseSlider, "De-Noise", "denoise");
    add (deessSlider,   "De-Ess",   "deess");
    add (toneSlider,    "Tone",     "tone");
    add (airSlider,     "Air",      "air");
    add (levelSlider,   "Leveler",  "level");
    add (mixSlider,     "Mix",      "mix");
    add (outputSlider,  "Output",   "output", " dB");

    startTimerHz (24);
}

SattariVocalMasterAudioProcessorEditor::~SattariVocalMasterAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariVocalMasterAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& name, const juce::String& suffix)
{
    s.setName (name);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
    s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);
}

void SattariVocalMasterAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariVocalMasterAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariVocalMasterAudioProcessorEditor::timerCallback()
{
    deEssGr = deEssGr * 0.6f + processor.getDeEssMeter() * 0.4f;
    gateGr  = gateGr  * 0.6f + processor.getGateMeter()  * 0.4f;
    inputMeterLabel.setText  ("IN "  + juce::String (juce::Decibels::gainToDecibels (processor.getInputMeter()  + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    outputMeterLabel.setText ("OUT " + juce::String (juce::Decibels::gainToDecibels (processor.getOutputMeter() + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    repaint();
}

void SattariVocalMasterAudioProcessorEditor::drawReduction (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto inner = area.reduced (16, 14);
    auto bar = [&] (juce::Rectangle<int> row, const juce::String& label, float amount, juce::Colour col)
    {
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, row.removeFromLeft (64), juce::Justification::centredLeft);
        auto track = row.reduced (4, 6).toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanel));
        g.fillRoundedRectangle (track, 4.0f);
        auto fill = track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, amount));
        g.setColour (col);
        g.fillRoundedRectangle (fill, 4.0f);
    };
    const int rowH = inner.getHeight() / 2;
    bar (inner.removeFromTop (rowH), "De-Ess", deEssGr * 2.2f, juce::Colour (0xff7dd3fc));
    bar (inner.removeFromTop (rowH), "Gate",   gateGr,         juce::Colour (sattari::brand::kAccent));
}

void SattariVocalMasterAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    for (auto* s : { &inputSlider, &isolateSlider, &denoiseSlider, &deessSlider, &toneSlider,
                     &airSlider, &levelSlider, &mixSlider, &outputSlider })
    {
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawText (s->getName(), s->getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
    }

    drawReduction (g, reductionArea);
}

void SattariVocalMasterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (66);
    auto titleCol = top.removeFromLeft (440);
    title.setBounds (titleCol.removeFromTop (38));
    subtitle.setBounds (titleCol.removeFromTop (24));
    auto meters = top.removeFromRight (150);
    inputMeterLabel.setBounds (meters.removeFromTop (24));
    outputMeterLabel.setBounds (meters.removeFromTop (24));

    auto presetRow = area.removeFromTop (32);
    userPresets.setBounds (presetRow.removeFromLeft (180));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (110));
    area.removeFromTop (10);

    reductionArea = area.removeFromTop (78);
    area.removeFromTop (26);   // room for knob captions

    // Nine knobs in a single centred row.
    auto knobs = area.removeFromTop (110);
    juce::Slider* row[] = { &inputSlider, &isolateSlider, &denoiseSlider, &deessSlider, &toneSlider,
                            &airSlider, &levelSlider, &mixSlider, &outputSlider };
    const int cw = knobs.getWidth() / (int) std::size (row);
    for (auto* s : row)
        s->setBounds (knobs.removeFromLeft (cw).reduced (4));
}
