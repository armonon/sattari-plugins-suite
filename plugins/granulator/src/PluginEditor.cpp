#include "PluginEditor.h"

// =============================================================================
//  Construction
// =============================================================================
SattariGranulatorAudioProcessorEditor::SattariGranulatorAudioProcessorEditor (SattariGranulatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "granulator")
{
    setLookAndFeel (&lookAndFeel);
    setSize (980, 640);
    setWantsKeyboardFocus (true);

    // ----- Header --------------------------------------------------------------
    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Granulator");
    addAndMakeVisible (title);

    // Shared KeyButton: MANUAL / FOLLOW SONG KEY, driving keyRoot / keyScale /
    // followKey exactly like Arp / Harmony / Maqam / VoxKey.
    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId    = "granulator";
        cfg.showFollow  = true;
        cfg.showAutoKey = false;
        cfg.rootNames   = SattariGranulatorAudioProcessor::keyRootChoices();
        cfg.scaleNames  = SattariGranulatorAudioProcessor::keyScaleChoices();
        cfg.hint        = "click: follow song key / set root + scale";
        keyButton.setConfig (cfg);
    }
    keyButton.onLinkFollow = [this] { setKeyParam ("followKey", 1.0f); };
    keyButton.onManualKey  = [this] (int root, int scaleIdx)
    {
        setKeyParam ("followKey", 0.0f);
        setKeyParam ("keyRoot",  (float) root);
        setKeyParam ("keyScale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    // Preset row: user presets + Save + A/B.
    styleCombo (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    addAndMakeVisible (userPresets);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    refreshUserPresets();

    // Master output knob (header, right side).
    styleKnob (outputKnob, true);
    outputKnob.setTextValueSuffix (" dB");
    styleCaption (outputCaption, "OUTPUT");
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (outputCaption);
    outputAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), "volume", outputKnob);

    // ----- Source strip --------------------------------------------------------
    addAndMakeVisible (waveform);

    sourceNameLabel.setJustificationType (juce::Justification::centredLeft);
    sourceNameLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    sourceNameLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (sourceNameLabel);

    loadButton.onClick      = [this] { openFileChooser(); };
    reverseButton.onClick   = [this] { audioProcessor.editReverse(); };
    normalizeButton.onClick = [this] { audioProcessor.editNormalize(); };
    trimButton.onClick      = [this] { audioProcessor.editTrimToSelection(); };
    monoFoldButton.onClick  = [this] { audioProcessor.editMonoFold(); };
    undoButton.onClick      = [this] { audioProcessor.editUndo(); };
    for (auto* b : { &loadButton, &reverseButton, &normalizeButton, &trimButton,
                     &monoFoldButton, &undoButton })
        addAndMakeVisible (*b);

    styleToggle (reverseGrainsToggle, "Reverse Grains");
    addAndMakeVisible (reverseGrainsToggle);
    reverseGrainsAttach = std::make_unique<ButtonAttachment> (audioProcessor.parameters(),
                                                              "srcReverse", reverseGrainsToggle);

    styleCombo (loopModeCombo);
    loopModeCombo.addItemList (juce::StringArray { "Off", "Loop" }, 1);
    styleCaption (loopCaption, "LOOP");
    addAndMakeVisible (loopModeCombo);
    addAndMakeVisible (loopCaption);
    loopModeAttach = std::make_unique<ComboAttachment> (audioProcessor.parameters(),
                                                        "srcLoopMode", loopModeCombo);

    styleCombo (channelsCombo);
    channelsCombo.addItemList (juce::StringArray { "Stereo", "Mono", "Left", "Right" }, 1);
    styleCaption (channelsCaption, "CHANNELS");
    addAndMakeVisible (channelsCombo);
    addAndMakeVisible (channelsCaption);
    channelsAttach = std::make_unique<ComboAttachment> (audioProcessor.parameters(),
                                                        "srcMono", channelsCombo);

    // ----- Tab strip -----------------------------------------------------------
    for (int i = 0; i < kNumTabs; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setClickingTogglesState (true);
        b.setRadioGroupId (0x6472);
        b.onClick = [this, i] { showTab (i); };
        addAndMakeVisible (b);
    }
    tabButtons[0].setToggleState (true, juce::dontSendNotification);

    for (auto* panel : panels)
        addAndMakeVisible (*panel);

    // ===== GRAIN tab ==========================================================
    addKnob  (grainPanel, grainSize, grainSizeCap, "SIZE",     "grainSize",   sliderAttachments);
    grainSize.setTextValueSuffix (" ms");
    addKnob  (grainPanel, density, densityCap,     "DENSITY",  "density",     sliderAttachments);
    addCombo (grainPanel, densityMode, densityModeCap, "MODE", { "Rate", "Overlap" }, "densityMode", comboAttachments);
    addKnob  (grainPanel, spray, sprayCap,         "SPRAY",    "spray",       sliderAttachments);
    addKnob  (grainPanel, posBase, posBaseCap,     "POSITION", "posBase",     sliderAttachments);
    addKnob  (grainPanel, posScanRate, posScanRateCap, "SCAN", "posScanRate", sliderAttachments);
    addToggle (grainPanel, posScanSync, "Scan Sync", "posScanSync", buttonAttachments);
    addCombo (grainPanel, posScanDiv, posScanDivCap, "DIV",
              SattariGranulatorAudioProcessor::divisionChoices(), "posScanDiv", comboAttachments);
    addKnob  (grainPanel, grainPan, grainPanCap,   "PAN",      "grainPan",    sliderAttachments);
    addCombo (grainPanel, window, windowCap,       "WINDOW",   { "Hann", "Gauss", "Tukey" }, "window", comboAttachments);
    addToggle (grainPanel, subOsc, "Sub Osc",      "subOsc",   buttonAttachments);
    addKnob  (grainPanel, subLevel, subLevelCap,   "SUB LVL",  "subLevel",    sliderAttachments);
    addCombo (grainPanel, subWave, subWaveCap,     "SUB WAVE", { "Sine", "Triangle" }, "subWave", comboAttachments);
    addKnob  (grainPanel, subOct, subOctCap,       "SUB OCT",  "subOct",      sliderAttachments);

    // ===== PITCH tab ==========================================================
    addKnob  (pitchPanel, tuneCoarse, tuneCoarseCap, "COARSE",  "tuneCoarse", sliderAttachments);
    tuneCoarse.setTextValueSuffix (" st");
    addKnob  (pitchPanel, tuneFine, tuneFineCap,     "FINE",    "tuneFine",   sliderAttachments);
    tuneFine.setTextValueSuffix (" ct");
    addKnob  (pitchPanel, pitchSpread, pitchSpreadCap, "SPREAD", "pitchSpread", sliderAttachments);
    pitchSpread.setTextValueSuffix (" ct");
    addKnob  (pitchPanel, glide, glideCap,           "GLIDE",   "glide",      sliderAttachments);
    addCombo (pitchPanel, glideMode, glideModeCap,   "GLIDE MODE", { "Always", "Legato" }, "glideMode", comboAttachments);
    addKnob  (pitchPanel, pitchEnvAmt, pitchEnvAmtCap, "P.ENV",  "pitchEnvAmt", sliderAttachments);
    pitchEnvAmt.setTextValueSuffix (" st");
    addKnob  (pitchPanel, pitchEnvAttack, pitchEnvAttackCap, "P.ATK", "pitchEnvAttack", sliderAttachments);
    pitchEnvAttack.setTextValueSuffix (" ms");
    addKnob  (pitchPanel, pitchEnvDecay, pitchEnvDecayCap, "P.DEC",  "pitchEnvDecay", sliderAttachments);
    pitchEnvDecay.setTextValueSuffix (" ms");
    addKnob  (pitchPanel, pitchLfoAmt, pitchLfoAmtCap, "VIB DEPTH", "pitchLfoAmt", sliderAttachments);
    pitchLfoAmt.setTextValueSuffix (" ct");
    addKnob  (pitchPanel, pitchLfoRate, pitchLfoRateCap, "VIB RATE", "pitchLfoRate", sliderAttachments);
    pitchLfoRate.setTextValueSuffix (" Hz");
    addToggle (pitchPanel, keySnap, "Key Snap", "keySnap", buttonAttachments);

    // ===== FILTER tab =========================================================
    addKnob  (filterPanel, cutoff, cutoffCap, "CUTOFF", "cutoff", sliderAttachments);
    cutoff.setTextValueSuffix (" Hz");
    addKnob  (filterPanel, reso, resoCap, "RESO", "reso", sliderAttachments);
    addCombo (filterPanel, filterType, filterTypeCap, "TYPE", { "LP24", "HP12", "BP" }, "filterType", comboAttachments);
    addKnob  (filterPanel, filterKeyTrack, filterKeyTrackCap, "KEY TRK", "filterKeyTrack", sliderAttachments);
    addKnob  (filterPanel, filterEnvAmt, filterEnvAmtCap, "ENV AMT", "filterEnvAmt", sliderAttachments);

    // ===== ENV tab ============================================================
    addKnob (envPanel, ampAttack,  ampAttackCap,  "AMP A", "ampAttack",  sliderAttachments); ampAttack.setTextValueSuffix (" ms");
    addKnob (envPanel, ampDecay,   ampDecayCap,   "AMP D", "ampDecay",   sliderAttachments); ampDecay.setTextValueSuffix (" ms");
    addKnob (envPanel, ampSustain, ampSustainCap, "AMP S", "ampSustain", sliderAttachments);
    addKnob (envPanel, ampRelease, ampReleaseCap, "AMP R", "ampRelease", sliderAttachments); ampRelease.setTextValueSuffix (" ms");
    addKnob (envPanel, fltAttack,  fltAttackCap,  "FLT A", "fltAttack",  sliderAttachments); fltAttack.setTextValueSuffix (" ms");
    addKnob (envPanel, fltDecay,   fltDecayCap,   "FLT D", "fltDecay",   sliderAttachments); fltDecay.setTextValueSuffix (" ms");
    addKnob (envPanel, fltSustain, fltSustainCap, "FLT S", "fltSustain", sliderAttachments);
    addKnob (envPanel, fltRelease, fltReleaseCap, "FLT R", "fltRelease", sliderAttachments); fltRelease.setTextValueSuffix (" ms");
    addKnob (envPanel, envVelSens, envVelSensCap, "VEL",   "envVelSens", sliderAttachments);
    addKnob (envPanel, modEnvAttack, modEnvAttackCap, "MOD A", "modEnvAttack", sliderAttachments); modEnvAttack.setTextValueSuffix (" ms");
    addKnob (envPanel, modEnvDecay,  modEnvDecayCap,  "MOD D", "modEnvDecay",  sliderAttachments); modEnvDecay.setTextValueSuffix (" ms");

    // ===== LFO tab ============================================================
    addKnob  (lfoPanel, lfo1Rate, lfo1RateCap, "LFO1 RATE", "lfo1Rate", sliderAttachments); lfo1Rate.setTextValueSuffix (" Hz");
    addCombo (lfoPanel, lfo1Shape, lfo1ShapeCap, "LFO1 SHAPE", { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, "lfo1Shape", comboAttachments);
    addToggle (lfoPanel, lfo1Sync, "LFO1 Sync", "lfo1Sync", buttonAttachments);
    addCombo (lfoPanel, lfo1Div, lfo1DivCap, "LFO1 DIV", SattariGranulatorAudioProcessor::divisionChoices(), "lfo1Div", comboAttachments);
    addCombo (lfoPanel, lfo1Dest, lfo1DestCap, "LFO1 DEST",
              { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, "lfo1Dest", comboAttachments);
    addKnob  (lfoPanel, lfo1Amt, lfo1AmtCap, "LFO1 AMT", "lfo1Amt", sliderAttachments);
    addKnob  (lfoPanel, lfo2Rate, lfo2RateCap, "LFO2 RATE", "lfo2Rate", sliderAttachments); lfo2Rate.setTextValueSuffix (" Hz");
    addCombo (lfoPanel, lfo2Shape, lfo2ShapeCap, "LFO2 SHAPE", { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, "lfo2Shape", comboAttachments);
    addToggle (lfoPanel, lfo2Sync, "LFO2 Sync", "lfo2Sync", buttonAttachments);
    addCombo (lfoPanel, lfo2Div, lfo2DivCap, "LFO2 DIV", SattariGranulatorAudioProcessor::divisionChoices(), "lfo2Div", comboAttachments);
    addCombo (lfoPanel, lfo2Dest, lfo2DestCap, "LFO2 DEST",
              { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, "lfo2Dest", comboAttachments);
    addKnob  (lfoPanel, lfo2Amt, lfo2AmtCap, "LFO2 AMT", "lfo2Amt", sliderAttachments);

    // ===== MOD tab (fixed routing view in v1) =================================
    addKnob  (modPanel, posEnvAmt, posEnvAmtCap, "POS ENV", "posEnvAmt", sliderAttachments);
    addKnob  (modPanel, posLfoAmt, posLfoAmtCap, "POS LFO", "posLfoAmt", sliderAttachments);
    addKnob  (modPanel, srcGain, srcGainCap, "SRC GAIN", "srcGain", sliderAttachments); srcGain.setTextValueSuffix (" dB");
    addKnob  (modPanel, width, widthCap, "WIDTH", "width", sliderAttachments);
    addKnob  (modPanel, tone, toneCap, "TONE", "tone", sliderAttachments);
    addKnob  (modPanel, voices, voicesCap, "VOICES", "voices", sliderAttachments);
    addCombo (modPanel, voiceMode, voiceModeCap, "VOICE MODE", { "Poly", "Mono", "Legato" }, "voiceMode", comboAttachments);

    // ----- Bottom keyboard -----------------------------------------------------
    keyboard.setAvailableRange (36, 96);   // C2..C7 fits the strip
    keyboard.setKeyWidth (18.0f);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                        juce::Colour (sattari::brand::productAccent ("granulator")).withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        juce::Colour (sattari::brand::productAccent ("granulator")).withAlpha (0.25f));
    addAndMakeVisible (keyboard);

    showTab (0);
    startTimerHz (24);
}

SattariGranulatorAudioProcessorEditor::~SattariGranulatorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// =============================================================================
//  Styling helpers
// =============================================================================
void SattariGranulatorAudioProcessorEditor::styleKnob (juce::Slider& s, bool feature)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, feature ? 80 : 62, feature ? 20 : 16);
}

void SattariGranulatorAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariGranulatorAudioProcessorEditor::styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (sattari::brand::kPanelRaised));
    c.setColour (juce::ComboBox::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    c.setColour (juce::ComboBox::outlineColourId, juce::Colour (sattari::brand::kHairline));
}

void SattariGranulatorAudioProcessorEditor::styleToggle (juce::ToggleButton& t, const juce::String& text)
{
    t.setButtonText (text);
    t.setColour (juce::ToggleButton::textColourId, juce::Colour (sattari::brand::kTextPrimary));
}

// =============================================================================
//  Widget builders (bind + add to a panel)
// =============================================================================
void SattariGranulatorAudioProcessorEditor::addKnob (juce::Component& panel, juce::Slider& s,
                                                     juce::Label& cap, const juce::String& text,
                                                     const char* paramId,
                                                     std::vector<std::unique_ptr<SliderAttachment>>& store)
{
    styleKnob (s);
    styleCaption (cap, text);
    panel.addAndMakeVisible (s);
    panel.addAndMakeVisible (cap);
    store.push_back (std::make_unique<SliderAttachment> (audioProcessor.parameters(), paramId, s));
}

void SattariGranulatorAudioProcessorEditor::addCombo (juce::Component& panel, juce::ComboBox& c,
                                                      juce::Label& cap, const juce::String& text,
                                                      const juce::StringArray& items, const char* paramId,
                                                      std::vector<std::unique_ptr<ComboAttachment>>& store)
{
    styleCombo (c);
    c.addItemList (items, 1);
    styleCaption (cap, text);
    panel.addAndMakeVisible (c);
    panel.addAndMakeVisible (cap);
    store.push_back (std::make_unique<ComboAttachment> (audioProcessor.parameters(), paramId, c));
}

void SattariGranulatorAudioProcessorEditor::addToggle (juce::Component& panel, juce::ToggleButton& t,
                                                       const juce::String& text, const char* paramId,
                                                       std::vector<std::unique_ptr<ButtonAttachment>>& store)
{
    styleToggle (t, text);
    panel.addAndMakeVisible (t);
    store.push_back (std::make_unique<ButtonAttachment> (audioProcessor.parameters(), paramId, t));
}

// =============================================================================
//  Preset / file helpers
// =============================================================================
void SattariGranulatorAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : presetManager.userPresetNames())
        userPresets.addItem (n, id++);
}

void SattariGranulatorAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariGranulatorAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load a sample to granulate",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.loadFile (file);
    });
}

void SattariGranulatorAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* p = audioProcessor.parameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void SattariGranulatorAudioProcessorEditor::showTab (int idx)
{
    currentTab = juce::jlimit (0, kNumTabs - 1, idx);
    for (int i = 0; i < kNumTabs; ++i)
        panels[(size_t) i]->setVisible (i == currentTab);
    tabButtons[(size_t) currentTab].setToggleState (true, juce::dontSendNotification);
}

// =============================================================================
//  Timer — reflect the effective key + source name / undo state
// =============================================================================
void SattariGranulatorAudioProcessorEditor::timerCallback()
{
    followingBus = audioProcessor.isFollowingBusKey();
    keyButton.setState (followingBus ? sattari::ui::KeyButton::Mode::Follow
                                     : sattari::ui::KeyButton::Mode::Manual,
                        audioProcessor.effectiveKeyRoot(),
                        audioProcessor.effectiveKeyScale());

    const auto name = audioProcessor.currentSourceName();
    sourceNameLabel.setText (name.isNotEmpty() ? name : "(no sample loaded)",
                             juce::dontSendNotification);

    undoButton.setEnabled (audioProcessor.canUndo());

    // Repaint the header region so the follow-status dot tracks the bus.
    repaint (0, 0, getWidth(), 60);
}

// =============================================================================
//  paint
// =============================================================================
void SattariGranulatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Follow-status dot next to the KeyButton: lit accent while a fresh bus key
    // is driving us, faint otherwise.
    const auto accent = juce::Colour (sattari::brand::productAccent ("granulator"));
    const auto dot = keyButton.getBounds().toFloat();
    juce::Rectangle<float> dotR (dot.getRight() + 8.0f, dot.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (followingBus ? accent : juce::Colour (sattari::brand::kTextFaint).withAlpha (0.5f));
    g.fillEllipse (dotR);
}

// =============================================================================
//  Layout
// =============================================================================
void SattariGranulatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    // ----- Header row ----------------------------------------------------------
    auto header = area.removeFromTop (48);
    title.setBounds (header.removeFromLeft (200));
    header.removeFromLeft (8);

    // Master output on the far right of the header.
    auto outCol = header.removeFromRight (70);
    outputCaption.setBounds (outCol.removeFromTop (13));
    outputKnob.setBounds (outCol);
    header.removeFromRight (14);

    // Preset cluster right of centre.
    auto presetCol = header.removeFromRight (330);
    auto presetRow = presetCol.removeFromTop (26);
    abRow.setBounds (presetRow.removeFromRight (84));
    presetRow.removeFromRight (6);
    savePresetButton.setBounds (presetRow.removeFromRight (56));
    presetRow.removeFromRight (6);
    userPresets.setBounds (presetRow);

    // KeyButton fills what remains between title and presets (leave room for dot).
    header.removeFromLeft (12);
    header.removeFromRight (18);   // status dot gutter
    keyButton.setBounds (header.withSizeKeepingCentre (juce::jmin (300, header.getWidth()), 44));

    area.removeFromTop (8);

    // ----- Source strip: edit-op buttons + source controls above the waveform --
    auto sourceBar = area.removeFromTop (30);
    loadButton.setBounds     (sourceBar.removeFromLeft (94));
    sourceBar.removeFromLeft (6);
    reverseButton.setBounds  (sourceBar.removeFromLeft (72));
    sourceBar.removeFromLeft (4);
    normalizeButton.setBounds (sourceBar.removeFromLeft (78));
    sourceBar.removeFromLeft (4);
    trimButton.setBounds     (sourceBar.removeFromLeft (56));
    sourceBar.removeFromLeft (4);
    monoFoldButton.setBounds (sourceBar.removeFromLeft (82));
    sourceBar.removeFromLeft (4);
    undoButton.setBounds     (sourceBar.removeFromLeft (56));
    sourceBar.removeFromLeft (12);
    // Source-shaping controls on the right of the strip.
    reverseGrainsToggle.setBounds (sourceBar.removeFromLeft (140).withSizeKeepingCentre (140, 22));
    {
        auto chCol = sourceBar.removeFromRight (90);
        channelsCaption.setBounds (chCol.removeFromTop (12));
        channelsCombo.setBounds (chCol);
        sourceBar.removeFromRight (8);
        auto loopCol = sourceBar.removeFromRight (70);
        loopCaption.setBounds (loopCol.removeFromTop (12));
        loopModeCombo.setBounds (loopCol);
    }

    area.removeFromTop (4);

    // ----- Waveform (always visible) ------------------------------------------
    auto waveRow = area.removeFromTop (218);
    sourceNameLabel.setBounds (waveRow.removeFromBottom (16));
    waveform.setBounds (waveRow);

    area.removeFromTop (8);

    // ----- Bottom keyboard -----------------------------------------------------
    keyboard.setBounds (area.removeFromBottom (72));
    area.removeFromBottom (8);

    // ----- Tab strip -----------------------------------------------------------
    auto tabRow = area.removeFromTop (26);
    const int tabW = 92;
    for (int i = 0; i < kNumTabs; ++i)
    {
        tabButtons[(size_t) i].setBounds (tabRow.removeFromLeft (tabW));
        tabRow.removeFromLeft (4);
    }

    area.removeFromTop (8);

    // ----- Rack body: all panels share the same rect --------------------------
    for (auto* panel : panels)
        panel->setBounds (area);

    const int cellW = 86;
    const int knobH = 92;
    const int comboH = 44;
    const int toggleH = 26;

    // Simple flow layouter: place a list of (widget, caption, height) cells,
    // wrapping to a new row when the current row runs out of width.
    struct Cell { juce::Component* w; juce::Label* cap; int height; };
    auto flow = [&] (juce::Component& panel, std::vector<Cell> cells)
    {
        auto body = panel.getLocalBounds().reduced (6);
        int x = body.getX();
        int y = body.getY();
        int rowH = 0;
        for (auto& c : cells)
        {
            if (x + cellW > body.getRight())   // wrap
            {
                x = body.getX();
                y += rowH + 8;
                rowH = 0;
            }
            juce::Rectangle<int> cell (x, y, cellW, c.height);
            if (c.cap != nullptr)
                c.cap->setBounds (cell.removeFromTop (14));
            c.w->setBounds (cell);
            rowH = juce::jmax (rowH, c.height);
            x += cellW + 4;
        }
    };

    flow (grainPanel, {
        { &grainSize, &grainSizeCap, knobH }, { &density, &densityCap, knobH },
        { &densityMode, &densityModeCap, comboH }, { &spray, &sprayCap, knobH },
        { &posBase, &posBaseCap, knobH }, { &posScanRate, &posScanRateCap, knobH },
        { &posScanSync, nullptr, toggleH }, { &posScanDiv, &posScanDivCap, comboH },
        { &grainPan, &grainPanCap, knobH }, { &window, &windowCap, comboH },
        { &subOsc, nullptr, toggleH }, { &subLevel, &subLevelCap, knobH },
        { &subWave, &subWaveCap, comboH }, { &subOct, &subOctCap, knobH } });

    flow (pitchPanel, {
        { &tuneCoarse, &tuneCoarseCap, knobH }, { &tuneFine, &tuneFineCap, knobH },
        { &pitchSpread, &pitchSpreadCap, knobH }, { &glide, &glideCap, knobH },
        { &glideMode, &glideModeCap, comboH }, { &pitchEnvAmt, &pitchEnvAmtCap, knobH },
        { &pitchEnvAttack, &pitchEnvAttackCap, knobH }, { &pitchEnvDecay, &pitchEnvDecayCap, knobH },
        { &pitchLfoAmt, &pitchLfoAmtCap, knobH }, { &pitchLfoRate, &pitchLfoRateCap, knobH },
        { &keySnap, nullptr, toggleH } });

    flow (filterPanel, {
        { &cutoff, &cutoffCap, knobH }, { &reso, &resoCap, knobH },
        { &filterType, &filterTypeCap, comboH }, { &filterKeyTrack, &filterKeyTrackCap, knobH },
        { &filterEnvAmt, &filterEnvAmtCap, knobH } });

    flow (envPanel, {
        { &ampAttack, &ampAttackCap, knobH }, { &ampDecay, &ampDecayCap, knobH },
        { &ampSustain, &ampSustainCap, knobH }, { &ampRelease, &ampReleaseCap, knobH },
        { &fltAttack, &fltAttackCap, knobH }, { &fltDecay, &fltDecayCap, knobH },
        { &fltSustain, &fltSustainCap, knobH }, { &fltRelease, &fltReleaseCap, knobH },
        { &envVelSens, &envVelSensCap, knobH }, { &modEnvAttack, &modEnvAttackCap, knobH },
        { &modEnvDecay, &modEnvDecayCap, knobH } });

    flow (lfoPanel, {
        { &lfo1Rate, &lfo1RateCap, knobH }, { &lfo1Shape, &lfo1ShapeCap, comboH },
        { &lfo1Sync, nullptr, toggleH }, { &lfo1Div, &lfo1DivCap, comboH },
        { &lfo1Dest, &lfo1DestCap, comboH }, { &lfo1Amt, &lfo1AmtCap, knobH },
        { &lfo2Rate, &lfo2RateCap, knobH }, { &lfo2Shape, &lfo2ShapeCap, comboH },
        { &lfo2Sync, nullptr, toggleH }, { &lfo2Div, &lfo2DivCap, comboH },
        { &lfo2Dest, &lfo2DestCap, comboH }, { &lfo2Amt, &lfo2AmtCap, knobH } });

    flow (modPanel, {
        { &posEnvAmt, &posEnvAmtCap, knobH }, { &posLfoAmt, &posLfoAmtCap, knobH },
        { &srcGain, &srcGainCap, knobH }, { &width, &widthCap, knobH },
        { &tone, &toneCap, knobH }, { &voices, &voicesCap, knobH },
        { &voiceMode, &voiceModeCap, comboH } });
}
