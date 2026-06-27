#include "RhythmGenieEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

using namespace sattari::rhythm;

namespace
{
const std::array<juce::Colour, kNumLanes> kLaneColours {
    juce::Colour (0xff62f5c8), juce::Colour (0xff7dd3fc),
    juce::Colour (0xff34d8b4), juce::Colour (0xff8ce0d0) };
}

SattariRhythmGenieAudioProcessorEditor::SattariRhythmGenieAudioProcessorEditor (SattariRhythmGenieAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "rhythm-genie")
{
    setLookAndFeel (&lookAndFeel);
    setSize (880, 540);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Rhythm Genie");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Host-synced MIDI drum-pattern generator");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    transportStatus.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    transportStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    transportStatus.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (transportStatus);

    styleBox.addItemList (SattariRhythmGenieAudioProcessor::styleChoices(), 1);  // populate before attach
    addAndMakeVisible (styleBox);
    styleAtt = std::make_unique<ComboAttachment> (processor.parameters(), "style", styleBox);

    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] { const auto n = userPresets.getText(); if (n.isNotEmpty()) presetManager.loadUserPreset (n); };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] { sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters()); };
    refreshUserPresets();

    setupSlider (densitySlider);  densitySlider.setName ("Density");
    setupSlider (swingSlider);    swingSlider.setName ("Swing");
    setupSlider (velocitySlider); velocitySlider.setName ("Velocity");
    setupSlider (gateSlider);     gateSlider.setName ("Gate");
    setupSlider (accentSlider);   accentSlider.setName ("Accent");
    setupSlider (humanizeSlider); humanizeSlider.setName ("Humanize");
    auto& params = processor.parameters();
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "density",  densitySlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "swing",    swingSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "velocity", velocitySlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "gate",     gateSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "accent",   accentSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "humanize", humanizeSlider));

    static const char* laneIds[kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
    for (int l = 0; l < kNumLanes; ++l)
    {
        laneToggles[(size_t) l].setButtonText ({});
        addAndMakeVisible (laneToggles[(size_t) l]);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (params, laneIds[l], laneToggles[(size_t) l]));
    }

    startTimerHz (30);
}

SattariRhythmGenieAudioProcessorEditor::~SattariRhythmGenieAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariRhythmGenieAudioProcessorEditor::setupSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    addAndMakeVisible (s);
}

void SattariRhythmGenieAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariRhythmGenieAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariRhythmGenieAudioProcessorEditor::timerCallback()
{
    const int step = processor.currentStep();
    transportStatus.setText (step >= 0 ? "Playing - step " + juce::String (step + 1) + " / 16"
                                       : "Stopped (press play in your DAW)",
                             juce::dontSendNotification);
    repaint();
}

void SattariRhythmGenieAudioProcessorEditor::drawGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    const int labelW = 78;
    auto grid = area.reduced (10).withTrimmedLeft (labelW);
    const float cellW = grid.getWidth()  / (float) kSteps;
    const float cellH = grid.getHeight() / (float) kNumLanes;
    const int   step  = processor.currentStep();

    auto& apvts = processor.parameters();
    const int   style = (int) std::round (apvts.getRawParameterValue ("style")->load());
    const float dens  = apvts.getRawParameterValue ("density")->load();
    const auto& pat   = pattern (style);

    // playhead column
    if (step >= 0)
    {
        g.setColour (juce::Colour (sattari::brand::kAccent).withAlpha (0.12f));
        g.fillRect (grid.getX() + step * cellW, (float) area.getY() + 6.0f, cellW, (float) area.getHeight() - 12.0f);
    }

    for (int lane = 0; lane < kNumLanes; ++lane)
    {
        const float y = grid.getY() + lane * cellH;
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (laneName (lane), area.getX() + 12, (int) y, labelW - 6, (int) cellH,
                    juce::Justification::centredLeft);

        const bool on = processor.laneEnabled (lane);
        for (int s = 0; s < kSteps; ++s)
        {
            const float x = grid.getX() + s * cellW;
            juce::Rectangle<float> cell (x + 1.5f, y + 1.5f, cellW - 3.0f, cellH - 3.0f);
            const bool hit = on && stepActive (pat, lane, s, dens);
            const bool beat = (s % 4) == 0;

            g.setColour (juce::Colour (beat ? sattari::brand::kPanelRaised : sattari::brand::kPanel));
            g.fillRoundedRectangle (cell, 3.0f);
            if (hit)
            {
                auto c = kLaneColours[(size_t) lane];
                const bool live = (s == step);
                g.setColour (live ? juce::Colours::white : c.withAlpha (0.92f));
                g.fillRoundedRectangle (cell.reduced (1.0f), 3.0f);
            }
        }
    }
}

void SattariRhythmGenieAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    auto caption = [&] (juce::Slider& s)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.getName(), s.getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
    };
    for (auto* s : { &densitySlider, &swingSlider, &velocitySlider, &gateSlider, &accentSlider, &humanizeSlider })
        caption (*s);

    drawGrid (g, gridArea);

    // lane-mute captions under the toggles
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (int l = 0; l < kNumLanes; ++l)
    {
        auto b = laneToggles[(size_t) l].getBounds();
        g.drawText (laneName (l), b.getRight() + 4, b.getY(), 90, b.getHeight(), juce::Justification::centredLeft);
    }
}

void SattariRhythmGenieAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (66);
    auto titleCol = top.removeFromLeft (380);
    title.setBounds (titleCol.removeFromTop (38));
    subtitle.setBounds (titleCol.removeFromTop (22));
    transportStatus.setBounds (top.removeFromBottom (24));

    auto presetRow = area.removeFromTop (32);
    styleBox.setBounds (presetRow.removeFromLeft (180));
    presetRow.removeFromLeft (10);
    userPresets.setBounds (presetRow.removeFromLeft (170));
    presetRow.removeFromLeft (8);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (110));
    area.removeFromTop (12);

    gridArea = area.removeFromTop (188);
    area.removeFromTop (28);   // room for knob captions

    auto knobs = area.removeFromTop (96);
    auto knobRow = knobs.withSizeKeepingCentre (juce::jmin (knobs.getWidth(), 680), knobs.getHeight());
    const int kw = knobRow.getWidth() / 6;
    for (auto* s : { &densitySlider, &swingSlider, &velocitySlider, &gateSlider, &accentSlider, &humanizeSlider })
        s->setBounds (knobRow.removeFromLeft (kw).reduced (4));

    area.removeFromTop (8);
    auto lanes = area.removeFromTop (28);
    const int lw = lanes.getWidth() / kNumLanes;
    for (int l = 0; l < kNumLanes; ++l)
        laneToggles[(size_t) l].setBounds (lanes.removeFromLeft (lw).removeFromLeft (44));
}
