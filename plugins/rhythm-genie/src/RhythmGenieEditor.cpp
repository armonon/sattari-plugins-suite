#include "RhythmGenieEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

using namespace sattari::rhythm;

namespace
{
// One colour per kit voice (8 lanes).
const std::array<juce::Colour, 8> kLaneColours {
    juce::Colour (0xff62f5c8), juce::Colour (0xff7dd3fc), juce::Colour (0xff34d8b4), juce::Colour (0xff8ce0d0),
    juce::Colour (0xfff5c862), juce::Colour (0xfff57d9c), juce::Colour (0xffc89cf5), juce::Colour (0xff9cf5c8) };

// The 8 kit voices in lane order (matches DrumPadMap / DrumKit lane assignment).
const char* const kPadNames[8] = { "Kick", "Snare", "C.Hat", "O.Hat", "Clap", "Tom", "Rim", "Ride" };
// Shaping-param bases for the four sequencer lanes (only these have Level/Tune/Decay).
const char* const kKitBase[kNumLanes] = { "kick", "snare", "chat", "ohat" };
}

SattariRhythmGenieAudioProcessorEditor::SattariRhythmGenieAudioProcessorEditor (SattariRhythmGenieAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "rhythm-genie")
{
    setLookAndFeel (&lookAndFeel);
    setSize (960, 772);
    setWantsKeyboardFocus (true);   // laptop keyboard plays the pads

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

    // --- Kit-shaping knobs: master + per-lane Level / Tune / Decay -------------
    setupKitKnob (kitLevelSlider);
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "kitLevel", kitLevelSlider));
    for (int l = 0; l < kNumLanes; ++l)
    {
        setupKitKnob (laneLevelS[(size_t) l]);
        setupKitKnob (laneTuneS [(size_t) l]);
        setupKitKnob (laneDecayS[(size_t) l]);
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, kKitBase[l] + juce::String ("Level"), laneLevelS[(size_t) l]));
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, kKitBase[l] + juce::String ("Tune"),  laneTuneS [(size_t) l]));
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, kKitBase[l] + juce::String ("Decay"), laneDecayS[(size_t) l]));
    }

    // --- Tap-drum pads: click to play; "Set" arms MIDI/key learn ---------------
    for (int i = 0; i < kNumPads; ++i)
    {
        auto& pad = padButtons[(size_t) i];
        pad.setButtonText (kPadNames[i]);
        pad.onClick = [this, i] { processor.pushLiveLane (i, 0.9f); grabKeyboardFocus(); };
        addAndMakeVisible (pad);

        auto& set = padSetButtons[(size_t) i];
        set.setButtonText ("Set");
        set.onClick = [this, i]
        {
            if (processor.learningPad() == i) processor.cancelLearn();   // toggle off
            else                              processor.armLearn (i);
            grabKeyboardFocus();
        };
        addAndMakeVisible (set);
    }
    refreshPadLabels();

    // Loop controls: toggle + bars.
    loopButton.setClickingTogglesState (true);
    addAndMakeVisible (loopButton);
    buttonAtts.push_back (std::make_unique<ButtonAttachment> (params, "loopOn", loopButton));
    loopBarsBox.addItemList ({ "1 bar", "2 bars", "4 bars" }, 1);
    addAndMakeVisible (loopBarsBox);
    loopBarsAtt = std::make_unique<ComboAttachment> (params, "loopBars", loopBarsBox);

    // Editable-grid controls: Generate fills lanes from the style, Clear wipes it.
    generateButton.onClick = [this] { processor.regenerateGrid(); repaint(); };
    clearButton.onClick    = [this] { processor.clearGrid();      repaint(); };
    addAndMakeVisible (generateButton);
    addAndMakeVisible (clearButton);

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

void SattariRhythmGenieAudioProcessorEditor::setupKitKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);   // caption drawn in paint()
    addAndMakeVisible (s);
}

// Laptop keyboard plays the pads (and captures a learn binding when a "Set" is armed).
bool SattariRhythmGenieAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const int kc = key.getKeyCode();
    if ((kc >= 'A' && kc <= 'Z') || (kc >= '0' && kc <= '9'))
    {
        processor.pushLiveKey (kc, 0.9f);
        return true;   // consume so it doesn't beep
    }
    return false;
}

// Map a point to a grid cell using the same geometry as drawGrid.
bool SattariRhythmGenieAudioProcessorEditor::gridCellAt (juce::Point<int> p, int& laneOut, int& stepOut) const
{
    const auto& bg = processor.beatGrid();
    const int lanes = bg.lanes(), steps = bg.steps();
    const int labelW = 78;
    auto gr = gridArea.reduced (10).withTrimmedLeft (labelW);
    if (! gr.contains (p) || steps <= 0 || lanes <= 0) return false;
    const int s = (int) ((float) (p.x - gr.getX()) / (gr.getWidth()  / (float) steps));
    const int l = (int) ((float) (p.y - gr.getY()) / (gr.getHeight() / (float) lanes));
    if (s < 0 || s >= steps || l < 0 || l >= lanes) return false;
    laneOut = l; stepOut = s; return true;
}

// Click a grid cell to toggle it (draw / erase steps live).
void SattariRhythmGenieAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    int lane = 0, step = 0;
    if (gridCellAt (e.getPosition(), lane, step))
    {
        processor.toggleCell (lane, step);
        repaint();
    }
}

// Show each pad's bound MIDI note + key, and highlight the one currently arming for learn.
void SattariRhythmGenieAudioProcessorEditor::refreshPadLabels()
{
    const int learning = processor.learningPad();
    for (int i = 0; i < kNumPads; ++i)
    {
        const int note = processor.padMidiNote (i);
        const int keyc = processor.padKeyCode (i);
        juce::String binding;
        if (note >= 0) binding << note;
        if (keyc > 0)  binding << (binding.isEmpty() ? "" : "/") << juce::String::charToString ((juce_wchar) keyc);
        padButtons[(size_t) i].setButtonText (juce::String (kPadNames[i]) + (binding.isEmpty() ? "" : "  " + binding));

        const bool arming = (learning == i);
        padSetButtons[(size_t) i].setButtonText (arming ? "…" : "Set");
        padSetButtons[(size_t) i].setColour (juce::TextButton::buttonColourId,
            arming ? juce::Colour (sattari::brand::kAccent) : juce::Colour (sattari::brand::kPanelRaised));
    }
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
    processor.publishTempoToBus();   // Rhythm Genie is the suite's tempo source
    const int step = processor.currentStep();
    transportStatus.setText (step >= 0 ? "Playing - step " + juce::String (step + 1) + " / 16"
                                       : "Stopped (press play in your DAW)",
                             juce::dontSendNotification);
    refreshPadLabels();
    repaint();
}

void SattariRhythmGenieAudioProcessorEditor::drawGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    const auto& bg = processor.beatGrid();
    const int lanes = bg.lanes();
    const int steps = bg.steps();
    const int labelW = 78;
    auto grid = area.reduced (10).withTrimmedLeft (labelW);
    const float cellW = grid.getWidth()  / (float) steps;
    const float cellH = grid.getHeight() / (float) lanes;
    const int   step  = processor.currentStep();

    // playhead column
    if (step >= 0)
    {
        g.setColour (juce::Colour (sattari::brand::kAccent).withAlpha (0.12f));
        g.fillRect (grid.getX() + step * cellW, (float) area.getY() + 6.0f, cellW, (float) area.getHeight() - 12.0f);
    }

    for (int lane = 0; lane < lanes; ++lane)
    {
        const float y = grid.getY() + lane * cellH;
        const bool on = processor.laneEnabled (lane);
        g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (on ? 1.0f : 0.4f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (lane < 8 ? kPadNames[lane] : "-", area.getX() + 12, (int) y, labelW - 6, (int) cellH,
                    juce::Justification::centredLeft);

        for (int s = 0; s < steps; ++s)
        {
            const float x = grid.getX() + s * cellW;
            juce::Rectangle<float> cell (x + 1.5f, y + 1.5f, cellW - 3.0f, cellH - 3.0f);
            const bool hit = bg.on (lane, s);
            const bool beat = (s % 4) == 0;

            g.setColour (juce::Colour (beat ? sattari::brand::kPanelRaised : sattari::brand::kPanel));
            g.fillRoundedRectangle (cell, 3.0f);
            if (hit)
            {
                auto c = kLaneColours[(size_t) (lane % (int) kLaneColours.size())];
                const bool live = (s == step);
                g.setColour (on ? (live ? juce::Colours::white : c.withAlpha (0.92f))
                                : c.withAlpha (0.30f));   // muted lane: dimmed
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

    auto sectionHeader = [&] (const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, area.getX(), area.getY() - 16, 240, 14, juce::Justification::centredLeft);
    };
    sectionHeader ("TAP PADS  -  click, or play your MIDI / keyboard  (Set = learn)", padsArea);
    sectionHeader ("KIT", kitArea);

    // Kit-knob captions (master + per-lane Level / Tune / Decay).
    auto knobCaption = [&] (juce::Slider& s, const juce::String& text)
    {
        auto b = s.getBounds();
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (text, b.getX() - 6, b.getBottom() - 2, b.getWidth() + 12, 14, juce::Justification::centred);
    };
    knobCaption (kitLevelSlider, "Master");
    for (int l = 0; l < kNumLanes; ++l)
    {
        knobCaption (laneLevelS[(size_t) l], juce::String (kPadNames[l]) + " Lvl");
        knobCaption (laneTuneS [(size_t) l], juce::String (kPadNames[l]) + " Tune");
        knobCaption (laneDecayS[(size_t) l], juce::String (kPadNames[l]) + " Dec");
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
    presetRow.removeFromLeft (14);
    loopButton.setBounds (presetRow.removeFromLeft (66));
    presetRow.removeFromLeft (4);
    loopBarsBox.setBounds (presetRow.removeFromLeft (86));
    area.removeFromTop (8);

    auto gridControls = area.removeFromTop (26);
    generateButton.setBounds (gridControls.removeFromLeft (96));
    gridControls.removeFromLeft (6);
    clearButton.setBounds (gridControls.removeFromLeft (72));
    area.removeFromTop (6);

    gridArea = area.removeFromTop (210);   // 8 lanes
    area.removeFromTop (24);   // room for knob captions

    auto knobs = area.removeFromTop (88);
    auto knobRow = knobs.withSizeKeepingCentre (juce::jmin (knobs.getWidth(), 680), knobs.getHeight());
    const int kw = knobRow.getWidth() / 6;
    for (auto* s : { &densitySlider, &swingSlider, &velocitySlider, &gateSlider, &accentSlider, &humanizeSlider })
        s->setBounds (knobRow.removeFromLeft (kw).reduced (4));

    area.removeFromTop (6);
    auto lanes = area.removeFromTop (24);
    const int lw = lanes.getWidth() / kNumLanes;
    for (int l = 0; l < kNumLanes; ++l)
        laneToggles[(size_t) l].setBounds (lanes.removeFromLeft (lw).removeFromLeft (40));

    // --- Tap-drum pads ---------------------------------------------------------
    area.removeFromTop (22);   // TAP PADS header
    padsArea = area.removeFromTop (74);
    {
        auto pads = padsArea;
        const int pw = pads.getWidth() / kNumPads;
        for (int i = 0; i < kNumPads; ++i)
        {
            auto col = pads.removeFromLeft (pw).reduced (4);
            padSetButtons[(size_t) i].setBounds (col.removeFromBottom (20));
            col.removeFromBottom (3);
            padButtons[(size_t) i].setBounds (col);
        }
    }

    // --- Kit-shaping knobs: master + per-lane Level / Tune / Decay -------------
    area.removeFromTop (22);   // KIT header
    kitArea = area.removeFromTop (78);
    {
        std::vector<juce::Slider*> ks { &kitLevelSlider };
        for (int l = 0; l < kNumLanes; ++l)
        {
            ks.push_back (&laneLevelS[(size_t) l]);
            ks.push_back (&laneTuneS [(size_t) l]);
            ks.push_back (&laneDecayS[(size_t) l]);
        }
        auto row = kitArea;
        const int n = (int) ks.size();
        const int cw = row.getWidth() / n;
        for (auto* s : ks)
            s->setBounds (row.removeFromLeft (cw).withTrimmedBottom (14).reduced (6));
    }
}
