#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

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
        const bool lit        = inScale (pc);
        const bool enabled    = (enabledMask >> pc) & 1;
        const bool isDetected = (detectedMidi >= 0)
                                && (((detectedMidi % 12) + 12) % 12) == pc;
        const auto pillBg = (lit && enabled) ? accent.withAlpha (isDetected ? 0.95f : 0.50f)
                                              : juce::Colour (sattari::brand::kPanelRaised);
        const auto stroke = (lit && enabled) ? accent.withAlpha (0.85f)
                                              : juce::Colour (sattari::brand::kHairline);
        const auto textCol = (! enabled) ? juce::Colour (sattari::brand::kTextFaint)
                                          : ((lit) ? juce::Colour (sattari::brand::kBackground)
                                                   : juce::Colour (sattari::brand::kTextMuted));
        g.setColour (pillBg);
        g.fillRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f);
        g.setColour (stroke);
        g.drawRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f, 1.0f);
        g.setColour (textCol);
        g.setFont (juce::FontOptions (r.getHeight() * 0.42f, juce::Font::plain));
        g.drawText (noteNames[pc], r.toNearestInt(), juce::Justification::centred, false);
        // Strike-through for blocked notes.
        if (! enabled)
        {
            g.setColour (juce::Colour (sattari::brand::kDanger).withAlpha (0.7f));
            const auto centre = r.getCentre();
            g.drawLine (r.getX() + 6.0f, centre.y, r.getRight() - 6.0f, centre.y, 1.4f);
        }
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
        sharpRects[i] = cell;       // cache hit-test rect
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
        naturalRects[i] = cell;     // cache hit-test rect
        drawPill (cell, naturals[i]);
        cursor.removeFromLeft (gapBot);
    }
}

int KeyPillRow::hitTestPill (juce::Point<float> p) const
{
    static const int sharps[5] = { 1, 3, 6, 8, 10 };
    for (int i = 0; i < 5; ++i)
        if (sharpRects[i].contains (p)) return sharps[i];
    static const int naturals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        if (naturalRects[i].contains (p)) return naturals[i];
    return -1;
}

void KeyPillRow::mouseDown (const juce::MouseEvent& e)
{
    const int pc = hitTestPill (e.position);
    if (pc < 0) return;
    if (e.mods.isShiftDown())
    {
        if (onPillShiftClicked) onPillShiftClicked (pc);
    }
    else
    {
        if (onPillClicked) onPillClicked (pc);
    }
}

// ============================================================================
// KeyButton — one tactile control for the whole key story
// ============================================================================
void KeyButton::paint (juce::Graphics& g)
{
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));
    const bool linked = (mode != Mode::Manual);
    auto r = getLocalBounds().toFloat().reduced (1.5f);

    // Tactile body: hover brightens, press darkens; accent wash + border when a
    // link (AutoKey / Follow) is driving the key.
    auto body = juce::Colour (sattari::brand::kPanelRaised);
    if (pressed)     body = body.darker (0.30f);
    else if (hover)  body = body.brighter (0.14f);
    g.setColour (linked ? accent.withAlpha (pressed ? 0.30f : 0.16f) : body);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (linked ? accent.withAlpha (0.9f) : juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r, 10.0f, linked ? 1.8f : 1.0f);

    auto inner = r.reduced (14.0f, 10.0f);

    // Top row: mode chip (left) + confidence lock meter (right, only while auto).
    auto topRow = inner.removeFromTop (14.0f);
    const char* chip = mode == Mode::Auto ? "AUTO-DETECT"
                     : mode == Mode::Follow ? "FOLLOWING SONG KEY"
                     : mode == Mode::Map ? "KEY MAP" : "MANUAL KEY";
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (linked ? accent : juce::Colour (sattari::brand::kTextMuted));
    const juce::String chipText = (linked ? juce::String::fromUTF8 ("\xe2\x9b\x93  ") : juce::String())
                                + chip;   // U+26D3 chains glyph when linked
    g.drawText (chipText, topRow, juce::Justification::centredLeft, false);

    // Confidence lock meter: 5 dots that fill as detection settles. Shown for the
    // live detectors (Auto / Follow / Map); hidden for a hand-set key (no detection).
    if (linked)
    {
        auto meter = topRow.removeFromRight (78.0f);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.setColour (juce::Colour (sattari::brand::kTextFaint));
        g.drawText ("LOCK", meter.removeFromLeft (26.0f), juce::Justification::centredRight, false);
        meter.removeFromLeft (4.0f);
        const int lit = juce::roundToInt (confidence * 5.0f);
        const float d = 6.0f, gap = 3.0f;
        float x = meter.getX();
        const float cy = meter.getCentreY();
        for (int i = 0; i < 5; ++i)
        {
            juce::Rectangle<float> dot (x, cy - d * 0.5f, d, d);
            g.setColour (i < lit ? accent : accent.withAlpha (0.22f));
            g.fillEllipse (dot);
            x += d + gap;
        }
    }

    // Big key name (centre) — the headline the whole plugin is tuning to.
    static const char* names[]   = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    static const char* scaleNm[] = { "major", "minor", "chromatic" };
    const juce::String keyText = juce::String (names[juce::jlimit (0, 11, rootIdx)])
                               + "  " + scaleNm[juce::jlimit (0, 2, scaleIndex)];
    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (juce::FontOptions (juce::jmin (32.0f, inner.getHeight() * 0.9f), juce::Font::bold));
    g.drawText (keyText, inner, juce::Justification::centredLeft, false);
}

void KeyButton::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    repaint();

    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    juce::PopupMenu menu;
    menu.addItem (1, "Link AutoKey  (auto-detect from this track)", true, mode == Mode::Auto);
    menu.addItem (2, "Follow Song Key  (from Sattari Key on master)", true, mode == Mode::Follow);
    menu.addSeparator();

    juce::PopupMenu manual;
    for (int root = 0; root < 12; ++root)
    {
        juce::PopupMenu sub;
        sub.addItem (100 + root * 2 + 0, "Major", true, mode == Mode::Manual && rootIdx == root && scaleIndex == 0);
        sub.addItem (100 + root * 2 + 1, "Minor", true, mode == Mode::Manual && rootIdx == root && scaleIndex == 1);
        manual.addSubMenu (names[root], sub);
    }
    menu.addSubMenu ("Set key manually", manual);
    menu.addItem (3, "Chromatic  (correct to nearest semitone)", true, mode == Mode::Manual && scaleIndex == 2);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int id)
        {
            pressed = false;
            repaint();
            if (id == 1)       { if (onLinkAuto)   onLinkAuto(); }
            else if (id == 2)  { if (onLinkFollow) onLinkFollow(); }
            else if (id == 3)  { if (onManualKey)  onManualKey (rootIdx, 2); }   // chromatic
            else if (id >= 100){ const int root = (id - 100) / 2, sc = (id - 100) % 2;
                                 if (onManualKey) onManualKey (root, sc); }
        });
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
    // keyRoot / scale are still populated + attached (they hold the manual key),
    // but the header dropdowns are gone — the single KeyButton is now the only
    // key control, so we don't add keyRoot/scale/keyScaleCaption to the view.
    keyRoot.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    scale.addItemList ({ "Major", "Minor", "Chromatic" }, 1);
    for (auto* c : { &vocalRange, &factoryPresets, &userPresets })
        addAndMakeVisible (*c);

    styleCaption (vocalRangeCaption, "SOUND SOURCE");
    addAndMakeVisible (vocalRangeCaption);

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
    addAndMakeVisible (abRow);
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
    // One unified tactile key control (replaces the AutoKey / Follow toggles +
    // status label). Its menu links AutoKey / Follow or sets the key by hand;
    // Auto and Follow are mutually exclusive, and a manual pick drops both.
    basicPanel.addAndMakeVisible (keyButton);
    keyButton.onLinkAuto = [this]
    {
        const bool on = audioProcessor.parameters().getRawParameterValue ("autoKey")->load() > 0.5f;
        setKeyParam ("followSongKey", 0.0f);
        setKeyParam ("autoKey", on ? 0.0f : 1.0f);
    };
    keyButton.onLinkFollow = [this]
    {
        const bool on = audioProcessor.parameters().getRawParameterValue ("followSongKey")->load() > 0.5f;
        setKeyParam ("autoKey", 0.0f);
        setKeyParam ("followSongKey", on ? 0.0f : 1.0f);
    };
    keyButton.onManualKey = [this] (int root, int scaleIdx)
    {
        setKeyParam ("autoKey", 0.0f);
        setKeyParam ("followSongKey", 0.0f);
        setKeyParam ("keyRoot", (float) root);
        setKeyParam ("scale", (float) scaleIdx);
    };

    // Three explicit action buttons under the readout — the discoverable controls.
    // AUTO-DETECT / FOLLOW toggle the live sources; SET KEY opens a direct key picker.
    // (The KeyButton readout itself still opens the same combined menu as a shortcut.)
    for (auto* b : { &autoDetectBtn, &followSongBtn, &setKeyBtn })
        basicPanel.addAndMakeVisible (*b);
    autoDetectBtn.onClick = [this] { if (keyButton.onLinkAuto)   keyButton.onLinkAuto(); };
    followSongBtn.onClick = [this] { if (keyButton.onLinkFollow) keyButton.onLinkFollow(); };
    setKeyBtn.onClick = [this]
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        auto& a = audioProcessor.parameters();
        const int curRoot  = juce::jlimit (0, 11, (int) std::round (a.getRawParameterValue ("keyRoot")->load()));
        const int curScale = juce::jlimit (0,  2, (int) std::round (a.getRawParameterValue ("scale")->load()));
        const bool manual  = a.getRawParameterValue ("autoKey")->load() < 0.5f
                          && a.getRawParameterValue ("followSongKey")->load() < 0.5f;
        juce::PopupMenu menu;
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu sub;
            sub.addItem (100 + root * 2 + 0, "Major", true, manual && curRoot == root && curScale == 0);
            sub.addItem (100 + root * 2 + 1, "Minor", true, manual && curRoot == root && curScale == 1);
            menu.addSubMenu (names[root], sub);
        }
        menu.addSeparator();
        menu.addItem (3, "Chromatic  (nearest semitone)", true, manual && curScale == 2);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setKeyBtn),
            [this, curRoot] (int id)
            {
                if (id == 3)         { if (keyButton.onManualKey) keyButton.onManualKey (curRoot, 2); }
                else if (id >= 100)  { const int root = (id - 100) / 2, sc = (id - 100) % 2;
                                       if (keyButton.onManualKey) keyButton.onManualKey (root, sc); }
            });
    };

    basicPanel.addAndMakeVisible (noteDisplay);
    basicPanel.addAndMakeVisible (pillRow);
    // Plain click toggles whether the note is a valid correction target.
    pillRow.onPillClicked = [this] (int pc) {
        if (auto* param = audioProcessor.parameters().getParameter ("enabledNotes"))
        {
            const auto& range = param->getNormalisableRange();
            const int current = (int) std::round (range.convertFrom0to1 (param->getValue()));
            const int flipped = current ^ (1 << pc);
            param->beginChangeGesture();
            param->setValueNotifyingHost (range.convertTo0to1 ((float) flipped));
            param->endChangeGesture();
        }
    };
    // Shift-click sets the key root (the previous behaviour).
    pillRow.onPillShiftClicked = [this] (int pc) {
        if (auto* param = audioProcessor.parameters().getParameter ("keyRoot"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) pc));
            param->endChangeGesture();
        }
    };
    styleCaption (editNotesLabel, "EDIT NOTES");
    basicPanel.addAndMakeVisible (editNotesLabel);

    // Engine selector (replaces the old Live-Engine + Low-Latency toggles). One 3-way
    // choice driving the liveEngine + lowLatency params, so presets/automation still work.
    styleCaption (engineCaption, "ENGINE");
    engineSelect.addItemList ({ "Studio  (best quality)", "Live",
                                juce::String::fromUTF8 ("Live \xc2\xb7 Low-Latency") }, 1);
    engineSelect.onChange = [this]
    {
        switch (engineSelect.getSelectedItemIndex())
        {
            case 0: setKeyParam ("liveEngine", 0.0f); setKeyParam ("lowLatency", 0.0f); break;  // Studio (RB)
            case 1: setKeyParam ("liveEngine", 1.0f); setKeyParam ("lowLatency", 0.0f); break;  // Live (PSOLA)
            case 2: setKeyParam ("liveEngine", 1.0f); setKeyParam ("lowLatency", 1.0f); break;  // Live low-latency
            default: break;
        }
    };
    basicPanel.addAndMakeVisible (engineCaption);
    basicPanel.addAndMakeVisible (engineSelect);

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
    // This Basic knob is attached to the `amount` param (correction strength), NOT the
    // Flex-Tune param — label it accordingly so it doesn't collide with the Pro Flex-Tune.
    styleCaption (flexTuneCaption, "AMOUNT");
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
    // Polyphonic tuning: shipping feature (benchmark: 96-98% per-note on separated
    // chords). Retunes each note of a chord/pad independently to the scale.
    proPanel.addAndMakeVisible (polyphonic);
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

    // Antares-class behaviour controls.
    proPanel.addAndMakeVisible (classicMode);
    proPanel.addAndMakeVisible (neuralPitch);

    // Transpose to Key: whole-mix musical transpose for beats/instruments. The semitone
    // slider is the shift; "Match Key" reads AutoKey's detected key and sets the shift
    // to land on the SET KEY target; enabling the mode auto-runs a Match once.
    proPanel.addAndMakeVisible (transposeMode);
    styleCaption (transposeCaption, "TRANSPOSE TO KEY  (beats / instruments)");
    proPanel.addAndMakeVisible (transposeCaption);
    transposeSemis.setSliderStyle (juce::Slider::LinearHorizontal);
    transposeSemis.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    transposeSemis.setRange (-12.0, 12.0, 1.0);
    transposeSemis.textFromValueFunction = [] (double v) {
        const int s = juce::roundToInt (v);
        return (s > 0 ? "+" : "") + juce::String (s) + " st"; };
    proPanel.addAndMakeVisible (transposeSemis);
    matchKeyButton.onClick = [this]
    {
        // source = AutoKey's detected root; target = the SET KEY root. Shift by the
        // nearest signed semitone delta so the whole mix lands on the target key.
        const auto snap = audioProcessor.getLiveSnapshot();
        auto& a = audioProcessor.parameters();
        const int src = juce::jlimit (0, 11, snap.detectedRoot);
        const int tgt = juce::jlimit (0, 11, (int) std::round (a.getRawParameterValue ("keyRoot")->load()));
        int d = ((tgt - src) % 12 + 12) % 12;   // 0..11
        if (d > 6) d -= 12;                      // nearest signed: -5..+6
        setKeyParam ("xposeSemis", (float) d);
    };
    proPanel.addAndMakeVisible (matchKeyButton);
    // Enabling the mode auto-runs Match once so it "just works" on drop-in.
    transposeMode.onClick = [this] { if (transposeMode.getToggleState()) matchKeyButton.onClick(); };
    transposeReadout.setJustificationType (juce::Justification::centredLeft);
    transposeReadout.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (transposeReadout);
    styleKnob (flexAmt);
    styleKnob (transition);
    styleKnob (throat);
    styleKnob (vibratoKnob);
    vibratoKnob.textFromValueFunction = [] (double v) {
        return v < 0.005 ? juce::String ("Off") : juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    styleCaption (vibratoCaption, "VIBRATO");
    proPanel.addAndMakeVisible (vibratoKnob);
    proPanel.addAndMakeVisible (vibratoCaption);
    flexAmt.textFromValueFunction    = [] (double v) { return v < 0.005 ? juce::String ("Rigid")
                                                                        : juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    transition.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    throat.textFromValueFunction     = [] (double v) { return juce::String (v, 2) + "x"; };
    styleCaption (flexAmtCaption, "FLEX-TUNE");
    styleCaption (transitionCaption, "TRANSITION");
    styleCaption (throatCaption, "THROAT");
    proPanel.addAndMakeVisible (flexAmt);
    proPanel.addAndMakeVisible (transition);
    proPanel.addAndMakeVisible (throat);
    proPanel.addAndMakeVisible (flexAmtCaption);
    proPanel.addAndMakeVisible (transitionCaption);
    proPanel.addAndMakeVisible (throatCaption);
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

    proPanel.addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };

    // Attachments
    auto& params = audioProcessor.parameters();
    vocalRangeAttach     = std::make_unique<ComboAttachment> (params, "vocalRange", vocalRange);
    keyRootAttach        = std::make_unique<ComboAttachment> (params, "keyRoot", keyRoot);
    scaleAttach          = std::make_unique<ComboAttachment> (params, "scale", scale);

    autoKeyAttach        = std::make_unique<ButtonAttachment> (params, "autoKey", autoKey);
    followKeyAttach      = std::make_unique<ButtonAttachment> (params, "followSongKey", followKey);

    // Style is a Choice param (5 modes), not Bool. The Modern/Classic toggle maps
    // off=Modern Rap (idx 2), on=Classic (idx 1). Other style values still live
    // and can be selected by factory presets; the toggle just reflects whichever
    // of those two is currently active.
    style.onClick = [this] {
        const int idx = style.getToggleState() ? 1 : 2;
        if (auto* param = audioProcessor.parameters().getParameter ("style"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) idx));
            param->endChangeGesture();
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
    polyphonicAttach       = std::make_unique<ButtonAttachment> (params, "polyphonic", polyphonic);
    classicModeAttach      = std::make_unique<ButtonAttachment> (params, "classicMode", classicMode);
    flexAmtAttach          = std::make_unique<SliderAttachment> (params, "flexTune", flexAmt);
    transitionAttach       = std::make_unique<SliderAttachment> (params, "transition", transition);
    throatAttach           = std::make_unique<SliderAttachment> (params, "throat", throat);
    vibratoAttach          = std::make_unique<SliderAttachment> (params, "vibrato", vibratoKnob);
    neuralPitchAttach      = std::make_unique<ButtonAttachment> (params, "neuralPitch", neuralPitch);
    transposeModeAttach    = std::make_unique<ButtonAttachment> (params, "xposeMode", transposeMode);
    transposeSemisAttach   = std::make_unique<SliderAttachment> (params, "xposeSemis", transposeSemis);

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

void SattariAutoPitchAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* p = audioProcessor.parameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
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

    // Publish the current correction key onto the cross-plugin bus so other
    // Sattari instances (e.g. Royal Chain's Tune) can follow. Throttled to
    // ~1 Hz inside publishCurrentKeyToBus() so the JSON file never thrashes.
    audioProcessor.publishCurrentKeyToBus();

    // Drive the single KeyButton: it shows the live key + how it's driven
    // (manual / AutoKey / Follow). This is now the only key control.
    const auto origin = audioProcessor.currentKeyOrigin();
    using KBMode = KeyButton::Mode;
    const KBMode kbMode = origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus ? KBMode::Follow
                        : origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::KeyMap    ? KBMode::Map
                        : origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::AutoKey  ? KBMode::Auto
                        : KBMode::Manual;
    keyButton.setState (kbMode, origin.root, origin.scale);

    // Smoothed "lock" meter: track pitch confidence while voiced, hold between notes
    // so the meter reflects how settled detection is rather than flickering per-note.
    const float lockTarget = snap.voiced ? snap.confidence : keyLock;
    keyLock += 0.12f * (lockTarget - keyLock);
    keyButton.setConfidence (keyLock);

    // Highlight the button matching the active source so the current mode is obvious.
    const auto acc = juce::Colour (sattari::brand::productAccent ("auto-pitch"));
    const auto idle = juce::Colour (sattari::brand::kPanelRaised);
    autoDetectBtn.setColour (juce::TextButton::buttonColourId, kbMode == KBMode::Auto   ? acc.withAlpha (0.85f) : idle);
    followSongBtn.setColour (juce::TextButton::buttonColourId, kbMode == KBMode::Follow ? acc.withAlpha (0.85f) : idle);
    setKeyBtn.setColour     (juce::TextButton::buttonColourId, kbMode == KBMode::Manual ? acc.withAlpha (0.85f) : idle);

    // Pills light for the key actually in use (the followed/detected key when
    // overriding, the manual key otherwise) — not the stale dropdown value.
    pillRow.setScale (origin.root, origin.scale);
    pillRow.setDetectedMidi (snap.detectedMidi);
    pillRow.setEnabledMask (juce::jlimit (0, 4095,
        juce::roundToInt (audioProcessor.parameters().getRawParameterValue ("enabledNotes")->load())));

    detuneLabel.setText (snap.voiced
        ? "Detune " + juce::String (snap.correctionCents, 1) + " ct"
        : "Detune -", juce::dontSendNotification);

    // Show only the live engine status; the JSON-trace summary is dev-only.
    traceStatus.setText (audioProcessor.getLiveStatusSummary(), juce::dontSendNotification);
    keyDetectionStatus.setText (audioProcessor.getKeyDetectionTabSummary(), juce::dontSendNotification);

    // Reflect the engine (params -> selector) so presets/automation keep it in sync.
    {
        auto& a = audioProcessor.parameters();
        const bool lowLat = a.getRawParameterValue ("lowLatency")->load() > 0.5f;
        const bool live   = a.getRawParameterValue ("liveEngine")->load() > 0.5f;
        const int idx = lowLat ? 2 : (live ? 1 : 0);
        if (engineSelect.getSelectedItemIndex() != idx)
            engineSelect.setSelectedItemIndex (idx, juce::dontSendNotification);
    }

    // Transpose-to-Key readout: detected source key -> shift -> resulting key.
    {
        static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const int src   = juce::jlimit (0, 11, snap.detectedRoot);
        const int semis = juce::jlimit (-12, 12,
            (int) std::round (audioProcessor.parameters().getRawParameterValue ("xposeSemis")->load()));
        const int res   = ((src + semis) % 12 + 12) % 12;
        const bool on   = audioProcessor.parameters().getRawParameterValue ("xposeMode")->load() > 0.5f;
        transposeReadout.setText (on
            ? juce::String ("detected ") + nm[src] + juce::String::fromUTF8 ("  \xe2\x86\x92  ")
                + (semis > 0 ? "+" : "") + juce::String (semis) + juce::String::fromUTF8 (" st  \xe2\x86\x92  ") + nm[res]
            : juce::String::fromUTF8 ("off \xe2\x80\x94 per-note correction active"),
            juce::dontSendNotification);
    }

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

    // Honest Neural Pitch label + state: the toggle silently falls back to YIN when no
    // crepe.onnx model is installed — say so AND disable it, so it never looks functional.
    const bool neuralOk = audioProcessor.neuralModelPresent();
    const juce::String wantedNeuralText = neuralOk ? "Neural Pitch" : "Neural Pitch (no model installed)";
    if (neuralPitch.getButtonText() != wantedNeuralText)
        neuralPitch.setButtonText (wantedNeuralText);
    setControlActive (neuralPitch, neuralOk);

    // ── Mode-aware enabled states ────────────────────────────────────────────────
    // Don't let a control look interactive when the current mode makes it do nothing.
    // Per-note correction is bypassed both when Correction is Off and when Transpose to
    // Key is On (a whole-mix shift replaces correction), so dim every correction control.
    auto& ap = audioProcessor.parameters();
    const bool corrOn  = ap.getRawParameterValue ("enableCorrection")->load() > 0.5f;
    const bool xposeOn = ap.getRawParameterValue ("xposeMode")->load() > 0.5f;
    const bool correctionActive = corrOn && ! xposeOn;
    juce::Component* correctionControls[] = {
        &retuneSpeed, &flexTune, &humanize, &style, &modernLabel, &classicLabel,
        &retuneSpeedCaption, &flexTuneCaption, &humanizeCaption, &pillRow, &editNotesLabel,
        &flexAmt, &transition, &throat, &vibratoKnob, &classicMode, &doNoHarm,
        &formantPreserve, &polyphonic, &formantShift, &flexAmtCaption, &transitionCaption,
        &throatCaption, &vibratoCaption, &formantCaption
    };
    for (auto* c : correctionControls)
        setControlActive (*c, correctionActive);

    // The transpose amount + Match Key are inert until Transpose to Key is engaged.
    setControlActive (transposeSemis, xposeOn);
    setControlActive (matchKeyButton, xposeOn);
}

void SattariAutoPitchAudioProcessorEditor::setControlActive (juce::Component& c, bool on)
{
    if (c.isEnabled() != on)       c.setEnabled (on);
    const float a = on ? 1.0f : 0.4f;
    if (c.getAlpha() != a)         c.setAlpha (a);
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
    // (Key & Scale dropdowns removed from the header — the KeyButton on the Basic
    //  tab is now the single key control.)

    // Preset (right of header)
    auto presetCol = header.removeFromRight (300);
    auto factoryRow = presetCol.removeFromTop (24);
    abRow.setBounds (factoryRow.removeFromRight (84));
    factoryRow.removeFromRight (6);
    factoryPresets.setBounds (factoryRow);
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

    // Key readout spans the top of the left column, with the 3 action buttons under it.
    keyButton.setBounds (left.removeFromTop (58));
    left.removeFromTop (6);
    auto keyBtnRow = left.removeFromTop (30);
    const int keyBw = (keyBtnRow.getWidth() - 12) / 3;
    autoDetectBtn.setBounds (keyBtnRow.removeFromLeft (keyBw));
    keyBtnRow.removeFromLeft (6);
    followSongBtn.setBounds (keyBtnRow.removeFromLeft (keyBw));
    keyBtnRow.removeFromLeft (6);
    setKeyBtn.setBounds (keyBtnRow);
    left.removeFromTop (10);

    noteDisplay.setBounds (left.removeFromTop ((int) (left.getHeight() * 0.55f)));
    left.removeFromTop (8);

    editNotesLabel.setBounds (left.removeFromTop (16));
    pillRow.setBounds (left.removeFromTop (80));

    left.removeFromTop (10);
    auto bottomRow = left.removeFromTop (32);
    detuneLabel.setBounds (bottomRow.removeFromLeft (150));
    // Engine selector on the right: caption + 3-way combo.
    auto engineArea = bottomRow.removeFromRight (juce::jmin (244, bottomRow.getWidth()));
    engineCaption.setBounds (engineArea.removeFromLeft (56));
    engineSelect.setBounds (engineArea.reduced (0, 2));

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
    pLeft.removeFromTop (4);
    polyphonic.setBounds (pLeft.removeFromTop (28));
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

    // Detector toggle: Neural Pitch (CREPE). The engine choice lives on the Basic tab
    // as the single Engine selector (Studio / Live / Live Low-Latency).
    pLeft.removeFromTop (10);
    neuralPitch.setBounds (pLeft.removeFromTop (28));

    // Antares-class behaviour row: Classic Mode toggle + Flex / Transition /
    // Throat / Vibrato.
    pLeft.removeFromTop (10);
    classicMode.setBounds (pLeft.removeFromTop (28));
    pLeft.removeFromTop (6);
    auto behaviourRow = pLeft.removeFromTop (140);
    const int bKw = behaviourRow.getWidth() / 4;
    auto flexAmtCell = behaviourRow.removeFromLeft (bKw);
    flexAmtCaption.setBounds (flexAmtCell.removeFromTop (14));
    flexAmt.setBounds (flexAmtCell);
    auto transCell = behaviourRow.removeFromLeft (bKw);
    transitionCaption.setBounds (transCell.removeFromTop (14));
    transition.setBounds (transCell);
    auto throatCell = behaviourRow.removeFromLeft (bKw);
    throatCaption.setBounds (throatCell.removeFromTop (14));
    throat.setBounds (throatCell);
    auto vibratoCell = behaviourRow;
    vibratoCaption.setBounds (vibratoCell.removeFromTop (14));
    vibratoKnob.setBounds (vibratoCell);

    // Transpose to Key (top of right column): whole-mix transpose for beats/instruments.
    transposeCaption.setBounds (pRight.removeFromTop (16));
    auto xRow = pRight.removeFromTop (28);
    transposeMode.setBounds (xRow.removeFromLeft ((int) (xRow.getWidth() * 0.62f)));
    xRow.removeFromLeft (8);
    matchKeyButton.setBounds (xRow);
    pRight.removeFromTop (4);
    transposeSemis.setBounds (pRight.removeFromTop (28));
    transposeReadout.setBounds (pRight.removeFromTop (18));
    pRight.removeFromTop (12);

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
    useKeyMap.setBounds (keymapHeader.removeFromLeft (140));
    keymapHeader.removeFromLeft (10);
    midiLearnButton.setBounds (keymapHeader);
    pRight.removeFromTop (6);
    keyMapStatusLabel.setBounds (pRight);
}
