#include "PluginEditor.h"

namespace
{
const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

// ============================================================================
// NoteDisplay
// ============================================================================
void NoteDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));

    auto noteArea = bounds.removeFromTop (bounds.getHeight() * 0.65f);
    auto barArea  = bounds;

    // Big note
    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (juce::FontOptions (juce::jmin (170.0f, noteArea.getHeight() * 0.9f),
                                  juce::Font::plain).withStyle ("Italic"));
    juce::String noteText = "-";
    if (snapshot.voiced && snapshot.detectedMidi >= 0)
    {
        const int pc = ((snapshot.detectedMidi % 12) + 12) % 12;
        noteText = noteNames[pc];
    }
    g.drawText (noteText, noteArea.toNearestInt(), juce::Justification::centred, false);

    // Cents bar — wide -100..+100 with a coloured indicator at correctionCents.
    barArea = barArea.reduced (10.0f, 6.0f);
    const float trackH = 14.0f;
    auto track = juce::Rectangle<float> (barArea.getX(),
                                          barArea.getCentreY() - trackH * 0.5f,
                                          barArea.getWidth(),
                                          trackH);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (track, trackH * 0.5f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);

    // Centre tick.
    const float centerX = track.getCentreX();
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.drawLine (centerX, track.getY() - 4.0f, centerX, track.getBottom() + 4.0f, 1.0f);

    // Tick marks every 25 cents.
    for (int c = -100; c <= 100; c += 25)
    {
        if (c == 0) continue;
        const float x = juce::jmap ((float) c, -100.0f, 100.0f, track.getX(), track.getRight());
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawLine (x, track.getY() + 1.0f, x, track.getBottom() - 1.0f, 1.0f);
    }

    // Indicator: a coloured block sliding along the track, width ~= correction magnitude.
    if (snapshot.voiced)
    {
        const float cents = juce::jlimit (-100.0f, 100.0f, snapshot.correctionCents);
        const float w = juce::jmax (8.0f, std::abs (cents) * (track.getWidth() * 0.5f / 100.0f));
        const float x = (cents >= 0.0f) ? centerX : centerX - w;
        juce::Rectangle<float> indicator (cents >= 0.0f ? centerX : centerX - w,
                                          track.getY(),
                                          (cents >= 0.0f ? w : w),
                                          track.getHeight());
        juce::ignoreUnused (x);
        g.setColour (accent.withAlpha (0.55f));
        g.fillRoundedRectangle (indicator.reduced (1.0f), trackH * 0.5f - 1.0f);
    }

    // Scale labels
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (10.0f));
    auto labelArea = juce::Rectangle<float> (track.getX(), track.getBottom() + 4.0f, track.getWidth(), 14.0f);
    g.drawText ("-100", labelArea.removeFromLeft (40.0f).toNearestInt(), juce::Justification::centredLeft, false);
    g.drawText ("+100", labelArea.removeFromRight (40.0f).toNearestInt(), juce::Justification::centredRight, false);
    g.drawText ("0", labelArea.toNearestInt(), juce::Justification::centred, false);
}

// ============================================================================
// KeyPillRow
// ============================================================================
void KeyPillRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));

    // Two rows in the screenshot: top row has the 5 sharps/flats (C# D# F# G# A#),
    // bottom row has the 7 naturals (C D E F G A B). Render the same way.
    const float pillH = bounds.getHeight() * 0.42f;
    auto topRow = bounds.removeFromTop (pillH);
    bounds.removeFromTop (6.0f);
    auto botRow = bounds.removeFromTop (pillH);

    // Scale intervals (matches scaleChoices: 0=major, 1=minor, 2=chromatic).
    auto inScale = [&] (int pc) -> bool
    {
        const int degree = ((pc - keyRootIdx) % 12 + 12) % 12;
        if (scaleIdx == 2) return true; // chromatic
        if (scaleIdx == 0)
            return degree == 0 || degree == 2 || degree == 4 || degree == 5
                || degree == 7 || degree == 9 || degree == 11;
        // minor
        return degree == 0 || degree == 2 || degree == 3 || degree == 5
            || degree == 7 || degree == 8 || degree == 10;
    };

    auto drawPill = [&] (juce::Rectangle<float> r, int pc)
    {
        const bool lit       = inScale (pc);
        const bool isDetected = (detectedMidi >= 0)
                                && (((detectedMidi % 12) + 12) % 12) == pc;
        const auto pillBg = lit ? accent.withAlpha (isDetected ? 0.95f : 0.50f)
                                : juce::Colour (sattari::brand::kPanelRaised);
        const auto stroke = lit ? accent.withAlpha (0.85f)
                                : juce::Colour (sattari::brand::kHairline);
        const auto textCol = lit ? juce::Colour (sattari::brand::kBackground)
                                 : juce::Colour (sattari::brand::kTextMuted);
        g.setColour (pillBg);
        g.fillRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f);
        g.setColour (stroke);
        g.drawRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f, 1.0f);
        g.setColour (textCol);
        g.setFont (juce::FontOptions (r.getHeight() * 0.42f, juce::Font::plain));
        g.drawText (noteNames[pc], r.toNearestInt(), juce::Justification::centred, false);
    };

    // Top row: positions match the black-key positions on a keyboard.
    // C#=1, D#=3, F#=6, G#=8, A#=10 → 5 pills, but laid out under the naturals
    // they're between. Simpler: just space the 5 sharps evenly.
    const int sharps[5] = { 1, 3, 6, 8, 10 };
    const float gapTop = 8.0f;
    const float pillWTop = (topRow.getWidth() - gapTop * 4.0f) / 5.0f;
    auto cursor = topRow;
    for (int i = 0; i < 5; ++i)
    {
        auto cell = cursor.removeFromLeft (pillWTop);
        drawPill (cell, sharps[i]);
        cursor.removeFromLeft (gapTop);
    }

    // Bottom row: 7 naturals
    const int naturals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const float gapBot = 6.0f;
    const float pillWBot = (botRow.getWidth() - gapBot * 6.0f) / 7.0f;
    cursor = botRow;
    for (int i = 0; i < 7; ++i)
    {
        auto cell = cursor.removeFromLeft (pillWBot);
        drawPill (cell, naturals[i]);
        cursor.removeFromLeft (gapBot);
    }
}

// ============================================================================
// Editor
// ============================================================================
SattariAutoPitchAudioProcessorEditor::SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "auto-pitch")
{
    setLookAndFeel (&lookAndFeel);
    setSize (960, 600);

    // Header
    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Auto Pitch");
    addAndMakeVisible (title);

    vocalRange.addItemList ({
        "Wide",
        "Bass Voice", "Baritone", "Tenor", "Alto / Tenor", "Alto", "Soprano",
        "Instrument: Bass", "Instrument: Mid", "Instrument: High"
    }, 1);
    keyRoot.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    scale.addItemList ({ "Major", "Minor", "Chromatic" }, 1);
    for (auto* c : { &vocalRange, &keyRoot, &scale, &factoryPresets, &userPresets })
        addAndMakeVisible (*c);

    styleCaption (vocalRangeCaption, "SOUND SOURCE");
    styleCaption (keyScaleCaption, "KEY & SCALE");
    addAndMakeVisible (vocalRangeCaption);
    addAndMakeVisible (keyScaleCaption);

    factoryPresets.setTextWhenNothingSelected ("Factory");
    registerFactoryPresets();
    factoryPresets.addItemList (presetManager.factoryPresetNames(), 1);
    factoryPresets.onChange = [this] {
        const int i = factoryPresets.getSelectedItemIndex();
        if (i >= 0) presetManager.loadFactoryPreset (i);
    };
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] {
        const auto n = userPresets.getText();
        if (n.isNotEmpty()) presetManager.loadUserPreset (n);
    };
    addAndMakeVisible (savePresetButton);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    refreshUserPresets();

    // Tabs
    tabBasic.setClickingTogglesState (true);
    tabPro.setClickingTogglesState (true);
    tabBasic.setRadioGroupId (10);
    tabPro.setRadioGroupId (10);
    tabBasic.setToggleState (true, juce::dontSendNotification);
    tabBasic.onClick = [this] { showTab (0); };
    tabPro.onClick   = [this] { showTab (1); };
    addAndMakeVisible (tabBasic);
    addAndMakeVisible (tabPro);

    // Panels
    addAndMakeVisible (basicPanel);
    addAndMakeVisible (proPanel);

    // ----- Basic -----
    basicPanel.addAndMakeVisible (autoKey);
    basicPanel.addAndMakeVisible (noteDisplay);
    basicPanel.addAndMakeVisible (pillRow);
    styleCaption (editNotesLabel, "EDIT NOTES");
    basicPanel.addAndMakeVisible (editNotesLabel);
    basicPanel.addAndMakeVisible (lowLatency);

    styleKnob (retuneSpeed, true);
    styleKnob (flexTune);
    styleKnob (humanize);
    humanize.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    flexTune.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    retuneSpeed.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    styleCaption (retuneSpeedCaption, "RETUNE SPEED");
    styleCaption (flexTuneCaption, "FLEX TUNE");
    styleCaption (humanizeCaption, "HUMANIZE");
    basicPanel.addAndMakeVisible (retuneSpeed);
    basicPanel.addAndMakeVisible (flexTune);
    basicPanel.addAndMakeVisible (humanize);
    basicPanel.addAndMakeVisible (retuneSpeedCaption);
    basicPanel.addAndMakeVisible (flexTuneCaption);
    basicPanel.addAndMakeVisible (humanizeCaption);

    style.setButtonText ("");
    basicPanel.addAndMakeVisible (style);
    styleCaption (modernLabel, "MODERN");
    styleCaption (classicLabel, "CLASSIC");
    basicPanel.addAndMakeVisible (modernLabel);
    basicPanel.addAndMakeVisible (classicLabel);

    styleCaption (detuneLabel, "Detune live");
    basicPanel.addAndMakeVisible (detuneLabel);

    // ----- Pro -----
    proPanel.addAndMakeVisible (enableCorrection);
    proPanel.addAndMakeVisible (doNoHarm);
    proPanel.addAndMakeVisible (formantPreserve);
    styleKnob (inputGain);
    styleKnob (outputGain);
    styleKnob (formantShift);
    inputGain.setTextValueSuffix (" dB");
    outputGain.setTextValueSuffix (" dB");
    formantShift.setTextValueSuffix (" st");
    styleCaption (inputCaption, "INPUT");
    styleCaption (outputCaption, "OUTPUT");
    styleCaption (formantCaption, "FORMANT");
    proPanel.addAndMakeVisible (inputGain);
    proPanel.addAndMakeVisible (outputGain);
    proPanel.addAndMakeVisible (formantShift);
    proPanel.addAndMakeVisible (inputCaption);
    proPanel.addAndMakeVisible (outputCaption);
    proPanel.addAndMakeVisible (formantCaption);
    // loadTraceButton is a dev-only JSON trace import. Plumbing kept for the
    // contract test (loadTraceFromClipboard) but the button stays off-UI in
    // shipping builds - it's not a user feature.
    loadTraceButton.onClick = [this] { loadTraceFromClipboard(); };
    traceStatus.setJustificationType (juce::Justification::topLeft);
    keyDetectionStatus.setJustificationType (juce::Justification::topLeft);
    traceStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    keyDetectionStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (traceStatus);
    proPanel.addAndMakeVisible (keyDetectionStatus);

    // Track Key Map (Pro tab) — scan a song -> key timeline, applied at playback.
    proPanel.addAndMakeVisible (scanTrackButton);
    proPanel.addAndMakeVisible (clearMapButton);
    proPanel.addAndMakeVisible (useKeyMap);
    keyMapStatusLabel.setJustificationType (juce::Justification::topLeft);
    keyMapStatusLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    keyMapStatusLabel.setText ("No key map loaded.\nScan a track to build a per-section key timeline that the live\n"
                               "correction follows via the host playhead.",
                               juce::dontSendNotification);
    proPanel.addAndMakeVisible (keyMapStatusLabel);
    scanTrackButton.onClick = [this] { onScanTrackClicked(); };
    clearMapButton.onClick  = [this] { audioProcessor.clearKeyMap(); };
    useKeyMapAttach = std::make_unique<ButtonAttachment> (audioProcessor.parameters(), "useKeyMap", useKeyMap);

    // Attachments
    auto& params = audioProcessor.parameters();
    vocalRangeAttach     = std::make_unique<ComboAttachment> (params, "vocalRange", vocalRange);
    keyRootAttach        = std::make_unique<ComboAttachment> (params, "keyRoot", keyRoot);
    scaleAttach          = std::make_unique<ComboAttachment> (params, "scale", scale);

    autoKeyAttach        = std::make_unique<ButtonAttachment> (params, "autoKey", autoKey);
    lowLatencyAttach     = std::make_unique<ButtonAttachment> (params, "lowLatency", lowLatency);

    // Style is a Choice param (5 modes), not Bool. The Modern/Classic toggle maps
    // off=Modern Rap (idx 2), on=Classic (idx 1). Other style values still live
    // and can be selected by factory presets; the toggle just reflects whichever
    // of those two is currently active.
    style.onClick = [this] {
        const int idx = style.getToggleState() ? 1 : 2;
        if (auto* p = audioProcessor.parameters().getParameter ("style"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
            p->endChangeGesture();
        }
    };

    retuneSpeedAttach    = std::make_unique<SliderAttachment> (params, "speed", retuneSpeed);
    flexTuneAttach       = std::make_unique<SliderAttachment> (params, "amount", flexTune);
    humanizeAttach       = std::make_unique<SliderAttachment> (params, "humanize", humanize);
    inputGainAttach      = std::make_unique<SliderAttachment> (params, "inputGain", inputGain);
    outputGainAttach     = std::make_unique<SliderAttachment> (params, "outputGain", outputGain);
    formantShiftAttach   = std::make_unique<SliderAttachment> (params, "formantShift", formantShift);

    enableCorrectionAttach = std::make_unique<ButtonAttachment> (params, "enableCorrection", enableCorrection);
    doNoHarmAttach         = std::make_unique<ButtonAttachment> (params, "doNoHarm", doNoHarm);
    formantPreserveAttach  = std::make_unique<ButtonAttachment> (params, "formantPreserve", formantPreserve);

    showTab (0);
    startTimerHz (15);
}

SattariAutoPitchAudioProcessorEditor::~SattariAutoPitchAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariAutoPitchAudioProcessorEditor::styleKnob (juce::Slider& s, bool feature)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, feature ? 90 : 70, feature ? 24 : 18);
}

void SattariAutoPitchAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariAutoPitchAudioProcessorEditor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetManager.addFactoryPreset ("Natural", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.6f},{"speed",0.4f},{"doNoHarm",1},
        {"formantPreserve",1},{"formantShift",0},{"lowLatency",0} });
    presetManager.addFactoryPreset ("Hard Tune", PV {
        {"style",1},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},
        {"formantPreserve",1},{"formantShift",0},{"lowLatency",0} });
    presetManager.addFactoryPreset ("Modern Rap", PV {
        {"style",2},{"enableCorrection",1},{"amount",0.95f},{"speed",0.9f},{"doNoHarm",0},
        {"formantPreserve",1},{"formantShift",0},{"lowLatency",1} });
    presetManager.addFactoryPreset ("Robot", PV {
        {"style",3},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},
        {"formantPreserve",0},{"formantShift",0},{"lowLatency",1} });
    presetManager.addFactoryPreset ("Deeper Throat", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.7f},{"speed",0.5f},{"doNoHarm",1},
        {"formantPreserve",1},{"formantShift",-3},{"lowLatency",0} });
    presetManager.addFactoryPreset ("Bright Lift", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.7f},{"speed",0.5f},{"doNoHarm",1},
        {"formantPreserve",1},{"formantShift",4},{"lowLatency",0} });
}

void SattariAutoPitchAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : presetManager.userPresetNames())
        userPresets.addItem (n, id++);
}

void SattariAutoPitchAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariAutoPitchAudioProcessorEditor::loadTraceFromClipboard()
{
    audioProcessor.applyTraceContractJson (juce::SystemClipboard::getTextFromClipboard());
}

void SattariAutoPitchAudioProcessorEditor::onScanTrackClicked()
{
    trackFileChooser = std::make_unique<juce::FileChooser> (
        "Pick a track to scan for the Key Map",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    trackFileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.startKeyMapScan (file);
    });
}

void SattariAutoPitchAudioProcessorEditor::showTab (int idx)
{
    currentTab = idx;
    basicPanel.setVisible (idx == 0);
    proPanel.setVisible   (idx == 1);
}

void SattariAutoPitchAudioProcessorEditor::timerCallback()
{
    const auto snap = audioProcessor.getLiveSnapshot();
    noteDisplay.setSnapshot (snap);

    const int keyRootIdx = juce::jlimit (0, 11,
        juce::roundToInt (audioProcessor.parameters().getRawParameterValue ("keyRoot")->load()));
    const int scaleIdx = juce::jlimit (0, 2,
        juce::roundToInt (audioProcessor.parameters().getRawParameterValue ("scale")->load()));
    pillRow.setScale (keyRootIdx, scaleIdx);
    pillRow.setDetectedMidi (snap.detectedMidi);

    detuneLabel.setText (snap.voiced
        ? "Detune " + juce::String (snap.correctionCents, 1) + " ct"
        : "Detune -", juce::dontSendNotification);

    // Show only the live engine status; the JSON-trace summary is dev-only.
    traceStatus.setText (audioProcessor.getLiveStatusSummary(), juce::dontSendNotification);
    keyDetectionStatus.setText (audioProcessor.getKeyDetectionTabSummary(), juce::dontSendNotification);

    // Track Key Map status panel.
    const auto km = audioProcessor.getKeyMapStatus();
    juce::String s;
    if (km.scanProgress >= 0 && km.scanProgress < 100)
        s << "Scanning: " << km.scanProgress << "%\n";
    if (km.hasMap)
    {
        s << "Loaded: " << km.loadedFileName << "\n";
        s << km.numSegments << " key segments\n";
        if (km.currentSegment >= 0)
            s << "Currently at segment " << (km.currentSegment + 1);
    }
    else if (km.lastError.isNotEmpty())
        s << "Error: " << km.lastError;
    else if (km.scanProgress < 0)
        s << "No key map loaded.\nScan a track to build a per-section key timeline that\n"
             "the live correction follows via the host playhead.";
    keyMapStatusLabel.setText (s, juce::dontSendNotification);
}

void SattariAutoPitchAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariAutoPitchAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    // Header row
    auto header = area.removeFromTop (50);
    title.setBounds (header.removeFromLeft (170));
    header.removeFromLeft (10);

    auto vocalBlock = header.removeFromLeft (180);
    vocalRangeCaption.setBounds (vocalBlock.removeFromTop (14));
    vocalRange.setBounds (vocalBlock);
    header.removeFromLeft (10);

    auto keyBlock = header.removeFromLeft (240);
    keyScaleCaption.setBounds (keyBlock.removeFromTop (14));
    auto keyRow = keyBlock;
    keyRoot.setBounds (keyRow.removeFromLeft (66));
    keyRow.removeFromLeft (6);
    scale.setBounds (keyRow);

    // Preset (right of header)
    auto presetCol = header.removeFromRight (300);
    factoryPresets.setBounds (presetCol.removeFromTop (24));
    presetCol.removeFromTop (2);
    auto userRow = presetCol;
    savePresetButton.setBounds (userRow.removeFromRight (60));
    userRow.removeFromRight (4);
    userPresets.setBounds (userRow);

    area.removeFromTop (10);

    // Tabs row
    auto tabRow = area.removeFromTop (28);
    tabBasic.setBounds (tabRow.removeFromLeft (90));
    tabRow.removeFromLeft (6);
    tabPro.setBounds (tabRow.removeFromLeft (90));

    area.removeFromTop (10);

    // Panels share the body area
    basicPanel.setBounds (area);
    proPanel.setBounds (area);

    // ---- Basic panel ----
    auto bArea = juce::Rectangle<int> (0, 0, area.getWidth(), area.getHeight()).reduced (4);
    auto left  = bArea.removeFromLeft ((int) (bArea.getWidth() * 0.6f));
    bArea.removeFromLeft (12);
    auto right = bArea;

    auto leftTop = left.removeFromTop (32);
    autoKey.setBounds (leftTop.removeFromLeft (160));

    noteDisplay.setBounds (left.removeFromTop ((int) (left.getHeight() * 0.55f)));
    left.removeFromTop (8);

    editNotesLabel.setBounds (left.removeFromTop (16));
    pillRow.setBounds (left.removeFromTop (80));

    left.removeFromTop (10);
    auto bottomRow = left.removeFromTop (32);
    detuneLabel.setBounds (bottomRow.removeFromLeft (200));
    lowLatency.setBounds (bottomRow.removeFromRight (140));

    // ---- Right side (big knob + 2 small + style) ----
    retuneSpeedCaption.setBounds (right.removeFromTop (16));
    auto bigKnob = right.removeFromTop (200);
    retuneSpeed.setBounds (bigKnob.withSizeKeepingCentre (juce::jmin (bigKnob.getWidth(), bigKnob.getHeight()),
                                                          juce::jmin (bigKnob.getWidth(), bigKnob.getHeight())));

    right.removeFromTop (8);
    auto twoKnobRow = right.removeFromTop (130);
    const int kw = twoKnobRow.getWidth() / 2;
    auto flexCell = twoKnobRow.removeFromLeft (kw);
    flexTuneCaption.setBounds (flexCell.removeFromTop (14));
    flexTune.setBounds (flexCell);
    auto humanizeCell = twoKnobRow;
    humanizeCaption.setBounds (humanizeCell.removeFromTop (14));
    humanize.setBounds (humanizeCell);

    right.removeFromTop (12);
    auto styleRow = right.removeFromTop (30);
    modernLabel.setBounds (styleRow.removeFromLeft (70));
    style.setBounds (styleRow.removeFromLeft (50));
    classicLabel.setBounds (styleRow.removeFromLeft (70));

    // ---- Pro panel ----
    auto pArea = juce::Rectangle<int> (0, 0, area.getWidth(), area.getHeight()).reduced (8);
    auto pLeft = pArea.removeFromLeft ((int) (pArea.getWidth() * 0.45f));
    pArea.removeFromLeft (12);
    auto pRight = pArea;

    enableCorrection.setBounds (pLeft.removeFromTop (28));
    pLeft.removeFromTop (4);
    doNoHarm.setBounds (pLeft.removeFromTop (28));
    pLeft.removeFromTop (4);
    formantPreserve.setBounds (pLeft.removeFromTop (28));
    pLeft.removeFromTop (16);

    auto knobsRow = pLeft.removeFromTop (140);
    const int pKw = knobsRow.getWidth() / 3;
    auto iCell = knobsRow.removeFromLeft (pKw);
    inputCaption.setBounds (iCell.removeFromTop (14));
    inputGain.setBounds (iCell);
    auto oCell = knobsRow.removeFromLeft (pKw);
    outputCaption.setBounds (oCell.removeFromTop (14));
    outputGain.setBounds (oCell);
    auto fCell = knobsRow;
    formantCaption.setBounds (fCell.removeFromTop (14));
    formantShift.setBounds (fCell);

    // Live status + key-detection (top half of right column).
    // loadTraceButton is intentionally not laid out - it's dev-only.
    auto statusArea = pRight.removeFromTop ((int) (pRight.getHeight() * 0.45f));
    traceStatus.setBounds (statusArea.removeFromTop ((int) (statusArea.getHeight() * 0.55f)));
    keyDetectionStatus.setBounds (statusArea);

    // Track Key Map (bottom half of right column)
    pRight.removeFromTop (10);
    auto keymapHeader = pRight.removeFromTop (28);
    scanTrackButton.setBounds (keymapHeader.removeFromLeft (130));
    keymapHeader.removeFromLeft (6);
    clearMapButton.setBounds  (keymapHeader.removeFromLeft (100));
    keymapHeader.removeFromLeft (10);
    useKeyMap.setBounds (keymapHeader);
    pRight.removeFromTop (6);
    keyMapStatusLabel.setBounds (pRight);
}
