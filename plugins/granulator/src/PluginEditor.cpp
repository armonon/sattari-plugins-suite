#include "PluginEditor.h"

// =============================================================================
//  Construction
// =============================================================================
SattariGranulatorAudioProcessorEditor::SattariGranulatorAudioProcessorEditor (SattariGranulatorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "granulator")
{
    setLookAndFeel (&lookAndFeel);
    setSize (980, 680);
    setWantsKeyboardFocus (true);

    selectedLayer = audioProcessor.selectedLayer();

    // ----- Header --------------------------------------------------------------
    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Granulator");
    addAndMakeVisible (title);

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

    // Preset row: user presets + Save + A/B (preset compare, not the layers).
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

    styleKnob (outputKnob, true);
    outputKnob.setTextValueSuffix (" dB");
    styleCaption (outputCaption, "OUTPUT");
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (outputCaption);
    outputAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), "volume", outputKnob);

    // ----- Layer / morph / capture bar -----------------------------------------
    styleCaption (layerCaption, "LAYER");
    addAndMakeVisible (layerCaption);
    for (auto* b : { &layerAButton, &layerBButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x6C41);   // "lA"
        b->setColour (juce::TextButton::buttonOnColourId,
                      juce::Colour (sattari::brand::productAccent ("granulator")));
        addAndMakeVisible (*b);
    }
    layerAButton.onClick = [this] { selectLayer (Layer::A); };
    layerBButton.onClick = [this] { selectLayer (Layer::B); };

    styleKnob (morphKnob, true);
    styleCaption (morphCaption, "MORPH");
    addAndMakeVisible (morphKnob);
    addAndMakeVisible (morphCaption);
    morphAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), "layerMorph", morphKnob);

    // Per-layer LEVEL / PAN — attachments are (re)built in rebindLayer().
    addLayerKnob (*this, layerLevel, layerLevelCap, "LEVEL", "level");
    layerLevel.setTextValueSuffix (" dB");
    addLayerKnob (*this, layerPan, layerPanCap, "PAN", "pan");

    // Capture cluster.
    styleCaption (captureCaption, "CAPTURE");
    addAndMakeVisible (captureCaption);
    styleToggle (captureArmButton, "Arm");
    addAndMakeVisible (captureArmButton);
    captureArmAttach = std::make_unique<ButtonAttachment> (audioProcessor.parameters(), "captureArm", captureArmButton);
    styleKnob (captureSecs);
    captureSecs.setTextValueSuffix (" s");
    styleCaption (captureSecsCap, "LEN");
    addAndMakeVisible (captureSecs);
    addAndMakeVisible (captureSecsCap);
    captureSecsAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), "captureSecs", captureSecs);
    for (auto* b : { &captureTargetA, &captureTargetB })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0x6354);   // "cT"
        addAndMakeVisible (*b);
    }
    captureTargetA.onClick = [this] { setKeyParam ("captureTarget", 0.0f); };
    captureTargetB.onClick = [this] { setKeyParam ("captureTarget", 1.0f); };

    // ----- Source strip --------------------------------------------------------
    addAndMakeVisible (waveform);

    sourceNameLabel.setJustificationType (juce::Justification::centredLeft);
    sourceNameLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    sourceNameLabel.setFont (sattari::brand::fontOptions (11.0f));
    addAndMakeVisible (sourceNameLabel);

    loadButton.onClick      = [this] { openFileChooser(); };
    reverseButton.onClick   = [this] { audioProcessor.editReverse (selectedLayer); };
    normalizeButton.onClick = [this] { audioProcessor.editNormalize (selectedLayer); };
    trimButton.onClick      = [this] { audioProcessor.editTrimToSelection (selectedLayer); };
    monoFoldButton.onClick  = [this] { audioProcessor.editMonoFold (selectedLayer); };
    undoButton.onClick      = [this] { audioProcessor.editUndo (selectedLayer); };
    for (auto* b : { &loadButton, &reverseButton, &normalizeButton, &trimButton,
                     &monoFoldButton, &undoButton })
        addAndMakeVisible (*b);

    styleToggle (reverseGrainsToggle, "Reverse Grains");
    addAndMakeVisible (reverseGrainsToggle);
    addLayerToggle (*this, reverseGrainsToggle, "Reverse Grains", "srcReverse");

    styleCombo (loopModeCombo);
    loopModeCombo.addItemList (juce::StringArray { "Off", "Loop" }, 1);
    styleCaption (loopCaption, "LOOP");
    addAndMakeVisible (loopModeCombo);
    addAndMakeVisible (loopCaption);
    layerComboBinds.push_back ({ &loopModeCombo, "srcLoopMode" });

    styleCombo (channelsCombo);
    channelsCombo.addItemList (juce::StringArray { "Stereo", "Mono", "Left", "Right" }, 1);
    styleCaption (channelsCaption, "CHANNELS");
    addAndMakeVisible (channelsCombo);
    addAndMakeVisible (channelsCaption);
    layerComboBinds.push_back ({ &channelsCombo, "srcMono" });

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

    // ===== GRAIN tab (PER-LAYER) ==============================================
    addLayerKnob  (grainPanel, grainSize, grainSizeCap, "SIZE", "grainSize");
    grainSize.setTextValueSuffix (" ms");
    addLayerKnob  (grainPanel, density, densityCap, "DENSITY", "density");
    addLayerCombo (grainPanel, densityMode, densityModeCap, "MODE", { "Rate", "Overlap" }, "densityMode");
    addLayerKnob  (grainPanel, spray, sprayCap, "SPRAY", "spray");
    addLayerKnob  (grainPanel, posBase, posBaseCap, "POSITION", "posBase");
    addLayerKnob  (grainPanel, posScanRate, posScanRateCap, "SCAN", "posScanRate");
    addLayerToggle (grainPanel, posScanSync, "Scan Sync", "posScanSync");
    addLayerCombo (grainPanel, posScanDiv, posScanDivCap, "DIV",
                   SattariGranulatorAudioProcessor::divisionChoices(), "posScanDiv");
    addLayerKnob  (grainPanel, grainPan, grainPanCap, "PAN", "grainPan");
    addLayerCombo (grainPanel, window, windowCap, "WINDOW", { "Hann", "Gauss", "Tukey" }, "window");
    // Smooth / texture (per-layer): interpolation + smoothness macro.
    addLayerCombo (grainPanel, interp, interpCap, "INTERP", { "Linear", "Cubic" }, "interp");
    addLayerKnob  (grainPanel, smoothness, smoothnessCap, "SMOOTH", "smoothness");
    // Rhythmic (tempo-synced) grains (per-layer): density + grain-size sync.
    addLayerToggle (grainPanel, densitySync, "Density Sync", "densitySync");
    addLayerCombo (grainPanel, densityDiv, densityDivCap, "DENS DIV",
                   SattariGranulatorAudioProcessor::divisionChoices(), "densityDiv");
    addLayerToggle (grainPanel, grainSizeSync, "Grain Sync", "grainSizeSync");
    addLayerCombo (grainPanel, grainSizeDiv, grainSizeDivCap, "SIZE DIV",
                   SattariGranulatorAudioProcessor::divisionChoices(), "grainSizeDiv");

    // ===== PITCH tab (per-layer offsets + UNISON + GLOBAL shaping) ============
    addLayerKnob  (pitchPanel, tuneCoarse, tuneCoarseCap, "COARSE", "tuneCoarse");
    tuneCoarse.setTextValueSuffix (" st");
    addLayerKnob  (pitchPanel, tuneFine, tuneFineCap, "FINE", "tuneFine");
    tuneFine.setTextValueSuffix (" ct");
    addLayerKnob  (pitchPanel, pitchSpread, pitchSpreadCap, "SPREAD", "pitchSpread");
    pitchSpread.setTextValueSuffix (" ct");
    // Unison (per-layer, pitch-domain: stacked detuned grain copies).
    addLayerKnob  (pitchPanel, unisonVoices, unisonVoicesCap, "UNI VC", "unisonVoices");
    addLayerKnob  (pitchPanel, unisonDetune, unisonDetuneCap, "UNI DET", "unisonDetune");
    unisonDetune.setTextValueSuffix (" ct");
    addLayerKnob  (pitchPanel, unisonWidth, unisonWidthCap, "UNI WD", "unisonWidth");
    addKnob  (pitchPanel, glide, glideCap, "GLIDE", "glide", sliderAttachments);
    addCombo (pitchPanel, glideMode, glideModeCap, "GLIDE MODE", { "Always", "Legato" }, "glideMode", comboAttachments);
    addKnob  (pitchPanel, pitchEnvAmt, pitchEnvAmtCap, "P.ENV", "pitchEnvAmt", sliderAttachments);
    pitchEnvAmt.setTextValueSuffix (" st");
    addKnob  (pitchPanel, pitchEnvAttack, pitchEnvAttackCap, "P.ATK", "pitchEnvAttack", sliderAttachments);
    pitchEnvAttack.setTextValueSuffix (" ms");
    addKnob  (pitchPanel, pitchEnvDecay, pitchEnvDecayCap, "P.DEC", "pitchEnvDecay", sliderAttachments);
    pitchEnvDecay.setTextValueSuffix (" ms");
    addKnob  (pitchPanel, pitchLfoAmt, pitchLfoAmtCap, "VIB DEPTH", "pitchLfoAmt", sliderAttachments);
    pitchLfoAmt.setTextValueSuffix (" ct");
    addKnob  (pitchPanel, pitchLfoRate, pitchLfoRateCap, "VIB RATE", "pitchLfoRate", sliderAttachments);
    pitchLfoRate.setTextValueSuffix (" Hz");
    addToggle (pitchPanel, keySnap, "Key Snap", "keySnap", buttonAttachments);

    // ===== FILTER tab (GLOBAL) ================================================
    addKnob  (filterPanel, cutoff, cutoffCap, "CUTOFF", "cutoff", sliderAttachments);
    cutoff.setTextValueSuffix (" Hz");
    addKnob  (filterPanel, reso, resoCap, "RESO", "reso", sliderAttachments);
    addCombo (filterPanel, filterType, filterTypeCap, "TYPE", { "LP24", "HP12", "BP" }, "filterType", comboAttachments);
    addKnob  (filterPanel, filterKeyTrack, filterKeyTrackCap, "KEY TRK", "filterKeyTrack", sliderAttachments);
    addKnob  (filterPanel, filterEnvAmt, filterEnvAmtCap, "ENV AMT", "filterEnvAmt", sliderAttachments);

    // ===== ENV tab (GLOBAL) ===================================================
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

    // ===== LFO tab (GLOBAL) ===================================================
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

    // ===== MOD tab (GLOBAL: sub + width/tone + voices) ========================
    addToggle (modPanel, subOsc, "Sub Osc", "subOsc", buttonAttachments);
    addKnob  (modPanel, subLevel, subLevelCap, "SUB LVL", "subLevel", sliderAttachments);
    addCombo (modPanel, subWave, subWaveCap, "SUB WAVE", { "Sine", "Triangle" }, "subWave", comboAttachments);
    addKnob  (modPanel, subOct, subOctCap, "SUB OCT", "subOct", sliderAttachments);
    addKnob  (modPanel, width, widthCap, "WIDTH", "width", sliderAttachments);
    addKnob  (modPanel, tone, toneCap, "TONE", "tone", sliderAttachments);
    addKnob  (modPanel, voices, voicesCap, "VOICES", "voices", sliderAttachments);
    addCombo (modPanel, voiceMode, voiceModeCap, "VOICE MODE", { "Poly", "Mono", "Legato" }, "voiceMode", comboAttachments);
    // Velocity routing (GLOBAL, bipolar -1..+1): how note velocity modulates
    // File Position, Grain Size and filter Cutoff.
    addKnob  (modPanel, velToPos, velToPosCap, "VEL>POS", "velToPos", sliderAttachments);
    addKnob  (modPanel, velToSize, velToSizeCap, "VEL>SIZE", "velToSize", sliderAttachments);
    addKnob  (modPanel, velToCutoff, velToCutoffCap, "VEL>CUT", "velToCutoff", sliderAttachments);

    // ----- Bottom keyboard -----------------------------------------------------
    keyboard.setAvailableRange (36, 96);   // C2..C7 fits the strip
    keyboard.setKeyWidth (18.0f);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                        juce::Colour (sattari::brand::productAccent ("granulator")).withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                        juce::Colour (sattari::brand::productAccent ("granulator")).withAlpha (0.25f));
    addAndMakeVisible (keyboard);

    // Bind the per-layer widgets to the selected layer + reflect the toggle/target.
    rebindLayer (selectedLayer);
    layerAButton.setToggleState (selectedLayer == Layer::A, juce::dontSendNotification);
    layerBButton.setToggleState (selectedLayer == Layer::B, juce::dontSendNotification);
    {
        const bool targetB = audioProcessor.parameters().getRawParameterValue ("captureTarget")->load() > 0.5f;
        captureTargetA.setToggleState (! targetB, juce::dontSendNotification);
        captureTargetB.setToggleState (targetB, juce::dontSendNotification);
    }

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
    l.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
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
//  GLOBAL widget builders (persistent attachments)
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
//  PER-LAYER widget builders (attachment (re)built in rebindLayer)
// =============================================================================
void SattariGranulatorAudioProcessorEditor::addLayerKnob (juce::Component& panel, juce::Slider& s,
                                                          juce::Label& cap, const juce::String& text,
                                                          const char* baseId)
{
    styleKnob (s);
    styleCaption (cap, text);
    panel.addAndMakeVisible (s);
    panel.addAndMakeVisible (cap);
    layerSliderBinds.push_back ({ &s, baseId });
}

void SattariGranulatorAudioProcessorEditor::addLayerCombo (juce::Component& panel, juce::ComboBox& c,
                                                           juce::Label& cap, const juce::String& text,
                                                           const juce::StringArray& items, const char* baseId)
{
    styleCombo (c);
    c.addItemList (items, 1);
    styleCaption (cap, text);
    panel.addAndMakeVisible (c);
    panel.addAndMakeVisible (cap);
    layerComboBinds.push_back ({ &c, baseId });
}

void SattariGranulatorAudioProcessorEditor::addLayerToggle (juce::Component& panel, juce::ToggleButton& t,
                                                            const juce::String& text, const char* baseId)
{
    styleToggle (t, text);
    panel.addAndMakeVisible (t);
    layerToggleBinds.push_back ({ &t, baseId });
}

// =============================================================================
//  Rebind all per-layer widgets to `layer`'s A_/B_ parameters.
// =============================================================================
void SattariGranulatorAudioProcessorEditor::rebindLayer (Layer layer)
{
    // Tear down existing per-layer attachments first (frees the old bindings).
    layerSliderAttachments.clear();
    layerComboAttachments.clear();
    layerToggleAttachments.clear();

    auto& apvts = audioProcessor.parameters();
    for (const auto& bind : layerSliderBinds)
    {
        const auto id = SattariGranulatorAudioProcessor::layerParamId (layer, bind.baseId.toRawUTF8());
        layerSliderAttachments.push_back (std::make_unique<SliderAttachment> (apvts, id, *bind.w));
    }
    for (const auto& bind : layerComboBinds)
    {
        const auto id = SattariGranulatorAudioProcessor::layerParamId (layer, bind.baseId.toRawUTF8());
        layerComboAttachments.push_back (std::make_unique<ComboAttachment> (apvts, id, *bind.w));
    }
    for (const auto& bind : layerToggleBinds)
    {
        const auto id = SattariGranulatorAudioProcessor::layerParamId (layer, bind.baseId.toRawUTF8());
        layerToggleAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *bind.w));
    }
}

void SattariGranulatorAudioProcessorEditor::selectLayer (Layer layer)
{
    selectedLayer = layer;
    // Mirror into the UI-only layerSel param so state recall remembers it.
    setKeyParam ("layerSel", layer == Layer::B ? 1.0f : 0.0f);
    rebindLayer (layer);
    layerAButton.setToggleState (layer == Layer::A, juce::dontSendNotification);
    layerBButton.setToggleState (layer == Layer::B, juce::dontSendNotification);
    waveform.repaint();   // WaveformView reads selectedLayer via the processor
    repaint();
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
    const Layer target = selectedLayer;
    fileChooser->launchAsync (flags, [this, target] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.loadFile (target, file);
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
//  Timer — key sync, selected-layer source name / undo, capture drain
// =============================================================================
void SattariGranulatorAudioProcessorEditor::timerCallback()
{
    followingBus = audioProcessor.isFollowingBusKey();
    keyButton.setState (followingBus ? sattari::ui::KeyButton::Mode::Follow
                                     : sattari::ui::KeyButton::Mode::Manual,
                        audioProcessor.effectiveKeyRoot(),
                        audioProcessor.effectiveKeyScale());

    // Reconcile the A|B selector with the (UI-only) layerSel param in case a
    // preset recall / host changed it out from under us — this re-points every
    // per-layer attachment + the WaveformView at the newly selected layer.
    if (const auto lyr = audioProcessor.selectedLayer(); lyr != selectedLayer)
        selectLayer (lyr);

    // Keep the capture-target A|B toggles in sync with the captureTarget param
    // (radio buttons only push to the param on click; a preset can change it).
    {
        const bool targetB = audioProcessor.parameters()
                                 .getRawParameterValue ("captureTarget")->load() > 0.5f;
        if (captureTargetB.getToggleState() != targetB)
        {
            captureTargetA.setToggleState (! targetB, juce::dontSendNotification);
            captureTargetB.setToggleState (targetB, juce::dontSendNotification);
        }
    }

    // Drain any completed Live Capture (publishes into the target layer).
    audioProcessor.serviceCapture();
    captureProgress = (double) audioProcessor.captureProgress();

    // Publish-to-key-bus: when Broadcast Key is on, push the currently-played key
    // to the cross-plugin bus so the granulator drives the suite (throttled +
    // change-gated inside; a no-op when off / following).
    audioProcessor.serviceKeyBroadcast();

    const auto name = audioProcessor.currentSourceName (selectedLayer);
    sourceNameLabel.setText (name.isNotEmpty()
                                 ? (juce::String (selectedLayer == Layer::A ? "A: " : "B: ") + name)
                                 : (juce::String (selectedLayer == Layer::A ? "A: " : "B: ") + "(no sample loaded)"),
                             juce::dontSendNotification);

    undoButton.setEnabled (audioProcessor.canUndo (selectedLayer));

    // Repaint the header + layer/capture bar so the follow dot + capture meter track.
    repaint (0, 0, getWidth(), 120);
}

// =============================================================================
//  paint
// =============================================================================
void SattariGranulatorAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    const auto accent = juce::Colour (sattari::brand::productAccent ("granulator"));

    // Follow-status dot next to the KeyButton.
    const auto dot = keyButton.getBounds().toFloat();
    juce::Rectangle<float> dotR (dot.getRight() + 8.0f, dot.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (followingBus ? accent : juce::Colour (sattari::brand::kTextFaint).withAlpha (0.5f));
    g.fillEllipse (dotR);

    // Capture progress bar under the capture cluster.
    if (captureProgress > 0.0)
    {
        auto barBounds = captureArmButton.getBounds().withY (captureArmButton.getBottom() + 2)
                             .withHeight (4).withWidth (captureSecs.getRight() - captureArmButton.getX());
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.fillRoundedRectangle (barBounds.toFloat(), 2.0f);
        auto fill = barBounds.withWidth ((int) (barBounds.getWidth() * captureProgress));
        g.setColour (accent);
        g.fillRoundedRectangle (fill.toFloat(), 2.0f);
    }
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

    auto outCol = header.removeFromRight (70);
    outputCaption.setBounds (outCol.removeFromTop (13));
    outputKnob.setBounds (outCol);
    header.removeFromRight (14);

    auto presetCol = header.removeFromRight (330);
    auto presetRow = presetCol.removeFromTop (26);
    abRow.setBounds (presetRow.removeFromRight (84));
    presetRow.removeFromRight (6);
    savePresetButton.setBounds (presetRow.removeFromRight (56));
    presetRow.removeFromRight (6);
    userPresets.setBounds (presetRow);

    header.removeFromLeft (12);
    header.removeFromRight (18);   // status dot gutter
    keyButton.setBounds (header.withSizeKeepingCentre (juce::jmin (300, header.getWidth()), 44));

    area.removeFromTop (8);

    // ----- Layer / morph / capture bar -----------------------------------------
    auto layerBar = area.removeFromTop (54);
    {
        // Layer select (A|B) on the far left.
        auto selCol = layerBar.removeFromLeft (96);
        layerCaption.setBounds (selCol.removeFromTop (12));
        layerAButton.setBounds (selCol.removeFromLeft (46).reduced (2));
        layerBButton.setBounds (selCol.reduced (2));
        layerBar.removeFromLeft (10);

        // Morph knob.
        auto morphCol = layerBar.removeFromLeft (70);
        morphCaption.setBounds (morphCol.removeFromTop (12));
        morphKnob.setBounds (morphCol);
        layerBar.removeFromLeft (6);

        // Per-layer LEVEL / PAN.
        auto lvlCol = layerBar.removeFromLeft (66);
        layerLevelCap.setBounds (lvlCol.removeFromTop (12));
        layerLevel.setBounds (lvlCol);
        auto panCol = layerBar.removeFromLeft (66);
        layerPanCap.setBounds (panCol.removeFromTop (12));
        layerPan.setBounds (panCol);
        layerBar.removeFromLeft (16);

        // Capture cluster on the right.
        captureCaption.setBounds (layerBar.removeFromTop (12));
        auto armCol = layerBar.removeFromLeft (60);
        captureArmButton.setBounds (armCol.withHeight (24));
        auto lenCol = layerBar.removeFromLeft (64);
        captureSecsCap.setBounds (lenCol.removeFromTop (12));
        captureSecs.setBounds (lenCol);
        auto tgtCol = layerBar.removeFromLeft (80);
        captureTargetA.setBounds (tgtCol.removeFromLeft (40).withHeight (24).reduced (2));
        captureTargetB.setBounds (tgtCol.withHeight (24).reduced (2));
    }

    area.removeFromTop (6);

    // ----- Source strip --------------------------------------------------------
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
    auto waveRow = area.removeFromTop (196);
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

    for (auto* panel : panels)
        panel->setBounds (area);

    const int cellW = 86;
    const int knobH = 92;
    const int comboH = 44;
    const int toggleH = 26;

    struct Cell { juce::Component* w; juce::Label* cap; int height; };
    auto flow = [&] (juce::Component& panel, std::vector<Cell> cells)
    {
        auto body = panel.getLocalBounds().reduced (6);
        int x = body.getX();
        int y = body.getY();
        int rowH = 0;
        for (auto& c : cells)
        {
            if (x + cellW > body.getRight())
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
        { &interp, &interpCap, comboH }, { &smoothness, &smoothnessCap, knobH },
        { &densitySync, nullptr, toggleH }, { &densityDiv, &densityDivCap, comboH },
        { &grainSizeSync, nullptr, toggleH }, { &grainSizeDiv, &grainSizeDivCap, comboH } });

    flow (pitchPanel, {
        { &tuneCoarse, &tuneCoarseCap, knobH }, { &tuneFine, &tuneFineCap, knobH },
        { &pitchSpread, &pitchSpreadCap, knobH },
        { &unisonVoices, &unisonVoicesCap, knobH }, { &unisonDetune, &unisonDetuneCap, knobH },
        { &unisonWidth, &unisonWidthCap, knobH },
        { &glide, &glideCap, knobH },
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
        { &subOsc, nullptr, toggleH }, { &subLevel, &subLevelCap, knobH },
        { &subWave, &subWaveCap, comboH }, { &subOct, &subOctCap, knobH },
        { &width, &widthCap, knobH }, { &tone, &toneCap, knobH },
        { &voices, &voicesCap, knobH }, { &voiceMode, &voiceModeCap, comboH },
        { &velToPos, &velToPosCap, knobH }, { &velToSize, &velToSizeCap, knobH },
        { &velToCutoff, &velToCutoffCap, knobH } });
}
