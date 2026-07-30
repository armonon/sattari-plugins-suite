// MainPluginEditor, part 1 of 2: the main editor (decks, mixer, transport,
// library, automix). The ArrangementRecorderView nested class lives in
// MainPluginEditor_Arrangement.cpp; shared drawing helpers are in
// MainPluginEditorInternal.h.
#include "MainPluginEditor.h"
#include "MainPluginEditorInternal.h"
#include "LayoutStack.h"
#include "StructureAnalysis.h"
#include "KeyChroma.h"

using namespace sdedit;

// Structure-region colours for the overview (intro/build/drop/breakdown/outro).
static juce::Colour waveStructureColour (const juce::String& label)
{
    if (label == "INTRO")     return juce::Colour (0xff4A9EFF);
    if (label == "BUILD")     return juce::Colour (0xff4AD9C4);
    if (label == "DROP")      return juce::Colour (0xffD4537E);
    if (label == "BREAKDOWN") return juce::Colour (0xffB47AFF);
    if (label == "OUTRO")     return juce::Colour (0xffFFB84A);
    return juce::Colour (0xff8A8A93);
}

juce::String MainPluginEditor::deckPrefix (int deckIndex)
{
    return "deck" + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, MainPluginProcessor::numDecks - 1, deckIndex)));
}

juce::String MainPluginEditor::deckLabel (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, MainPluginProcessor::numDecks - 1, deckIndex)));
}

juce::String MainPluginEditor::padKeyLabel (int padIndex)
{
    return juce::String (juce::jlimit (1, PadBank::numPads, padIndex + 1));
}

int MainPluginEditor::padIndexForKeyPress (const juce::KeyPress& key)
{
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '8')
        return (int) (c - '1');

    return -1;
}


MainPluginEditor::MainPluginEditor (MainPluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      presetManager (p.parameters(), "stemdeck")
{
    pendingSlotSeekSec.fill (-1.0);

    setLookAndFeel (&lookAndFeel);
    thumbnailFormatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    thumbnailFormatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif

    setResizable (true, true);
    setResizeLimits (1320, 820, 4200, 2600);
    setSize (1680, 1000);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    setupComboBox (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    refreshUserPresets();

    // TOOLS toggle reveals the advanced toolbar (plugin host / AI master /
    // rekordbox / project I/O / utility chips) hidden for the clean mockup view.
    // Advanced tools: a real, visible header toggle (title double-click still works
    // as a shortcut). Reveals rekordbox I/O, project save/open, metronome, talkover,
    // undo/redo, MIDI learn, mic + sync utilities.
    advancedToggle.setClickingTogglesState (true);
    advancedToggle.setToggleState (advancedVisible, juce::dontSendNotification);
    advancedToggle.setTooltip ("Show advanced tools: Rekordbox import/export, project save/open, "
                               "metronome, talkover, undo/redo, MIDI learn, mic & sync utilities");
    advancedToggle.onClick = [this]
    {
        advancedVisible = advancedToggle.getToggleState();
        resized();
        repaint();
    };
    addAndMakeVisible (advancedToggle);

    // ── Channel add / remove ────────────────────────────────────────────────
    // Live, zero-dropout: the engine publishes a new immutable graph snapshot, so
    // channels already playing are never interrupted (StemDeckChannelDropout gate).
    setupButton (addChannelButton);
    addChannelButton.setTooltip ("Add a deck channel (live — playing channels keep playing)");
    addChannelButton.onClick = [this] { showAddChannelMenu (&addChannelButton); };
    addAndMakeVisible (addChannelButton);

    setupButton (removeChannelButton);
    removeChannelButton.setTooltip ("Remove the last deck channel (live)");
    removeChannelButton.onClick = [this]
    {
        requestChannelRemoval (processorRef.getNumChannels() - 1);
    };
    addAndMakeVisible (removeChannelButton);
    refreshChannelButtons();
    title.setInterceptsMouseClicks (true, false);
    title.addMouseListener (this, false);

    // Discoverable Settings gear (advanced tools + showcase toggle live here).
    setupButton (settingsButton);
    settingsButton.setButtonText (u8 ("\xe2\x9a\x99"));   // gear glyph (UTF-8)
    settingsButton.setTooltip ("Settings");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    setupButton (commandButton);
    commandButton.setButtonText (u8 ("Search  \xe2\x8c\x98K"));
    commandButton.setTooltip ("Search every StemDeck action (Command-K)");
    commandButton.onClick = [this] { showCommandPalette(); };
    addAndMakeVisible (commandButton);

    setupButton (inspectorButton);
    inspectorButton.setButtonText ("Inspect");
    inspectorButton.setTooltip ("Focused deck details, preflight status, and quick actions");
    inspectorButton.onClick = [this] { showQuickInspector(); };
    addAndMakeVisible (inspectorButton);

    setupButton (focusModeButton);
    focusModeButton.setClickingTogglesState (true);
    focusModeButton.setButtonText ("Focus");
    focusModeButton.setTooltip ("Hide other channels and concentrate on the focused deck");
    focusModeButton.onClick = [this] { setFocusMode (focusModeButton.getToggleState()); };
    addAndMakeVisible (focusModeButton);

    setupFileBrowser();
    setupLibrary();

    title.setText ("StemDeck", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText (u8 ("FOUR DECKS  \xe2\x80\xa2  STEM MIXER  \xe2\x80\xa2  ARRANGEMENT"), juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    subtitle.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    standaloneWorkflowGuide.setText (
        u8 ("RECORD captures master + 4 deck WAVs + automation \xe2\x80\xa2 Reload bounces back into decks for post-set tweaks"),
        juce::dontSendNotification);
    standaloneWorkflowGuide.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    standaloneWorkflowGuide.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    standaloneWorkflowGuide.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (standaloneWorkflowGuide);

    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    tempoLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    addAndMakeVisible (tempoLabel);

    pluginHostStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pluginHostStatus.setFont (juce::FontOptions (11.5f));
    pluginHostStatus.setJustificationType (juce::Justification::centredLeft);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    addAndMakeVisible (pluginHostStatus);

    auto setupSectionLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.82f));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };
    setupSectionLabel (liveMixerLabel, "LIVE MIXER / STEM DECKS");
    setupSectionLabel (arrangementLabel, "ARRANGEMENT CAPTURE");
    setupSectionLabel (samplerLabel, "PERFORMANCE PADS");

    setupButton (scanPluginsButton);
    setupButton (reloadPluginCacheButton);
    setupButton (loadPluginButton);
    setupButton (clearChainButton);
    setupComboBox (pluginChainSelector);
    setupComboBox (pluginSelector);
    pluginHostMessage.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pluginHostMessage.setFont (juce::FontOptions (11.5f));
    pluginHostMessage.setJustificationType (juce::Justification::centredLeft);
    pluginHostMessage.setText ("FX host cache: " + processorRef.getPluginHostCacheFile().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (scanPluginsButton);
    addAndMakeVisible (reloadPluginCacheButton);
    addAndMakeVisible (loadPluginButton);
    addAndMakeVisible (clearChainButton);
    addAndMakeVisible (pluginHostMessage);

    masterKeyLabel.setText ("MASTER KEY", juce::dontSendNotification);
    masterKeyLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    masterKeyLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    masterKeyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (masterKeyLabel);
    setupComboBox (masterKey);
    {
        const auto keyChoices = processorRef.getKeyTargetChoices();
        for (int i = 0; i < keyChoices.size(); ++i)
            masterKey.addItem (keyChoices[i], i + 1);
        masterKey.setSelectedId (1, juce::dontSendNotification);
    }
    masterKeyAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), "masterKey", masterKey);
    setupButton (syncAllDecksButton);
    syncAllDecksButton.onClick = [this] { showSyncAllDecksMenu(); };
    addAndMakeVisible (syncAllDecksButton);

    // BEAT SYNC toggle: locks every deck's tempo to the GLOBAL tempo so they play
    // beat-matched, and makes the Global Tempo knob audibly re-time all synced decks.
    // Without this, tempo warp is off and the global tempo does nothing to the decks.
    setupButton (syncBpmButton);
    syncBpmButton.setClickingTogglesState (true);
    syncBpmButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4AD9C4).withAlpha (0.35f));
    syncBpmButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff4AD9C4));
    syncBpmButton.setTooltip ("BEAT SYNC: lock all decks to the global tempo (beat-match). Turn on, then the Global Tempo knob speeds/slows every synced deck together. (hotkey: B)");
    syncBpmButton.onClick = [this]
    {
        if (syncBpmButton.getToggleState())
            syncBpmAll();   // seed global tempo from the master deck, enable warp, phase-lock
        else if (auto* b = processorRef.parameters().getParameter ("beatSync"))
        {
            b->setValueNotifyingHost (0.0f);
            recordStatus.setText ("Beat sync off — decks play at their native tempo", juce::dontSendNotification);
        }
    };
    addAndMakeVisible (syncBpmButton);
    setupButton (syncKeyButton);
    syncKeyButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4AD9C4).withAlpha (0.35f));
    syncKeyButton.setColour (juce::TextButton::textColourOnId, juce::Colour (0xff4AD9C4));
    syncKeyButton.setTooltip ("KEY SYNC: harmonically match all decks to the master deck's key, Camelot-style (hotkey: K)");
    syncKeyButton.onClick = [this] { syncKeyAll(); };
    addAndMakeVisible (syncKeyButton);
    setupButton (stemLinkToggle);
    stemLinkAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "stemLink", stemLinkToggle);
    addAndMakeVisible (stemLinkToggle);

    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto prefix = deckPrefix (d);
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            processorRef.parameters().addParameterListener (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain", this);
    }

    const auto chainNames = processorRef.getPluginHostChainNames();
    for (int i = 0; i < chainNames.size(); ++i)
        pluginChainSelector.addItem (chainNames[i], i + 1);
    pluginChainSelector.setSelectedId (1, juce::dontSendNotification);
    refreshHostPluginChoices();
    scanPluginsButton.onClick = [this] { runPluginHostScan(); };
    reloadPluginCacheButton.onClick = [this]
    {
        const bool loaded = processorRef.loadPluginHostCache();
        refreshHostPluginChoices();
        pluginHostMessage.setText (loaded ? "Reloaded FX cache" : "No FX cache found yet", juce::dontSendNotification);
    };
    loadPluginButton.onClick = [this] { loadSelectedHostPlugin(); };
    clearChainButton.onClick = [this]
    {
        const int chainIndex = pluginChainSelector.getSelectedId() - 1;
        closeAllPluginWindows();   // clearing destroys the plugin processors
        processorRef.clearHostPluginChain (chainIndex);
        pluginHostMessage.setText ("Cleared " + pluginChainSelector.getText(), juce::dontSendNotification);
        fxRack.repaint();
    };
    setupButton (sattariFxButton);
    sattariFxButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4AD9C4).withAlpha (0.20f));
    sattariFxButton.setTooltip ("Add a Sattari-suite effect to the selected chain (deck / mic / master)");
    sattariFxButton.onClick = [this] { showSattariFxMenu(); };
    addAndMakeVisible (sattariFxButton);

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckName = deckLabel (i).toUpperCase();
        deckWaves[(size_t) i] = std::make_unique<WaveformDisplay> (thumbnailFormatManager, thumbnailCache, deckName);
        // Focus follows the user's hands: dropping a track on a deck focuses it.
        deckWaves[(size_t) i]->setOnFileDropped ([this, i] (const juce::File& f)
                                                 { setFocusedDeck (i); requestDeckLoad (i, f); });
        // Empty decks show a real call to action; clicking it opens the file chooser.
        deckWaves[(size_t) i]->setShowDropHint (true);
        deckWaves[(size_t) i]->setOnBrowseRequest ([this, i] { openDeckChooser (i); });
        addAndMakeVisible (*deckWaves[(size_t) i]);

        deckWaveRegions[(size_t) i] = std::make_unique<stemdeck::WaveformRegion>();
        deckWaveRegions[(size_t) i]->onFilesDropped = [this, i] (const juce::StringArray& files)
        {
            if (! files.isEmpty()) { setFocusedDeck (i); requestDeckLoad (i, juce::File (files[0])); }
        };
        deckWaveRegions[(size_t) i]->onSeekNorm = [this, i] (float norm)
        {
            const double total = processorRef.getDeckTotalSeconds (i);
            if (total > 0) processorRef.seekDeckSeconds (i, norm * total);
        };
        addChildComponent (*deckWaveRegions[(size_t) i]);   // shown only for loaded decks (resized())

        deckStrips[(size_t) i] = std::make_unique<stemdeck::DeckStrip> (i);
        configureDeckStrip (i);
        addAndMakeVisible (*deckStrips[(size_t) i]);

        setupDeck (deckControls[(size_t) i], deckPrefix (i), u8 ("\xe2\x96\xb6 PLAY"));   // stopped default; timer swaps to STOP
        {
            const auto accent = deckAccent (i);
            auto& dc = deckControls[(size_t) i];
            for (auto* knob : { &dc.low, &dc.mid, &dc.high, &dc.filter })
            {
                knob->setColour (juce::Slider::rotarySliderFillColourId, accent);
                knob->setColour (juce::Slider::trackColourId, accent);
            }
            for (auto& sg : dc.stemGains)
            {
                sg.setColour (juce::Slider::rotarySliderFillColourId, accent);
                sg.setColour (juce::Slider::trackColourId, accent);
            }
            for (auto* fader : { &dc.volume, &dc.fader, &dc.pitch })
                fader->setColour (juce::Slider::trackColourId, accent);
            dc.play   .setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
            dc.cue    .setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
            dc.liveKey.setColour (juce::ToggleButton::tickColourId, accent);
            dc.keyLock.setColour (juce::ToggleButton::tickColourId, accent);
        }
        deckControls[(size_t) i].load.onClick = [this, i] { openDeckChooser (i); };
        deckControls[(size_t) i].loadStem.onClick = [this, i]
        {
            const int selectedLane = deckControls[(size_t) i].stemLane.getSelectedId() - 1;
            openDeckStemChooser (i, selectedLane < 0 ? DeckPlayer::fullMixStem : selectedLane);
        };
        deckControls[(size_t) i].aiStems.onClick = [this, i] { processorRef.requestDeckAiStemsAsync (i); };
        // CUE = return to the cue point (hot cue 1 when set, else track start) —
        // real DJ cue semantics rather than a mislabelled rewind.
        deckControls[(size_t) i].cue.setTooltip ("Jump to the cue point (hot cue 1 if set, else track start)");
        deckControls[(size_t) i].cue.onClick = [this, i]
        {
            const double cuePoint = processorRef.getHotCue (i, 0);
            if (cuePoint >= 0.0)
                processorRef.seekDeckSeconds (i, cuePoint);
            else
                processorRef.rewindDeck (i);
        };

        // Audio→MIDI: extract the deck's melody (YIN) into an arrangement MIDI clip.
        {
            auto& btn = deckControls[(size_t) i].extractMidi;
            setupButton (btn);
            btn.setButtonText (u8 ("\xe2\x86\x92 MIDI"));
            btn.setTooltip ("Extract melody from this deck's audio into a MIDI clip (monophonic)");
            btn.onClick = [this, i]
            {
                const juce::File file (processorRef.getDeckPath (i));
                auto& dcc = deckControls[(size_t) i];
                if (! file.existsAsFile())
                {
                    dcc.stemStatus.setText ("Load audio first", juce::dontSendNotification);
                    return;
                }
                const bool neural = NeuralTranscriber::isAvailable();
                dcc.stemStatus.setText (neural ? u8 ("Neural transcription (basic-pitch)\xe2\x80\xa6")
                                               : u8 ("Extracting melody (YIN)\xe2\x80\xa6"),
                                        juce::dontSendNotification);
                juce::Component::SafePointer<MainPluginEditor> safe (this);
                uiWorkPool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
                    [safe, file, i, neural]() -> juce::ThreadPoolJob::JobStatus
                {
                    // Self-contained: no editor/processor state touched off-thread.
                    std::vector<MelodyExtractor::Note> notes;
                    juce::String msg;
                    bool usedNeural = false;
                    if (neural)
                        usedNeural = NeuralTranscriber::transcribe (file, notes, msg);
                    if (! usedNeural)   // not installed, failed, or produced nothing → YIN
                    {
                        juce::AudioFormatManager fm;
                        fm.registerBasicFormats();
                        if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
                        {
                            const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                            (juce::int64) (reader->sampleRate * 90.0));
                            juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
                            reader->read (&buf, 0, len, 0, true, true);
                            notes = MelodyExtractor::extract (buf, reader->sampleRate);
                        }
                    }
                    juce::MessageManager::callAsync ([safe, notes = std::move (notes), i, usedNeural]() mutable
                    {
                        if (safe != nullptr)
                            safe->applyExtractedMelody (i, notes, usedNeural);
                    });
                    return juce::ThreadPoolJob::jobHasFinished;
                }));
            };
            addAndMakeVisible (btn);
        }

        // Beatbox→drums: classify transients into kick/snare/hat, print a drum clip.
        {
            auto& btn = deckControls[(size_t) i].extractDrums;
            setupButton (btn);
            btn.setButtonText (u8 ("\xe2\x86\x92 DRUMS"));
            btn.setTooltip ("Turn beatboxing / percussion into a drum pattern (kick, snare, hat)");
            btn.onClick = [this, i]
            {
                const juce::File file (processorRef.getDeckPath (i));
                auto& dcc = deckControls[(size_t) i];
                if (! file.existsAsFile())
                {
                    dcc.stemStatus.setText ("Load audio first", juce::dontSendNotification);
                    return;
                }
                dcc.stemStatus.setText (u8 ("Detecting drum hits\xe2\x80\xa6"), juce::dontSendNotification);
                juce::Component::SafePointer<MainPluginEditor> safe (this);
                uiWorkPool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
                    [safe, file, i]() -> juce::ThreadPoolJob::JobStatus
                {
                    juce::AudioFormatManager fm;
                    fm.registerBasicFormats();
                    std::vector<MelodyExtractor::Note> drumHits;
                    if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
                    {
                        const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                        (juce::int64) (reader->sampleRate * 60.0));
                        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
                        reader->read (&buf, 0, len, 0, true, true);
                        drumHits = DrumExtractor::extract (buf, reader->sampleRate);
                    }
                    juce::MessageManager::callAsync ([safe, drumHits = std::move (drumHits), i]() mutable
                    {
                        if (safe != nullptr)
                            safe->applyExtractedDrums (i, drumHits);
                    });
                    return juce::ThreadPoolJob::jobHasFinished;
                }));
            };
            addAndMakeVisible (btn);
        }

        // Seek on waveform click
        deckWaves[(size_t) i]->setOnSeekRequest ([this, i] (double seconds)
        {
            processorRef.seekDeckSeconds (i, seconds);
        });

        setupHotCueButtons (i);
        setupLoopControls (i);
        setupToolTabs (i);

        // Beat jump buttons — back: bars[0]=4 [1]=8 [2]=16 [3]=32
        {
            static constexpr double kBars[] = { 4.0, 8.0, 16.0, 32.0 };
            static constexpr const char* kBackLabels[] = { "◀4", "◀8", "◀16", "◀32" };
            static constexpr const char* kFwdLabels[]  = { "4▶", "8▶", "16▶", "32▶" };
            auto& dc = deckControls[(size_t) i];
            for (int b = 0; b < 4; ++b)
            {
                dc.beatJumpBack[b].setButtonText (u8 (kBackLabels[b]));
                setupButton (dc.beatJumpBack[b]);
                dc.beatJumpBack[b].onClick = [this, i, b] { processorRef.beatJumpDeck (i, -kBars[b]); };
                addAndMakeVisible (dc.beatJumpBack[b]);

                dc.beatJumpFwd[b].setButtonText (u8 (kFwdLabels[b]));
                setupButton (dc.beatJumpFwd[b]);
                dc.beatJumpFwd[b].onClick = [this, i, b] { processorRef.beatJumpDeck (i, kBars[b]); };
                addAndMakeVisible (dc.beatJumpFwd[b]);
            }
        }

        // Built-in FX sliders
        {
            auto& dc = deckControls[(size_t) i];
            setupSlider (dc.reverbMix, true);
            dc.reverbMix.setTooltip ("Reverb mix");
            dc.reverbMix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            dc.reverbMix.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (dc.reverbMix);
            dc.reverbMixAttachment = std::make_unique<SliderAttachment> (
                processorRef.parameters(), deckPrefix (i) + "RevMix", dc.reverbMix);

            setupSlider (dc.echoMix, true);
            dc.echoMix.setTooltip ("Echo mix");
            dc.echoMix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            dc.echoMix.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (dc.echoMix);
            dc.echoMixAttachment = std::make_unique<SliderAttachment> (
                processorRef.parameters(), deckPrefix (i) + "EchoMix", dc.echoMix);

            setupMacroKnob (i);

            // Per-stem FX: filter + space/echo send for drums/bass/music/vocals.
            static const char* stemShort[4] = { "Drums", "Bass", "Music", "Vocals" };
            for (int s = 0; s < 4; ++s)
            {
                const auto id = DeckPlayer::stemLaneId (DeckPlayer::drumsStem + s);
                setupSlider (dc.stemFilt[(size_t) s], true);
                dc.stemFilt[(size_t) s].setTooltip (juce::String (stemShort[s]) + " filter (LP ↔ HP)");
                dc.stemFilt[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemFilt[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemFilt[(size_t) s]);
                dc.stemFiltAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Filt", dc.stemFilt[(size_t) s]);

                setupSlider (dc.stemSend[(size_t) s], true);
                dc.stemSend[(size_t) s].setTooltip (juce::String (stemShort[s]) + " space/echo send");
                dc.stemSend[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemSend[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemSend[(size_t) s]);
                dc.stemSendAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Send", dc.stemSend[(size_t) s]);

                // Independent per-stem pitch (semitones) — e.g. pitch vocals only.
                setupSlider (dc.stemPitch[(size_t) s], true);
                dc.stemPitch[(size_t) s].setTooltip (juce::String (stemShort[s]) + " pitch (semitones, independent)");
                dc.stemPitch[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemPitch[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemPitch[(size_t) s]);
                dc.stemPitchAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Pitch", dc.stemPitch[(size_t) s]);
            }
        }

        setupSlipAndRoll (i);

        // ── Crossfader assign buttons: A / Ctr / B ───────────────────────────
        static const char* const kCfLabels[] = { "A", "Ctr", "B" };
        auto& dc2 = deckControls[(size_t) i];
        for (int s = 0; s < 3; ++s)
        {
            auto& btn = dc2.cfSideBtns[(size_t) s];
            btn.setButtonText (kCfLabels[s]);
            setupButton (btn);
            btn.setClickingTogglesState (false);
            const int side = s;
            const int deckIdx = i;
            btn.onClick = [this, deckIdx, side]
            {
                processorRef.setDeckCfSide (deckIdx, side);
                // Refresh toggle visuals
                for (int ss = 0; ss < 3; ++ss)
                    deckControls[(size_t) deckIdx].cfSideBtns[(size_t) ss]
                        .setToggleState (ss == side, juce::dontSendNotification);
            };
            addAndMakeVisible (btn);
        }
        // Set initial toggle state from state
        const int curSide = processorRef.getDeckCfSide (i);
        for (int s = 0; s < 3; ++s)
            dc2.cfSideBtns[(size_t) s].setToggleState (s == curSide, juce::dontSendNotification);
    }

    // Tap tempo
    setupButton (tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };
    addAndMakeVisible (tapTempoButton);

    // Universal transport: one button to play / stop every deck at once. Self-paints
    // via ToolButton (pill + vector icon); `isActive` drives the STOP/playing state.
    masterPlayButton.setTooltip ("Play or stop every deck at once");
    masterPlayButton.isActive = [this]
    {
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f)
                return true;
        return false;
    };
    masterPlayButton.onClick = [this]
    {
        const bool anyPlaying = masterPlayButton.isActive();
        const float target = anyPlaying ? 0.0f : 1.0f;   // any playing -> stop all, else start all
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (i) + "Play"))
                p->setValueNotifyingHost (target);
    };
    addAndMakeVisible (masterPlayButton);

    // Prominent top-bar REC: arm/stop capturing the performance. Turns solid red
    // while recording via `isActive`.
    topRecordButton.setTooltip ("Record your performance into the arrangement");
    topRecordButton.isActive = [this] { return processorRef.isMasterRecording(); };
    topRecordButton.onClick = [this]
    {
        if (processorRef.isMasterRecording())
            recordStatus.setText ("Hold STOP in the recording bar to keep and finish this take",
                                  juce::dontSendNotification);
        else
            requestRecording (2);
    };
    addAndMakeVisible (topRecordButton);

    automixButton.setTooltip ("Auto-mix: phrase-aligned, tempo- and key-matched transition from the playing deck to the next loaded deck");
    automixButton.onClick = [this] { startAutomix(); };
    addAndMakeVisible (automixButton);

    automixStatusLabel.setFont (juce::FontOptions (10.0f));
    automixStatusLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    automixStatusLabel.setJustificationType (juce::Justification::centredLeft);
    automixStatusLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (automixStatusLabel);

    // Camelot wheel & MIDI learn buttons
    setupButton (camelotButton);
    camelotButton.onClick = [this] { showCamelotWheel (-1); };
    addAndMakeVisible (camelotButton);

    setupButton (midiLearnButton);
    midiLearnButton.onClick = [this] { updateMidiLearnUI(); };
    addAndMakeVisible (midiLearnButton);

    libraryMetaLabel.setFont (juce::FontOptions (10.0f));
    libraryMetaLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    libraryMetaLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (libraryMetaLabel);

    addAndMakeVisible (mixerPanel);
    masterFxPad.setAccent (deckAccent (0));   // product cyan
    masterFxPad.attach (processorRef.parameters(), "masterFxX", "masterFxY");
    addAndMakeVisible (masterFxPad);
    setupSlider (crossfader, false);
    // A crossfader's raw 0..1 value is meaningless; show the actual A/C <-> B/D
    // balance instead, and double-click snaps back to CENTER. Honour REVERSE so the
    // label names the bus you're actually fading toward.
    crossfader.textFromValueFunction = [this] (double v) -> juce::String
    {
        if (std::abs (v - 0.5) < 0.02) return "CENTER";
        const bool rev = processorRef.parameters().getRawParameterValue ("crossfaderReverse")->load() > 0.5f;
        const bool acSide = (v < 0.5) != rev;   // reverse swaps which bus each end feeds
        const int pct = (int) std::round (std::abs (v - 0.5) * 200.0);
        return (acSide ? "A/C " : "B/D ") + juce::String (pct) + "%";
    };
    crossfader.setDoubleClickReturnValue (true, 0.5);
    crossfader.updateText();
    // The CROSSFADER panel header carries the tabular value; hide the slider's own
    // textbox so the fader reads as one clean control (no duplicate 0.5 under it).
    crossfader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    crossfader.setTooltip ("Crossfader: blends the A/C bus into the B/D bus. Double-click to centre.");

    // Response curve: Smooth (equal-power blend) · Linear · Sharp (fast crossover) ·
    // Cut (scratch/transform — near-instant near either edge).
    setupComboBox (crossfaderCurve);
    crossfaderCurve.addItem ("Smooth", 1);
    crossfaderCurve.addItem ("Linear", 2);
    crossfaderCurve.addItem ("Sharp",  3);
    crossfaderCurve.addItem ("Cut",    4);
    crossfaderCurve.setTooltip ("Crossfader curve: Smooth = long blends, Sharp/Cut = fast cuts & scratching");
    addAndMakeVisible (crossfaderCurve);

    setupButton (crossfaderReverse);
    crossfaderReverse.setClickingTogglesState (true);
    crossfaderReverse.setColour (juce::TextButton::buttonOnColourId,
                                 juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    crossfaderReverse.setTooltip ("Reverse (hamster): swap which end feeds A/C vs B/D");
    crossfaderReverse.onClick = [this] { crossfader.updateText(); };   // keep the readout in sync
    addAndMakeVisible (crossfaderReverse);
    setupSlider (masterBpm, false, " BPM");
    setupSlider (master, true, " dB");
    setupSlider (limiter, true, " dB");
    // Widen these readouts so "120.0 BPM" / "-60.0 dB" fit without truncation.
    masterBpm.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    master   .setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    limiter  .setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    hostSync.setButtonText ("Host sync");
    setupButton (hostSync);
    addAndMakeVisible (hostSync);
    beatSync.setButtonText ("Beat sync");
    setupButton (beatSync);
    addAndMakeVisible (beatSync);
    micMonitor.setButtonText ("Mic monitor");
    setupButton (micMonitor);
    addAndMakeVisible (micMonitor);
    liveMidiButton.setButtonText ("Live MIDI");
    setupButton (liveMidiButton);
    liveMidiButton.setTooltip ("Play a connected MIDI controller through the built-in instrument (into the mix)");
    addAndMakeVisible (liveMidiButton);
    setupSlider (micGain, false, " dB");
    micGain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    setupButton (revealRecordingButton);
    revealRecordingButton.setEnabled (false);
    revealRecordingButton.onClick = [this] { revealLastRecording(); };
    addAndMakeVisible (revealRecordingButton);
    setupButton (reloadIntoDecksButton);
    reloadIntoDecksButton.setEnabled (false);
    reloadIntoDecksButton.onClick = [this] { reloadLastRecordingIntoDecks(); };
    addAndMakeVisible (reloadIntoDecksButton);
    setupButton (bounceEditsButton);
    bounceEditsButton.setEnabled (false);
    bounceEditsButton.onClick = [this] { bounceArrangementEdits(); };
    addAndMakeVisible (bounceEditsButton);
    setupButton (snapToggleButton);
    snapToggleButton.setClickingTogglesState (true);
    snapToggleButton.onClick = [this]
    {
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        arrangementView.setSnap (snapToggleButton.getToggleState(), 0.25, bpm);
        arrangementView.repaint();
    };
    addAndMakeVisible (snapToggleButton);
    setupButton (razorButton);
    razorButton.setClickingTogglesState (true);
    razorButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kDanger).withAlpha (0.6f));
    razorButton.onClick = [this]
    {
        arrangementView.setRazorMode (razorButton.getToggleState());
    };
    addAndMakeVisible (razorButton);
    setupButton (playArrButton);
    playArrButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    playArrButton.onClick = [this] { toggleArrangementPlayback(); };
    addAndMakeVisible (playArrButton);
    setupButton (arrRecordButton);
    arrRecordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5f6a));
    arrRecordButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    arrRecordButton.onClick = [this]
    {
        if (processorRef.isMasterRecording())
            recordStatus.setText ("Hold STOP in the recording bar to keep and finish this take",
                                  juce::dontSendNotification);
        else
            requestRecording (2);
    };
    addAndMakeVisible (arrRecordButton);
    setupButton (undoButton);
    undoButton.onClick = [this] { arrangementView.undo(); };
    addAndMakeVisible (undoButton);
    setupButton (redoButton);
    redoButton.onClick = [this] { arrangementView.redo(); };
    addAndMakeVisible (redoButton);
    setupButton (arrZoomOutButton);
    arrZoomOutButton.setTooltip ("Zoom out");
    arrZoomOutButton.onClick = [this] { arrangementView.zoomOut(); };
    addAndMakeVisible (arrZoomOutButton);
    setupButton (arrZoomInButton);
    arrZoomInButton.setTooltip ("Zoom in");
    arrZoomInButton.onClick = [this] { arrangementView.zoomIn(); };
    addAndMakeVisible (arrZoomInButton);
    setupButton (arrZoomFitButton);
    arrZoomFitButton.setTooltip ("Zoom to fit");
    arrZoomFitButton.onClick = [this] { arrangementView.zoomToFit(); };
    addAndMakeVisible (arrZoomFitButton);
    setupButton (arrLoopButton);
    arrLoopButton.setClickingTogglesState (true);
    arrLoopButton.setTooltip ("Loop PLAYBACK of the arrangement region (drag the ruler to set it) — not a loop-record mode");
    arrLoopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    arrLoopButton.onClick = [this] { arrangementView.setLoopEnabled (arrLoopButton.getToggleState()); };
    addAndMakeVisible (arrLoopButton);

    setupButton (pianoRollButton);
    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    // ARRANGER nav → the full arrangement surface: energy arc, edit toolbar and the
    // capture timeline. This used to be a footer on the perform screen, which is
    // where the product's whole argument was living. The per-clip piano roll still
    // opens separately via onEditMidiClip.
    pianoRollButton.onClick = [this]
    {
        const bool show = pianoRollButton.getToggleState();
        // resized() routes through layoutArrangerSurface(), which shows or hides the
        // arranger's controls from this toggle — so lay out BEFORE fading in, or the
        // first frame of the fade is the previous size.
        resized();
        if (show) { refreshEnergyArc();
                    fadeOverlay (arrangementView, true); fadeOverlay (energyArc, true);
                    arrangementView.toFront (true); energyArc.toFront (true); }
        else      { fadeOverlay (arrangementView, false); fadeOverlay (energyArc, false); }
    };
    addAndMakeVisible (pianoRollButton);

    // Sattari FX rack — occupies the crossfader's slot in the Master Control Band.
    {
        // Scope selector replaces the combo box (reference §5: the scope selector IS
        // the point). Only chains the engine actually has are offered.
        //
        // (This comment used to say per-stem plugin chains "do not exist". They do:
        // HostGraph declares 16 of them — 4 decks x 4 stems, appended after master
        // so the original chain ids keep their persisted numeric values — and
        // MainPluginProcessor drives them from TrackDeckBody::onStemFx.)
        refreshFxScopes();
        fxRack.setScope (0);   // Master
        fxRack.getPluginCount = [this] { return processorRef.getChainPluginCount (fxRack.currentChainIndex()); };
        fxRack.getPluginName  = [this] (int i) { return processorRef.getChainPluginName (fxRack.currentChainIndex(), i); };
        fxRack.onAdd          = [this] { addSattariFxToChain (fxRack.currentChainIndex()); };
        fxRack.onEdit         = [this] (int i) { openChainPluginEditor (fxRack.currentChainIndex(), i); };
        fxRack.onRemove       = [this] (int i)
        {
            // Close only THIS plugin's editor — removal destroys just that processor.
            if (auto* p = processorRef.getChainPluginProcessor (fxRack.currentChainIndex(), i))
                for (int w = pluginWindows.size(); --w >= 0;)
                    if (pluginWindows[w]->ownedProcessor() == p)
                        pluginWindows.remove (w);

            processorRef.removeChainPlugin (fxRack.currentChainIndex(), i);
            fxRack.repaint();
        };
        fxRack.isBypassed     = [this] (int i) { return processorRef.isChainPluginBypassed (fxRack.currentChainIndex(), i); };
        fxRack.onToggleBypass = [this] (int i)
        {
            const int c = fxRack.currentChainIndex();
            processorRef.setChainPluginBypassed (c, i, ! processorRef.isChainPluginBypassed (c, i));
        };
        fxRack.onReorder      = [this] (int from, int to)
        {
            processorRef.moveChainPlugin (fxRack.currentChainIndex(), from, to);
            fxRack.repaint();
        };
        fxRack.onChainChanged = [this] { fxRack.repaint(); };
        addAndMakeVisible (fxRack);
    }

    // ScenePilot energy-arc arranger sits atop the ARRANGER (Auto Director + editable
    // section energy); the clip timeline is the "Auto Pro Arrangement" underneath.
    addChildComponent (energyArc);
    energyArc.onArcEdited = [this] { /* energy edits captured to the arc; applied on export */ };

    // Mix arrangement overlay — hidden until the ARRANGER nav is toggled. Waveforms
    // + structure come from the live deck buffers/analysis via these providers.
    addChildComponent (mixArrangeView);
    mixArrangeView.setAudioProvider ([this] (const juce::String& id, double& rate) -> const juce::AudioBuffer<float>*
    {
        const int deck = id.getIntValue();
        if (auto a = processorRef.getDeckLoadedAudio (deck)) { rate = a->sourceSampleRate; return &a->audio; }
        return nullptr;
    });
    mixArrangeView.setStructureProvider ([this] (const juce::String& id)
    {
        const int deck = id.getIntValue();
        if (auto a = processorRef.getDeckLoadedAudio (deck))
        {
            const double bpm = a->analysis.detectedBpm > 1.0f ? a->analysis.detectedBpm : 124.0;
            return stemdeck::segmentStructure (a->audio, a->sourceSampleRate, bpm);
        }
        return std::vector<stemdeck::Section> {};
    });

    pianoRoll.onClose = [this]
    {
        pianoRoll.setVisible (false);
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
    };
    pianoRoll.onNotesChanged = [this]
    {
        std::vector<ArrangementRecorderView::MidiNote> notes;
        for (const auto& n : pianoRoll.getNotes())
            notes.push_back ({ n.pitch, n.startBeats, n.lengthBeats, n.velocity });
        arrangementView.setMidiClipNotes (pianoRollLane, pianoRollClip, std::move (notes));
    };
    pianoRoll.setBars (8);
    addChildComponent (pianoRoll);   // hidden until toggled

    arrangementView.onEditMidiClip = [this] (int lane, int clip) { openPianoRollForClip (lane, clip); };

    // ── Mixer overlay (deck channel strips: volume + mute/solo) ──
    setupButton (mixerButton);
    mixerButton.setClickingTogglesState (true);
    mixerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    mixerButton.onClick = [this]
    {
        const bool on = mixerButton.getToggleState();
        if (on) { pianoRollButton.setToggleState (false, juce::dontSendNotification); fadeOverlay (pianoRoll, false); fadeOverlay (arrangementView, false); fadeOverlay (energyArc, false); resized(); }
        fadeOverlay (mixerView, on);
        if (on) mixerView.toFront (true);
    };
    addAndMakeVisible (mixerButton);

    // ── Four-workspace nav: SETS / LIVE / MIX / STUDIO ──────────────────────
    // Icon + label are painted in paint(); buttons are naked click targets that
    // only show a subtle highlight when active.
    for (juce::Button* b : { (juce::Button*) &toggleLibraryButton, (juce::Button*) &navDecksButton,
                             (juce::Button*) &mixerButton, (juce::Button*) &pianoRollButton })
    {
        b->setButtonText ({});
        b->getProperties().set ("navNaked", true);
    }
    toggleLibraryButton.setTitle ("Sets");
    toggleLibraryButton.setDescription ("Find music and prepare a live set");
    navDecksButton.setTitle ("Live");
    navDecksButton.setDescription ("Perform with decks, inputs, stems, cues, and pads");
    mixerButton.setTitle ("Mix");
    mixerButton.setDescription ("Channel strips, routing, effects, and metering");
    pianoRollButton.setTitle ("Studio");
    pianoRollButton.setDescription ("Arrange, refine, mix, and export captured takes");
    setupButton (navDecksButton);
    navDecksButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    navDecksButton.onClick = [this]
    {
        fadeOverlay (pianoRoll, false);
        fadeOverlay (arrangementView, false);
        fadeOverlay (energyArc, false);
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
        resized();
        fadeOverlay (mixerView, false);
        mixerButton.setToggleState (false, juce::dontSendNotification);
        if (toggleLibraryButton.getToggleState()) { toggleLibraryButton.setToggleState (false, juce::dontSendNotification); toggleLibrary(); }
        if (toggleBrowserButton.getToggleState()) { toggleBrowserButton.setToggleState (false, juce::dontSendNotification); toggleFileBrowser(); }
    };
    addAndMakeVisible (navDecksButton);

    // Global Key Lock: engages pitch-preserve on all four decks at once.
    setupButton (keyLockAllButton);
    keyLockAllButton.setClickingTogglesState (true);
    keyLockAllButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    keyLockAllButton.onClick = [this]
    {
        const float v = keyLockAllButton.getToggleState() ? 1.0f : 0.0f;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (i) + "KeyLock"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
    };
    addAndMakeVisible (keyLockAllButton);

    // Smart mastering assistant controls (toolbar): toggle + target mode.
    setupButton (aiMasterButton);
    aiMasterButton.setClickingTogglesState (true);
    aiMasterButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    // AI-honesty (UI prompt Part 4 / band 3.1): name the outcome, not the technology.
    // The label reflects the target picked in the adjacent selector (updated in the timer).
    aiMasterButton.setButtonText ("Master -14");
    aiMasterButton.setTooltip ("Auto-master the mix to the selected loudness target (EQ balance + bus dynamics + limiting to the target LUFS)");
    addAndMakeVisible (aiMasterButton);
    aiMasterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.parameters(), "smartMasterOn", aiMasterButton);

    aiMasterMode.addItem ("Streaming -14", 1);
    aiMasterMode.addItem ("Club -9", 2);
    aiMasterMode.addItem ("Warm -16", 3);
    addAndMakeVisible (aiMasterMode);
    aiMasterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.parameters(), "smartMasterMode", aiMasterMode);

    // AI-master depth (was hardwired at 0.8). Compact rotary — space in the master
    // panel is tight, so no textbox; the % shows on drag + in the tooltip.
    aiMasterAmount.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    aiMasterAmount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    aiMasterAmount.setPopupDisplayEnabled (true, true, this);
    aiMasterAmount.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    aiMasterAmount.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    aiMasterAmount.setTooltip ("AI master depth: how strongly it pushes toward the target (0 = off, 100% = full).");
    aiMasterAmount.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.7f));
    addAndMakeVisible (aiMasterAmount);
    aiMasterAmountAttachment = std::make_unique<SliderAttachment> (
        processorRef.parameters(), "smartMasterAmount", aiMasterAmount);

    // ── Rekordbox XML interop: export library + cues / import cue points ────
    importedCues = RekordboxIO::loadCueStore();
    setupButton (rbxExportButton);
    rbxExportButton.setTooltip ("Export the analyzed library + hot cues as rekordbox.xml");
    rbxExportButton.onClick = [this]
    {
        rbxChooser = std::make_unique<juce::FileChooser> ("Export rekordbox XML",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("rekordbox.xml"), "*.xml");
        rbxChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<MainPluginEditor> (this)]
            (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto dest = fc.getResult();
                if (dest == juce::File() || safe->libraryView == nullptr)
                    return;
                const auto entries = safe->libraryView->getAnalyzedEntries();
                const bool ok = RekordboxIO::exportXml (dest, entries,
                    [safe] (const juce::String& path, std::vector<RekordboxIO::Cue>& cues)
                    {
                        if (safe == nullptr) return;
                        for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // loaded decks: live cues
                            if (safe->processorRef.getDeckPath (d) == path)
                                for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
                                    if (const double s = safe->processorRef.getHotCue (d, c); s >= 0.0)
                                        cues.push_back ({ c, s });
                        if (cues.empty())                                          // else: previously imported
                            if (const auto found = safe->importedCues.find (path);
                                found != safe->importedCues.end())
                                cues = found->second;
                    });
                if (safe != nullptr)
                    safe->recordStatus.setText (
                        ok ? "Exported " + juce::String ((int) entries.size())
                                 + " tracks to " + dest.getFileName()
                           : "Rekordbox export failed",
                        juce::dontSendNotification);
            });
    };
    addAndMakeVisible (rbxExportButton);

    setupButton (rbxImportButton);
    rbxImportButton.setTooltip ("Import cue points / metadata from a rekordbox XML export");
    rbxImportButton.onClick = [this]
    {
        rbxChooser = std::make_unique<juce::FileChooser> ("Import rekordbox XML",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.xml");
        rbxChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe = juce::Component::SafePointer<MainPluginEditor> (this)]
            (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto src = fc.getResult();
                if (! src.existsAsFile())
                    return;
                int seen = 0;
                auto imported = RekordboxIO::importXml (src, &seen);
                for (auto& [path, cues] : imported)
                    safe->importedCues[path] = std::move (cues);
                RekordboxIO::saveCueStore (safe->importedCues);
                for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // apply to loaded decks now
                    safe->applyImportedCues (d);
                safe->recordStatus.setText (
                    "Imported cues for " + juce::String ((int) imported.size())
                        + " of " + juce::String (seen) + " tracks",
                    juce::dontSendNotification);
            });
    };
    addAndMakeVisible (rbxImportButton);

    mixerView.numChannels = processorRef.getNumChannels();
    mixerView.onClose    = [this] { fadeOverlay (mixerView, false); mixerButton.setToggleState (false, juce::dontSendNotification); };
    mixerView.getName    = [this] (int i) { return processorRef.getChannelName (i); };
    mixerView.getColour  = [] (int i) { return deckAccent (i); };
    mixerView.getSubtitle = [this] (int i)
    {
        switch (processorRef.getChannelType (i))
        {
            case ChannelType::trackDeck:      return juce::String ("TRACK DECK");
            case ChannelType::input:          return juce::String ("LIVE INPUT");
            case ChannelType::midiInstrument: return juce::String ("MIDI INSTRUMENT");
            case ChannelType::sampler:        return juce::String ("SAMPLER");
            case ChannelType::pluginHost:     return juce::String ("PLUGIN HOST");
        }
        return juce::String ("CHANNEL");
    };
    mixerView.getRoute = [this] (int i)
    {
        const int slot = processorRef.getChannelSlot (i);
        if (processorRef.getChannelType (i) != ChannelType::trackDeck
            || ! juce::isPositiveAndBelow (slot, MainPluginProcessor::numDecks))
            return juce::String ("CENTER");
        const int side = processorRef.getDeckCfSide (slot);
        return side == 0 ? juce::String ("A / C BUS")
                         : side == 2 ? juce::String ("B / D BUS")
                                     : juce::String ("CENTER");
    };
    mixerView.getPeak = [this] (int i) { return processorRef.getChannelActivity (i); };
    mixerView.getMasterPeak = [this] (int ch) { return processorRef.getMasterPeak (ch); };
    // Live lane mix: mute/solo/gain changes are applied to the running engine
    // immediately (TE audio lanes + MIDI synth lanes), not just at the next
    // schedule rebuild.
    arrangementView.onLaneMixChanged = [this]
    {
        for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
        {
            const float gain = arrangementView.laneAudible (lane) ? arrangementView.laneGainLinear (lane) : 0.0f;
            processorRef.setTeArrangementLaneGain (lane, gain);
            processorRef.setMidiLaneGain (lane, gain);
        }
    };
    mixerView.getGainDb = [this] (int i)
    {
        const int slot = processorRef.getChannelSlot (i);
        if (processorRef.getChannelType (i) == ChannelType::trackDeck
            && juce::isPositiveAndBelow (slot, MainPluginProcessor::numDecks))
            if (auto* value = processorRef.parameters().getRawParameterValue (
                    deckPrefix (slot) + "Volume"))
                return value->load();
        if (auto* input = dynamic_cast<InputBody*> (processorRef.getChannelBody (i)))
            return juce::Decibels::gainToDecibels (input->getInputGain(), -60.0f);
        if (auto* instrument = dynamic_cast<MidiInstrumentBody*> (
                processorRef.getChannelBody (i)))
            return juce::Decibels::gainToDecibels (instrument->getGain(), -60.0f);
        return 0.0f;
    };
    mixerView.setGainDb = [this] (int i, float db)
    {
        const int slot = processorRef.getChannelSlot (i);
        if (processorRef.getChannelType (i) == ChannelType::trackDeck
            && juce::isPositiveAndBelow (slot, MainPluginProcessor::numDecks))
        {
            if (auto* parameter = processorRef.parameters().getParameter (
                    deckPrefix (slot) + "Volume"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (db));
            return;
        }
        const float linear = juce::Decibels::decibelsToGain (db, -60.0f);
        if (processorRef.getChannelType (i) == ChannelType::input)
            processorRef.setInputGainDispatched (i, linear);
        else if (processorRef.getChannelType (i) == ChannelType::midiInstrument)
            processorRef.setInstGainDispatched (i, linear);
    };
    mixerView.isMuted    = [this] (int i) { return processorRef.isChannelMuted (i); };
    mixerView.isSoloed   = [this] (int i) { return processorRef.isChannelSoloed (i); };
    mixerView.toggleMute = [this] (int i) { processorRef.toggleChannelMute (i); };
    mixerView.toggleSolo = [this] (int i) { processorRef.toggleChannelSolo (i); };
    addChildComponent (mixerView);   // hidden until toggled

    setupButton (saveProjButton);
    saveProjButton.setTooltip ("Save the arrangement + piano-roll to a .stemdeck project");
    saveProjButton.onClick = [this] { saveProject(); };
    addAndMakeVisible (saveProjButton);
    setupButton (openProjButton);
    openProjButton.setTooltip ("Open a .stemdeck project");
    openProjButton.onClick = [this] { loadProject(); };
    addAndMakeVisible (openProjButton);

    setupButton (metronomeButton);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setTooltip ("Metronome click during arrangement playback");
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    metronomeButton.onClick = [this] { processorRef.setMetronomeEnabled (metronomeButton.getToggleState()); };
    addAndMakeVisible (metronomeButton);

    setupButton (talkoverButton);
    talkoverButton.setClickingTogglesState (true);
    talkoverButton.setTooltip ("Talkover ducking: auto-reduces deck volume when mic is active");
    talkoverButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5f6a).withAlpha (0.65f));
    talkoverButton.onClick = [this] { processorRef.setTalkoverEnabled (talkoverButton.getToggleState()); };
    addAndMakeVisible (talkoverButton);

    setupButton (overlapModeButton);
    overlapModeButton.setTooltip ("Clip overlap mode: Keep / Push / Replace");
    overlapModeButton.onClick = [this]
    {
        // Cycle Keep → Push → Replace → Keep
        const auto mode = arrangementView.getOverlapMode();
        if (mode == ArrangementRecorderView::OverlapMode::Keep)
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Push);
            overlapModeButton.setButtonText ("Push");
        }
        else if (mode == ArrangementRecorderView::OverlapMode::Push)
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Replace);
            overlapModeButton.setButtonText ("Repl");
        }
        else
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Keep);
            overlapModeButton.setButtonText ("Keep");
        }
    };
    addAndMakeVisible (overlapModeButton);

    // Launch-all: queue clip[0] of every occupied deck lane at next beat
    setupButton (launchAllButton);
    launchAllButton.setTooltip ("Queue every channel lane to launch at the next beat");
    launchAllButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d5c40));
    launchAllButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.92f));
    launchAllButton.onClick = [this] { arrangementView.triggerAllSlots(); };
    addAndMakeVisible (launchAllButton);

    // Wire follow-action popup request from ArrangementRecorderView
    arrangementView.onFollowActionRequest = [this] (int lane, int clip) { showFollowActionMenu (lane, clip); };

    arrangementView.onSeek = [this] (double s)
    {
        arrangementCursorSeconds = s;
        if (processorRef.isArrangementPlaying())
            processorRef.setArrangementPositionSeconds (s);
    };
    arrangementView.onLoopChanged = [this] (double s, double e, bool on)
    {
        arrangementLoopStart = s; arrangementLoopEnd = e; arrangementLoopEnabled = on;
        arrLoopButton.setToggleState (on, juce::dontSendNotification);
        processorRef.setArrangementLoop (s, e, on);
    };
    arrangementView.onStopDeckRequest = [this] (int lane)
    {
        if (lane >= 0 && lane < MainPluginProcessor::numDecks)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (lane) + "Play"))
                p->setValueNotifyingHost (0.0f);
    };
    arrangementView.onClearDeckRequest = [this] (int lane)
    {
        if (lane >= 0 && lane < MainPluginProcessor::numDecks)
            requestDeckClear (lane);
    };
    recordStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    recordStatus.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    recordStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (recordStatus);
    recordingHealthBar.onRecordRequested = [this] (int countInBars)
    {
        requestRecording (countInBars);
    };
    recordingHealthBar.onStopConfirmed = [this]
    {
        if (processorRef.isMasterRecording())
            toggleRecording();
    };
    recordingHealthBar.onUndo = [this]
    {
        if (arrangementView.canUndo())
            arrangementView.undo();
    };
    recordingHealthBar.onClearClip = [this]
    {
        processorRef.resetLiveLoudness();
        recordStatus.setText ("Clip hold cleared", juce::dontSendNotification);
    };
    addAndMakeVisible (recordingHealthBar);
    arrangementView.initialise (thumbnailFormatManager, thumbnailCache);
    arrangementView.setIdle ("Hit Record to capture your set across every channel lane.");
    addAndMakeVisible (arrangementView);

    crossfaderAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "crossfader", crossfader);
    crossfaderCurveAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), "crossfaderCurve", crossfaderCurve);
    crossfaderReverseAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "crossfaderReverse", crossfaderReverse);
    masterBpmAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "masterBpm", masterBpm);
    masterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "master", master);
    limiterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "limiterCeiling", limiter);
    hostSyncAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "hostSync", hostSync);
    beatSyncAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "beatSync", beatSync);
    micMonitorAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "micMonitor", micMonitor);
    liveMidiAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "liveInstrument", liveMidiButton);
    micGainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "micGain", micGain);

    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto& pad = pads[(size_t) i];
        pad.trigger.setButtonText ({});   // blank interior (mockup pads are unlabeled)
        setupButton (pad.trigger);
        // djay-Pro pads: spectrum colour per pad, drawn as a dark cell with a
        // glowing coloured border (LookAndFeel reads the "padColour" property;
        // the editor paints a soft halo behind each pad).
        {
            const auto pc = juce::Colour (padSpectrum ((int) i));
            pad.trigger.getProperties().set ("padColour", (int) pc.getARGB());
            pad.trigger.setColour (juce::TextButton::textColourOffId, pc.brighter (0.5f));
        }
        setupButton (pad.load);
        setupSlider (pad.gain, false, " dB");
        pad.trigger.onClick = [this, i]
        {
            processorRef.triggerPad (i);
            padFlashMs[(size_t) i] = juce::Time::getMillisecondCounterHiRes();
        };
        pad.load.onClick = [this, i] { openPadChooser (i); };
        pad.gainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "pad" + juce::String (i + 1) + "Gain", pad.gain);
        addAndMakeVisible (pad.trigger);
        addAndMakeVisible (pad.load);
        addAndMakeVisible (pad.gain);
    }

    startTimerHz (30);   // smooth meters, nav-underline slide, and pad flashes

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]
    {
        if (safeThis != nullptr)
        {
            safeThis->grabKeyboardFocus();
            safeThis->offerRecoveryAutosave();
            juce::Timer::callAfterDelay (900, [safeThis]
            {
                if (safeThis != nullptr
                    && juce::JUCEApplicationBase::isStandaloneApp())
                    safeThis->showOnboarding (false);
            });
        }
    });
}

bool MainPluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'K')
    {
        showCommandPalette();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'I')
    {
        showQuickInspector();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getKeyCode() >= '1' && key.getKeyCode() <= '4')
    {
        const PrimarySurface surfaces[] = { PrimarySurface::library, PrimarySurface::perform,
                                            PrimarySurface::mixer, PrimarySurface::arranger };
        selectPrimarySurface (surfaces[key.getKeyCode() - '1']);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S')
    {
        if (key.getModifiers().isShiftDown())
            createSessionSnapshot();
        else
            saveProject();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O')
    {
        if (key.getModifiers().isShiftDown())
            loadProject();
        else
            selectPrimarySurface (PrimarySurface::files);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
        {
            if (arrangementView.canRedo()) arrangementView.redo();
            else                           redoSessionAction();
        }
        else
        {
            if (arrangementView.canUndo()) arrangementView.undo();
            else                           undoSessionAction();
        }
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        if (arrangementView.canRedo()) arrangementView.redo();
        else                           redoSessionAction();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (arrangementView.getSelectedLane() >= 0)
        {
            // Shift+Delete resets the whole lane; plain Delete removes the
            // selected clip (a no-op on the last remaining clip in a lane).
            if (key.getModifiers().isShiftDown())
                arrangementView.resetSelectedClipEdits();
            else
                arrangementView.deleteSelectedClip();
            return true;
        }
    }

    // All-deck sync hotkeys: B = BPM sync, K = key sync (no modifiers; pads use 1-8).
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (key.getKeyCode() == 'B') { syncBpmAll(); return true; }
        if (key.getKeyCode() == 'K') { syncKeyAll(); return true; }
        if (key.getKeyCode() == 'F') { setFocusMode (! focusMode); return true; }
        if (key.getKeyCode() == 'L') { openDeckChooser (focusedDeck); return true; }
        if (key.getKeyCode() == 'R') { toggleRecording(); return true; }
    }

    const int padIndex = padIndexForKeyPress (key);
    if (juce::isPositiveAndBelow (padIndex, PadBank::numPads))
    {
        processorRef.triggerPad (padIndex);
        return true;
    }

    return false;
}

void MainPluginEditor::setupButton (juce::Button& button)
{
    using namespace sattari::brand;
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId,    juce::Colour (kPanelRaised));
    button.setColour (juce::TextButton::buttonOnColourId,  juce::Colour (kAccent).withAlpha (0.35f).withBrightness (0.55f));
    button.setColour (juce::TextButton::textColourOffId,   juce::Colour (kTextPrimary).withAlpha (0.9f));
    button.setColour (juce::TextButton::textColourOnId,    juce::Colour (kTextPrimary));
    button.setColour (juce::ToggleButton::textColourId,    juce::Colour (kTextPrimary).withAlpha (0.9f));
    button.setColour (juce::ToggleButton::tickColourId,    juce::Colour (kAccent));
    button.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (kHairline));
}

void MainPluginEditor::setupSlider (juce::Slider& slider, bool rotary, const juce::String& suffix)
{
    using namespace sattari::brand;
    slider.setWantsKeyboardFocus (false);
    slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
    slider.setNumDecimalPlacesToDisplay (1);   // clean, untruncated readouts
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (kAccent));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kHairline));
    slider.setColour (juce::Slider::trackColourId,               juce::Colour (kAccent));
    slider.setColour (juce::Slider::backgroundColourId,          juce::Colour (kPanelDeep));
    slider.setColour (juce::Slider::thumbColourId,               juce::Colour (kTextPrimary));
    slider.setColour (juce::Slider::textBoxTextColourId,         juce::Colour (kTextPrimary));
    slider.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

// Create/refresh a compact strip for every NON-TrackDeck channel and hide the rest.
// Deck channels keep their full stem panel; inputs/instruments get a per-type strip.
// Channel type picker (spec §6: "+" opens a type picker). Only the implemented
// body types are offered; the rest are later build-order steps.
void MainPluginEditor::showAddChannelMenu (juce::Component* target)
{
    std::array<bool, MainPluginProcessor::numDecks> deckSlotUsed {};
    for (int i = 0; i < processorRef.getNumChannels(); ++i)
    {
        const int slot = processorRef.getChannelSlot (i);
        if (processorRef.getChannelType (i) == ChannelType::trackDeck
            && juce::isPositiveAndBelow (slot, MainPluginProcessor::numDecks))
            deckSlotUsed[(size_t) slot] = true;
    }
    const bool hasFreeDeckSlot =
        std::find (deckSlotUsed.begin(), deckSlotUsed.end(), false)
            != deckSlotUsed.end();

    juce::PopupMenu m;
    m.addSectionHeader ("Add channel");
    m.addItem (1, "Track Deck", hasFreeDeckSlot);
    m.addItem (2, "Input (mic / line)");
    m.addItem (3, "MIDI Instrument");

    auto opts = juce::PopupMenu::Options();
    if (target != nullptr) opts = opts.withTargetComponent (target);
    else                   opts = opts.withTargetScreenArea (juce::Rectangle<int> (addChannelTileBounds).translated (getScreenX(), getScreenY()));

    m.showMenuAsync (opts,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            const auto type = r == 2 ? ChannelType::input
                            : r == 3 ? ChannelType::midiInstrument
                                     : ChannelType::trackDeck;
            safe->checkpointSessionUndo ("Add channel");
            if (safe->processorRef.addChannel (type) >= 0)
            {
                safe->refreshChannelStrips();
                safe->refreshChannelButtons();
                safe->resized();
                safe->repaint();
            }
        });
}

void MainPluginEditor::refreshChannelStrips()
{
    const int total = processorRef.getNumChannels();
    for (int i = 0; i < (int) channelStrips.size(); ++i)
    {
        const bool wanted = i < total && processorRef.getChannelType (i) != ChannelType::trackDeck;
        auto& strip = channelStrips[(size_t) i];

        if (! wanted)
        {
            if (strip != nullptr) { strip->setVisible (false); strip->setBounds ({}); }
            continue;
        }

        if (strip == nullptr)
        {
            strip = std::make_unique<stemdeck::ChannelStrip>();
            addAndMakeVisible (*strip);
            const int idx = i;
            strip->onRequestFocus = [this, idx] { setFocusedDeck (idx); };
            strip->onToggleMonitor = [this, idx]
            {
                if (auto* b = processorRef.getChannelBody (idx))
                    if (auto* in = dynamic_cast<InputBody*> (b))
                        in->setMonitorEnabled (! in->isMonitorEnabled());
            };
            strip->onGainChanged = [this, idx] (float gain)
            {
                if (auto* b = processorRef.getChannelBody (idx))
                {
                    if (auto* in = dynamic_cast<InputBody*> (b))               in->setInputGain (gain);
                    else if (auto* mi = dynamic_cast<MidiInstrumentBody*> (b)) mi->setGain (gain);
                }
            };
            strip->onEqChanged = [this, idx] (int band, float db)
            {
                auto* b = processorRef.getChannelBody (idx);
                if (b == nullptr) return;
                auto* in = dynamic_cast<InputBody*> (b);
                auto* mi = dynamic_cast<MidiInstrumentBody*> (b);
                if (band == 0)      { if (in) in->setLowDb (db);  else if (mi) mi->setLowDb (db); }
                else if (band == 1) { if (in) in->setMidDb (db);  else if (mi) mi->setMidDb (db); }
                else                { if (in) in->setHighDb (db); else if (mi) mi->setHighDb (db); }
            };
            strip->onFilterChanged = [this, idx] (float f)
            {
                if (auto* b = processorRef.getChannelBody (idx))
                {
                    if (auto* in = dynamic_cast<InputBody*> (b))               in->setFilter (f);
                    else if (auto* mi = dynamic_cast<MidiInstrumentBody*> (b)) mi->setFilter (f);
                }
            };
            // Mute/solo live on the Channel FRAME (not the body) so they mean the same
            // thing for every channel type and solo can be resolved mix-wide.
            strip->onRemove = [this, idx]
            {
                requestChannelRemoval (idx);
            };
            strip->onToggleMute = [this, idx] { processorRef.toggleChannelMute (idx); };
            strip->onToggleSolo = [this, idx] { processorRef.toggleChannelSolo (idx); };
            strip->onCycleSource = [this, idx]
            {
                auto* b = processorRef.getChannelBody (idx);
                if (b == nullptr) return;
                if (auto* in = dynamic_cast<InputBody*> (b))
                {
                    // -1 (auto) -> 0 -> 1 -> back to auto: the usual mono-source picks.
                    const int next = in->getSourceChannel() >= 1 ? -1 : in->getSourceChannel() + 1;
                    in->setSourceChannel (next);
                }
                else if (auto* mi = dynamic_cast<MidiInstrumentBody*> (b))
                    mi->setMidiChannel ((mi->getMidiChannel() + 1) % 17);   // 0 = omni, then 1..16
            };
        }

        stemdeck::ChannelStrip::State st;
        st.type    = processorRef.getChannelType (i);
        st.name    = processorRef.getChannelName (i);
        st.accent  = deckAccent (i % MainPluginProcessor::numDecks);
        st.focused = (i == focusedDeck);
        if (auto* b = processorRef.getChannelBody (i))
        {
            if (auto* in = dynamic_cast<InputBody*> (b))
            {
                st.gain = in->getInputGain();
                st.level = juce::jlimit (0.0f, 1.0f, in->getInputPeak());
                st.clipping = in->getInputPeak() > 0.99f;
                st.monitor = in->isMonitorEnabled();
                st.lowDb = in->getLowDb(); st.midDb = in->getMidDb(); st.highDb = in->getHighDb();
                st.filter = in->getFilter();
                st.sourceChannel = in->getSourceChannel();
                // Receded until this input is actually carrying something: an explicit
                // device channel was picked, or signal is arriving on the auto source.
                st.hasContent = in->getSourceChannel() >= 0 || in->getInputPeak() > 0.002f;
            }
            else if (auto* mi = dynamic_cast<MidiInstrumentBody*> (b))
            {
                st.gain = mi->getGain();
                st.active = mi->isActive();
                st.level = mi->isActive() ? 0.6f : 0.0f;
                st.midiChannel = mi->getMidiChannel();
                st.lowDb = mi->getLowDb(); st.midDb = mi->getMidDb(); st.highDb = mi->getHighDb();
                st.filter = mi->getFilter();
                st.hasContent = mi->hasReceivedMidi();
            }
        }
        st.muted  = processorRef.isChannelMuted (i);
        st.soloed = processorRef.isChannelSoloed (i);
        strip->setState (st);
        strip->setVisible (true);
    }
}

void MainPluginEditor::refreshChannelButtons()
{
    const int total = processorRef.getNumChannels();
    const int decks = processorRef.getNumTrackDecks();
    // The engine's parameter pool bounds adding; the editor only draws numDecks deck
    // strips, so extra TrackDecks beyond that exist in the engine but have no strip.
    addChannelButton.setEnabled (total < MainPluginProcessor::getMaxChannels());
    removeChannelButton.setEnabled (total > 1);
    // ONE noun. The model is "channel" (a channel is a track deck, an input, or a
    // MIDI instrument), so the buttons, the add-tile and the arranger lanes all say
    // channel. The old readout was "4D+2I", which is only legible if you already
    // know the architecture — the count is what a user actually wants, and the
    // breakdown belongs in the tooltip.
    addChannelButton.setButtonText ("+ CHANNEL");
    removeChannelButton.setButtonText (juce::String (total) + " CH");
    addChannelButton.setTooltip ("Add a channel — track deck, audio input, or MIDI instrument");
    removeChannelButton.setTooltip (
        juce::String (total) + " channels: " + juce::String (decks) + " track deck"
        + (decks == 1 ? "" : "s")
        + (total > decks ? ", " + juce::String (total - decks) + " input/instrument" : juce::String())
        + "  \xe2\x80\xa2  click to remove the last one");
}

void MainPluginEditor::setupComboBox (juce::ComboBox& comboBox)
{
    using namespace sattari::brand;
    comboBox.setWantsKeyboardFocus (false);
    comboBox.setColour (juce::ComboBox::backgroundColourId,    juce::Colour (kPanelRaised));
    comboBox.setColour (juce::ComboBox::textColourId,          juce::Colour (kTextPrimary).withAlpha (0.92f));
    comboBox.setColour (juce::ComboBox::outlineColourId,       juce::Colour (kHairline));
    comboBox.setColour (juce::ComboBox::arrowColourId,         juce::Colour (kAccent));
    comboBox.setColour (juce::PopupMenu::backgroundColourId,   juce::Colour (kPanelDeep));
    comboBox.setColour (juce::PopupMenu::textColourId,         juce::Colour (kTextPrimary));
    comboBox.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (kAccent).withAlpha (0.20f));
    comboBox.setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (kTextPrimary));
    // Style only — do NOT steal a combo that already belongs to a sub-panel. This
    // used to unconditionally addAndMakeVisible(), which reparented e.g. the FX
    // rack's chain selector onto the editor at (0,0), on top of the nav bar.
    if (comboBox.getParentComponent() == nullptr)
        addAndMakeVisible (comboBox);
}

void MainPluginEditor::setupDeck (DeckControls& controls, const juce::String& prefix, const juce::String& playText)
{
    juce::ignoreUnused (playText);
    setupButton (controls.load);
    setupButton (controls.loadStem);
    setupButton (controls.aiStems);
    setupButton (controls.play);
    controls.play.setLookAndFeel (&playIconLnF);   // triangle / pause icon, not a text toggle
    setupButton (controls.cue);
    setupComboBox (controls.stemLane);
    setupComboBox (controls.tempoInterpretation);
    setupComboBox (controls.syncMode);
    setupComboBox (controls.keyTarget);
    const auto stemLabels = processorRef.getStemLaneLabels();
    for (int i = 0; i < stemLabels.size(); ++i)
        controls.stemLane.addItem (stemLabels[i], i + 1);
    controls.stemLane.setSelectedId (DeckPlayer::fullMixStem + 1, juce::dontSendNotification);
    const auto tempoChoices = processorRef.getTempoInterpretationChoices();
    for (int i = 0; i < tempoChoices.size(); ++i)
        controls.tempoInterpretation.addItem (tempoChoices[i], i + 1);
    controls.tempoInterpretation.setSelectedId (1, juce::dontSendNotification);
    const auto syncChoices = processorRef.getSyncModeChoices();
    for (int i = 0; i < syncChoices.size(); ++i)
        controls.syncMode.addItem (syncChoices[i], i + 1);
    controls.syncMode.setSelectedId (1, juce::dontSendNotification);
    const auto keyTargets = processorRef.getKeyTargetChoices();
    for (int i = 0; i < keyTargets.size(); ++i)
        controls.keyTarget.addItem (keyTargets[i], i + 1);
    controls.keyTarget.setSelectedId (1, juce::dontSendNotification);
    controls.keyLock.setButtonText ("KeyLock");
    setupButton (controls.keyLock);
    controls.liveKey.setButtonText ("LiveKey");
    setupButton (controls.liveKey);
    controls.keyInfo.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    controls.keyInfo.setJustificationType (juce::Justification::centredLeft);
    controls.keyInfo.setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (controls.keyInfo);
    controls.stemStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    controls.stemStatus.setJustificationType (juce::Justification::centredLeft);
    controls.stemStatus.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    controls.stemStatus.setText ("AI stems: load a full mix", juce::dontSendNotification);
    addAndMakeVisible (controls.stemStatus);
    setupSlider (controls.bpm, false, " BPM");
    setupSlider (controls.beatOffset, false, " beat");
    setupSlider (controls.downbeat, false, " beat");
    setupSlider (controls.volume, false, " dB");
    setupSlider (controls.fader, false);
    setupSlider (controls.pitch, false, " st");
    setupSlider (controls.low, true, " dB");
    setupSlider (controls.mid, true, " dB");
    setupSlider (controls.high, true, " dB");
    setupSlider (controls.filter, true);

    controls.volume.setSliderStyle (juce::Slider::LinearVertical);
    controls.fader.setSliderStyle (juce::Slider::LinearVertical);
    controls.pitch.setSliderStyle (juce::Slider::LinearVertical);
    // djay-Pro feel: vertical faders read by position; values via tooltip on hover.
    controls.volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.fader .setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.pitch .setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.volume.setPopupDisplayEnabled (true, true, this);
    controls.fader .setPopupDisplayEnabled (true, true, this);
    controls.pitch .setPopupDisplayEnabled (true, true, this);
    // Clean EQ knobs — no inline value text; value shows on hover (matches stems/faders).
    for (auto* eq : { &controls.low, &controls.mid, &controls.high, &controls.filter })
    {
        eq->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        eq->setPopupDisplayEnabled (true, true, this);
    }
    controls.bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    controls.beatOffset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    controls.downbeat.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);

    addAndMakeVisible (controls.load);
    addAndMakeVisible (controls.loadStem);
    addAndMakeVisible (controls.aiStems);
    addAndMakeVisible (controls.play);
    addAndMakeVisible (controls.cue);
    addAndMakeVisible (controls.keyLock);
    addAndMakeVisible (controls.liveKey);

    controls.playAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "Play", controls.play);
    controls.keyLockAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "KeyLock", controls.keyLock);
    controls.liveKeyAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "LiveKey", controls.liveKey);
    controls.tempoInterpretationAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "TempoInterpretation", controls.tempoInterpretation);
    controls.syncModeAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "SyncMode", controls.syncMode);
    controls.keyTargetAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "KeyTarget", controls.keyTarget);
    controls.bpmAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Bpm", controls.bpm);
    controls.beatOffsetAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "BeatOffset", controls.beatOffset);
    controls.downbeatAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Downbeat", controls.downbeat);
    controls.volumeAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Volume", controls.volume);
    controls.faderAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Fader", controls.fader);
    controls.pitchAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Pitch", controls.pitch);
    controls.lowAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Low", controls.low);
    controls.midAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Mid", controls.mid);
    controls.highAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "High", controls.high);
    controls.filterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Filter", controls.filter);

    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
    {
        auto& knob = controls.stemGains[(size_t) lane];
        // StemGainKnob's ctor sets rotary / range 0..200 / unity-at-top / dbl-click 100.
        knob.setStemColour (stemLaneColour (lane));
        knob.setName (DeckPlayer::stemLaneLabel (lane));
        knob.setTooltip (DeckPlayer::stemLaneLabel (lane) + " stem gain (100 = unity, 200 = +6 dB)");
        addAndMakeVisible (knob);
        controls.stemGainAttachments[(size_t) lane] = std::make_unique<SliderAttachment> (
            processorRef.parameters(),
            prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
            knob);
    }
}

void MainPluginEditor::applyExtractedMelody (int deckIndex, const std::vector<MelodyExtractor::Note>& notes, bool neural)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    auto& dc = deckControls[(size_t) deckIndex];
    if (notes.empty())
    {
        dc.stemStatus.setText ("No melody found (needs a clear monophonic line)", juce::dontSendNotification);
        return;
    }

    // Seconds → beats at the current project tempo, then into a MIDI clip on
    // this deck's arrangement lane (editable in the piano roll).
    const double bpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
    const double beatsPerSec = bpm / 60.0;

    std::vector<ArrangementRecorderView::MidiNote> midiNotes;
    midiNotes.reserve (notes.size());
    for (const auto& n : notes)
        midiNotes.push_back ({ n.pitch,
                               n.startSeconds * beatsPerSec,
                               juce::jmax (0.125, n.lengthSeconds * beatsPerSec),
                               n.velocity });

    const int idx = arrangementView.addMidiClip (deckIndex, 0.0);
    arrangementView.setMidiClipNotes (deckIndex, idx, std::move (midiNotes));
    dc.stemStatus.setText (juce::String (neural ? "Neural: " : "YIN: ")
                             + juce::String ((int) notes.size()) + u8 (" notes \xe2\x86\x92 MIDI clip"),
                           juce::dontSendNotification);
}

void MainPluginEditor::applyImportedCues (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    const auto path = processorRef.getDeckPath (deckIndex);
    if (path.isEmpty())
        return;
    const auto found = importedCues.find (path);
    if (found == importedCues.end())
        return;
    for (const auto& cue : found->second)
        processorRef.setHotCue (deckIndex, cue.num, cue.seconds);
    refreshHotCueButtonColours (deckIndex);
    updateWaveformHotCues (deckIndex);
}

void MainPluginEditor::applyExtractedDrums (int deckIndex, const std::vector<MelodyExtractor::Note>& hits)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    auto& dc = deckControls[(size_t) deckIndex];
    if (hits.empty())
    {
        dc.stemStatus.setText ("No drum hits detected", juce::dontSendNotification);
        return;
    }

    const double bpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
    const double beatsPerSec = bpm / 60.0;

    int kicks = 0, snares = 0, hats = 0;
    std::vector<ArrangementRecorderView::MidiNote> midiNotes;
    midiNotes.reserve (hits.size());
    for (const auto& h : hits)
    {
        midiNotes.push_back ({ h.pitch, h.startSeconds * beatsPerSec, 0.25, h.velocity });
        if (h.pitch == 36) ++kicks; else if (h.pitch == 42) ++hats; else ++snares;
    }

    const int idx = arrangementView.addMidiClip (deckIndex, 0.0);
    arrangementView.setMidiClipNotes (deckIndex, idx, std::move (midiNotes));
    dc.stemStatus.setText ("Drums: " + juce::String (kicks) + " kick / " + juce::String (snares)
                             + " snare / " + juce::String (hats) + u8 (" hat \xe2\x86\x92 MIDI clip"),
                           juce::dontSendNotification);
}

void MainPluginEditor::openDeckStemChooser (int deckIndex, int stemLaneIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)
        || ! juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes))
        return;

    deckChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         deckIndex, stemLaneIndex] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safe->requestDeckStemLoad (deckIndex, stemLaneIndex, file);
        });
}

juce::String MainPluginEditor::supportedPattern() const
{
    return "*.wav;*.aif;*.aiff;*.flac;*.mp3";
}

void MainPluginEditor::openDeckChooser (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;

    deckChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         deckIndex] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safe->requestDeckLoad (deckIndex, file);
        });
}

void MainPluginEditor::requestDeckLoad (int deckIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)
        || ! file.existsAsFile())
        return;

    const auto current = processorRef.getDeckPath (deckIndex);
    const bool playing = processorRef.parameters().getRawParameterValue (
        deckPrefix (deckIndex) + "Play")->load() > 0.5f;
    const bool protectedDeck = current.isNotEmpty()
                            && (playing || processorRef.isMasterRecording());

    auto performLoad = [safe = juce::Component::SafePointer<MainPluginEditor> (this),
                        deckIndex, file]
    {
        if (safe == nullptr) return;
        if (auto* play = safe->processorRef.parameters().getParameter (
                deckPrefix (deckIndex) + "Play"))
            play->setValueNotifyingHost (0.0f);
        safe->setFocusedDeck (deckIndex);
        safe->processorRef.loadDeckFileAsync (deckIndex, file);
        if (auto* wave = safe->deckWaves[(size_t) deckIndex].get())
            wave->setFile (file);
    };

    if (! protectedDeck)
    {
        performLoad();
        return;
    }

    juce::PopupMenu confirm;
    confirm.addSectionHeader (processorRef.isMasterRecording()
        ? "This channel is part of the active take"
        : "This channel is currently playing");
    confirm.addItem (1, "Keep current channel");
    confirm.addItem (2, "Stop and load over it");
    confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [performLoad] (int result)
        {
            if (result == 2)
                performLoad();
        });
}

void MainPluginEditor::requestDeckStemLoad (int deckIndex, int stemLaneIndex,
                                            const juce::File& file)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)
        || ! juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes)
        || ! file.existsAsFile())
        return;

    const bool replacing = processorRef.getDeckStemPath (deckIndex, stemLaneIndex).isNotEmpty()
                        || (stemLaneIndex == DeckPlayer::fullMixStem
                            && processorRef.getDeckPath (deckIndex).isNotEmpty());
    const bool playing = processorRef.parameters().getRawParameterValue (
        deckPrefix (deckIndex) + "Play")->load() > 0.5f;
    auto performLoad = [safe = juce::Component::SafePointer<MainPluginEditor> (this),
                        deckIndex, stemLaneIndex, file]
    {
        if (safe == nullptr) return;
        safe->processorRef.loadDeckStemFileAsync (deckIndex, stemLaneIndex, file);
        if (auto* wave = safe->deckWaves[(size_t) deckIndex].get())
        {
            if (stemLaneIndex == DeckPlayer::fullMixStem)
                wave->setFile (file);
            else
                wave->setStemFile (stemLaneIndex, file);
        }
    };

    if (! replacing || (! playing && ! processorRef.isMasterRecording()))
    {
        performLoad();
        return;
    }

    juce::PopupMenu confirm;
    confirm.addSectionHeader (processorRef.isMasterRecording()
        ? "Replacing this source changes the active take"
        : "This channel is currently playing");
    confirm.addItem (1, "Keep current source");
    confirm.addItem (2, "Replace source");
    confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [performLoad] (int result) { if (result == 2) performLoad(); });
}

void MainPluginEditor::requestDeckClear (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;

    const bool playing = processorRef.parameters().getRawParameterValue (
        deckPrefix (deckIndex) + "Play")->load() > 0.5f;
    auto clear = [safe = juce::Component::SafePointer<MainPluginEditor> (this), deckIndex]
    {
        if (safe == nullptr) return;
        safe->checkpointSessionUndo ("Clear " + deckLabel (deckIndex));
        if (auto* play = safe->processorRef.parameters().getParameter (
                deckPrefix (deckIndex) + "Play"))
            play->setValueNotifyingHost (0.0f);
        safe->processorRef.clearDeck (deckIndex);
        if (auto* wave = safe->deckWaves[(size_t) deckIndex].get())
            wave->setFile (juce::File());
        safe->recordStatus.setText ("Cleared channel "
                                    + juce::String::charToString ((juce_wchar) ('A' + deckIndex)),
                                    juce::dontSendNotification);
    };

    if (! processorRef.isMasterRecording() && ! playing)
    {
        clear();
        return;
    }

    juce::PopupMenu confirm;
    confirm.addSectionHeader (processorRef.isMasterRecording()
        ? "Clearing now changes the active take"
        : "This channel is currently playing");
    confirm.addItem (1, "Keep channel");
    confirm.addItem (2, "Stop and clear channel");
    confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [clear] (int result) { if (result == 2) clear(); });
}

void MainPluginEditor::requestChannelRemoval (int channelIndex)
{
    if (! juce::isPositiveAndBelow (channelIndex, processorRef.getNumChannels()))
        return;
    if (processorRef.isMasterRecording())
    {
        recordStatus.setText ("Channel removal is locked while recording - stop and keep the take first",
                              juce::dontSendNotification);
        return;
    }

    auto remove = [safe = juce::Component::SafePointer<MainPluginEditor> (this), channelIndex]
    {
        if (safe == nullptr)
            return;
        safe->checkpointSessionUndo ("Remove " + safe->processorRef.getChannelName (channelIndex));
        if (! safe->processorRef.removeChannel (channelIndex))
            return;
        safe->refreshChannelStrips();
        safe->refreshChannelButtons();
        safe->resized();
        safe->repaint();
    };

    bool playing = false;
    if (processorRef.getChannelType (channelIndex) == ChannelType::trackDeck
        && channelIndex < MainPluginProcessor::numDecks)
        playing = processorRef.parameters().getRawParameterValue (
            deckPrefix (channelIndex) + "Play")->load() > 0.5f;
    if (! playing)
    {
        remove();
        return;
    }

    juce::PopupMenu confirm;
    confirm.addSectionHeader ("This channel is currently playing");
    confirm.addItem (1, "Keep channel");
    confirm.addItem (2, "Stop and remove channel");
    confirm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [remove] (int result) { if (result == 2) remove(); });
}

void MainPluginEditor::openPadChooser (int padIndex)
{
    padChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         padIndex] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safe->processorRef.loadPadFileAsync (padIndex, file);
        });
}

void MainPluginEditor::toggleRecording()
{
    if (processorRef.isMasterRecording())
    {
        const auto file = processorRef.stopMasterRecording();
        const auto arrangement = processorRef.getLastArrangementFile();
        auto savedMsg = file.existsAsFile()
                            ? "Saved: " + file.getFileName() + " + arrangement " + arrangement.getFileName()
                            : juce::String ("Recording stopped");

        // ── Live MIDI you played during the take -> piano roll, ready to edit ──
        // Captured in samples and converted to beats here, so the notes stay correct
        // if the tempo changes later (capture intent, not a baked rendering).
        {
            const double bpm = (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load();
            const auto captured = processorRef.getCapturedMidiNotes (bpm);
            if (! captured.empty())
            {
                std::vector<PianoRollView::Note> notes;
                notes.reserve (captured.size());
                for (const auto& c : captured)
                    notes.push_back ({ c.pitch, c.startBeats, c.lengthBeats, c.velocity });

                pianoRoll.setBpm (bpm > 1.0 ? bpm : 120.0);
                pianoRoll.setNotes (std::move (notes));
                savedMsg += " - captured " + juce::String ((int) captured.size()) + " MIDI notes";
                if (processorRef.didMidiCaptureOverflow())
                    savedMsg += " (capture buffer full - some events dropped)";
            }
        }

        recordStatus.setText (savedMsg, juce::dontSendNotification);
        loadLastArrangementIntoView (savedMsg);
        // Recording is the handoff boundary: put the completed take directly in
        // front of the artist, ready to trim, arrange, mix, and export.
        selectPrimarySurface (PrimarySurface::arranger);
        return;
    }

    requestRecording (2);
}

void MainPluginEditor::requestRecording (int countInBars)
{
    if (processorRef.isMasterRecording() || pendingRecordingStartMs > 0.0)
        return;

    // NOTE: do NOT clear the arrangement or disable Reveal/Reload/Bounce yet —
    // cancelling the save dialog must leave the current work untouched. Only wipe
    // once a destination is chosen and recording actually starts.
    recordChooser.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         countInBars] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
                return;   // cancelled — arrangement + buttons preserved

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            if (! safe->recordingPreflight (file))
                return;

            const int bars = juce::jlimit (0, 2, countInBars);
            if (bars > 0)
            {
                const double bpm = juce::jlimit (40.0, 240.0,
                    (double) safe->processorRef.parameters().getRawParameterValue ("masterBpm")->load());
                const double countInMs = (double) bars * 4.0 * 60000.0 / bpm;
                safe->pendingRecordingFile = file;
                safe->pendingRecordingStartMs = juce::Time::getMillisecondCounterHiRes() + countInMs;
                safe->restoreMetronomeAfterCountIn = safe->processorRef.isMetronomeEnabled();
                safe->processorRef.setMetronomeEnabled (true);
                safe->recordStatus.setText (
                    juce::String (bars) + "-bar count-in - take "
                        + juce::String (safe->processorRef.getCurrentTakeNumber() + 1)
                        + " starts next",
                    juce::dontSendNotification);
                return;
            }
            safe->startRecordingNow (file);
        });
}

bool MainPluginEditor::recordingPreflight (const juce::File& destination)
{
    const auto sampleRate = processorRef.getRecordingSampleRate();
    const auto blockSize = processorRef.getRecordingBlockSize();
    if (sampleRate <= 0.0 || blockSize <= 0)
    {
        recordStatus.setText (
            "Recording unavailable · check the audio device and try again",
            juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Audio Device Not Ready",
            "StemDeck does not have a valid sample rate and buffer size. "
            "Reconnect or configure the audio device before recording.");
        return false;
    }

    auto volume = destination.getParentDirectory();
    if (! volume.isDirectory())
        volume = volume.getParentDirectory();
    // A normal four-deck take writes master + raw input + clean and printed deck
    // lanes (roughly 170 MB/min at 48 kHz/24-bit stereo). Require enough headroom
    // for a useful set rather than allowing a take that will fail after two minutes.
    constexpr juce::int64 minimumFreeBytes = 3LL * 1024LL * 1024LL * 1024LL;
    const auto freeBytes = volume.getBytesFreeOnVolume();
    if (freeBytes >= 0 && freeBytes < minimumFreeBytes)
    {
        recordStatus.setText (
            "Recording blocked · less than 3 GB free at the destination",
            juce::dontSendNotification);
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Not Enough Recording Space",
            "Choose a destination with at least 3 GB free. StemDeck preserves a "
            "master, raw input, clean editable deck sources, printed safety lanes, "
            "MIDI, and intent data during a take.");
        return false;
    }

    if (processorRef.getAudioCpuLoad() > 0.90f)
        recordStatus.setText (
            "High CPU before take · consider increasing the audio buffer",
            juce::dontSendNotification);
    return true;
}

void MainPluginEditor::startRecordingNow (const juce::File& file)
{
    // Recording replaces the editable arrangement view. Capture a recoverable
    // restore point before the destructive transition.
    createSessionSnapshot();
    const bool started = processorRef.beginMasterRecording (file);
    recordStatus.setText (started ? "Recording: " + file.getFileName()
                                  : "Could not start recording", juce::dontSendNotification);
    if (! started)
        return;

    arrangementView.clearArrangement();
    revealRecordingButton.setEnabled (false);
    reloadIntoDecksButton.setEnabled (false);
    bounceEditsButton.setEnabled (false);
    arrangementView.setRecordingProgress (0.0, file.getFullPathName());
}

void MainPluginEditor::revealLastRecording()
{
    const auto masterRec = processorRef.getMasterRecordingTarget();
    const auto target = masterRec.existsAsFile() ? masterRec : processorRef.getLastArrangementFile();
    if (target.existsAsFile())
        target.revealToUser();
}

void MainPluginEditor::reloadLastRecordingIntoDecks()
{
    int loaded = 0;
    const auto& clips = arrangementView.getClips();
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto file = clips[(size_t) i].empty() ? juce::File() : clips[(size_t) i].front().file;
        if (! file.existsAsFile() || file.getSize() < 1024)
            file = processorRef.getLastDeckRecordingFile (i);
        if (! file.existsAsFile() || file.getSize() < 1024)
            continue;
        processorRef.loadDeckFileAsync (i, file);
        if (auto* wave = deckWaves[(size_t) i].get())
            wave->setFile (file);
        ++loaded;
    }
    recordStatus.setText (loaded > 0
                              ? "Reloaded " + juce::String (loaded) + " deck capture" + (loaded == 1 ? "" : "s") + " into decks A–D"
                              : u8 ("No deck captures to reload \xe2\x80\x94 record a set first"),
                          juce::dontSendNotification);
}

void MainPluginEditor::toggleArrangementPlayback()
{
    if (processorRef.isArrangementPlaying())
    {
        processorRef.stopArrangement();
        recordStatus.setText ("Arrangement stopped", juce::dontSendNotification);
        return;
    }

    // Build clip specs from the editable deck lanes (the master lane is their sum,
    // so we skip it to avoid doubling). Timeline position == the clip's recorded
    // region, so the edited multi-clip arrangement plays back in place.
    const auto& clips = arrangementView.getClips();
    std::vector<MainPluginProcessor::ArrangementClipSpec> specs;
    for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
    {
        if (! arrangementView.laneAudible (lane)) continue;   // honour per-lane mute/solo
        for (const auto& clip : clips[(size_t) lane])
        {
            if (! clip.file.existsAsFile() || clip.durationSeconds <= 0.0) continue;
            const double start = juce::jlimit (0.0, clip.durationSeconds, clip.trimStartSeconds);
            const double end   = juce::jlimit (start, clip.durationSeconds,
                                               clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
            if (end - start < 0.02) continue;

            MainPluginProcessor::ArrangementClipSpec spec;
            spec.file                 = clip.file;
            spec.timelineStartSeconds = clip.timelineStartSeconds;   // free-positioned
            spec.sourceStartSeconds   = start;
            spec.lengthSeconds        = end - start;
            spec.gain                 = arrangementView.laneGainLinear (lane);
            spec.fadeInSeconds        = clip.fadeInSeconds;
            spec.fadeOutSeconds       = clip.fadeOutSeconds;
            spec.lane                 = lane;
            if (const auto* automation =
                    arrangementView.editAutomationForLane (lane);
                automation != nullptr && automation->visible
                    && automation->target == 0)
                spec.gainAutomation = automation->points;
            if (const auto* dsp =
                    arrangementView.capturedDspForLane (lane);
                dsp != nullptr)
            {
                spec.lowAutomation = dsp->lowDb;
                spec.midAutomation = dsp->midDb;
                spec.highAutomation = dsp->highDb;
                spec.filterAutomation = dsp->filter;
            }
            specs.push_back (spec);
        }
    }

    // Piano-roll MIDI notes play through the built-in synth on the same transport.
    const double midiBpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());   // project tempo
    const double secsPerBeat = 60.0 / midiBpm;
    std::vector<MidiClipPlayer::SourceNote> midiNotes;
    double midiLenSec = 0.0;
    for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
    {
        if (lane < MainPluginProcessor::numDecks && ! arrangementView.laneAudible (lane)) continue;   // mute/solo
        for (int ci = 0; ci < arrangementView.numMidiClips (lane); ++ci)
        {
            const double clipStartBeats = arrangementView.midiClipStart (lane, ci) / secsPerBeat;
            for (const auto& n : arrangementView.midiClipNotes (lane, ci))
            {
                const double absStartBeats = clipStartBeats + n.startBeats;
                midiNotes.push_back ({ lane, n.pitch, absStartBeats, n.lengthBeats, n.velocity });
                midiLenSec = juce::jmax (midiLenSec, (absStartBeats + n.lengthBeats) * secsPerBeat);
            }
        }
    }
    // Per-lane MIDI gain (true gain stage on each lane's synth, independent of velocity).
    for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
        processorRef.setMidiLaneGain (lane, lane < MainPluginProcessor::numDecks
                                                ? arrangementView.laneGainLinear (lane) : 1.0f);

    double audioLenSec = 0.0;
    for (const auto& s : specs)
        audioLenSec = juce::jmax (audioLenSec, s.timelineStartSeconds + s.lengthSeconds);

    if (specs.empty() && midiNotes.empty())
    {
        recordStatus.setText (u8 ("Nothing to play \xe2\x80\x94 record, drop a file, or draw notes first"), juce::dontSendNotification);
        return;
    }

    processorRef.setMidiNotes (midiNotes, midiBpm);
    processorRef.setMetronomeBpm (midiBpm);
    processorRef.buildAndStartArrangement (std::move (specs),
                                           juce::jmax (audioLenSec, midiLenSec),
                                           arrangementCursorSeconds);
    processorRef.setArrangementLoop (arrangementLoopStart, arrangementLoopEnd, arrangementLoopEnabled);
    recordStatus.setText (u8 ("Playing arrangement\xe2\x80\xa6"), juce::dontSendNotification);
}

void MainPluginEditor::openPianoRollForClip (int lane, int clip)
{
    if (! juce::isPositiveAndBelow (lane, ArrangementRecorderView::numEditableLanes)) lane = 0;
    if (! juce::isPositiveAndBelow (clip, arrangementView.numMidiClips (lane)))
        clip = arrangementView.numMidiClips (lane) > 0 ? 0 : arrangementView.addMidiClip (lane, arrangementCursorSeconds);
    pianoRollLane = lane;
    pianoRollClip = clip;

    std::vector<PianoRollView::Note> notes;
    for (const auto& n : arrangementView.midiClipNotes (lane, clip))
        notes.push_back ({ n.pitch, n.startBeats, n.lengthBeats, n.velocity });
    pianoRoll.setNotes (std::move (notes));

    mixerButton.setToggleState (false, juce::dontSendNotification);
    fadeOverlay (mixerView, false);
    pianoRollButton.setToggleState (true, juce::dontSendNotification);
    fadeOverlay (pianoRoll, true);
    pianoRoll.toFront (true);
}

juce::ValueTree MainPluginEditor::makeProjectState() const
{
    juce::ValueTree root ("STEMDECK_PROJECT");
    root.setProperty ("projectVersion", kProjectVersion, nullptr);
    root.setProperty ("autosavedUtc", juce::Time::currentTimeMillis(), nullptr);
    const bool activeTake = processorRef.isMasterRecording();
    root.setProperty ("activeTake", activeTake, nullptr);
    root.setProperty ("takeNumber", processorRef.getCurrentTakeNumber(), nullptr);
    if (activeTake)
    {
        root.setProperty ("recordedSamples",
                          (double) processorRef.getRecordedSamples(), nullptr);
        root.setProperty ("recordingSampleRate",
                          processorRef.getRecordingSampleRate(), nullptr);
        juce::ValueTree media ("ACTIVE_TAKE_MEDIA");
        media.setProperty (
            "master",
            processorRef.getMasterRecordingTarget().getFullPathName(), nullptr);
        media.setProperty (
            "mic",
            processorRef.getMicRecordingTarget().getFullPathName(), nullptr);
        for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
        {
            juce::ValueTree lane ("LANE");
            lane.setProperty ("slot", deck, nullptr);
            lane.setProperty (
                "source",
                processorRef.getLastDeckRecordingFile (deck)
                    .getFullPathName(), nullptr);
            lane.setProperty (
                "printed",
                processorRef.getLastDeckPrintedRecordingFile (deck)
                    .getFullPathName(), nullptr);
            media.appendChild (lane, nullptr);
        }
        root.appendChild (media, nullptr);
    }

    juce::MemoryBlock processorState;
    processorRef.getStateInformation (processorState);
    root.setProperty ("processorState", processorState.toBase64Encoding(), nullptr);
    root.appendChild (arrangementView.toState(), nullptr);
    root.appendChild (pianoRoll.toState(), nullptr);
    return root;
}

void MainPluginEditor::restoreProjectState (const juce::ValueTree& root,
                                            const juce::String& sourceLabel,
                                            const juce::File& projectFile)
{
    auto resolvedRoot = root.createCopy();
    const int fileVersion =
        (int) resolvedRoot.getProperty ("projectVersion", 1);
    if (fileVersion > kProjectVersion)
    {
        recordStatus.setText ("Project was saved by a newer StemDeck (v"
                              + juce::String (fileVersion) + ") - not loaded",
                              juce::dontSendNotification);
        return;
    }

    // Portable package projects retain the original paths for diagnostics and map
    // them to files relative to Session.stemdeck at open time.
    juce::StringPairArray packagePaths;
    if (projectFile.existsAsFile())
    {
        const auto mediaMap =
            resolvedRoot.getChildWithName ("PACKAGE_MEDIA");
        for (int i = 0; i < mediaMap.getNumChildren(); ++i)
        {
            const auto entry = mediaMap.getChild (i);
            const auto original =
                entry.getProperty ("original").toString();
            const auto relative =
                entry.getProperty ("relative").toString();
            if (original.isNotEmpty() && relative.isNotEmpty())
                packagePaths.set (
                    original,
                    projectFile.getParentDirectory()
                        .getChildFile (relative).getFullPathName());
        }
    }

    auto rewriteProjectPaths = [&packagePaths] (
        auto&& self, juce::ValueTree node) -> void
    {
        for (int property = 0;
             property < node.getNumProperties(); ++property)
        {
            const auto name = node.getPropertyName (property);
            const auto current = node.getProperty (name).toString();
            if (packagePaths.containsKey (current))
                node.setProperty (
                    name, packagePaths.getValue (current, current), nullptr);
        }
        for (int child = 0; child < node.getNumChildren(); ++child)
            self (self, node.getChild (child));
    };
    rewriteProjectPaths (rewriteProjectPaths, resolvedRoot);

    const auto encoded =
        resolvedRoot.getProperty ("processorState").toString();
    if (encoded.isNotEmpty())
    {
        juce::MemoryBlock processorState;
        if (processorState.fromBase64Encoding (encoded) && processorState.getSize() > 0)
        {
            // Restoring the rack destroys/recreates plugin processors. Close native
            // editor windows first so none retain a reference to an old instance.
            closeAllPluginWindows();
            if (packagePaths.size() > 0)
                processorRef.remapStateMediaPaths (
                    processorState, packagePaths);
            processorRef.setStateInformation (processorState.getData(),
                                              (int) processorState.getSize());
        }
    }

    arrangementView.fromState (
        resolvedRoot.getChildWithName ("ARRANGEMENT"));
    pianoRoll.fromState (
        resolvedRoot.getChildWithName ("PIANOROLL"));
    refreshChannelStrips();
    refreshChannelButtons();
    resized();
    repaint();
    recordStatus.setText ("Recovered " + sourceLabel
                          + (encoded.isNotEmpty() ? "" : " (arrangement only - pre-v2 file)"),
                          juce::dontSendNotification);
}

juce::File MainPluginEditor::recoveryAutosaveFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Sattari Audio")
        .getChildFile ("StemDeck")
        .getChildFile ("Recovery")
        .getChildFile ("last-session.stemdeck");
}

juce::File MainPluginEditor::recoveryIntentFile()
{
    return recoveryAutosaveFile().getSiblingFile ("last-session-intent.sdev");
}

juce::File MainPluginEditor::sessionSnapshotsDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Sattari Audio")
        .getChildFile ("StemDeck")
        .getChildFile ("Snapshots");
}

void MainPluginEditor::createSessionSnapshot()
{
    auto xml = makeProjectState().createXml();
    if (xml == nullptr)
    {
        recordStatus.setText ("Could not create a session snapshot",
                              juce::dontSendNotification);
        return;
    }

    const auto text = xml->toString();
    const auto stamp = juce::Time::getCurrentTime().formatted (
        "%Y-%m-%d_%H-%M-%S");
    const auto directory = sessionSnapshotsDirectory();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    autosavePool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [text, directory, stamp, safe]() -> juce::ThreadPoolJob::JobStatus
        {
            directory.createDirectory();
            auto file = directory.getChildFile ("Snapshot_" + stamp + ".stemdeck");
            for (int copy = 2; file.existsAsFile(); ++copy)
                file = directory.getChildFile (
                    "Snapshot_" + stamp + "_" + juce::String (copy) + ".stemdeck");
            const bool ok = file.replaceWithText (text, false, false, "\n");

            // Keep a useful, bounded history rather than silently growing forever.
            juce::Array<juce::File> snapshots;
            directory.findChildFiles (
                snapshots, juce::File::findFiles, false, "Snapshot_*.stemdeck");
            std::sort (snapshots.begin(), snapshots.end(),
                       [] (const juce::File& a, const juce::File& b)
                       {
                           return a.getLastModificationTime() > b.getLastModificationTime();
                       });
            for (int i = 20; i < snapshots.size(); ++i)
                snapshots.getReference (i).deleteFile();

            juce::MessageManager::callAsync ([safe, ok, name = file.getFileNameWithoutExtension()]
            {
                if (safe != nullptr)
                    safe->recordStatus.setText (
                        ok ? "Created " + name : "Session snapshot failed",
                        juce::dontSendNotification);
            });
            return juce::ThreadPoolJob::jobHasFinished;
        }));
}

void MainPluginEditor::restoreLatestSnapshot()
{
    juce::Array<juce::File> snapshots;
    sessionSnapshotsDirectory().findChildFiles (
        snapshots, juce::File::findFiles, false, "Snapshot_*.stemdeck");
    std::sort (snapshots.begin(), snapshots.end(),
               [] (const juce::File& a, const juce::File& b)
               {
                   return a.getLastModificationTime() > b.getLastModificationTime();
               });
    if (snapshots.isEmpty())
    {
        recordStatus.setText ("No session snapshots yet", juce::dontSendNotification);
        return;
    }

    if (auto xml = juce::XmlDocument::parse (snapshots.getFirst()))
    {
        checkpointSessionUndo ("Restore latest snapshot");
        restoreProjectState (juce::ValueTree::fromXml (*xml),
                             snapshots.getFirst().getFileNameWithoutExtension());
    }
    else
        recordStatus.setText ("Latest snapshot could not be read",
                              juce::dontSendNotification);
}

void MainPluginEditor::repairMissingMedia()
{
    struct Target
    {
        enum class Kind { deck, stem, pad } kind = Kind::deck;
        int index = 0, lane = 0;
        juce::String missingPath;
    };

    std::vector<Target> missingTargets;
    const auto& state = processorRef.parameters().state;
    for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
    {
        const auto prefix = deckPrefix (deck);
        const auto full = state.getProperty (prefix + "File").toString();
        if (full.isNotEmpty() && ! juce::File (full).existsAsFile())
            missingTargets.push_back ({ Target::Kind::deck, deck, 0, full });
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto path = state.getProperty (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "File").toString();
            if (path.isNotEmpty() && ! juce::File (path).existsAsFile())
                missingTargets.push_back ({ Target::Kind::stem, deck, lane, path });
        }
    }
    for (int pad = 0; pad < PadBank::numPads; ++pad)
    {
        const auto path = state.getProperty ("pad" + juce::String (pad + 1) + "File").toString();
        if (path.isNotEmpty() && ! juce::File (path).existsAsFile())
            missingTargets.push_back ({ Target::Kind::pad, pad, 0, path });
    }

    if (missingTargets.empty())
    {
        recordStatus.setText ("All session media is linked", juce::dontSendNotification);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose a folder containing the missing audio",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         chooser, targetsToSearch = std::move (missingTargets)] (const juce::FileChooser& fc) mutable
        {
            if (safe == nullptr) return;
            const auto root = fc.getResult();
            if (! root.isDirectory()) return;
            safe->recordStatus.setText (
                "Searching for " + juce::String ((int) targetsToSearch.size()) + " missing files...",
                juce::dontSendNotification);

            safe->uiWorkPool.addJob (
                std::function<juce::ThreadPoolJob::JobStatus()> (
                [safe, root, searchTargets = std::move (targetsToSearch)]() mutable
                -> juce::ThreadPoolJob::JobStatus
                {
                    struct Match { Target target; juce::File file; };
                    std::vector<Match> resolved;
                    for (const auto& target : searchTargets)
                    {
                        juce::Array<juce::File> found;
                        root.findChildFiles (found, juce::File::findFiles, true,
                                             juce::File (target.missingPath).getFileName());
                        if (! found.isEmpty())
                            resolved.push_back ({ target, found.getFirst() });
                    }

                    juce::MessageManager::callAsync (
                        [safe, resolvedMatches = std::move (resolved),
                         wanted = (int) searchTargets.size()]() mutable
                        {
                            if (safe == nullptr) return;
                            if (! resolvedMatches.empty())
                                safe->checkpointSessionUndo ("Relink missing media");
                            for (const auto& match : resolvedMatches)
                            {
                                switch (match.target.kind)
                                {
                                    case Target::Kind::deck:
                                        safe->processorRef.loadDeckFileAsync (
                                            match.target.index, match.file);
                                        break;
                                    case Target::Kind::stem:
                                        safe->processorRef.loadDeckStemFileAsync (
                                            match.target.index, match.target.lane, match.file);
                                        break;
                                    case Target::Kind::pad:
                                        safe->processorRef.loadPadFileAsync (
                                            match.target.index, match.file);
                                        break;
                                }
                            }
                            safe->recordStatus.setText (
                                "Relinked " + juce::String ((int) resolvedMatches.size()) + " of "
                                    + juce::String (wanted) + " missing files",
                                juce::dontSendNotification);
                        });
                    return juce::ThreadPoolJob::jobHasFinished;
                }));
        });
}

void MainPluginEditor::scheduleRecoveryAutosave()
{
    if (! recoveryDecisionMade || autosaveInFlight->exchange (true))
        return;

    auto xml = makeProjectState().createXml();
    if (xml == nullptr)
    {
        autosaveInFlight->store (false);
        return;
    }

    const auto text = xml->toString();
    const auto file = recoveryAutosaveFile();
    const auto intentFile = recoveryIntentFile();
    const bool hasActiveTake = processorRef.isIntentCaptureActive();
    auto intentEvents = hasActiveTake ? processorRef.getIntentCaptureSnapshot()
                                      : std::vector<stemdeck::events::Event> {};
    stemdeck::events::Header intentHeader;
    intentHeader.sampleRate = processorRef.getRecordingSampleRate();
    intentHeader.createdUtc = juce::Time::currentTimeMillis();
    intentHeader.blockSize = (std::uint32_t) juce::jmax (1, processorRef.getRecordingBlockSize());
    const auto flag = autosaveInFlight;
    autosavePool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [file, intentFile, text, flag, hasActiveTake,
         intentHeader, intentEvents = std::move (intentEvents)]() -> juce::ThreadPoolJob::JobStatus
        {
            auto parent = file.getParentDirectory();
            if (! parent.exists())
                parent.createDirectory();
            file.replaceWithText (text, false, false, "\n");
            // A project snapshot alone is not enough during a live take: it knows
            // the current mixer state, but not the sequence of performed actions.
            // Checkpoint the deterministic stream beside it every five seconds so
            // a crash loses at most one autosave interval of intent.
            if (hasActiveTake)
                stemdeck::events::Writer::write (intentFile, intentHeader, intentEvents);
            else
                intentFile.deleteFile();   // never pair a newer idle snapshot with an old take
            flag->store (false);
            return juce::ThreadPoolJob::jobHasFinished;
        }));
}

void MainPluginEditor::offerRecoveryAutosave()
{
    const auto file = recoveryAutosaveFile();
    if (! file.existsAsFile())
    {
        recoveryDecisionMade = true;
        return;
    }

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        recoveryDecisionMade = true;
        return;
    }
    const auto snapshot = juce::ValueTree::fromXml (*xml);
    const bool interruptedTake =
        (bool) snapshot.getProperty ("activeTake", false);
    const auto activeMedia =
        snapshot.getChildWithName ("ACTIVE_TAKE_MEDIA");
    int recoverableMediaFiles = 0;
    if (activeMedia.isValid())
    {
        auto countFile = [&recoverableMediaFiles] (const juce::var& path)
        {
            if (juce::File (path.toString()).existsAsFile())
                ++recoverableMediaFiles;
        };
        countFile (activeMedia.getProperty ("master"));
        countFile (activeMedia.getProperty ("mic"));
        for (int lane = 0; lane < activeMedia.getNumChildren(); ++lane)
        {
            countFile (activeMedia.getChild (lane).getProperty ("source"));
            countFile (activeMedia.getChild (lane).getProperty ("printed"));
        }
    }
    const auto intentFile = recoveryIntentFile();
    int checkpointEvents = 0;
    if (intentFile.existsAsFile())
    {
        stemdeck::events::Header checkpointHeader;
        std::vector<stemdeck::events::Event> checkpoint;
        if (stemdeck::events::Reader::read (intentFile, checkpointHeader, checkpoint)
            == stemdeck::events::LoadResult::ok)
            checkpointEvents = (int) checkpoint.size();
    }

    juce::PopupMenu recovery;
    recovery.addSectionHeader ("A recoverable StemDeck session was found");
    if (interruptedTake)
        recovery.addSectionHeader (
            "Interrupted take: " + juce::String (recoverableMediaFiles)
            + " partial audio file"
            + (recoverableMediaFiles == 1 ? juce::String()
                                          : juce::String ("s")));
    recovery.addItem (1, checkpointEvents > 0
                           ? "Recover session + " + juce::String (checkpointEvents) + " captured actions"
                           : "Recover autosaved session");
    recovery.addItem (2, "Start fresh");
    recovery.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         snapshot, file, intentFile, checkpointEvents,
         interruptedTake, recoverableMediaFiles] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1)
            {
                safe->restoreProjectState (snapshot, "autosaved session");
                if (checkpointEvents > 0)
                    safe->recordStatus.setText (
                        "Recovered session; preserved " + juce::String (checkpointEvents)
                        + " deterministic actions in " + intentFile.getFileName(),
                        juce::dontSendNotification);
                else if (interruptedTake)
                    safe->recordStatus.setText (
                        "Recovered interrupted take with "
                        + juce::String (recoverableMediaFiles)
                        + " partial audio files preserved",
                        juce::dontSendNotification);
            }
            else if (result == 2)
            {
                file.deleteFile();
                intentFile.deleteFile();
            }
            safe->recoveryDecisionMade = true;
        });
}

void MainPluginEditor::saveProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), chooser]
        (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension ("stemdeck");
            const auto root = safe->makeProjectState();
            if (auto xml = root.createXml())
                xml->writeTo (f);
            safe->recordStatus.setText ("Saved project: " + f.getFileName(),
                                        juce::dontSendNotification);
        });
}

void MainPluginEditor::loadProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Open StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), chooser]
        (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
            {
                safe->checkpointSessionUndo ("Open project");
                safe->restoreProjectState (juce::ValueTree::fromXml (*xml),
                                           "project: " + f.getFileName(), f);
            }
        });
}

void MainPluginEditor::bounceArrangementEdits()
{
    const auto& clips = arrangementView.getClips();
    int rendered = 0;
    juce::String errors;
    auto laneName = [] (int i) -> juce::String
    {
        return i == ArrangementRecorderView::masterLane
                   ? juce::String ("Master")
                   : "Deck " + juce::String::charToString ((juce_wchar) ('A' + i));
    };
    for (int i = 0; i < ArrangementRecorderView::numEditableLanes; ++i)
    {
        const auto& laneClips = clips[(size_t) i];
        if (laneClips.empty() || ! laneClips.front().file.existsAsFile() || laneClips.front().durationSeconds <= 0.0)
            continue;

        const juce::File sourceFile = laneClips.front().file;

        // Decide whether this lane needs flattening: more than one clip (splits /
        // deletes), or a single clip that has been trimmed / faded.
        bool needsBounce = laneClips.size() > 1;
        double laneTimelineEnd = 0.0;
        for (const auto& clip : laneClips)
        {
            const double endSec = clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds;
            laneTimelineEnd = juce::jmax (laneTimelineEnd, clip.timelineEndSeconds());
            const double fadeIn  = juce::jlimit (0.0, endSec - clip.trimStartSeconds, clip.fadeInSeconds);
            const double fadeOut = juce::jlimit (0.0, endSec - clip.trimStartSeconds - fadeIn, clip.fadeOutSeconds);
            if (clip.trimStartSeconds > 0.001 || endSec < clip.durationSeconds - 0.001
                || std::abs (clip.timelineStartSeconds - clip.trimStartSeconds) > 0.001
                || fadeIn > 0.001 || fadeOut > 0.001)
                needsBounce = true;
        }
        if (! needsBounce || laneTimelineEnd < 0.05)
            continue;

        // A lane may contain clips from unrelated files and sample rates. Pick one
        // output format, then open and render EACH clip's own source below.
        double sr = 0.0;
        int channels = 0;
        for (const auto& clip : laneClips)
            if (std::unique_ptr<juce::AudioFormatReader> probe {
                    thumbnailFormatManager.createReaderFor (clip.file) })
            {
                if (sr <= 0.0 && probe->sampleRate > 0.0)
                    sr = probe->sampleRate;
                channels = juce::jmax (
                    channels, (int) juce::jmax (1u, probe->numChannels));
            }
        if (sr <= 0.0 || channels <= 0)
        {
            errors << laneName (i) << ": read failed. ";
            continue;
        }
        channels = juce::jlimit (1, 8, channels);
        const int totalSamples = juce::jmax (1, (int) std::round (laneTimelineEnd * sr));

        // Flattened lane timeline: gaps are silence, each clip placed at its
        // source/timeline position with its fades applied.
        juce::AudioBuffer<float> out (channels, totalSamples);
        out.clear();

        bool ok = true;
        for (const auto& clip : laneClips)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (
                thumbnailFormatManager.createReaderFor (clip.file));
            if (reader == nullptr || reader->sampleRate <= 0.0)
            {
                ok = false;
                break;
            }

            const double sourceSr = reader->sampleRate;
            const double sourceDuration =
                (double) reader->lengthInSamples / sourceSr;
            const double startSec = juce::jlimit (
                0.0, sourceDuration, clip.trimStartSeconds);
            const double endSec = juce::jlimit (
                startSec, sourceDuration,
                clip.trimEndSeconds > 0.0
                    ? clip.trimEndSeconds : sourceDuration);
            const int sourceSamples = (int) std::round (
                (endSec - startSec) * sourceSr);
            const int renderedSamples = (int) std::round (
                (endSec - startSec) * sr);
            if (sourceSamples < 1 || renderedSamples < 1) continue;

            const int sourceChannels =
                (int) juce::jmax (1u, reader->numChannels);
            juce::AudioBuffer<float> region (
                sourceChannels, sourceSamples + 2);
            region.clear();
            if (! reader->read (
                    &region, 0, sourceSamples,
                    (juce::int64) std::round (startSec * sourceSr),
                    true, true))
            {
                ok = false;
                break;
            }

            const double regionLen = endSec - startSec;
            const double fadeIn  = juce::jlimit (0.0, regionLen, clip.fadeInSeconds);
            const double fadeOut = juce::jlimit (0.0, regionLen - fadeIn, clip.fadeOutSeconds);
            const int destOffset = juce::jlimit (0, totalSamples, (int) std::round (clip.timelineStartSeconds * sr));
            const int copyLen = juce::jmin (
                renderedSamples, totalSamples - destOffset);
            const double sourcePerOutput = sourceSr / sr;
            for (int sample = 0; sample < copyLen; ++sample)
            {
                const double sourcePosition =
                    juce::jmin ((double) sourceSamples - 1.0,
                                (double) sample * sourcePerOutput);
                const int s0 = (int) sourcePosition;
                const int s1 = juce::jmin (sourceSamples - 1, s0 + 1);
                const float frac =
                    (float) (sourcePosition - (double) s0);
                const double time = (double) sample / sr;
                float fade = 1.0f;
                if (fadeIn > 0.0 && time < fadeIn)
                    fade *= (float) (time / fadeIn);
                if (fadeOut > 0.0 && time > regionLen - fadeOut)
                    fade *= (float) juce::jlimit (
                        0.0, 1.0, (regionLen - time) / fadeOut);

                for (int ch = 0; ch < channels; ++ch)
                {
                    const int sourceCh =
                        juce::jmin (ch, sourceChannels - 1);
                    const float* data = region.getReadPointer (sourceCh);
                    const float value =
                        data[s0] + frac * (data[s1] - data[s0]);
                    out.addSample (ch, destOffset + sample,
                                   value * fade);
                }
            }
        }
        if (! ok)
        {
            errors << laneName (i) << ": region read failed. ";
            continue;
        }

        auto destFile = sourceFile.getParentDirectory()
                           .getChildFile (sourceFile.getFileNameWithoutExtension() + "-edited")
                           .withFileExtension ("wav");
        destFile.deleteFile();

        if (auto outStream = destFile.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (outStream.get(), sr, (unsigned int) channels, 24, {}, 0));
            if (writer != nullptr)
            {
                outStream.release();
                if (writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
                {
                    writer.reset();
                    arrangementView.updateClipAfterBounce (i, destFile, laneTimelineEnd);
                    ++rendered;
                    continue;
                }
            }
        }
        errors << laneName (i) << ": write failed. ";
    }

    if (rendered > 0)
        recordStatus.setText ("Bounced " + juce::String (rendered) + " edited lane" + (rendered == 1 ? "" : "s") + u8 (" \xe2\x80\x94 Reload to Decks uses the new versions"),
                              juce::dontSendNotification);
    else
        recordStatus.setText (errors.isNotEmpty() ? errors : u8 ("No edits to bounce \xe2\x80\x94 drag a clip edge or fade handle first"),
                              juce::dontSendNotification);
}

int MainPluginEditor::parseDeckIndexFromParamID (const juce::String& id)
{
    if (! id.startsWith ("deck") || id.length() < 5) return -1;
    const auto ch = id[4];
    if (ch >= 'A' && ch < 'A' + MainPluginProcessor::numDecks)
        return ch - 'A';
    return -1;
}

int MainPluginEditor::parseStemLaneFromParamID (const juce::String& id)
{
    const auto stemPos = id.indexOf ("Stem");
    if (stemPos < 0) return -1;
    const auto rest = id.substring (stemPos + 4);
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        if (rest.startsWith (DeckPlayer::stemLaneId (lane)))
            return lane;
    return -1;
}

void MainPluginEditor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (stemLinkMirroring) return;
    if (! stemLinkToggle.getToggleState()) return;
    const int srcDeck = parseDeckIndexFromParamID (parameterID);
    const int laneIdx = parseStemLaneFromParamID (parameterID);
    if (srcDeck < 0 || laneIdx < 0) return;
    const juce::ScopedValueSetter<bool> guard (stemLinkMirroring, true);
    auto& params = processorRef.parameters();
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        if (d == srcDeck) continue;
        const auto id = deckPrefix (d) + "Stem" + DeckPlayer::stemLaneId (laneIdx) + "Gain";
        if (auto* p = params.getParameter (id))
        {
            const auto range = params.getParameterRange (id);
            p->setValueNotifyingHost (range.convertTo0to1 (newValue));
        }
    }
}

void MainPluginEditor::syncAllDecksTo (int sourceDeckIndex)
{
    if (! juce::isPositiveAndBelow (sourceDeckIndex, MainPluginProcessor::numDecks))
        return;
    auto& params = processorRef.parameters();
    const auto src = deckPrefix (sourceDeckIndex);
    const std::array<const char*, 4> floatParams { "Bpm", "BeatOffset", "Downbeat", "KeyTarget" };
    for (int dst = 0; dst < MainPluginProcessor::numDecks; ++dst)
    {
        if (dst == sourceDeckIndex) continue;
        const auto dstPrefix = deckPrefix (dst);
        for (const auto* paramName : floatParams)
        {
            if (auto* srcParam = params.getParameter (src + paramName))
                if (auto* dstParam = params.getParameter (dstPrefix + paramName))
                    dstParam->setValueNotifyingHost (srcParam->getValue());
        }
    }
    recordStatus.setText ("Synced decks to Deck " + juce::String::charToString ((juce_wchar) ('A' + sourceDeckIndex))
                          + " (BPM + grid + key target)",
                          juce::dontSendNotification);
}

void MainPluginEditor::showSyncAllDecksMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Snap all other decks to:");
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto path = processorRef.getDeckPath (i);
        const auto label = "Deck " + juce::String::charToString ((juce_wchar) ('A' + i))
                         + (path.isNotEmpty() ? juce::String (" (") + juce::File (path).getFileName() + ")"
                                              : juce::String ("  (empty)"));
        menu.addItem (i + 1, label, path.isNotEmpty());
    }
    const juce::Component::SafePointer<MainPluginEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&syncAllDecksButton),
        [safeThis] (int choice)
        {
            if (safeThis != nullptr && choice > 0)
                safeThis->syncAllDecksTo (choice - 1);
        });
}

void MainPluginEditor::loadLastArrangementIntoView (juce::String statusLine)
{
    const auto masterRec = processorRef.getMasterRecordingTarget();
    const auto canonicalArrangement = processorRef.getLastArrangementFile();
    std::array<juce::File, MainPluginProcessor::numDecks> decks;
    bool anyDeck = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        decks[(size_t) i] = processorRef.getLastDeckRecordingFile (i);
        if (decks[(size_t) i].existsAsFile() && decks[(size_t) i].getSize() > 1024)
            anyDeck = true;
    }

    if (masterRec.existsAsFile() || anyDeck)
    {
        const bool loadedCanonical =
            arrangementView.setArrangementFromCapture (
                canonicalArrangement, statusLine);
        if (! loadedCanonical)
            arrangementView.setArrangement (
                masterRec, decks, std::move (statusLine));
        revealRecordingButton.setEnabled (true);
        reloadIntoDecksButton.setEnabled (anyDeck);
        bounceEditsButton.setEnabled (anyDeck);

        // Canonical arrangement loading already imports all automation lanes and
        // promotes the captured channel fader into editable automation. The legacy
        // event-log parser is retained only for older takes.
        if (loadedCanonical)
            return;

        const auto eventLog = processorRef.getLastPerformanceEventLogFile();
        if (eventLog.existsAsFile())
        {
            std::array<ArrangementRecorderView::DeckAutomation, MainPluginProcessor::numDecks> automation;
            const auto json = juce::JSON::parse (eventLog);
            if (const auto* events = json.getProperty ("events", {}).getArray())
            {
                struct CurveSpec { const char* param; const char* label; juce::Colour colour; float lo, hi; };
                const std::array<CurveSpec, 3> specs {
                    CurveSpec { "volume", "VOL",    juce::Colour (0xff67f6d2), -60.0f,  6.0f },
                    CurveSpec { "pitch",  "PITCH",  juce::Colour (0xffff7a8d), -12.0f, 12.0f },
                    CurveSpec { "filter", "FILTER", juce::Colour (0xffffcf4a),  -1.0f,  1.0f }
                };

                for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
                {
                    const auto deckName = "Deck " + juce::String::charToString ((juce_wchar) ('A' + d));
                    std::array<ArrangementRecorderView::AutomationCurve, 3> curves;
                    for (size_t k = 0; k < specs.size(); ++k)
                    {
                        curves[k].label = specs[k].label;
                        curves[k].colour = specs[k].colour;
                        curves[k].minValue = specs[k].lo;
                        curves[k].maxValue = specs[k].hi;
                    }
                    for (const auto& v : *events)
                    {
                        if (v["deck"].toString() != deckName) continue;
                        const auto parameter = v["parameter"].toString();
                        const auto t = (double) v["timeSeconds"];
                        for (size_t k = 0; k < specs.size(); ++k)
                        {
                            if (parameter == specs[k].param)
                            {
                                const auto value = (float) v["value"].toString().getDoubleValue();
                                curves[k].points.emplace_back (t, value);
                                break;
                            }
                        }
                    }
                    for (auto& c : curves)
                        if (! c.points.empty())
                            automation[(size_t) d].push_back (std::move (c));
                }
            }
            arrangementView.setDeckAutomation (std::move (automation));
        }
    }
    else
    {
        arrangementView.setIdle (u8 ("Recording produced no files \xe2\x80\x94 check the destination folder."));
    }
}

void MainPluginEditor::refreshHostPluginChoices()
{
    knownHostPlugins = processorRef.getKnownHostPlugins();
    pluginSelector.clear (juce::dontSendNotification);
    for (int i = 0; i < knownHostPlugins.size(); ++i)
    {
        const auto& plugin = knownHostPlugins.getReference (i);
        pluginSelector.addItem (plugin.name + " [" + plugin.pluginFormatName + "]", i + 1);
    }
    if (knownHostPlugins.size() > 0)
        pluginSelector.setSelectedId (1, juce::dontSendNotification);
    else
        pluginSelector.setText ("No scanned FX", juce::dontSendNotification);
}

void MainPluginEditor::runPluginHostScan()
{
    pluginHostMessage.setText (u8 ("Scanning FX defaults\xe2\x80\xa6"), juce::dontSendNotification);
    scanPluginsButton.setEnabled (false);

    uiWorkPool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]()
        -> juce::ThreadPoolJob::JobStatus
    {
        if (safeThis == nullptr)
            return juce::ThreadPoolJob::jobHasFinished;

        // Sattari-only. A full third-party scan crashes the host (Serato Sample calls
        // main-thread-only Text Input Services from the scan thread → SIGTRAP).
        auto result = safeThis->processorRef.scanSattariPlugins();
        juce::MessageManager::callAsync ([safeThis, result]() mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->scanPluginsButton.setEnabled (true);
            safeThis->refreshHostPluginChoices();
            juce::String message = "Scanned " + result.scannedFormats.joinIntoString (", ")
                                 + u8 (" \xe2\x80\xa2 known FX: ") + juce::String (result.plugins.size());
            if (! result.blacklistedFiles.isEmpty())
                message += u8 (" \xe2\x80\xa2 blacklisted: ") + juce::String (result.blacklistedFiles.size());
            safeThis->pluginHostMessage.setText (message, juce::dontSendNotification);
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

void MainPluginEditor::loadSelectedHostPlugin()
{
    const int chainIndex = pluginChainSelector.getSelectedId() - 1;
    const int pluginIndex = pluginSelector.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (pluginIndex, knownHostPlugins.size()))
    {
        pluginHostMessage.setText ("Pick an FX plugin first", juce::dontSendNotification);
        return;
    }

    loadPluginButton.setEnabled (false);
    pluginHostMessage.setText ("Loading " + pluginSelector.getText() + u8 (" \xe2\x86\x92 ") + pluginChainSelector.getText(), juce::dontSendNotification);
    processorRef.loadHostPluginToChainAsync (chainIndex, pluginIndex,
        [safeThis = juce::Component::SafePointer<MainPluginEditor> (this)] (bool success, juce::String message)
        {
            juce::MessageManager::callAsync ([safeThis, success, message]
            {
                if (safeThis == nullptr)
                    return;
                safeThis->loadPluginButton.setEnabled (true);
                safeThis->pluginHostMessage.setText ((success ? "Loaded: " : "FX load failed: ") + message, juce::dontSendNotification);
            });
        });
}

void MainPluginEditor::showSattariFxMenu()
{
    if (knownHostPlugins.isEmpty())
    {
        runPluginHostScan();
        pluginHostMessage.setText ("Scanning plugins\xe2\x80\xa6 tap SATTARI FX again once done", juce::dontSendNotification);
        return;
    }

    // Curate the Sattari suite from the scanned plugins (name or manufacturer).
    struct Entry { int index; juce::String name; };
    std::vector<Entry> suite;
    for (int i = 0; i < knownHostPlugins.size(); ++i)
    {
        const auto& d = knownHostPlugins.getReference (i);
        if (d.name.startsWithIgnoreCase ("Sattari") || d.manufacturerName.containsIgnoreCase ("Sattari")
            || d.name.equalsIgnoreCase ("AutoPitch") || d.name.containsIgnoreCase ("Morph"))
            suite.push_back ({ i, d.name });
    }
    if (suite.empty())
    {
        pluginHostMessage.setText ("No Sattari plugins found \xe2\x80\x94 check your VST3 folder", juce::dontSendNotification);
        return;
    }

    const int chainIndex = juce::jmax (0, pluginChainSelector.getSelectedId() - 1);
    const auto chainName = pluginChainSelector.getText().isNotEmpty() ? pluginChainSelector.getText()
                                                                      : juce::String ("Deck A");

    juce::PopupMenu menu;
    menu.addSectionHeader ("Sattari FX  \xe2\x86\x92  " + chainName);
    for (size_t k = 0; k < suite.size(); ++k)
        menu.addItem ((int) k + 1, suite[k].name);
    menu.addSeparator();
    menu.addItem (9000, "Clear " + chainName + " FX");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sattariFxButton),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), suite, chainIndex, chainName] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 9000)
            {
                safe->closeAllPluginWindows();   // clearing destroys the processors
                safe->processorRef.clearHostPluginChain (chainIndex);
                safe->pluginHostMessage.setText ("Cleared " + chainName + " FX", juce::dontSendNotification);
                return;
            }
            const auto& e = suite[(size_t) (r - 1)];
            safe->pluginHostMessage.setText ("Loading " + e.name + " \xe2\x86\x92 " + chainName, juce::dontSendNotification);
            safe->processorRef.loadHostPluginToChainAsync (chainIndex, e.index,
                [safe, name = e.name, chainName] (bool ok, juce::String msg)
                {
                    juce::MessageManager::callAsync ([safe, ok, name, chainName, msg]
                    {
                        if (safe == nullptr) return;
                        safe->pluginHostMessage.setText (ok ? "Loaded " + name + " on " + chainName
                                                            : "FX load failed: " + msg, juce::dontSendNotification);
                    });
                });
        });
}

void MainPluginEditor::addSattariFxToChain (int chainIndex)
{
    struct Entry { int index; juce::String name; };
    std::vector<Entry> suite;
    for (int i = 0; i < knownHostPlugins.size(); ++i)
    {
        const auto& d = knownHostPlugins.getReference (i);
        // Exclude StemDeck itself — hosting the host inside a chain would recurse/fail.
        if (d.name.equalsIgnoreCase ("Sattari StemDeck") || d.name.equalsIgnoreCase ("StemDeck")) continue;
        if (d.name.startsWithIgnoreCase ("Sattari") || d.manufacturerName.containsIgnoreCase ("Sattari")
            || d.name.equalsIgnoreCase ("AutoPitch") || d.name.containsIgnoreCase ("Morph"))
            suite.push_back ({ i, d.name });
    }

    // First use: scan, then auto-reopen the menu (no confusing "tap again").
    if (suite.empty())
    {
        if (knownHostPlugins.isEmpty() && ! pluginScanInFlight)
        {
            pluginScanInFlight = true;
            pluginHostMessage.setText ("Scanning your plugins\xe2\x80\xa6", juce::dontSendNotification);
            uiWorkPool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
                [safe = juce::Component::SafePointer<MainPluginEditor> (this), chainIndex]()
                -> juce::ThreadPoolJob::JobStatus
            {
                if (safe == nullptr) return juce::ThreadPoolJob::jobHasFinished;
                auto res = safe->processorRef.scanSattariPlugins();   // Sattari-only: safe
                juce::MessageManager::callAsync ([safe, chainIndex, n = res.plugins.size(),
                                                  blocked = res.blacklistedFiles]
                {
                    if (safe == nullptr) return;
                    safe->pluginScanInFlight = false;
                    safe->refreshHostPluginChoices();
                    juce::String msg = "Found " + juce::String (n) + " Sattari plugin(s)";
                    if (! blocked.isEmpty()) msg += "  \xe2\x80\xa2  skipped (crashed before): " + blocked.joinIntoString (", ");
                    safe->pluginHostMessage.setText (msg, juce::dontSendNotification);
                    safe->fxRack.setStatus (msg);
                    safe->addSattariFxToChain (chainIndex);   // reopen with results
                });
                return juce::ThreadPoolJob::jobHasFinished;
            }));
            return;
        }
        pluginHostMessage.setText ("No Sattari plugins found \xe2\x80\x94 check your VST3 folder", juce::dontSendNotification);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("Add Sattari FX");
    for (size_t k = 0; k < suite.size(); ++k) menu.addItem ((int) k + 1, suite[k].name);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fxRack)
                            .withTargetScreenArea (fxRack.localAreaToGlobal (fxRack.addSlotBounds())),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), suite, chainIndex] (int r)
        {
            if (safe == nullptr || r == 0) return;
            const auto& e = suite[(size_t) (r - 1)];
            safe->fxRack.setStatus ("Loading " + e.name + "\xe2\x80\xa6");
            safe->processorRef.loadHostPluginToChainAsync (chainIndex, e.index,
                [safe, name = e.name] (bool ok, juce::String msg)
                {
                    juce::MessageManager::callAsync ([safe, ok, name, msg]
                    {
                        if (safe == nullptr) return;
                        safe->pluginHostMessage.setText (ok ? "Loaded " + name : "FX load failed: " + msg, juce::dontSendNotification);
                        safe->fxRack.setStatus (ok ? juce::String() : "Failed: " + msg);
                        safe->fxRack.repaint();
                    });
                });
        });
}

void MainPluginEditor::openChainPluginEditor (int chainIndex, int pluginIndex)
{
    auto* proc = processorRef.getChainPluginProcessor (chainIndex, pluginIndex);
    if (proc == nullptr) { pluginHostMessage.setText ("Plugin has no editor", juce::dontSendNotification); return; }

    // Already open? Bring it forward rather than co-owning the same editor object.
    for (auto* w : pluginWindows)
        if (w->ownedProcessor() == proc) { w->toFront (true); return; }

    auto* win = new stemdeck::PluginEditorWindow (*proc, proc->getName(),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this)] (stemdeck::PluginEditorWindow* w)
        {
            juce::MessageManager::callAsync ([safe, w] { if (safe) safe->pluginWindows.removeObject (w); });
        });
    pluginWindows.add (win);
}

MainPluginEditor::~MainPluginEditor()
{
    stopTimer();
    // Background analysis and plugin scans can read processor-owned state. Join
    // them before either the editor or its processor reference can be destroyed.
    uiWorkPool.removeAllJobs (true, -1);
    autosavePool.removeAllJobs (true, 1500);
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto prefix = deckPrefix (d);
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            processorRef.parameters().removeParameterListener (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain", this);
    }
    if (fileBrowser != nullptr)
        fileBrowser->removeListener (this);
    browserThread.stopThread (1500);
    for (auto& dc : deckControls) dc.play.setLookAndFeel (nullptr);   // drop icon LnF before it dies
    setLookAndFeel (nullptr);
}

void MainPluginEditor::setupFileBrowser()
{
    browserThread.startThread (juce::Thread::Priority::background);

    setupButton (toggleBrowserButton);
    toggleBrowserButton.setClickingTogglesState (true);
    toggleBrowserButton.onClick = [this] { toggleFileBrowser(); };
    addAndMakeVisible (toggleBrowserButton);

    setupButton (browserUpButton);
    browserUpButton.onClick = [this] { browserNavigateUp(); };
    addChildComponent (browserUpButton);

    setupButton (browserHomeButton);
    browserHomeButton.onClick = [this] { browserNavigateTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory)); };
    addChildComponent (browserHomeButton);

    browserPathLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
    browserPathLabel.setFont (juce::FontOptions (11.5f));
    browserPathLabel.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (browserPathLabel);

    fileBrowser = std::make_unique<juce::FileTreeComponent> (browserContents);
    fileBrowser->addListener (this);
    fileBrowser->setColour (juce::TreeView::backgroundColourId, juce::Colour (sattari::brand::kPanelDeep));
    fileBrowser->setColour (juce::TreeView::linesColourId, juce::Colour (sattari::brand::kHairline));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.30f));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::textColourId, juce::Colour (sattari::brand::kTextPrimary).withAlpha (0.92f));
    addChildComponent (*fileBrowser);

    browserNavigateTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory));
}

void MainPluginEditor::setupLibrary()
{
    setupButton (toggleLibraryButton);
    toggleLibraryButton.setClickingTogglesState (true);
    toggleLibraryButton.onClick = [this] { toggleLibrary(); };
    addAndMakeVisible (toggleLibraryButton);

    libraryView = std::make_unique<MusicLibraryView> (thumbnailFormatManager);
    libraryView->onLoadFile = [this] (const juce::File& f) { fileDoubleClicked (f); };
    // AI set builder: top four harmonic picks land straight on decks A-D.
    libraryView->onLoadSet = [this] (const std::array<juce::File, 4>& set)
    {
        for (int d = 0; d < (int) set.size(); ++d)
        {
            if (! set[(size_t) d].existsAsFile())
                continue;
            requestDeckLoad (d, set[(size_t) d]);
        }
    };
    addChildComponent (*libraryView);
    libraryView->restorePersistedCrates();
}

void MainPluginEditor::toggleLibrary()
{
    libraryVisible = toggleLibraryButton.getToggleState();
    if (libraryView != nullptr)
        libraryView->setVisible (libraryVisible);

    // Library and the basic file browser share the left column — keep one open.
    if (libraryVisible && browserVisible)
    {
        browserVisible = false;
        toggleBrowserButton.setToggleState (false, juce::dontSendNotification);
        if (fileBrowser != nullptr)        fileBrowser->setVisible (false);
        browserUpButton.setVisible (false);
        browserHomeButton.setVisible (false);
        browserPathLabel.setVisible (false);
    }
    resized();
}

void MainPluginEditor::toggleFileBrowser()
{
    browserVisible = toggleBrowserButton.getToggleState();
    if (fileBrowser != nullptr)
        fileBrowser->setVisible (browserVisible);
    browserUpButton.setVisible (browserVisible);
    browserHomeButton.setVisible (browserVisible);
    browserPathLabel.setVisible (browserVisible);

    // Mutually exclusive with the library panel — they share the left column.
    if (browserVisible && libraryVisible)
    {
        libraryVisible = false;
        toggleLibraryButton.setToggleState (false, juce::dontSendNotification);
        if (libraryView != nullptr)
            libraryView->setVisible (false);
    }
    resized();
}

void MainPluginEditor::browserNavigateTo (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    browserContents.setDirectory (folder, true, true);
    browserPathLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
}

void MainPluginEditor::browserNavigateUp()
{
    const auto current = browserContents.getDirectory();
    auto parent = current.getParentDirectory();
    if (parent == current)
        return;
    browserNavigateTo (parent);
}

void MainPluginEditor::fileDoubleClicked (const juce::File& file)
{
    if (file.isDirectory())
    {
        browserNavigateTo (file);
        return;
    }
    if (! file.existsAsFile())
        return;

    int target = -1;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (processorRef.getDeckPath (i).isEmpty()) { target = i; break; }

    if (target >= 0)
    {
        requestDeckLoad (target, file);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ("All decks are loaded \xe2\x80\x94 pick one to replace"));
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto name = "Replace Deck " + juce::String::charToString ((juce_wchar) ('A' + i))
                        + " (" + juce::File (processorRef.getDeckPath (i)).getFileName() + ")";
        menu.addItem (i + 1, name);
    }
    menu.addSeparator();
    menu.addItem (99, "Cancel");
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), file]
        (int choice)
        {
            if (safe == nullptr || choice <= 0 || choice >= 99) return;
            const int idx = choice - 1;
            safe->requestDeckLoad (idx, file);
        });
}

void MainPluginEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void MainPluginEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void MainPluginEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking the "StemDeck PRO" title reveals/hides the advanced toolbar.
    if (e.eventComponent == &title)
    {
        advancedVisible = ! advancedVisible;
        advancedToggle.setToggleState (advancedVisible, juce::dontSendNotification);
        resized();
        repaint();
        return;
    }

    // Double-clicking a deck's BPM chip opens the unified tempo panel (§5).
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckBpmChipBounds[(size_t) i].contains (e.getPosition()))
        {
            showTempoPanel (i);
            return;
        }
}

void MainPluginEditor::applyPerStemKeyLock (int deckIndex, int semitones)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)) return;
    const float st = (float) juce::jlimit (-12, 12, semitones);
    // Tonal stems get the shift; drums stay at 0 (no pitch). Also clear the whole-deck
    // KeyTarget so the two key-lock modes don't stack.
    auto setStemPitch = [&] (int lane, float semis)
    {
        if (auto* p = processorRef.parameters().getParameter (deckPrefix (deckIndex) + "Stem" + DeckPlayer::stemLaneId (lane) + "Pitch"))
            p->setValueNotifyingHost (p->convertTo0to1 (semis));
    };
    setStemPitch (DeckPlayer::drumsStem,  0.0f);   // drums untouched
    setStemPitch (DeckPlayer::bassStem,   st);
    setStemPitch (DeckPlayer::musicStem,  st);
    setStemPitch (DeckPlayer::vocalsStem, st);
    if (auto* kt = processorRef.parameters().getParameter (deckPrefix (deckIndex) + "KeyTarget"))
        kt->setValueNotifyingHost (kt->convertTo0to1 (0.0f));   // whole-track lock off
}

void MainPluginEditor::showTempoPanel (int deckIndex)
{
    const int deck = juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks) ? deckIndex : focusedDeck;
    const auto ana = processorRef.getDeckActiveAnalysis (deck);
    const double detected = ana.detectedBpm > 0.0f ? (double) ana.detectedBpm
                                                    : (double) deckControls[(size_t) deck].bpm.getValue();

    stemdeck::TempoPanelModel m;
    m.trackBpm  = detected;
    m.nativeBpm = detected;
    m.liveBpm   = (double) deckControls[(size_t) deck].bpm.getValue();   // current playing tempo
    m.source    = "detected";
    m.confidence = -1.0f;                       // unknown — the analysis emits no BPM confidence
    m.dynamic   = ana.tempoCurve.size() > 2;    // has a real drift curve => dynamic material

    // Detected drift curve -> normalised (beat 0..1, bpm).
    const double totalSamp = ana.durationSeconds * ana.sampleRate;
    if (totalSamp > 0.0)
        for (const auto& pt : ana.tempoCurve)
            m.map.push_back ({ (float) juce::jlimit (0.0, 1.0, pt.first / totalSamp), (double) pt.second });

    // Retroactive lane is the editor-held MASTER lane (normalised); empty until drawn.
    m.lane = masterTempoLaneNorm;
    const double total = processorRef.getDeckTotalSeconds (deck);
    m.playheadNorm = total > 0.0 ? (float) (processorRef.getDeckPlayheadSeconds (deck) / total) : 0.0f;

    auto panel = std::make_unique<stemdeck::TempoPanel>();
    panel->setSize (720, 240);
    panel->setModel (m);
    panel->onLaneChanged = [safe = juce::Component::SafePointer<MainPluginEditor> (this)]
                           (std::vector<std::pair<float, double>> lane)
    {
        if (safe != nullptr) { safe->masterTempoLaneNorm = std::move (lane); safe->refreshMixArrangement(); }
    };

    auto anchor = ! deckBpmChipBounds[(size_t) deck].isEmpty() ? deckBpmChipBounds[(size_t) deck]
                                                               : getLocalBounds().withSizeKeepingCentre (720, 240);
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor, this);
}

void MainPluginEditor::setDeckBpm (int deckIndex, double bpm)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)) return;
    if (auto* p = processorRef.parameters().getParameter (deckPrefix (deckIndex) + "Bpm"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (40.0, 240.0, bpm)));
    repaint();
}

void MainPluginEditor::showKeyMenuForDeck (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)) return;
    const auto choices = processorRef.getKeyTargetChoices();
    const int current = (int) std::round (processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "KeyTarget")->load());
    juce::PopupMenu menu;
    menu.addSectionHeader ("Set Deck " + juce::String::charToString ((juce_wchar) ('A' + deckIndex)) + " key");
    for (int c = 0; c < choices.size(); ++c)
        menu.addItem (c + 1, choices[c], true, c == current);
    juce::Component::SafePointer<MainPluginEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe, deckIndex] (int choice)
        {
            if (safe == nullptr || choice <= 0) return;
            if (auto* p = safe->processorRef.parameters().getParameter (safe->deckPrefix (deckIndex) + "KeyTarget"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (choice - 1)));
            safe->repaint();
        });
}

void MainPluginEditor::syncBpmAll()
{
    auto& p = processorRef.parameters();
    const auto masterLetter = juce::String::charToString ((juce_wchar) ('A' + masterDeckIndex));
    const float refBpm = (float) p.getRawParameterValue (deckPrefix (masterDeckIndex) + "Bpm")->load();
    if (auto* mb = p.getParameter ("masterBpm"))
        mb->setValueNotifyingHost (mb->convertTo0to1 (refBpm));
    for (const char* id : { "hostSync", "beatSync" })
        if (auto* b = p.getParameter (id))
            b->setValueNotifyingHost (1.0f);
    // Downbeat phase-lock: force strict sync mode (choice 0) on every deck so the
    // WarpEngine grid-aligns each deck's downbeat to the master beat position.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (auto* sm = p.getParameter (deckPrefix (d) + "SyncMode"))
            sm->setValueNotifyingHost (sm->convertTo0to1 (0.0f));
    recordStatus.setText ("BPM SYNC: all decks warped + downbeat phase-locked to Deck " + masterLetter
                          + " (" + juce::String (refBpm, 1) + " BPM)", juce::dontSendNotification);
    repaint();
}

void MainPluginEditor::syncKeyAll()
{
    auto& p = processorRef.parameters();
    const auto masterLetter = juce::String::charToString ((juce_wchar) ('A' + masterDeckIndex));
    const auto refA = processorRef.getDeckAnalysis (masterDeckIndex);
    if (! refA.hasKey())
    {
        recordStatus.setText ("KEY SYNC: Deck " + masterLetter + " has no analyzed key yet — load/analyze audio first",
                              juce::dontSendNotification);
        return;
    }
    const int keyA = refA.keyPitchClass;
    // Camelot-compatible pitch classes relative to the master deck: tonic, +7 (fifth
    // up), +5 (fourth up / fifth down) — the harmonically adjacent moves.
    const std::array<int, 3> compat { keyA, (keyA + 7) % 12, (keyA + 5) % 12 };

    // masterKey Off so per-deck KeyTargets take effect.
    if (auto* mk = p.getParameter ("masterKey"))
        mk->setValueNotifyingHost (mk->convertTo0to1 (0.0f));

    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        int target = keyA;
        const auto a = processorRef.getDeckAnalysis (d);
        if (d != masterDeckIndex && a.hasKey())
        {
            // Choose the compatible key needing the SMALLEST pitch shift from this
            // deck's own key (harmonic mixing without over-shifting the audio).
            int best = compat[0], bestShift = 99;
            for (int c : compat)
            {
                const int raw = ((c - a.keyPitchClass) % 12 + 12) % 12;
                const int shift = juce::jmin (raw, 12 - raw);
                if (shift < bestShift) { bestShift = shift; best = c; }
            }
            target = best;
        }
        if (auto* kt = p.getParameter (deckPrefix (d) + "KeyTarget"))
            kt->setValueNotifyingHost (kt->convertTo0to1 ((float) (target + 1)));
    }
    recordStatus.setText ("KEY SYNC (harmonic): decks matched to Deck " + masterLetter + " "
                          + processorRef.getKeyTargetChoices()[keyA + 1] + " via Camelot-compatible keys",
                          juce::dontSendNotification);
    repaint();
}

void MainPluginEditor::setShowcaseMode (bool on)
{
    showcaseMode = on;
    // Decks A/B/C: demo waveform + PLAY-on only in showcase; otherwise reflect real
    // loaded audio / real transport state.
    for (int i = 0; i < juce::jmin (3, MainPluginProcessor::numDecks); ++i)
    {
        if (auto* w = deckWaves[(size_t) i].get())
        {
            w->setDemoWaveform (on);
            w->setShowDropHint (! on);
        }
        const bool realPlaying = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        deckControls[(size_t) i].play.setToggleState (on ? true : realPlaying, juce::dontSendNotification);
    }
    arrangementView.setShowDemoBlocks (on);
    recordStatus.setText (on ? "Showcase demo content ON" : "Showcase off — showing live loaded audio",
                          juce::dontSendNotification);
    resized();
    repaint();
}

void MainPluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Settings");
    menu.addItem (1, "Advanced tools (plugin host, AI master, rekordbox, project I/O)", true, advancedVisible);
    menu.addItem (2, "Showcase demo content (fake titles / waveforms / clips)", true, showcaseMode);
    menu.addSeparator();
    menu.addItem (3, "Re-sync motorized controllers", true, false);
    menu.addItem (4, "Create session snapshot", true, false);
    menu.addItem (5, "Restore latest snapshot", true, false);
    menu.addItem (6, "Repair missing media...", true, false);
    menu.addSeparator();
    menu.addItem (7, "Session History...", true, false);
    menu.addItem (8, "Export Center...", true, false);
    menu.addItem (9, "Diagnostics...", true, false);
    menu.addItem (10, "Welcome guide...", true, false);
    juce::Component::SafePointer<MainPluginEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
        [safe] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == 1) { safe->advancedVisible = ! safe->advancedVisible; safe->advancedToggle.setToggleState (safe->advancedVisible, juce::dontSendNotification); safe->resized(); safe->repaint(); }
            else if (choice == 2) safe->setShowcaseMode (! safe->showcaseMode);
            else if (choice == 3) safe->processorRef.getMidiMapping().resyncFeedback();
            else if (choice == 4) safe->createSessionSnapshot();
            else if (choice == 5) safe->restoreLatestSnapshot();
            else if (choice == 6) safe->repairMissingMedia();
            else if (choice == 7) safe->showSessionHistory();
            else if (choice == 8) safe->showExportCenter();
            else if (choice == 9) safe->showDiagnostics();
            else if (choice == 10) safe->showOnboarding (true);
        });
}

void MainPluginEditor::showCommandPalette()
{
    using Command = stemdeck::CommandPalette::Command;
    std::vector<Command> commands {
        { 1,  "Open Library",       "Search, prepare, and load your music",           u8 ("\xe2\x8c\x98" "1") },
        { 2,  "Open Perform",       "Decks, stems, cues, and performance pads",       u8 ("\xe2\x8c\x98" "2") },
        { 3,  "Open Mix",           "Channel strips, routing, and metering",           u8 ("\xe2\x8c\x98" "3") },
        { 4,  "Open Arrange",       "Captured takes, clips, and export",               u8 ("\xe2\x8c\x98" "4") },
        { 5,  "Browse Audio Files", "Open the local audio-file browser",               u8 ("\xe2\x8c\x98O") },
        { 10, "Choose Track",       "Load audio into the focused deck",                "L" },
        { 11, "Create Stems",       "Separate the focused deck into musical stems",    "" },
        { 12, focusMode ? "Exit Focus Mode" : "Enter Focus Mode",
                                      "Concentrate on the selected deck",               "F" },
        { 13, "Sync All Tempos",    "Match every deck to the master deck",             "B" },
        { 14, "Sync All Keys",      "Harmonically match every deck",                   "K" },
        { 15, processorRef.isMasterRecording() ? "Finish Recording" : "Start Recording",
                                      "Capture master, decks, MIDI, and intent",         "R" },
        { 16, "Save Project",       "Save the complete StemDeck session",              u8 ("\xe2\x8c\x98S") },
        { 17, "Open Project",       "Restore a StemDeck session",                      u8 ("\xe2\x87\xa7\xe2\x8c\x98O") },
        { 18, "Inspect Focused Deck", "Details, preflight, and quick actions",          u8 ("\xe2\x8c\x98I") },
        { 19, "MIDI Learn",         "Map a controller to a StemDeck action",            "" },
        { 20, "Scan Sattari FX",    "Refresh the safe curated plugin list",             "" },
        { 21, "Create Snapshot",    "Keep a timestamped restore point for this session", u8 ("\xe2\x87\xa7\xe2\x8c\x98S") },
        { 22, "Restore Latest Snapshot", "Return to the newest named restore point",     "" },
        { 23, "Repair Missing Files", "Find and relink moved session audio by filename",  "" }
        ,{ 24, "Session History",     "Browse, annotate, restore, and remove restore points", "" }
        ,{ 25, "Export Center",       "Preflight and package the current session",       "" }
        ,{ 26, "Diagnostics",         "Copy a privacy-safe troubleshooting report",      "" }
        ,{ 27, "Welcome Guide",       "Review the four-step getting-started guide",      "" }
        ,{ 28, "Undo Session Change", "Undo a channel add, remove, or destructive clear", "" }
        ,{ 29, "Redo Session Change", "Redo the last session-level change",               "" }
    };

    auto panel = std::make_unique<stemdeck::CommandPalette> (std::move (commands));
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    panelPtr->onDismiss = [panelPtr]
    {
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    panelPtr->onInvoke = [safe, panelPtr] (int id)
    {
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        juce::MessageManager::callAsync ([safe, id]
        {
            if (safe != nullptr)
                safe->performCommand (id);
        });
    };

    const auto anchor = commandButton.isVisible() && ! commandButton.getBounds().isEmpty()
                      ? commandButton.getBounds()
                      : getLocalBounds().withSizeKeepingCentre (80, 30);
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor, this);
}

void MainPluginEditor::showQuickInspector()
{
    auto panel = std::make_unique<stemdeck::QuickInspector>();
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);

    const auto path = processorRef.getDeckPath (focusedDeck);
    const auto analysis = processorRef.getDeckActiveAnalysis (focusedDeck);
    stemdeck::QuickInspector::State state;
    state.deckName = deckLabel (focusedDeck);
    state.trackName = path.isNotEmpty() ? juce::File (path).getFileNameWithoutExtension()
                                        : juce::String();
    state.source = path.isNotEmpty() ? juce::File (path).getParentDirectory().getFileName()
                                     : "Drop audio or choose a track";
    state.tempo = analysis.detectedBpm > 1.0f ? juce::String (analysis.detectedBpm, 1) + " BPM"
                                              : u8 ("\xe2\x80\x94");
    state.musicalKey = analysis.hasKey() ? analysis.keyName() : u8 ("\xe2\x80\x94");
    state.analysis = path.isEmpty() ? "Waiting for audio"
                   : juce::String (analysis.keySource == TrackAnalysis::Source::neural
                                      ? "Neural" : "DSP")
                         + u8 (" \xe2\x80\xa2 version ") + juce::String ((int) analysis.analysisVersion);
    state.loaded = path.isNotEmpty();
    state.playing = processorRef.parameters().getRawParameterValue (
                        deckPrefix (focusedDeck) + "Play")->load() > 0.5f;
    if (state.playing)
        state.analysis = u8 ("Playing \xe2\x80\xa2 ") + state.analysis;
    state.focusMode = focusMode;
    state.audioStatus = juce::String (processorRef.getRecordingSampleRate() / 1000.0, 1)
                      + u8 (" kHz \xe2\x80\xa2 ") + juce::String (processorRef.getRecordingBlockSize())
                      + " samples";
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto freeGb = (double) music.getBytesFreeOnVolume() / (1024.0 * 1024.0 * 1024.0);
    state.storageStatus = freeGb > 0.0 ? juce::String (freeGb, 1) + " GB recording space"
                                      : "Storage status unavailable";
    state.midiStatus = processorRef.hasMidiBindings() ? "MIDI controller linked"
                                                       : u8 ("MIDI ready \xe2\x80\xa2 no mappings");
    state.captureStatus = "Intent capture "
                        + juce::String (processorRef.getIntentCaptureCoverage()) + "/"
                        + juce::String (processorRef.getIntentCaptureTargetCount());
    panelPtr->setState (std::move (state));

    const auto dismissInspector = [panelPtr]
    {
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    panelPtr->onToggleFocus = [safe, dismissInspector]
    {
        dismissInspector();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->setFocusMode (! safe->focusMode);
        });
    };
    panelPtr->onLoad  = [safe, dismissInspector]
    {
        dismissInspector();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->openDeckChooser (safe->focusedDeck);
        });
    };
    panelPtr->onSplit = [safe, dismissInspector]
    {
        dismissInspector();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->processorRef.requestDeckAiStemsAsync (safe->focusedDeck);
        });
    };
    panelPtr->onSync = [safe, dismissInspector]
    {
        dismissInspector();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) { safe->syncBpmAll(); safe->syncKeyAll(); }
        });
    };
    panelPtr->onClear = [safe, dismissInspector]
    {
        dismissInspector();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->requestDeckClear (safe->focusedDeck);
        });
    };

    const auto anchor = inspectorButton.isVisible() && ! inspectorButton.getBounds().isEmpty()
                      ? inspectorButton.getBounds()
                      : getLocalBounds().withSizeKeepingCentre (80, 30);
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor, this);
}

void MainPluginEditor::performCommand (int commandId)
{
    switch (commandId)
    {
        case 1:  selectPrimarySurface (PrimarySurface::library); break;
        case 2:  selectPrimarySurface (PrimarySurface::perform); break;
        case 3:  selectPrimarySurface (PrimarySurface::mixer); break;
        case 4:  selectPrimarySurface (PrimarySurface::arranger); break;
        case 5:  selectPrimarySurface (PrimarySurface::files); break;
        case 10: openDeckChooser (focusedDeck); break;
        case 11: processorRef.requestDeckAiStemsAsync (focusedDeck); break;
        case 12: setFocusMode (! focusMode); break;
        case 13: syncBpmAll(); break;
        case 14: syncKeyAll(); break;
        case 15: toggleRecording(); break;
        case 16: saveProject(); break;
        case 17: loadProject(); break;
        case 18: showQuickInspector(); break;
        case 19: updateMidiLearnUI(); break;
        case 20: runPluginHostScan(); break;
        case 21: createSessionSnapshot(); break;
        case 22: restoreLatestSnapshot(); break;
        case 23: repairMissingMedia(); break;
        case 24: showSessionHistory(); break;
        case 25: showExportCenter(); break;
        case 26: showDiagnostics(); break;
        case 27: showOnboarding (true); break;
        case 28: undoSessionAction(); break;
        case 29: redoSessionAction(); break;
        default: break;
    }
}

void MainPluginEditor::setFocusMode (bool enabled)
{
    if (enabled)
        focusedDeck = juce::jlimit (0, juce::jmax (0, processorRef.getNumTrackDecks() - 1),
                                    focusedDeck);
    focusMode = enabled;
    focusModeButton.setToggleState (enabled, juce::dontSendNotification);
    focusModeButton.setButtonText (enabled ? "Exit Focus" : "Focus");
    recordStatus.setText (
        enabled ? deckLabel (focusedDeck) + u8 (" focus \xe2\x80\xa2 other channels remain audible")
                : u8 ("Focus mode off \xe2\x80\xa2 all channels visible"),
        juce::dontSendNotification);
    resized();
    repaint();
}

void MainPluginEditor::setMasterDeck (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks) && deckIndex != masterDeckIndex)
    {
        masterDeckIndex = deckIndex;
        recordStatus.setText ("Master deck = Deck " + juce::String::charToString ((juce_wchar) ('A' + deckIndex))
                              + " (BPM/Key sync reference)", juce::dontSendNotification);
        repaint();
    }
}

void MainPluginEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckCollapsedBounds[(size_t) i].contains (e.getPosition()))
        {
            setFocusedDeck (i);
            return;
        }

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckBadgeBounds[(size_t) i].contains (e.getPosition()))
        {
            setMasterDeck (i);
            return;
        }

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        if (deckBpmChipBounds[(size_t) i].contains (e.getPosition()))
        {
            bpmDragDeck    = i;
            bpmDragStartY  = e.getPosition().y;
            bpmDragStartVal = (double) deckControls[(size_t) i].bpm.getValue();
            return;
        }
        if (deckKeyChipBounds[(size_t) i].contains (e.getPosition()))
        {
            showKeyMenuForDeck (i);
            return;
        }
    }

    // Console deck meters: click to cycle this deck's crossfader assignment
    // (A/C side -> Center -> B/D side), mirrored to the deck's Ctr/A/B buttons.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (deckMeterBounds[(size_t) d].contains (e.getPosition()))
        {
            const int next = (processorRef.getDeckCfSide (d) + 1) % 3;
            processorRef.setDeckCfSide (d, next);
            for (int ss = 0; ss < 3; ++ss)
                deckControls[(size_t) d].cfSideBtns[(size_t) ss].setToggleState (ss == next, juce::dontSendNotification);
            repaint();
            return;
        }

    // GLOBAL TEMPO box: vertical drag adjusts the master tempo (same gesture as
    // the deck BPM chips) — the painted box is a real control, not a readout.
    if (masterTempoBounds.contains (e.getPosition()))
    {
        masterBpmDragging  = true;
        bpmDragStartY      = e.getPosition().y;
        bpmDragStartVal    = masterBpm.getValue();
        return;
    }

    if (! addChannelTileBounds.isEmpty() && addChannelTileBounds.contains (e.getPosition()))
    {
        showAddChannelMenu (nullptr);
        return;
    }

    // Focus follows the user's hands: clicking a deck panel focuses that deck.
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckPanelBounds[(size_t) i].contains (e.getPosition()))
        {
            setFocusedDeck (i);
            return;
        }
}

void MainPluginEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (bpmDragDeck >= 0)   // vertical drag on a BPM chip = nudge tempo
        setDeckBpm (bpmDragDeck, bpmDragStartVal + (double) (bpmDragStartY - e.getPosition().y) * 0.25);
    else if (masterBpmDragging)
        masterBpm.setValue (bpmDragStartVal + (double) (bpmDragStartY - e.getPosition().y) * 0.25,
                            juce::sendNotificationSync);
}

void MainPluginEditor::mouseUp (const juce::MouseEvent&)
{
    bpmDragDeck = -1;   // disarm so later, unrelated drags don't keep nudging tempo
    masterBpmDragging = false;
}

void MainPluginEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckBpmChipBounds[(size_t) i].contains (e.getPosition()))
        {
            setDeckBpm (i, (double) deckControls[(size_t) i].bpm.getValue() + wheel.deltaY * 20.0);
            return;
        }

    if (masterTempoBounds.contains (e.getPosition()))
        masterBpm.setValue (masterBpm.getValue() + wheel.deltaY * 20.0, juce::sendNotificationSync);
}

void MainPluginEditor::drawNavIcon (juce::Graphics& g, int which, juce::Rectangle<float> b, juce::Colour c) const
{
    g.setColour (c);
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    b = juce::Rectangle<float> (s, s).withCentre (b.getCentre());
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    const float t = juce::jmax (1.4f, s * 0.09f);   // stroke

    switch (which)
    {
        case 0:   // LIBRARY — folder
        {
            juce::Path p;
            p.startNewSubPath (x, y + h * 0.28f);
            p.lineTo (x, y + h * 0.85f); p.lineTo (x + w, y + h * 0.85f); p.lineTo (x + w, y + h * 0.15f);
            p.lineTo (x + w * 0.45f, y + h * 0.15f); p.lineTo (x + w * 0.34f, y + h * 0.28f); p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (t));
            break;
        }
        case 1:   // DECKS — 2×2 grid
        {
            const float g2 = w * 0.12f, cw = (w - g2) * 0.5f, ch = (h - g2) * 0.5f;
            for (int r = 0; r < 2; ++r)
                for (int col = 0; col < 2; ++col)
                    g.fillRoundedRectangle (x + (float) col * (cw + g2), y + (float) r * (ch + g2), cw, ch, 1.5f);
            break;
        }
        case 2:   // MIXER — three vertical sliders
        {
            for (int i = 0; i < 3; ++i)
            {
                const float cx = x + w * (0.2f + 0.3f * (float) i);
                g.drawLine (cx, y, cx, y + h, t);
                const float capY = y + h * (i == 1 ? 0.55f : 0.28f);
                g.fillRoundedRectangle (cx - w * 0.10f, capY, w * 0.20f, h * 0.14f, 1.5f);
            }
            break;
        }
        case 3:   // ARRANGER — timeline rows
        {
            for (int i = 0; i < 3; ++i)
            {
                const float ry = y + h * (0.15f + 0.32f * (float) i);
                g.fillRoundedRectangle (x, ry, w * (i == 1 ? 0.6f : 0.9f), h * 0.13f, 1.5f);
            }
            break;
        }
        default:  // PERFORMANCE — 3×3 dots
        {
            const float d = w * 0.2f, gap = (w - d * 3.0f) * 0.5f;
            for (int r = 0; r < 3; ++r)
                for (int col = 0; col < 3; ++col)
                    g.fillEllipse (x + (float) col * (d + gap), y + (float) r * (d + gap), d, d);
            break;
        }
    }
}

void MainPluginEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintModernBackground (g, getLocalBounds());

    // --- Header bar: flat, no backdrop. Thin hairline underline only. ---
    if (! headerBarBounds.isEmpty())
    {
        namespace sdp = stemdeck::tokens;
        const auto r = headerBarBounds.toFloat();
        g.setColour (juce::Colour (sdp::borderSubtle).withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 1.0f, r.getWidth() - 8.0f, 1.0f));
    }

    // --- Four workspaces: icon above label, one calm accent for current context. ---
    {
        static const char* navLabels[4] = { "Sets", "Live", "Mix", "Studio" };
        const juce::Button* navBtns[4] = { &toggleLibraryButton, &navDecksButton,
                                           &mixerButton, &pianoRollButton };
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = navRects[(size_t) i];
            if (cell.isEmpty()) continue;
            const bool active = navBtns[i]->getToggleState() || (i == 0 && browserVisible);
            const auto col = active ? deckAccent (0) : juce::Colour (stemdeck::tokens::fgTertiary);
            auto icon = cell.withHeight (22).translated (0, 3).toFloat().reduced ((cell.getWidth() - 22) * 0.5f, 0.0f);
            drawNavIcon (g, i, icon, col.withAlpha (active ? 0.95f : 0.7f));
            g.setColour (col.withAlpha (active ? 0.98f : 0.72f));
            g.setFont (juce::FontOptions (9.5f, active ? juce::Font::bold : juce::Font::plain));
            g.drawFittedText (navLabels[i], cell.withTop (cell.getY() + 27).withHeight (13),
                              juce::Justification::centred, 1);
        }

        // Animated active-view underline: one accent bar that slides between
        // nav cells (position eased in timerCallback).
        if (navUnderlineW > 1.0f && ! navRects[0].isEmpty())
        {
            const float y = (float) navRects[0].getBottom() - 2.0f;
            g.setColour (deckAccent (0));
            g.fillRoundedRectangle (navUnderlineX, y, navUnderlineW, 2.0f, 1.0f);
        }
    }

    // --- Pad-trigger flash: colour halo that decays over ~350 ms ---
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < PadBank::numPads; ++i)
        {
            const double age = nowMs - padFlashMs[(size_t) i];
            if (age < 0.0 || age > 350.0) continue;
            const float k = 1.0f - (float) (age / 350.0);
            const auto pb = pads[(size_t) i].trigger.getBounds().toFloat();
            if (pb.isEmpty()) continue;
            g.setColour (juce::Colour (padSpectrum (i)).withAlpha (0.35f * k * k));
            g.drawRoundedRectangle (pb.expanded (2.0f + 3.0f * (1.0f - k)), 8.0f, 2.0f);
        }
    }

    // --- Tempo chip in the right cluster ---
    if (! tempoChipBounds.isEmpty())
    {
        namespace sdp = stemdeck::tokens;
        const auto r = tempoChipBounds.toFloat();
        g.setColour (juce::Colour (sdp::bgWell));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (deckAccent (0).withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (sdp::fgTertiary));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        // "HOST TEMPO" is only honest inside a DAW. Standalone has no host and this
        // chip just mirrors the GLOBAL TEMPO clock, so label it plainly there.
        const bool inHost = processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone
                         && processorRef.wrapperType != juce::AudioProcessor::wrapperType_Undefined;
        g.drawText (inHost ? "HOST TEMPO" : "TEMPO",
                    juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 2.0f, r.getWidth() - 16.0f, 10.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (stemdeck::tokens::borderSubtle).withAlpha (0.5f));
    g.drawHorizontalLine (title.getBounds().getBottom() + 9, 22.0f, (float) getWidth() - 22.0f);

    // --- "+" add-channel tile: the channel model, visible in the layout ---
    if (! addChannelTileBounds.isEmpty())
    {
        namespace sdp = stemdeck::tokens;
        auto r = addChannelTileBounds.toFloat().reduced (2.0f);

        // Dashed outline reads as "empty slot you can fill", distinct from a real panel.
        juce::Path outline;
        outline.addRoundedRectangle (r, 10.0f);
        const float dashes[] { 5.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (juce::Colour (sdp::borderSubtle).withAlpha (0.9f));
        g.fillPath (dashed);

        const auto c = r.getCentre();
        g.setColour (juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
        const float armLen = 9.0f, thick = 2.0f;
        g.fillRoundedRectangle (c.x - armLen, c.y - thick * 0.5f, armLen * 2.0f, thick, 1.0f);
        g.fillRoundedRectangle (c.x - thick * 0.5f, c.y - armLen, thick, armLen * 2.0f, 1.0f);

        g.setColour (juce::Colour (sdp::fgTertiary));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold).withKerningFactor (0.06f));
        g.drawText ("CHANNEL", addChannelTileBounds.withTop (addChannelTileBounds.getCentreY() + 16).withHeight (12),
                    juce::Justification::centred);
    }

    // --- Deck panels: flat brand-palette card per deck, accent stripe at the top ---
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckBounds = deckPanelBounds[(size_t) i];
        if (deckBounds.isEmpty())
            continue;
        const bool deckPlaying = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        const auto stateAccent = processorRef.isMasterRecording()
                               ? juce::Colour (stemdeck::tokens::meterHot)
                               : deckPlaying ? juce::Colour (stemdeck::tokens::stemBas)
                                             : deckAccent (i);
        // Reference §3: radius-panel (9), and identity carried by a 3px LEFT rail
        // rather than a top stripe — the same rail an idle channel uses (§4), so a
        // deck and a channel read as the same kind of object.
        drawPanel (g, deckBounds, stateAccent, stemdeck::tokens::radiusPanel,
                   deckPlaying || processorRef.isMasterRecording());
        {
            juce::Graphics::ScopedSaveState railClip (g);
            juce::Path rounded;
            rounded.addRoundedRectangle (deckBounds.toFloat(), stemdeck::tokens::radiusPanel);
            g.reduceClipRegion (rounded);
            g.setColour (deckAccent (i).withAlpha (deckPlaying ? 0.95f : 0.75f));
            g.fillRect (deckBounds.removeFromLeft (3));
        }

        // Segmented-control track + the well its active panel sits in.
        if (const auto strip = deckTabStripBounds[(size_t) i]; ! strip.isEmpty())
        {
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgWell2));
            g.fillRoundedRectangle (strip.toFloat(), 8.0f);
        }
        if (const auto well = deckTabPanelBounds[(size_t) i]; ! well.isEmpty())
        {
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgWell));
            g.fillRoundedRectangle (well.toFloat(), stemdeck::tokens::radiusWell);
        }

        // --- Deck header strip: letter badge + filename + key chip ---
        // The badge / BPM / key rectangles are HIT TARGETS and are computed by
        // layoutDeckHeaderChips() during resized(); paint() only reads them.
        if (! deckHeaderBounds[(size_t) i].isEmpty())
        {
            namespace T = stemdeck::tokens;

            // ── Row 1: identity ──────────────────────────────────────────────
            const juce::String letter = juce::String::charToString ((juce_wchar) ('A' + i));
            const auto badge = deckBadgeBounds[(size_t) i];
            g.setColour (deckAccent (i).withAlpha (0.95f));
            g.fillRoundedRectangle (badge.toFloat(), 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (letter, badge, juce::Justification::centred);
            // Master-deck indicator: bright white ring around the letter badge
            // (this deck is the BPM/Key sync reference). Click any badge to change.
            if (i == masterDeckIndex)
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawRoundedRectangle (badge.toFloat().expanded (2.5f), 6.0f, 1.2f);
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.drawRoundedRectangle (badge.toFloat().expanded (1.5f), 5.0f, 1.6f);
            }

            // The track name is HERO tier: it is the strip's identity and now owns
            // the full column width on its own row, at tName rather than sharing a
            // 22px row with three chips (which left it about six characters).
            const auto nameBox = deckNameBounds[(size_t) i];
            if (! nameBox.isEmpty())
            {
                if (! deckHasContent (i))
                {
                    g.setColour (juce::Colour (T::fgMuted).withAlpha (0.8f));
                    g.setFont (juce::FontOptions (T::tSmall, juce::Font::bold).withKerningFactor (T::trackLabel));
                    g.drawText ("EMPTY", nameBox, juce::Justification::centredLeft);
                }
                else
                {
                    static const char* kDemoTitle[4] = {
                        "Moneyplagg & MealThePred\xe2\x80\xa6",
                        "Life is real controlle way",
                        "Moneyplagg & MealThePred\xe2\x80\xa6",
                        "" };
                    const auto path = processorRef.getDeckPath (i);
                    const auto file = juce::File (path).getFileNameWithoutExtension();
                    const bool useDemo = showcaseMode && i < 4 && kDemoTitle[i][0] != '\0';
                    const auto display = useDemo ? juce::String (juce::CharPointer_UTF8 (kDemoTitle[i]))
                                                 : (file.isNotEmpty() ? file : juce::String ("Untitled"));
                    g.setColour (juce::Colour (T::fgPrimary).withAlpha (0.95f));
                    g.setFont (juce::FontOptions (T::tName, juce::Font::bold));
                    g.drawText (display, nameBox, juce::Justification::centredLeft, true);
                }
            }

            // ── Row 2: the numerics ──────────────────────────────────────────
            if (deckHasContent (i))
            {
                const auto bpmValue   = deckControls[(size_t) i].bpm.getValue();
                const auto detectKey  = deckControls[(size_t) i].keyInfo.getText().upToFirstOccurrenceOf (" ", false, false);
                const int  keyChoice  = (int) std::round (processorRef.parameters().getRawParameterValue (deckPrefix (i) + "KeyTarget")->load());
                const bool detectValid = detectKey.isNotEmpty() && detectKey != "Key:" && detectKey != "unknown";
                const auto keyText     = keyChoice > 0 ? processorRef.getKeyTargetChoices()[keyChoice]
                                                       : (detectValid ? detectKey : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")));

                // Interactive chips read brighter than static ones — that IS the
                // affordance (drag the BPM, click the key).
                auto drawChip = [&] (juce::Rectangle<int> chip, const juce::String& text, float size)
                {
                    g.setColour (juce::Colour (T::bgWell));
                    g.fillRoundedRectangle (chip.toFloat(), T::radiusPill);
                    g.setColour (deckAccent (i).withAlpha (0.55f));
                    g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), T::radiusPill, 1.0f);
                    g.setColour (deckAccent (i));
                    g.setFont (juce::FontOptions (size, juce::Font::bold));
                    g.drawText (text, chip.reduced (5, 0), juce::Justification::centred);
                };

                if (const auto bpmChip = deckBpmChipBounds[(size_t) i]; ! bpmChip.isEmpty())
                    drawChip (bpmChip, juce::String (bpmValue, 1), T::tControl);
                if (const auto keyChip = deckKeyChipBounds[(size_t) i]; ! keyChip.isEmpty())
                    drawChip (keyChip, juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xa9 ")) + keyText, T::tBody);

                // Separation provenance (NEURAL / DSP) is RECESSED tier: honest about
                // which engine produced the stems, but it is not a control.
                if (const auto srcChip = deckSrcChipBounds[(size_t) i]; ! srcChip.isEmpty())
                {
                    const bool neural = processorRef.deckHasNeuralStems (i);
                    const bool dsp    = processorRef.isDeckRtssActive (i);
                    if (neural || dsp)
                    {
                        const auto srcCol = neural ? juce::Colour (0xff62f5c8) : juce::Colour (T::fgTertiary);
                        g.setColour (srcCol.withAlpha (0.10f));
                        g.fillRoundedRectangle (srcChip.toFloat(), T::radiusPill);
                        g.setColour (srcCol.withAlpha (0.65f));
                        g.setFont (juce::FontOptions (T::tCaption, juce::Font::bold).withKerningFactor (T::trackLabel));
                        g.drawText (neural ? "NEURAL" : "DSP", srcChip, juce::Justification::centred);
                    }
                }
            }
        }

        // --- STEM SEPARATION centerpiece: accent-tinted module + per-stem labels ---
        auto stemBox = deckStemBounds[(size_t) i];
        if (! stemBox.isEmpty())
        {
            // djay-Pro stem cells: DRUMS | BASS | MUSIC | VOCALS | FX — each a
            // rounded box with a header; matches the fader layout in resized().
            static constexpr std::array<const char*, 5> cellNames { "DRUMS", "BASS", "MUSIC", "VOCALS", "FX" };
            const int cellW = juce::jmax (28, stemBox.getWidth() / 5);
            auto cells = stemBox;
            for (int s = 0; s < 5; ++s)
            {
                const auto cell = (s < 4 ? cells.removeFromLeft (cellW) : cells).reduced (3, 2);
                g.setColour (deckAccent (i).withAlpha (0.08f));
                g.fillRoundedRectangle (cell.toFloat(), 5.0f);
                g.setColour (deckAccent (i).withAlpha (0.30f));
                g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 5.0f, 1.0f);

                g.setColour (deckAccent (i).withAlpha (0.92f));
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText (cellNames[(size_t) s], cell.withHeight (12).translated (0, 2),
                            juce::Justification::centred);

                if (s == 4)   // FX cell caption under the reverb knob (C = Delay, per mockup)
                {
                    g.setColour (juce::Colour (stemdeck::tokens::fgTertiary));
                    g.setFont (juce::FontOptions (8.5f));
                    g.drawText (i == 2 ? "Delay" : "Reverb", cell.withTop (cell.getBottom() - 11).translated (0, -1),
                                juce::Justification::centred);
                }
            }

            // (NEURAL / DSP separation-source badge now lives in the deck header —
            // see 1.1 — so it no longer collides with the VOCALS cell label.)
        }
    }

    // --- Control labels above each control (matches djay-Pro nomenclature) ---
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto drawAbove = [&g] (const juce::String& text, const juce::Component& component)
        {
            if (! component.isVisible()) return;   // tab-hidden controls draw no stray label
            // Expanded horizontally because the label belongs to the CELL, not to the
            // knob inside it. An EQ knob is ~16px wide at a four-channel layout, so
            // measuring the label against the knob ellipsized "HIGH" to "HI…" —
            // which breaks the same rule that made PITCH into "PIT" two lines below.
            auto r = component.getBounds().withHeight (12).translated (0, -13).expanded (7, 0);
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (deckAccent (i).withAlpha (0.92f));
        drawAbove ("VOL",   controls.volume);
        drawAbove ("XF",    controls.fader);
        drawAbove ("PIT",   controls.pitch);   // "PITCH" clipped in the 32px fader column — never ellipsize a permanent label
        drawAbove ("LOW",   controls.low);
        drawAbove ("MID",   controls.mid);
        drawAbove ("HIGH",  controls.high);
        drawAbove ("FILT",  controls.filter);
        drawAbove ("BPM",   controls.bpm);
        drawAbove ("BEAT",  controls.beatOffset);
        drawAbove ("DOWN",  controls.downbeat);
    }

    using namespace sattari::brand;

    // Per-stem FX grid headers (only when a deck's FX tab is active): column
    // labels DR/BA/MU/VO over the filter row, and F/S markers on the far left.
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto& dc = deckControls[(size_t) i];
        if (dc.activeToolTab != DeckControls::TabFx) continue;
        auto box = deckStemFxBounds[(size_t) i];
        if (box.isEmpty()) continue;
        static const char* cols[4] = { "DR", "BA", "MU", "VO" };
        auto header = box.removeFromTop (9);
        g.setColour (deckAccent (i).withAlpha (0.8f));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        const int colW = juce::jmax (18, header.getWidth() / 4);
        for (int s = 0; s < 4; ++s)
            g.drawText (cols[s], header.removeFromLeft (colW), juce::Justification::centred);
    }

    // Mixer panel box (left band).
    if (! mixerPanel.getBounds().isEmpty() && ! redoButton.getBounds().isEmpty())
    {
        const auto mixBox = mixerPanel.getBounds().getUnion (redoButton.getBounds()).expanded (10, 8);
        drawPanel (g, mixBox, juce::Colour (kAccent), 10.0f);
    }

    // Labels above the top-row sliders.
    g.setColour (juce::Colour (kTextMuted));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto sliderLabel = [&] (const juce::String& t, juce::Component& c)
    {
        if (c.isVisible() && ! c.getBounds().isEmpty())
            g.drawText (t, c.getBounds().withHeight (12).translated (0, -13), juce::Justification::centred);
    };
    sliderLabel (u8 ("CROSSFADER A/C \xe2\x86\x94 B/D"), crossfader);
    sliderLabel ("GLOBAL TEMPO", masterBpm);
    sliderLabel ("MASTER GAIN",  master);
    sliderLabel ("LIMIT",        limiter);
    sliderLabel ("MIC GAIN",     micGain);

    // Per-deck channel meters flanking the crossfader — see which deck is hot and
    // how the mix balances left (A/C) vs right (B/D) at a glance.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto mb = deckMeterBounds[(size_t) d];
        if (mb.isEmpty()) continue;
        const auto r = mb.toFloat();
        g.setColour (juce::Colour (stemdeck::tokens::bgWell));
        g.fillRoundedRectangle (r, 3.0f);

        auto barZone = r.reduced (2.0f).withTrimmedBottom (11.0f);   // reserve bottom for the letter
        const float peak = juce::jmax (processorRef.getDeckPeak (d, 0), processorRef.getDeckPeak (d, 1));
        const float db   = juce::Decibels::gainToDecibels (peak, -40.0f);
        const float norm = juce::jmap (juce::jlimit (-40.0f, 0.0f, db), -40.0f, 0.0f, 0.0f, 1.0f);
        const int   nSeg = 9;
        const int   lit  = (int) std::round (norm * (float) nSeg);
        const float segGap = 1.5f;
        const float segH = (barZone.getHeight() - segGap * (nSeg - 1)) / (float) nSeg;
        const auto accent = deckAccent (d);
        for (int seg = 0; seg < nSeg; ++seg)
        {
            const float frac = (float) (seg + 1) / (float) nSeg;
            juce::Colour c = frac > 0.9f ? juce::Colour (kDanger)
                           : frac > 0.7f ? juce::Colour (0xffffb020) : accent;
            g.setColour (seg < lit ? c : c.withAlpha (0.12f));
            const float y = barZone.getBottom() - (float) (seg + 1) * segH - (float) seg * segGap;
            g.fillRoundedRectangle (barZone.getX(), y, barZone.getWidth(), segH, 1.0f);
        }
        // Deck letter + crossfader-side indicator ("<" A/C, "=" centre, ">" B/D).
        // Clicking the meter cycles the side.
        const int   side = processorRef.getDeckCfSide (d);
        const char* mark = side == 0 ? "<" : side == 2 ? ">" : "=";
        g.setColour (accent.withAlpha (lit > 0 ? 0.95f : 0.5f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + d)) + " " + mark,
                    r.withTop (r.getBottom() - 11.0f), juce::Justification::centred);
    }

    // Focused deck marker: a 1px accent edge along the top of its column. Focus is
    // shown by emphasis, not by resizing, so the four decks stay lined up.
    if (! deckPanelBounds[(size_t) focusedDeck].isEmpty())
    {
        const auto fb = deckPanelBounds[(size_t) focusedDeck].toFloat();
        g.setColour (deckAccent (focusedDeck).withAlpha (0.9f));
        g.fillRoundedRectangle (fb.getX() + 2.0f, fb.getY(), fb.getWidth() - 4.0f, 2.0f, 1.0f);
    }

    // ── Collapsed deck strips (unused in the horizontal layout; kept for the
    // optional two-deck transition view) ───────────────────────────────────────
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto sb = deckCollapsedBounds[(size_t) i];
        if (sb.isEmpty()) continue;
        const bool loaded = deckHasContent (i);
        const bool playing = processorRef.parameters().getRawParameterValue (
            deckPrefix (i) + "Play")->load() > 0.5f;
        const auto accent = deckAccent (i);
        const auto stateAccent = processorRef.isMasterRecording()
                               ? juce::Colour (stemdeck::tokens::meterHot)
                               : playing ? juce::Colour (stemdeck::tokens::stemBas)
                                         : juce::Colour (stemdeck::tokens::borderSubtle);
        const float dim = loaded ? 1.0f : 0.4f;
        const auto r = sb.toFloat();

        g.setColour (juce::Colour (0xff131316));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (stateAccent);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        // 2px deck accent bar + letter.
        g.setColour (accent.withAlpha (dim));
        g.fillRoundedRectangle (r.getX() + 5.0f, r.getY() + 6.0f, 2.0f, r.getHeight() - 12.0f, 1.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                    juce::Rectangle<int> (sb.getX() + 10, sb.getY(), 14, sb.getHeight()),
                    juce::Justification::centredLeft);

        // BPM + key on the right (tabular-ish readouts).
        auto right = sb.reduced (10, 0).removeFromRight (112);
        g.setColour (juce::Colour (0xffB8B8BE).withAlpha (dim));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (loaded ? juce::String ((float) deckControls[(size_t) i].bpm.getValue(), 1) : juce::String ("--"),
                    right.removeFromLeft (54), juce::Justification::centredRight);
        right.removeFromLeft (6);
        const auto keyTxt = processorRef.getDeckKeyInfo (i).upToFirstOccurrenceOf (" ", false, false);
        g.setColour (accent.withAlpha (dim));
        g.drawText ((loaded && keyTxt.isNotEmpty() && keyTxt != "Key:") ? keyTxt : juce::String ("--"),
                    right, juce::Justification::centredRight);
    }

    // ── Master Control Band: bounded-panel frames + headers ─────────────────────
    {
        auto panelFrame = [&g] (juce::Rectangle<int> b, const juce::String& header)
        {
            if (b.isEmpty()) return;
            const auto r = b.toFloat();
            g.setColour (juce::Colour (0xff131316));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (0xff2A2A30));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            if (header.isNotEmpty())
            {
                g.setColour (juce::Colour (0xff8A8A93));
                g.setFont (juce::FontOptions (8.0f, juce::Font::bold).withKerningFactor (0.08f));
                g.drawText (header, b.reduced (8, 5).withHeight (10), juce::Justification::centredLeft);
            }
        };
        // SCOPE + MASTER FX + MASTER OUT self-draw their own panel look; the SATTARI
        // FX rack self-draws its own frame in the crossfader's old slot. Frame SYNC.
        panelFrame (syncPanelBounds, "SYNC");
    }

    // --- MASTER OUTPUT panel: title, stereo segment meters + dB scale ---
    if (! masterOutBounds.isEmpty())
    {
        namespace sdp = stemdeck::tokens;
        const auto r = masterOutBounds.toFloat();
        g.setColour (juce::Colour (sdp::bgPanel));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.35f).reduced (3.0f, 2.0f), 8.0f);
        g.setColour (juce::Colour (sdp::borderSubtle).withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (juce::Colour (kAccent));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("MASTER OUTPUT", masterOutBounds.reduced (12, 6).withHeight (14),
                    juce::Justification::centredLeft);

        // GLOBAL TEMPO box: label, value (120.0), and a small green "synced" square.
        if (! masterTempoBounds.isEmpty())
        {
            const auto tb = masterTempoBounds.toFloat();
            g.setColour (juce::Colour (sdp::bgWell));
            g.fillRoundedRectangle (tb, 5.0f);
            g.setColour (juce::Colour (sdp::borderSubtle));
            g.drawRoundedRectangle (tb.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (juce::Colour (sdp::fgTertiary));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText ("GLOBAL TEMPO", tb.withHeight (11.0f).translated (0.0f, 3.0f), juce::Justification::centred);
            const float bpm = (float) processorRef.parameters().getRawParameterValue ("masterBpm")->load();
            auto valRow = tb.withTrimmedTop (13.0f);
            g.setColour (juce::Colour (sdp::fgPrimary));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));   // set-and-forget → smaller than the crossfader value
            g.drawText (juce::String (bpm, 1), valRow.translated (6.0f, -2.0f), juce::Justification::centred);
            g.setColour (juce::Colour (0xff37d98a));
            g.fillRoundedRectangle (valRow.getRight() - 15.0f, valRow.getCentreY() - 6.0f, 8.0f, 8.0f, 1.5f);
        }

        if (! masterMeterBounds.isEmpty())
        {
            auto meterArea = masterMeterBounds;
            auto scaleCol = meterArea.removeFromRight (22);
            const int nSeg = 14;
            const float segGap = 2.0f;
            const float segH = ((float) meterArea.getHeight() - segGap * (nSeg - 1)) / (float) nSeg;
            const int barW = juce::jmax (6, (meterArea.getWidth() - 6) / 2);

            for (int chIdx = 0; chIdx < 2; ++chIdx)
            {
                const float db   = juce::Decibels::gainToDecibels (processorRef.getMasterPeak (chIdx), -40.0f);
                const float norm = juce::jmap (juce::jlimit (-40.0f, 0.0f, db), -40.0f, 0.0f, 0.0f, 1.0f);
                const int   lit  = (int) std::round (norm * (float) nSeg);
                const float x    = (float) meterArea.getX() + (float) chIdx * (float) (barW + 6);

                for (int seg = 0; seg < nSeg; ++seg)
                {
                    const float frac = (float) (seg + 1) / (float) nSeg;   // 0..1 bottom→top
                    juce::Colour c = frac > 0.92f ? juce::Colour (kDanger)
                                   : frac > 0.72f ? juce::Colour (0xffffb020)
                                                  : juce::Colour (0xff37d98a);
                    const bool on = seg < lit;
                    g.setColour (on ? c : c.withAlpha (0.13f));
                    const float y = (float) meterArea.getBottom() - (float) (seg + 1) * segH - (float) seg * segGap;
                    g.fillRoundedRectangle (x, y, (float) barW, segH, 1.0f);
                }
            }

            // dB scale ticks — standard progression 0 / -6 / -12 / -18 / -24.
            g.setColour (juce::Colour (sdp::fgTertiary));
            g.setFont (juce::FontOptions (8.5f));
            for (const float tickDb : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
            {
                const float tn = juce::jmap (tickDb, -40.0f, 0.0f, 0.0f, 1.0f);
                const int y = meterArea.getBottom() - (int) (tn * (float) meterArea.getHeight()) - 4;
                g.drawText (juce::String ((int) tickDb), scaleCol.getX(), y, scaleCol.getWidth(), 8,
                            juce::Justification::centredLeft);
            }
        }

        // Live integrated LUFS (primary) + safety-limiter gain reduction. Always on —
        // the running loudness number matters while mixing, not only at export.
        if (! masterLufsBounds.isEmpty())
        {
            auto lb = masterLufsBounds.toFloat();
            const double ilufs = processorRef.getLiveIntegratedLufs();
            const double mlufs = processorRef.getLiveMomentaryLufs();
            const float  grDb  = processorRef.getLimiterGrDb();
            const float  ceil  = processorRef.parameters().getRawParameterValue ("limiterCeiling")->load();

            // Integrated LUFS — primary readout.
            auto top = lb.removeFromTop (17.0f);
            g.setColour (juce::Colour (sdp::fgTertiary));
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            g.drawText ("LUFS-I", top.removeFromLeft (40.0f), juce::Justification::centredLeft);
            const bool haveI = ilufs > -70.0;
            g.setColour (juce::Colour (sdp::fgPrimary));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (haveI ? juce::String (ilufs, 1) : juce::String (u8 ("\xe2\x80\x93\xe2\x80\x93")),
                        top, juce::Justification::centredRight);

            // Second row: momentary (small) + limiter GR bar with ceiling.
            g.setColour (juce::Colour (sdp::fgTertiary));
            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
            g.drawText (juce::String ("M ") + (mlufs > -70.0 ? juce::String (mlufs, 1) : juce::String ("--")),
                        lb.removeFromLeft (46.0f), juce::Justification::centredLeft);

            auto grA = lb;
            g.setColour (juce::Colour (sdp::fgTertiary));
            g.setFont (juce::FontOptions (7.5f));
            g.drawText ("GR", grA.removeFromLeft (14.0f), juce::Justification::centredLeft);
            // ceiling caption at the right.
            auto capA = grA.removeFromRight (46.0f);
            g.drawText (juce::String ("ceil ") + juce::String (ceil, 1), capA, juce::Justification::centredRight);
            // amber GR bar (0..6 dB) so heavy limiting is visible peripherally.
            auto track = grA.reduced (2.0f, 0.0f).withSizeKeepingCentre (grA.getWidth() - 4.0f, 4.0f);
            g.setColour (juce::Colour (sdp::borderSubtle));
            g.fillRoundedRectangle (track, 1.5f);
            const float grNorm = juce::jlimit (0.0f, 1.0f, grDb / 6.0f);
            if (grNorm > 0.001f)
            {
                g.setColour (juce::Colour (0xffFFB84A));
                g.fillRoundedRectangle (track.withWidth (track.getWidth() * grNorm), 1.5f);
            }
        }

        // Smart-master readout: loudness vs. the mode target + a live gain-reduction
        // bar, so the "AI" is legible instead of a black box.
        if (aiMasterButton.getToggleState())
        {
            const float lufs   = processorRef.getSmartMasterLufs();
            const float gr     = processorRef.getSmartMasterGr();
            const int   mode   = aiMasterMode.getSelectedId();          // 1 Streaming, 2 Club, 3 Warm
            const float target = mode == 2 ? -9.0f : mode == 3 ? -16.0f : -14.0f;
            const bool  live   = lufs > -45.0f;

            auto strip  = masterOutBounds.reduced (12, 4).withTop (masterOutBounds.getBottom() - 16).toFloat();
            auto grArea = strip.removeFromRight (58.0f);

            // Loudness current → target: green when locked in (±1 dB), amber while converging.
            const bool locked = live && std::abs (lufs - target) <= 1.0f;
            juce::String txt = (live ? juce::String (lufs, 1) : juce::String ("--"))
                             + u8 (" \xe2\x86\x92 ") + juce::String (target, 0) + " LUFS";
            g.setColour ((locked ? juce::Colour (0xff37d98a) : juce::Colour (0xffffb020)).withAlpha (0.95f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (txt, strip, juce::Justification::bottomLeft);

            // Gain-reduction bar (0..6 dB), lit amber as the bus compressor works.
            g.setColour (juce::Colour (kTextMuted));
            g.setFont (juce::FontOptions (7.5f));
            g.drawText ("GR", grArea.removeFromLeft (13.0f), juce::Justification::centredLeft);
            auto barTrack = grArea.withSizeKeepingCentre (grArea.getWidth(), 4.0f);
            g.setColour (juce::Colour (kHairline));
            g.fillRoundedRectangle (barTrack, 1.5f);
            const float grNorm = juce::jlimit (0.0f, 1.0f, gr / 6.0f);
            if (grNorm > 0.001f)
            {
                g.setColour (juce::Colour (0xffffb020));
                g.fillRoundedRectangle (barTrack.withWidth (barTrack.getWidth() * grNorm), 1.5f);
            }
        }
    }

    // Empty decks are already reduced to a single actionable load surface in
    // resized(). No scrim is needed: the old overlay made even the primary empty
    // action look disabled, which is the opposite of progressive disclosure.
}

bool MainPluginEditor::deckHasContent (int i) const
{
    if (! juce::isPositiveAndBelow (i, MainPluginProcessor::numDecks)) return false;
    if (processorRef.getDeckPath (i).isNotEmpty()) return true;
    static const char* kDemo[4] = { "x", "x", "x", "" };   // A/B/C have showcase titles, D does not
    return showcaseMode && i < 4 && kDemo[i][0] != '\0';
}

// ── Focus model ───────────────────────────────────────────────────────────────
// Show/hide a deck's full control surface. Collapsed decks keep only play + cue +
// a thin overview waveform; everything else is hidden so the focused deck can have
// the height. Hidden controls keep their attachments, so state survives focus flips.
void MainPluginEditor::setDeckDetailVisible (int i, bool vis)
{
    auto& dc = deckControls[(size_t) i];
    auto hide = [vis] (juce::Component& c) { c.setVisible (vis); if (! vis) c.setBounds ({}); };

    for (juce::Component* c : { (juce::Component*) &dc.load,      (juce::Component*) &dc.stemLane,
                                (juce::Component*) &dc.loadStem,  (juce::Component*) &dc.aiStems,
                                (juce::Component*) &dc.extractMidi, (juce::Component*) &dc.extractDrums,
                                (juce::Component*) &dc.tempoInterpretation, (juce::Component*) &dc.syncMode,
                                (juce::Component*) &dc.keyTarget, (juce::Component*) &dc.keyLock,
                                (juce::Component*) &dc.liveKey,   (juce::Component*) &dc.keyInfo,
                                (juce::Component*) &dc.stemStatus,
                                (juce::Component*) &dc.bpm,       (juce::Component*) &dc.beatOffset,
                                (juce::Component*) &dc.downbeat,  (juce::Component*) &dc.volume,
                                (juce::Component*) &dc.fader,     (juce::Component*) &dc.pitch,
                                (juce::Component*) &dc.low,       (juce::Component*) &dc.mid,
                                (juce::Component*) &dc.high,      (juce::Component*) &dc.filter,
                                (juce::Component*) &dc.loopIn,    (juce::Component*) &dc.loopOut,
                                (juce::Component*) &dc.loopToggle,(juce::Component*) &dc.loopHalf,
                                (juce::Component*) &dc.loopDouble,(juce::Component*) &dc.introEndBtn,
                                (juce::Component*) &dc.outroStartBtn, (juce::Component*) &dc.slipButton,
                                (juce::Component*) &dc.reverbMix, (juce::Component*) &dc.echoMix,
                                (juce::Component*) &dc.macroKnob })
        hide (*c);

    for (auto& s : dc.stemGains)     hide (s);
    for (auto& b : dc.hotCues)       hide (b);
    for (auto& b : dc.beatRollBtns)  hide (b);
    for (auto& b : dc.beatJumpBack)  hide (b);
    for (auto& b : dc.beatJumpFwd)   hide (b);
    for (auto& b : dc.cfSideBtns)    hide (b);
    for (auto& b : dc.toolTabs)      hide (b);
    for (auto& s : dc.stemFilt)      hide (s);
    for (auto& s : dc.stemSend)      hide (s);
    for (auto& s : dc.stemPitch)     hide (s);
}

// A collapsed deck: accent bar + letter (painted), play, thin overview waveform with
// playhead, then BPM/key (painted) and cue. Enough to monitor a deck you're about to
// bring in — nothing more.
void MainPluginEditor::layoutCollapsedDeck (int i, juce::Rectangle<int> strip)
{
    deckCollapsedBounds[(size_t) i] = strip;
    deckHeaderBounds[(size_t) i] = {};   // full-deck chrome doesn't paint when collapsed
    deckStemBounds[(size_t) i]   = {};
    setDeckDetailVisible (i, false);

    auto& dc = deckControls[(size_t) i];
    auto r = strip.reduced (8, 6);
    r.removeFromLeft (22);                       // painted accent bar + deck letter

    dc.play.setVisible (true);
    dc.play.setBounds (r.removeFromLeft (30).withSizeKeepingCentre (30, 22));
    r.removeFromLeft (4);
    dc.cue.setVisible (true);
    dc.cue.setBounds (r.removeFromLeft (42).withSizeKeepingCentre (42, 22));
    r.removeFromLeft (8);

    r.removeFromRight (120);                     // painted BPM + key readouts
    if (auto* w = deckWaves[(size_t) i].get())
    {
        w->setVisible (true);
        w->setBounds (r);                        // thin overview waveform
    }
}

// Downsample a buffer to `bins` absolute-peak values (0..1). Runs on a worker thread.
std::vector<float> MainPluginEditor::extractPeaks (const juce::AudioBuffer<float>& buf, int bins)
{
    std::vector<float> out ((size_t) juce::jmax (1, bins), 0.0f);
    const int n = buf.getNumSamples(), ch = buf.getNumChannels();
    if (n <= 0 || ch <= 0) return out;
    for (int b = 0; b < bins; ++b)
    {
        const int s0 = (int) ((juce::int64) b * n / bins);
        const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (b + 1) * n / bins));
        float peak = 0.0f;
        for (int c = 0; c < ch; ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (int i = s0; i < s1 && i < n; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        }
        out[(size_t) b] = juce::jmin (1.0f, peak);
    }
    return out;
}

// Extract overview + per-stem peaks for a deck on a worker thread (no blocking work
// on the UI thread, §8), then push them to the region on the message thread. Stem
// columns fall back to the full mix when that lane has no separated audio.
void MainPluginEditor::refreshDeckWaveData (int i)
{
    auto full = processorRef.getDeckLoadedAudio (i);
    if (full == nullptr || deckWaveRegions[(size_t) i] == nullptr) return;

    // lane order drums/bass/music/vocals (1..4); column order VOX/DRM/BAS/OTH.
    std::array<std::shared_ptr<const DeckPlayer::LoadedAudio>, 4> st;
    for (int l = 0; l < 4; ++l) st[(size_t) l] = processorRef.getDeckStemLoadedAudio (i, l + 1);

    const double total = full->audio.getNumSamples() > 0 && full->sourceSampleRate > 0
                       ? full->audio.getNumSamples() / full->sourceSampleRate : 0.0;
    const double bpm = (double) processorRef.getDeckActiveAnalysis (i).detectedBpm;

    const double sr = full->sourceSampleRate;
    auto safe = juce::Component::SafePointer<MainPluginEditor> (this);
    uiWorkPool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> (
        [safe, i, full, st, total, bpm, sr]() -> juce::ThreadPoolJob::JobStatus
    {
        auto overview = extractPeaks (full->audio, 2048);
        std::array<std::vector<float>, 4> stemPk;
        for (int l = 0; l < 4; ++l)
            if (st[(size_t) l] && st[(size_t) l]->audio.getNumSamples() > 0)
                stemPk[(size_t) l] = extractPeaks (st[(size_t) l]->audio, 2048);

        // Structure (intro/build/drop/breakdown/outro) from per-bar energy.
        std::vector<stemdeck::WaveformRegion::StructureRegion> regions;
        if (bpm > 1.0)
        {
            const auto sections = stemdeck::segmentStructure (full->audio, sr, bpm);
            double lastEnd = 0.0;
            for (const auto& s : sections) lastEnd = juce::jmax (lastEnd, s.endBeat);
            if (lastEnd > 0.0)
                for (const auto& s : sections)
                    regions.push_back ({ (float) (s.startBeat / lastEnd), (float) (s.endBeat / lastEnd),
                                         s.label.toLowerCase(), waveStructureColour (s.label) });
        }

        // Key-over-time (§6.2 Phase 0): chroma key per window -> camelot-labelled bands
        // so modulations are visible on the overview. Absolute key accuracy is the
        // user's audio check; the pipeline (windowing/smoothing) is unit-tested.
        std::vector<stemdeck::WaveformRegion::KeyBand> keyBands;
        {
            const auto curve = stemdeck::analyzeKeyCurve (full->audio, sr, 12);
            for (const auto& seg : stemdeck::segmentKeyCurve (curve, 1.0))
            {
                const int num = stemdeck::camelotNumberFor (seg.pitchClass, seg.minor);
                keyBands.push_back ({ (float) seg.startBeat, (float) seg.endBeat,
                                      juce::String (num) + (seg.minor ? "A" : "B"),
                                      juce::Colour::fromHSV ((num - 1) / 12.0f, 0.45f, seg.minor ? 0.7f : 0.9f, 1.0f) });
            }
        }

        juce::MessageManager::callAsync ([safe, i, overview = std::move (overview),
                                          stemPk = std::move (stemPk), regions = std::move (regions),
                                          keyBands = std::move (keyBands), total, bpm]() mutable
        {
            if (safe == nullptr) return;
            auto* r = safe->deckWaveRegions[(size_t) i].get();
            if (r == nullptr) return;
            r->setTotalSeconds (total);
            if (bpm > 0) r->setBpm (bpm);
            r->setOverviewPeaks (overview);
            r->setStructure (regions);
            r->setKeyBands (keyBands);
            // Map columns VOX/DRM/BAS/OTH -> lanes vocals/drums/bass/music (st idx 3/0/1/2).
            const int colToSt[4] = { 3, 0, 1, 2 };
            for (int col = 0; col < 4; ++col)
            {
                auto& pk = stemPk[(size_t) colToSt[col]];
                r->setStemPeaks (col, pk.empty() ? overview : pk);
            }
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

void MainPluginEditor::setFocusedDeck (int i)
{
    i = juce::jlimit (0, MainPluginProcessor::numDecks - 1, i);
    if (i == focusedDeck) return;
    focusedDeck = i;
    resized();
    repaint();
}

// Keep the live/empty transition explicit. Empty controls are now removed from
// layout rather than merely faded, so the remaining load target stays fully legible.
// ── Three tiers of visual weight ────────────────────────────────────────────
// A channel strip carries roughly forty interactive controls. Rendered at one
// prominence they give the eye no entry point: the PLAY button competes for
// attention with the CUE/LOOP/FX/SYNC/SRC tab row and the 1–8 cue pads, so
// finding the transport under pressure is a search rather than a glance.
//
// In a perform context about five controls need to be instantly hittable —
// play, cue, level, filter, and the crossfade assign. Those are HERO. The
// things you set and leave (EQ, pitch, stem gains) are STANDARD. The things
// that are navigation or configuration rather than performance (the tab strip,
// cue pads, per-stem FX grid, beat-jump) are RECESSED.
//
// This is purely a rendering weight — every control keeps exactly the same hit
// target and behaviour. It costs nothing functionally and transforms the scan.
void MainPluginEditor::applyDeckControlWeights (int i)
{
    if (! juce::isPositiveAndBelow (i, MainPluginProcessor::numDecks))
        return;

    constexpr float kHero = 1.0f, kStandard = 0.86f, kRecessed = 0.62f;

    auto& c = deckControls[(size_t) i];
    auto set = [] (juce::Component& comp, float a) { if (comp.isVisible()) comp.setAlpha (a); };

    // HERO — the five things reached for without looking.
    set (c.play,   kHero);
    set (c.cue,    kHero);
    set (c.volume, kHero);
    set (c.filter, kHero);
    for (auto& b : c.cfSideBtns) set (b, kHero);

    // STANDARD — set once, adjusted deliberately.
    set (c.low, kStandard); set (c.mid, kStandard); set (c.high, kStandard);
    set (c.fader, kStandard); set (c.pitch, kStandard);
    for (auto& k : c.stemGains) set (k, kStandard);
    set (c.bpm, kStandard); set (c.beatOffset, kStandard); set (c.downbeat, kStandard);

    // RECESSED — navigation and configuration, not performance.
    for (auto& t : c.toolTabs)      set (t, kRecessed);
    for (auto& h : c.hotCues)       set (h, kRecessed);
    for (auto& b : c.beatRollBtns)  set (b, kRecessed);
    for (auto& b : c.beatJumpBack)  set (b, kRecessed);
    for (auto& b : c.beatJumpFwd)   set (b, kRecessed);
    for (auto& s : c.stemFilt)      set (s, kRecessed);
    for (auto& s : c.stemSend)      set (s, kRecessed);
    for (auto& s : c.stemPitch)     set (s, kRecessed);
    set (c.introEndBtn, kRecessed);
    set (c.outroStartBtn, kRecessed);
    set (c.loopIn, kRecessed); set (c.loopOut, kRecessed); set (c.loopHalf, kRecessed);
    set (c.loopDouble, kRecessed);
    set (c.reverbMix, kRecessed); set (c.echoMix, kRecessed);
    set (c.tempoInterpretation, kRecessed); set (c.syncMode, kRecessed); set (c.keyTarget, kRecessed);
    set (c.load, kRecessed); set (c.loadStem, kRecessed); set (c.stemLane, kRecessed);
    set (c.extractMidi, kRecessed); set (c.extractDrums, kRecessed);

    // LOOP and Slip stay STANDARD — they are performance controls, just not in
    // the top five, and recessing them would hide a live loop's on-state.
    set (c.loopToggle, kStandard);
    set (c.slipButton, kStandard);
    set (c.aiStems, kStandard);
    set (c.keyLock, kStandard);
    set (c.liveKey, kStandard);
    set (c.macroKnob, kStandard);
}

// ── Per-deck control strip: wire callbacks to the processor ────────────────────
void MainPluginEditor::configureDeckStrip (int i)
{
    auto* strip = deckStrips[(size_t) i].get();
    if (strip == nullptr) return;
    auto& cb = strip->callbacks;
    const auto prefix = deckPrefix (i);
    auto& apvts = processorRef.parameters();

    auto setFloat = [&apvts] (const juce::String& id, float value)
    {
        if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id)))
            fp->setValueNotifyingHost (fp->convertTo0to1 (value));
    };
    auto getFloat = [&apvts] (const juce::String& id) -> float
    {
        if (auto* v = apvts.getRawParameterValue (id)) return v->load();
        return 0.0f;
    };
    auto setBoolP = [&apvts] (const juce::String& id, bool on)
    {
        if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (id)))
            bp->setValueNotifyingHost (on ? 1.0f : 0.0f);
    };
    auto secPerBeat = [this, i]
    {
        const float bpm = processorRef.getDeckActiveAnalysis (i).detectedBpm;
        return 60.0 / (double) (bpm > 20.0f ? bpm : 120.0f);
    };

    cb.onRequestFocus = [this, i] { setFocusedDeck (i); };   // single source of truth

    cb.onTogglePlay = [this, prefix]
    {
        if (auto* p = processorRef.parameters().getParameter (prefix + "Play"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    };

    // CUE: press = jump to the cue point (setting one at the playhead if none) and
    // play; release = stop and return to the cue.
    cb.onCue = [this, i, prefix] (bool press)
    {
        auto* play = processorRef.parameters().getParameter (prefix + "Play");
        if (press)
        {
            double cue = processorRef.getHotCue (i, 0);
            if (cue < 0.0) { processorRef.setHotCue (i, 0, processorRef.getDeckPlayheadSeconds (i)); cue = processorRef.getHotCue (i, 0); }
            if (cue >= 0.0) processorRef.seekDeckSeconds (i, cue);
            if (play) play->setValueNotifyingHost (1.0f);
            stripCueHeld[(size_t) i] = true;
        }
        else
        {
            if (play) play->setValueNotifyingHost (0.0f);
            const double cue = processorRef.getHotCue (i, 0);
            if (cue >= 0.0) processorRef.seekDeckSeconds (i, cue);
            stripCueHeld[(size_t) i] = false;
        }
    };

    // Loop cluster — halve / double the pending length; toggle arms a loop of that
    // length at the playhead. Re-applies live if the loop is already active.
    auto reapplyLoop = [this, i, secPerBeat]
    {
        if (! processorRef.isDeckLoopActive (i)) return;
        double in = processorRef.getDeckLoopInSeconds (i);
        if (in < 0.0) in = processorRef.getDeckPlayheadSeconds (i);
        processorRef.setDeckLoopSeconds (i, in, in + stripLoopBeats[(size_t) i] * secPerBeat());
    };
    cb.onLoopHalve  = [this, i, reapplyLoop] { stripLoopBeats[(size_t) i] = juce::jmax (0.125, stripLoopBeats[(size_t) i] * 0.5);  reapplyLoop(); };
    cb.onLoopDouble = [this, i, reapplyLoop] { stripLoopBeats[(size_t) i] = juce::jmin (32.0,  stripLoopBeats[(size_t) i] * 2.0);  reapplyLoop(); };
    cb.onLoopToggle = [this, i, secPerBeat]
    {
        if (processorRef.isDeckLoopActive (i)) { processorRef.setDeckLoopActive (i, false); return; }
        const double in = processorRef.getDeckPlayheadSeconds (i);
        processorRef.setDeckLoopSeconds (i, in, in + stripLoopBeats[(size_t) i] * secPerBeat());
        processorRef.setDeckLoopActive (i, true);
    };

    cb.onBeatJump = [this, i] (int dir) { processorRef.beatJumpDeck (i, (double) (dir * stripJumpBars[(size_t) i])); };
    cb.onCycleBeatJump = [this, i]
    {
        static const int steps[] { 1, 2, 4, 8, 16 };
        int idx = 0; for (int k = 0; k < 5; ++k) if (steps[k] == stripJumpBars[(size_t) i]) idx = k;
        stripJumpBars[(size_t) i] = steps[(idx + 1) % 5];
    };

    cb.onToggleSync = [this, prefix, &apvts]
    {
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "SyncMode")))
        {
            const int cur = cp->getIndex();
            const int next = cur == 0 ? 1 : 0;   // off <-> first sync mode
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) next));
        }
    };

    // KEY: click opens the Camelot wheel for this deck; scroll nudges transposition.
    cb.onOpenKey  = [this, i] { showCamelotWheel (i); };
    cb.onNudgeKey = [this, prefix, getFloat, setFloat] (int steps)
    {
        setFloat (prefix + "Pitch", juce::jlimit (-12.0f, 12.0f, getFloat (prefix + "Pitch") + (float) steps));
    };

    // MARK: drop a marker (hot cue in the next free slot) at the playhead and flash a
    // confirmation. This is a cue-marker feature by design — not take/comp capture.
    cb.onMarkTake = [this, i]
    {
        int slot = -1;
        for (int c = 1; c < MainPluginProcessor::numHotCues; ++c)
            if (processorRef.getHotCue (i, c) < 0.0) { slot = c; break; }
        if (slot >= 0) processorRef.setHotCue (i, slot, processorRef.getDeckPlayheadSeconds (i));
        automixStatusLabel.setText (slot >= 0 ? "Marker set on deck " + juce::String (deckLabel (i))
                                              : "All marker slots used",
                                    juce::dontSendNotification);
    };

    // Stem switches are event-owned processor state. This preserves intent across
    // capture/replay instead of inferring it from editor-only gain bookkeeping.
    cb.onToggleStem = [this, i] (int idx)
    {
        processorRef.setStemMutedDispatched (i, idx, ! processorRef.isStemMuted (i, idx));
    };
    cb.onSoloStem = [this, i] (int idx)
    {
        processorRef.setStemSoloedDispatched (i, idx, ! processorRef.isStemSoloed (i, idx));
    };

    cb.onCycleQuantize = [this, i] { stripQuant[(size_t) i] = (stripQuant[(size_t) i] + 1) % 5; };
    cb.onToggleSlip    = [this, i] { processorRef.setDeckSlipEnabled (i, ! processorRef.isDeckSlipEnabled (i)); };

    // FX sends: A = echo, B = reverb (real mix params). C is a UI-only placeholder
    // for a third send rack (pending routing).
    cb.onToggleFx = [this, i, prefix, getFloat, setFloat] (int which)
    {
        if (which == 0) setFloat (prefix + "EchoMix", getFloat (prefix + "EchoMix") > 0.02f ? 0.0f : 0.35f);
        else if (which == 1) setFloat (prefix + "RevMix", getFloat (prefix + "RevMix") > 0.02f ? 0.0f : 0.35f);
    };

    strip->onExpandChanged = [this] { resized(); };
}

// ── Push live processor state into every deck strip ───────────────────────────
void MainPluginEditor::refreshDeckStrips()
{
    auto& apvts = processorRef.parameters();
    auto getFloat = [&apvts] (const juce::String& id) -> float
    { if (auto* v = apvts.getRawParameterValue (id)) return v->load(); return 0.0f; };

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto* strip = deckStrips[(size_t) i].get();
        if (strip == nullptr) continue;
        const auto prefix = deckPrefix (i);

        stemdeck::DeckStrip::State s;
        s.hasTrack = processorRef.getDeckPath (i).isNotEmpty();
        s.playing  = getFloat (prefix + "Play") > 0.5f;
        s.cueSet   = processorRef.getHotCue (i, 0) >= 0.0;
        s.cueHeld  = stripCueHeld[(size_t) i];
        s.loopActive = processorRef.isDeckLoopActive (i);
        s.loopArmed  = ! s.loopActive && processorRef.getDeckLoopOutSeconds (i) > processorRef.getDeckLoopInSeconds (i);
        s.loopBeats  = stripLoopBeats[(size_t) i];
        s.beatJumpBars = stripJumpBars[(size_t) i];

        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (prefix + "SyncMode")))
        {
            s.syncOn = cp->getIndex() != 0;
            const auto phase = processorRef.getDeckSyncPhase (i);   // measured, not faked
            s.syncLocked  = phase.locked;
            s.phaseOffset = phase.offset;
        }

        const auto an = processorRef.getDeckActiveAnalysis (i);
        if (an.hasKey())
        {
            static const int camMaj[] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
            static const int camMin[] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
            const int num = (an.keyIsMinor ? camMin : camMaj)[an.keyPitchClass % 12];
            s.keyCamelot = juce::String (num) + (an.keyIsMinor ? "A" : "B");
            s.keyLowConf = an.keyConfidence < 0.5f;
        }
        else { s.keyCamelot = "--"; s.keyLowConf = false; }
        s.keySemis = (int) std::lround (getFloat (prefix + "Pitch"));

        s.slipOn = processorRef.isDeckSlipEnabled (i);
        s.quantIndex = stripQuant[(size_t) i];

        for (int k = 0; k < 4; ++k)
        {
            s.stem[(size_t) k] = processorRef.isStemSoloed (i, k) ? 2
                              : processorRef.isStemMuted (i, k) ? 1 : 0;
        }
        s.sepProgress = processorRef.getDeckStemSeparationProgress (i);   // real AI-split job progress

        s.fx = { { getFloat (prefix + "EchoMix") > 0.02f,
                   getFloat (prefix + "RevMix")  > 0.02f,
                   false } };   // third send removed (routed no audio)
        s.focused = (i == focusedDeck);

        strip->setState (s);
    }
}

// Geometry for the three interactive chips in a deck's header strip: the letter
// badge (click = make this the sync master), the BPM chip (click/drag/wheel) and
// the key chip (click = key menu).
//
// These are hit targets, so they must be a product of the LAYOUT pass. They used
// to be assigned as a side effect of paint(): mouseDown() and friends read
// rectangles that only existed once a paint had run at the current size, so any
// click arriving between a resize and the next paint was tested against stale
// geometry. Deriving them here makes the layout the single source of truth and
// leaves paint() a pure function of it.
void MainPluginEditor::layoutDeckHeaderChips (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;

    const auto idx = (size_t) deckIndex;
    auto headerBar = deckHeaderBounds[idx];

    deckBadgeBounds[idx]   = {};
    deckBpmChipBounds[idx] = {};
    deckKeyChipBounds[idx] = {};
    deckNameBounds[idx]    = {};
    deckSrcChipBounds[idx] = {};

    if (headerBar.isEmpty())
        return;

    // TWO ROWS. One row could not hold badge + name + three chips at a ~230px
    // column: the chips were sized by `jmin(x, width/3)` taken in sequence, so the
    // BPM chip collapsed to ~39px ("120.2…") and the track name was left ~21px
    // ("ang…"). Meanwhile an EMPTY channel gave "READY FOR AUDIO" the full width —
    // the most informative text on the strip was the most starved, and the least
    // informative was the most generous.
    //
    // Row 1 is identity: badge + track name, which now gets the whole width.
    // Row 2 is the numerics: BPM and key, which a DJ reads constantly and which
    // therefore deserve a real size rather than a 39px sliver.
    auto row1 = headerBar.removeFromTop (juce::jmin (20, headerBar.getHeight()));
    deckBadgeBounds[idx] = row1.removeFromLeft (20);
    row1.removeFromLeft (stemdeck::tokens::gap5);
    deckNameBounds[idx] = row1;

    // An empty channel shows its drop target instead of chips — nothing to hit.
    if (! deckHasContent (deckIndex) || headerBar.getHeight() <= 2)
        return;

    headerBar.removeFromTop (stemdeck::tokens::gap1);
    auto row2 = headerBar;

    // BPM is the number read most, so it leads and keeps the larger cell.
    const int bpmW = juce::jmin (76, juce::jmax (48, row2.getWidth() * 40 / 100));
    deckBpmChipBounds[idx] = row2.removeFromLeft (bpmW);
    row2.removeFromLeft (stemdeck::tokens::gap3);

    const int keyW = juce::jmin (52, juce::jmax (34, row2.getWidth() * 45 / 100));
    deckKeyChipBounds[idx] = row2.removeFromLeft (keyW);
    row2.removeFromLeft (stemdeck::tokens::gap3);

    // NEURAL / DSP is provenance, not a control: recessed tier, and the first
    // thing to go when the row is tight.
    if (row2.getWidth() >= 40)
        deckSrcChipBounds[idx] = row2.removeFromRight (juce::jmin (44, row2.getWidth()));
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (50);
    headerBarBounds = header;

    // ── Header: ONE priority stack, left to right ────────────────────────────
    // This used to be three independent passes — removeFromLeft for the nav and
    // transport, removeFromRight for the tools cluster, and "whatever's left" for
    // the title. Their fixed widths sum to 1528px, so on a 1280-wide window (a
    // 1232px bar) the two passes overran each other by ~296px and removeFromRight
    // silently clamped: the presets combo painted straight over the tempo chip.
    //
    // Expressing the bar as a single stack makes that impossible — LayoutStack
    // sheds whole items, lowest priority first, and never returns overlapping
    // rectangles. Priorities read as "what does a DJ lose last": the transport
    // and the deck nav outrank the tempo readout, which outranks the preset
    // plumbing, which outranks the wordmark.
    {
        namespace L = stemdeck::layout;
        namespace T = stemdeck::tokens;

        enum HdrIdx { hNav0, hNav1, hNav2, hNav3, hSearch, hPlay, hFocus, hInspect,
                      hTitle, hTempo, hPresets, hSavePreset, hAddCh, hRemoveCh,
                      hTools, hSettings, hNumItems };

        const std::vector<L::Item> items {
            L::Item::fixed (66, /*pri*/ 9, T::gap3),   // LIBRARY
            L::Item::fixed (58, /*pri*/ 9, T::gap3),   // PERFORM
            L::Item::fixed (52, /*pri*/ 8, T::gap3),   // MIX
            L::Item::fixed (68, /*pri*/ 8, T::gap6),   // ARRANGE
            L::Item::fixed (94, /*pri*/ 7, T::gap5),   // global command palette
            L::Item::fixed (118, /*pri*/ 10, T::gap5), // PLAY ALL — transport never sheds
            L::Item::fixed (66,  /*pri*/ 6, T::gap3),  // distraction-free focus
            L::Item::fixed (70,  /*pri*/ 5, T::gap6),  // contextual inspector
            // The wordmark is branding: min 0, soaks up leftover, sheds silently.
            L::Item::flexible (0, 0, 1.0f, /*pri*/ 1, T::gap6),
            L::Item::fixed (150, /*pri*/ 8, T::gap6),  // TEMPO chip — BPM is load-bearing
            L::Item::fixed (160, /*pri*/ 2, T::gap6),  // user presets
            L::Item::fixed (108, /*pri*/ 3, T::gap5),  // Save Preset
            L::Item::fixed (82,  /*pri*/ 5, T::gap3),  // + CHANNEL
            L::Item::fixed (52,  /*pri*/ 5, T::gap5),  // channel count
            L::Item::fixed (64,  /*pri*/ 6, T::gap5),  // TOOLS
            L::Item::fixed (32,  /*pri*/ 6, 0),        // gear
        };
        static_assert (hNumItems == 16, "header item count must match the priority table");
        jassert ((int) items.size() == hNumItems);

        const auto cells = L::columns (header, items);

        // Nav cells keep their 46px tall click target and feed navRects, which
        // paint() uses for the icon/label and the sliding underline.
        juce::Component* navBtns[4] = { &toggleLibraryButton, &navDecksButton,
                                        &mixerButton, &pianoRollButton };
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = cells[(size_t) (hNav0 + i)];
            const auto sized = cell.isEmpty() ? juce::Rectangle<int>()
                                              : cell.withSizeKeepingCentre (cell.getWidth(), 46);
            navRects[(size_t) i] = sized;
            navBtns[i]->setBounds (sized);
            navBtns[i]->setVisible (! sized.isEmpty());
        }
        toggleBrowserButton.setVisible (false);
        toggleBrowserButton.setBounds ({});

        // Height is per-item (34 for transport, 30 for chrome, 28 for the combo);
        // an empty cell means the item was shed, so it gets empty bounds.
        auto place = [&cells] (juce::Component& c, int idx, int h)
        {
            const auto cell = cells[(size_t) idx];
            if (cell.isEmpty()) { c.setVisible (false); c.setBounds ({}); return; }
            c.setVisible (true);
            c.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), h));
        };

        place (commandButton,       hSearch,     30);
        place (masterPlayButton,    hPlay,       34);
        place (focusModeButton,     hFocus,      30);
        place (inspectorButton,     hInspect,    30);
        topRecordButton.setVisible (false); topRecordButton.setBounds ({});
        automixButton.setVisible (false); automixButton.setBounds ({});
        automixStatusLabel.setVisible (false); automixStatusLabel.setBounds ({});
        place (savePresetButton,    hSavePreset, 30);
        place (userPresets,         hPresets,    28);
        place (addChannelButton,    hAddCh,      30);
        place (removeChannelButton, hRemoveCh,   30);
        place (advancedToggle,      hTools,      30);
        place (settingsButton,      hSettings,   30);

        // The tempo chip is painted, not a Component; tempoLabel sits inside it.
        const auto tempoCell = cells[(size_t) hTempo];
        tempoChipBounds = tempoCell.isEmpty() ? juce::Rectangle<int>()
                                              : tempoCell.withSizeKeepingCentre (tempoCell.getWidth(), 30);
        tempoLabel.setBounds (tempoChipBounds.isEmpty() ? juce::Rectangle<int>()
                                                        : tempoChipBounds.reduced (10, 0));
        tempoLabel.setVisible (! tempoChipBounds.isEmpty());

        // A Label WRAPS when its text does not fit, so a narrow title cell rendered
        // "StemDec / k PRO" across two lines rather than degrading. The wordmark is
        // the lowest-priority thing in the bar: below its legible width it goes.
        const auto titleCell = cells[(size_t) hTitle];
        title.setBounds (titleCell.getWidth() >= 96 ? titleCell : juce::Rectangle<int>());
        subtitle.setBounds ({});   // mode line retired from the header (mockup layout)
    }

    // View-switcher strip removed (the mockup has no transport row under the title).

    if (browserVisible)
    {
        const int browserWidth = juce::jmin (300, area.getWidth() / 4);
        auto browserCol = area.removeFromLeft (browserWidth);
        area.removeFromLeft (10);
        auto browserHeader = browserCol.removeFromTop (28);
        browserHomeButton.setBounds (browserHeader.removeFromLeft (52));
        browserHeader.removeFromLeft (4);
        browserUpButton.setBounds (browserHeader.removeFromLeft (40));
        browserHeader.removeFromLeft (6);
        browserPathLabel.setBounds (browserHeader);
        browserCol.removeFromTop (4);
        if (fileBrowser != nullptr)
            fileBrowser->setBounds (browserCol);
    }
    else if (libraryVisible && libraryView != nullptr)
    {
        const int libraryWidth = juce::jmin (460, area.getWidth() / 3);
        auto libraryCol = area.removeFromLeft (libraryWidth);
        area.removeFromLeft (10);
        libraryView->setBounds (libraryCol);
    }

    // Advanced tools toolbar (master key / AI master / rekordbox / project I/O) —
    // hidden by default for the clean 1:1 view, revealed via TOOLS.
    juce::Component* advancedComps[] = {
        &standaloneWorkflowGuide,
        &masterKeyLabel, &masterKey, &syncAllDecksButton,
        &rbxExportButton, &rbxImportButton, &saveProjButton, &openProjButton,
        &metronomeButton, &talkoverButton };   // overlapMode moved to the ARRANGER toolbar

    // FX plugin-host controls: only shown when the hosting path is actually wired
    // (see STEMDECK_ENABLE_PLUGIN_HOST). Kept out of the array above so they never
    // leak into shipping/demo layout even when TOOLS is open.
    juce::Component* pluginHostComps[] = {
        &pluginHostStatus, &pluginChainSelector, &pluginSelector, &loadPluginButton,
        &clearChainButton, &scanPluginsButton, &reloadPluginCacheButton, &pluginHostMessage,
        &sattariFxButton };
    for (auto* c : pluginHostComps) { c->setVisible (false); c->setBounds ({}); }

    if (advancedVisible)
    {
        for (auto* c : advancedComps) c->setVisible (true);

        auto infoRow = area.removeFromTop (16);
       #if STEMDECK_ENABLE_PLUGIN_HOST
        for (auto* c : pluginHostComps) c->setVisible (true);
        standaloneWorkflowGuide.setBounds (infoRow.removeFromLeft (infoRow.getWidth() * 2 / 3));
        pluginHostStatus.setBounds (infoRow);
       #else
        standaloneWorkflowGuide.setBounds (infoRow);
       #endif
        area.removeFromTop (4);

       #if STEMDECK_ENABLE_PLUGIN_HOST
        auto fxRow = area.removeFromTop (22);
        pluginChainSelector.setBounds (fxRow.removeFromLeft (130));   fxRow.removeFromLeft (4);
        sattariFxButton.setBounds (fxRow.removeFromLeft (86));        fxRow.removeFromLeft (6);
        pluginSelector.setBounds (fxRow.removeFromLeft (210));        fxRow.removeFromLeft (4);
        loadPluginButton.setBounds (fxRow.removeFromLeft (60));       fxRow.removeFromLeft (3);
        clearChainButton.setBounds (fxRow.removeFromLeft (60));       fxRow.removeFromLeft (3);
        scanPluginsButton.setBounds (fxRow.removeFromLeft (56));      fxRow.removeFromLeft (3);
        reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (74)); fxRow.removeFromLeft (8);
        pluginHostMessage.setBounds (fxRow);
        area.removeFromTop (6);
       #endif

        auto keyRow = area.removeFromTop (28);
        masterKeyLabel.setBounds (keyRow.removeFromLeft (108));       keyRow.removeFromLeft (4);
        masterKey.setBounds (keyRow.removeFromLeft (110));            keyRow.removeFromLeft (10);
        syncAllDecksButton.setBounds (keyRow.removeFromLeft (128));   keyRow.removeFromLeft (10);
        rbxExportButton.setBounds (keyRow.removeFromLeft (72));       keyRow.removeFromLeft (4);
        rbxImportButton.setBounds (keyRow.removeFromLeft (72));       keyRow.removeFromLeft (16);
        saveProjButton.setBounds (keyRow.removeFromLeft (90));        keyRow.removeFromLeft (6);
        openProjButton.setBounds (keyRow.removeFromLeft (90));        keyRow.removeFromLeft (12);
        metronomeButton.setBounds (keyRow.removeFromLeft (54));       keyRow.removeFromLeft (6);
        talkoverButton.setBounds (keyRow.removeFromLeft (54));
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : advancedComps) { c->setVisible (false); c->setBounds ({}); }
    }

    // Persistent recording + system-health HUD. It stays visible on every primary
    // surface so transport safety never disappears when moving into the arranger.
    recordingHealthBar.setVisible (true);
    recordingHealthBar.setBounds (area.removeFromTop (88));
    area.removeFromTop (stemdeck::tokens::gap6);

    // "LIVE MIXER / STEM DECKS" section title removed (mockup has no such label).
    liveMixerLabel.setBounds ({});
    liveMixerLabel.setVisible (false);
    area.removeFromTop (2);

    // Deck row gets a fixed band; the arrangement below takes ALL remaining height
    // (so it is large and grows as the window resizes).
    // ── Four decks side by side, horizontally ───────────────────────────────────
    // Equal-width columns across the full band. The band is deliberately tall (52%
    // of the remaining height) so the waveform — which claims the flexible middle of
    // each column — gets real height instead of the old ~40px sliver. The focused
    // deck is marked with an accent edge rather than a different size, so the four
    // decks stay lined up.
    // Strips must exist before we assign bounds — resized() runs before the first
    // timer tick (and does, on construction).
    refreshChannelStrips();

    // The perform screen is now three fixed-purpose bands, so allocate explicitly
    // rather than by percentage. The master band and the pad strip take what they
    // need and the CHANNELS take everything else — they are the point of this
    // screen, and under the old 52% rule they were capped while a footer grew.
    constexpr int kMasterBandH = 128;
    constexpr int kPadStripH   = 132;
    const int reservedBelow = kMasterBandH + kPadStripH + stemdeck::tokens::gap6 * 2;
    auto decksArea = area.removeFromTop (juce::jmax (378, area.getHeight() - reservedBelow));
    const int deckGap = 10;
    // Render only the channels that actually exist: removing a channel widens the
    // rest, adding one narrows them. Clamped to the deck UIs the editor owns —
    // channels past that live in the engine but have no strip yet (Step 6 proper).
    const int visibleDecks = juce::jlimit (1, MainPluginProcessor::numDecks, processorRef.getNumTrackDecks());

    // EVERY channel is a box of the same size in one row — a deck shows its stem
    // panel, an input/instrument shows its own body. Same frame, different content
    // (spec §2.1); that consistency is the point of the channel model.
    const int totalColumns = juce::jlimit (1, MainPluginProcessor::numDecks, processorRef.getNumChannels());

    // Always-visible "+" tile at the end of the row, so the channel model is
    // discoverable in the layout instead of hidden behind a toolbar button.
    addChannelTileBounds = {};
    if (processorRef.getNumChannels() < MainPluginProcessor::getMaxChannels() && decksArea.getWidth() > 400)
    {
        addChannelTileBounds = decksArea.removeFromRight (58);
        decksArea.removeFromRight (deckGap);
    }

    // ── Column widths: an idle channel is a drop target, not a cockpit ───────
    // Equal widths (the previous rule) meant an empty channel claimed as much of
    // the row as a channel you were actually mixing on. With one track loaded and
    // three empty, three quarters of the perform surface was dashed rectangle
    // while the working channel's EQ knobs were squeezed to ~16px.
    //
    // An idle TRACK DECK now takes a narrow fixed column — enough for its header
    // and a legible drop target — and hands the difference to the channels doing
    // work. Input and MIDI-instrument channels are never "idle": they carry live
    // meters, so they always count as active.
    constexpr int kIdleColumnW = 132;
    // A channel strip stops becoming more usable past roughly this width — its
    // controls just stretch. Without a cap, one loaded channel beside three idle
    // ones ballooned to ~1130px and gave itself a 610px-wide PLAY button, which
    // is the same "space in the wrong place" failure as the footer, inverted.
    constexpr int kMaxChannelW = 344;

    int idleCols = 0;
    for (int c = 0; c < visibleDecks; ++c)
        if (! deckHasContent (c)) ++idleCols;

    const int totalGaps  = deckGap * juce::jmax (0, totalColumns - 1);
    const int avail      = decksArea.getWidth() - totalGaps;
    const int activeCols = totalColumns - idleCols;

    // Equal columns when nothing is loaded — an all-idle row should fill the
    // width rather than huddle at the left.
    int activeW = juce::jlimit (120, kMaxChannelW, avail / juce::jmax (1, totalColumns));
    int idleW   = activeW;

    if (idleCols > 0 && activeCols > 0)
    {
        const int candidate = (avail - idleCols * kIdleColumnW) / activeCols;
        if (candidate > kIdleColumnW)
        {
            activeW = juce::jlimit (120, kMaxChannelW, candidate);
            // Hand any slack left by the cap back to the idle columns, so a capped
            // row still fills the width instead of trailing dead space.
            idleW = juce::jlimit (kIdleColumnW, activeW,
                                  (avail - activeCols * activeW) / juce::jmax (1, idleCols));
        }
    }

    // Focus hierarchy: the channel under the user's hands receives the detail
    // width; loaded peers collapse to monitoring strips. Targets are eased at
    // 30 Hz below, using the reference's 220 ms drawer duration.
    std::array<int, MainPluginProcessor::numDecks> widthTargets {};
    int otherActiveW = activeW;
    int focusW = activeW;
    const bool focusedIsActive = focusedDeck < totalColumns
                              && (focusedDeck >= visibleDecks || deckHasContent (focusedDeck));
    if (focusedIsActive && activeCols > 1)
    {
        const int otherCount = activeCols - 1;
        focusW = juce::jlimit (activeW, 520,
                              avail - idleCols * idleW - otherCount * 180);
        const int left = avail - idleCols * idleW - focusW;
        otherActiveW = juce::jlimit (180, activeW, left / juce::jmax (1, otherCount));
        focusW = juce::jmin (520, focusW + juce::jmax (0, left - otherActiveW * otherCount));
    }
    for (int col = 0; col < totalColumns; ++col)
    {
        const bool idle = col < visibleDecks && ! deckHasContent (col);
        widthTargets[(size_t) col] = idle ? idleW
                                  : col == focusedDeck ? focusW : otherActiveW;
        if (focusMode)
            widthTargets[(size_t) col] = col == focusedDeck
                                      ? juce::jmax (1, avail - (totalColumns - 1))
                                      : 1;
        deckWidthTarget[(size_t) col] = (float) widthTargets[(size_t) col];
        if (deckWidthCurrent[(size_t) col] <= 0.0f)
            deckWidthCurrent[(size_t) col] = deckWidthTarget[(size_t) col];
    }

    const auto columnWidth = [this] (int col)
    {
        return juce::jmax (1, (int) std::round (deckWidthCurrent[(size_t) col]));
    };

    // Centre the row when the caps leave it narrower than the band.
    {
        int used = totalGaps;
        for (int c = 0; c < totalColumns; ++c) used += columnWidth (c);
        if (const int slack = decksArea.getWidth() - used; slack > 0)
            decksArea.removeFromLeft (slack / 2);
    }

    // Non-deck channels claim their column first (they sit after the decks, since
    // addChannel appends), leaving the deck columns for the deck loop below.
    {
        auto scan = decksArea;
        for (int col = 0; col < totalColumns; ++col)
        {
            auto colBounds = scan.removeFromLeft (columnWidth (col));
            if (col < totalColumns - 1) scan.removeFromLeft (deckGap);

            if (col >= visibleDecks)
                if (auto* st = channelStrips[(size_t) col].get())
                {
                    if (focusMode && col != focusedDeck)
                    {
                        st->setVisible (false);
                        st->setBounds ({});
                        continue;
                    }
                    st->setVisible (true);
                    st->setAlpha (1.0f);
                    st->setBounds (colBounds);   // full column, loaded or not
                }
        }
    }

    for (int deckIndex = 0; deckIndex < MainPluginProcessor::numDecks; ++deckIndex)
    {
        if (deckIndex >= visibleDecks)
        {
            // Channel removed — retire its strip entirely.
            setDeckDetailVisible (deckIndex, false);
            deckPanelBounds[(size_t) deckIndex]     = {};
            deckHeaderBounds[(size_t) deckIndex]    = {};
            deckCollapsedBounds[(size_t) deckIndex] = {};
            layoutDeckHeaderChips (deckIndex);   // clears the chip hit targets too
            if (auto* strip = deckStrips[(size_t) deckIndex].get()) { strip->setVisible (false); strip->setBounds ({}); }
            if (auto* w = deckWaves[(size_t) deckIndex].get())       { w->setVisible (false); w->setBounds ({}); }
            if (auto* r = deckWaveRegions[(size_t) deckIndex].get()) { r->setVisible (false); r->setBounds ({}); }
            continue;
        }

        auto laneOuter = decksArea.removeFromLeft (columnWidth (deckIndex));
        if (deckIndex < visibleDecks - 1) decksArea.removeFromLeft (deckGap);

        if (focusMode && deckIndex != focusedDeck)
        {
            setDeckDetailVisible (deckIndex, false);
            deckPanelBounds[(size_t) deckIndex] = {};
            deckHeaderBounds[(size_t) deckIndex] = {};
            deckCollapsedBounds[(size_t) deckIndex] = {};
            layoutDeckHeaderChips (deckIndex);
            if (auto* strip = deckStrips[(size_t) deckIndex].get())
            {
                strip->setVisible (false);
                strip->setBounds ({});
            }
            if (auto* wave = deckWaves[(size_t) deckIndex].get())
            {
                wave->setVisible (false);
                wave->setBounds ({});
            }
            if (auto* region = deckWaveRegions[(size_t) deckIndex].get())
            {
                region->setVisible (false);
                region->setBounds ({});
            }
            continue;
        }

        const bool loaded = deckHasContent (deckIndex);
        const bool collapsed = loaded && deckIndex != focusedDeck;
        if (collapsed)
        {
            const auto stripBounds = laneOuter.withHeight (58);
            deckPanelBounds[(size_t) deckIndex] = stripBounds;
            deckTabStripBounds[(size_t) deckIndex] = {};
            deckTabPanelBounds[(size_t) deckIndex] = {};
            deckStemFxBounds[(size_t) deckIndex] = {};
            layoutCollapsedDeck (deckIndex, stripBounds);
            if (auto* region = deckWaveRegions[(size_t) deckIndex].get())
            {
                region->setVisible (false);
                region->setBounds ({});
            }
            if (auto* strip = deckStrips[(size_t) deckIndex].get())
            {
                strip->setVisible (false);
                strip->setBounds ({});
            }
            continue;
        }

        deckCollapsedBounds[(size_t) deckIndex] = {};
        setDeckDetailVisible (deckIndex, loaded);
        deckPanelBounds[(size_t) deckIndex] = laneOuter;
        auto lane = laneOuter.reduced (10, 10);

        auto& controls = deckControls[(size_t) deckIndex];

        // Header strip: two rows (identity, then numerics) — see layoutDeckHeaderChips.
        auto headerStrip = lane.removeFromTop (38);
        deckHeaderBounds[(size_t) deckIndex] = headerStrip;
        layoutDeckHeaderChips (deckIndex);   // hit targets — geometry belongs here, not in paint()
        lane.removeFromTop (6);

        // Progressive disclosure: an empty channel is a large, calm load target,
        // not a disabled copy of the entire performance surface. The waveform
        // owns the whole body and opens the chooser on click. Once audio lands,
        // resized() runs again and reveals the complete deck in place.
        if (! loaded)
        {
            deckStemBounds[(size_t) deckIndex]    = {};
            deckTabStripBounds[(size_t) deckIndex] = {};
            deckTabPanelBounds[(size_t) deckIndex] = {};
            deckStemFxBounds[(size_t) deckIndex] = {};
            controls.play.setVisible (false); controls.play.setBounds ({});
            controls.cue .setVisible (false); controls.cue .setBounds ({});

            if (auto* region = deckWaveRegions[(size_t) deckIndex].get())
            {
                region->setVisible (false);
                region->setBounds ({});
            }
            if (auto* wave = deckWaves[(size_t) deckIndex].get())
            {
                wave->setVisible (true);
                // RECESSED tier. An idle channel is an invitation, not a subject:
                // letting the drop target fill its whole column made three empty
                // channels the heaviest thing on the screen. Cap and centre it so
                // the column can still absorb layout slack without the emptiness
                // shouting louder than the channel actually playing.
                wave->setAlpha (0.72f);
                wave->setBounds (lane.reduced (2, 2)
                                     .withSizeKeepingCentre (juce::jmin (lane.getWidth() - 4, 210),
                                                             juce::jmin (lane.getHeight() - 4, 260)));
            }
            continue;
        }

        controls.play.setVisible (true);
        controls.cue .setVisible (true);

        // ---- Deck lane: ONE priority stack, top to bottom ----------------------
        //
        // This was a bottom-up chain of removeFromBottom(<literal>) with the
        // waveform silently inheriting whatever was left. The chrome below the
        // waveform is ~311px fixed, so at the 378px minimum band height the
        // waveform got 67px — about 17px per stem lane — and the stem-coloured
        // waveform, the Tier-1 differentiator, collapsed into four flat bars.
        // Chrome took 82% of the deck exactly when space was scarce.
        //
        // Now every row declares a floor, a preferred size and a priority, and
        // LayoutStack shrinks the LOWEST priority first. The waveform has the
        // highest priority AND a hard floor, so the chrome gives up its slack
        // before the waveform gives up anything. Same 378px band now yields a
        // ~123px waveform with every row still present.
        constexpr int kLabelHeadroom = 13;   // matches drawAbove()'s -13 offset
        const int wellH = [tab = controls.activeToolTab]
        {
            switch (tab)
            {
                case DeckControls::TabCue:  return 24 + 2 + 18;                    // cues + intro/outro
                case DeckControls::TabLoop: return 22 + 2 + 20 + 2 + 16;           // three rows
                case DeckControls::TabFx:   return kLabelHeadroom + 52;            // DR/BA/MU/VO headers
                case DeckControls::TabSync: return kLabelHeadroom + 22 + 2 + 20 + 2 + 18;
                // 38, not 30: the full-mix trim knob renders its value ("100") inside
                // its own bounds, and a shorter row silently drops that readout.
                case DeckControls::TabSrc:  return 24 + 2 + 38;
                default:                    return 66;
            }
        }();

        juce::Rectangle<int> tabContent, tabStrip, mixBand, transport, knobRow, statusRow;
        {
            namespace L = stemdeck::layout;
            namespace T = stemdeck::tokens;

            // The waveform's floor. Below this the four stem columns cannot show
            // envelope structure and the display stops carrying information.
            constexpr int kWaveFloor = 110;

            enum { dWave, dKnobs, dTransport, dMix, dTabStrip, dTabContent, dStatus, dNumRows };
            const std::vector<L::Item> rowSpec {
                // Signature row: highest priority and a hard floor, but it ASKS for
                // only its floor and then grows into whatever is left over. Asking
                // for a large preferred size instead would squeeze the chrome even
                // on a roomy window — that cost the CUE tab its intro/outro row at
                // 1680x1050, where there was plenty of space for both.
                L::Item::flexible (kWaveFloor, kWaveFloor, 1.0f, /*pri*/ 10, T::gap5),
                L::Item::flexible (32, 46, 0.0f, /*pri*/ 7, T::gap5),   // stem gain knobs
                L::Item::fixed    (28,           /*pri*/ 9, T::gap5),   // PLAY / CUE — must stay
                L::Item::flexible (62, 78, 0.0f, /*pri*/ 8, T::gap6),   // EQ + faders
                L::Item::fixed    (26,           /*pri*/ 5, T::gap2),   // tab strip
                // The tab well shrinks to its first row (the 8 hot cues) before
                // anything else is sacrificed — losing cues beats losing the wave.
                L::Item::flexible (26, juce::jmax (52, wellH) + 2 * T::gap2, 0.0f, /*pri*/ 4, T::gap3),
                L::Item::fixed    (advancedVisible ? 13 : 0, /*pri*/ 1, 0),
            };
            jassert ((int) rowSpec.size() == dNumRows);

            const auto r = L::rows (lane, rowSpec);
            lane       = r[dWave];        // the waveform region owns what it won
            knobRow    = r[dKnobs];
            transport  = r[dTransport];
            mixBand    = r[dMix].isEmpty() ? juce::Rectangle<int>()
                                           : r[dMix].withTrimmedTop (kLabelHeadroom + 1);
            tabStrip   = r[dTabStrip];
            tabContent = r[dTabContent];
            statusRow  = r[dStatus];
        }

        // Status row (key / stem provider status).
        // Deck footer status text removed for the clean 1:1 view (advanced only).
        if (advancedVisible && ! statusRow.isEmpty())
        {
            controls.keyInfo.setVisible (true); controls.stemStatus.setVisible (true);
            controls.keyInfo   .setBounds (statusRow.removeFromLeft (statusRow.getWidth() / 2));
            controls.stemStatus.setBounds (statusRow);
        }
        else
        {
            controls.keyInfo.setVisible (false);   controls.keyInfo.setBounds ({});
            controls.stemStatus.setVisible (false); controls.stemStatus.setBounds ({});
        }

        // Toolbox content (the active tab's controls render here).
        // Reference §3: the panel is min-height 52 and GROWS to fit its content, so
        // size the well to the active tab instead of forcing all five into one height.
        deckTabPanelBounds[(size_t) deckIndex] = tabContent;
        if (! tabContent.isEmpty())
            tabContent = tabContent.reduced (stemdeck::tokens::gap2);   // the well's own padding

        // Toolbox tab strip (CUE / LOOP / FX / SYNC / SRC). The strip is a track
        // (painted in paint()) with the segments inset by its 3px padding; gap-2
        // between segments, per the reference.
        deckTabStripBounds[(size_t) deckIndex] = tabStrip;
        if (! tabStrip.isEmpty())
        {
            auto seg = tabStrip.reduced (stemdeck::tokens::gap2);
            const int tw = seg.getWidth() / DeckControls::NumToolTabs;
            for (int t = 0; t < DeckControls::NumToolTabs; ++t)
            {
                auto cell = seg.removeFromLeft (t == DeckControls::NumToolTabs - 1 ? seg.getWidth() : tw);
                controls.toolTabs[(size_t) t].setVisible (true);
                controls.toolTabs[(size_t) t].setBounds (cell.withTrimmedRight (
                    t == DeckControls::NumToolTabs - 1 ? 0 : stemdeck::tokens::gap2));
            }
        }
        else
        {
            for (auto& tab : controls.toolTabs) { tab.setVisible (false); tab.setBounds ({}); }
        }

        // Mix band: EQ knobs (left) + VOL / FADER / PITCH vertical faders (right).
        {
            const int faderColW = 32, fgap = 4;
            auto fadersCol = mixBand.removeFromRight (faderColW * 3 + fgap * 2);
            auto eqArea = mixBand.withTrimmedRight (8);
            const int eqW = juce::jmax (26, eqArea.getWidth() / 4);
            // Smaller knobs (was reduced(3,2)) so the rings read light and the
            // LOW/MID/HIGH/FILT labels above have room.
            controls.low   .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.mid   .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.high  .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.filter.setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.volume.setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.fader .setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.pitch .setBounds (fadersCol.reduced (3, 2));
        }

        // Transport row — PLAY + CUE + 3 tiny CF-assign buttons on the right
        {
            auto cfCol = transport.removeFromRight (54);
            transport.removeFromRight (4);
            const int cfW = cfCol.getWidth() / 3;
            for (int s = 0; s < 3; ++s)
                controls.cfSideBtns[(size_t) s].setBounds (cfCol.removeFromLeft (cfW).reduced (1, 3));
            const int w = transport.getWidth();
            controls.play.setBounds (transport.removeFromLeft (w * 58 / 100).reduced (1, 0));
            controls.cue .setBounds (transport.reduced (1, 0));
        }

        // ── Waveform + stem gain (§2/§3) ─────────────────────────────────────────
        // Loaded deck: the two-zone WaveformRegion fills the lane and the stem gain
        // knobs dock DIRECTLY UNDER their columns (vocal knob under the vocal column).
        // Empty deck: the flat WaveformDisplay is the drop CTA and the old stem cells
        // stay as the labelled mini-fader strip.
        auto* region = deckWaveRegions[(size_t) deckIndex].get();
        auto* wave   = deckWaves[(size_t) deckIndex].get();

        if (loaded && region != nullptr)
        {
            deckStemBounds[(size_t) deckIndex] = {};                 // no painted cells
            region->setVisible (true);
            region->setBounds (lane);
            if (wave) { wave->setVisible (false); wave->setBounds ({}); }

            // Dock a knob under each column. Column order is VOX/DRM/BAS/OTH; the stem
            // lanes are vocals/drums/bass/music.
            const int colToLane[4] = { DeckPlayer::vocalsStem, DeckPlayer::drumsStem,
                                       DeckPlayer::bassStem,   DeckPlayer::musicStem };
            const auto cols = region->columnRects();
            for (int c = 0; c < 4; ++c)
            {
                const auto cr = cols[(size_t) c].translated (region->getX(), 0);
                const int sz = juce::jmin (40, juce::jmin (cr.getWidth(), knobRow.getHeight()));
                auto& knob = controls.stemGains[(size_t) colToLane[c]];
                knob.setVisible (true);
                knob.setBounds (juce::Rectangle<int> (cr.getX(), knobRow.getY(), cr.getWidth(), knobRow.getHeight())
                                    .withSizeKeepingCentre (sz, sz));
            }
            controls.reverbMix.setVisible (false); controls.reverbMix.setBounds ({});   // reverb -> FX tab
        }
        else
        {
            // Fallback: a loaded deck with no WaveformRegion. The stem strip needs
            // more height than the docked-knob row, so reclaim that row — the two
            // are adjacent, so their union is exactly the space both would occupy.
            auto avail = knobRow.isEmpty() ? lane : lane.getUnion (knobRow);

            // Empty deck: djay-Pro cells — DRUMS/BASS/MUSIC/VOCALS mini-faders + FX.
            auto stemStrip = avail.removeFromBottom (juce::jmin (72, avail.getHeight() / 2));
            deckStemBounds[(size_t) deckIndex] = stemStrip;
            const int cellW = juce::jmax (28, stemStrip.getWidth() / 5);
            auto cells = stemStrip;
            for (int s = 0; s < 4; ++s)
            {
                auto cell = cells.removeFromLeft (cellW).reduced (3, 2);
                cell.removeFromTop (13);
                auto& knob = controls.stemGains[(size_t) (DeckPlayer::drumsStem + s)];
                knob.setVisible (true);
                knob.setBounds (cell.reduced (2, 0));
            }
            auto fxCell = cells.reduced (3, 2);
            fxCell.removeFromTop (13);
            fxCell.removeFromBottom (10);
            controls.reverbMix.setVisible (true);
            controls.reverbMix.setBounds (fxCell.reduced (2, 0));
            avail.removeFromBottom (stemdeck::tokens::gap5);

            if (region) { region->setVisible (false); region->setBounds ({}); }
            if (wave)   { wave->setVisible (true);     wave->setBounds (avail); }
        }

        // ---- Active toolbox tab content ----
        // The well shrinks under pressure and can shed entirely; skip the content
        // pass rather than laying controls out inside a zero-height rectangle.
        if (tabContent.isEmpty())
        {
            applyDeckToolTabVisibility (deckIndex);
            applyDeckControlWeights (deckIndex);
            continue;
        }

        switch (controls.activeToolTab)
        {
            case DeckControls::TabCue:
            {
                auto row = tabContent.removeFromTop (24);
                const int w = juce::jmax (12, row.getWidth() / 8);
                for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
                    controls.hotCues[(size_t) c].setBounds (row.removeFromLeft (w).reduced (1, 0));
                tabContent.removeFromTop (2);
                auto row2 = tabContent.removeFromTop (18);
                controls.introEndBtn  .setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (1, 0));
                controls.outroStartBtn.setBounds (row2.reduced (1, 0));
                break;
            }
            case DeckControls::TabLoop:
            {
                // Row 1: IN / OUT / LOOP / 1/2 / x2 / Slip
                auto r1 = tabContent.removeFromTop (22);
                const int bw = juce::jmax (24, r1.getWidth() / 6);
                controls.loopIn    .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopOut   .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopToggle.setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopHalf  .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopDouble.setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.slipButton.setBounds (r1.reduced (1, 0));
                tabContent.removeFromTop (2);
                // Row 2: beat-roll buttons (1/4 1/2 1 2 4 8)
                auto r2 = tabContent.removeFromTop (20);
                const int rw = juce::jmax (12, r2.getWidth() / 6);
                for (int b = 0; b < 6; ++b)
                    controls.beatRollBtns[(size_t) b].setBounds (r2.removeFromLeft (rw).reduced (1, 0));
                tabContent.removeFromTop (2);
                // Row 3: beat-jump back/fwd
                auto r3 = tabContent.removeFromTop (16);
                const int jw = juce::jmax (10, r3.getWidth() / 8);
                for (int b = 3; b >= 0; --b)
                    controls.beatJumpBack[(size_t) b].setBounds (r3.removeFromLeft (jw).reduced (1, 0));
                for (int b = 0; b < 4; ++b)
                    controls.beatJumpFwd[(size_t) b].setBounds (r3.removeFromLeft (jw).reduced (1, 0));
                break;
            }
            case DeckControls::TabFx:
            {
                // Deck-wide FX knobs on the left. Reverb docks here for LOADED decks
                // (its stem-strip cell is retired when the waveform region is shown);
                // empty decks keep reverb in the stem cell, so skip it here.
                const int knobW = juce::jmin (46, tabContent.getWidth() / (loaded ? 7 : 6));
                if (loaded)
                {
                    controls.reverbMix.setVisible (true);
                    controls.reverbMix.setBounds (tabContent.removeFromLeft (knobW).reduced (3, 2));
                    tabContent.removeFromLeft (3);
                }
                controls.echoMix  .setBounds (tabContent.removeFromLeft (knobW).reduced (3, 2));
                tabContent.removeFromLeft (3);
                controls.macroKnob.setBounds (tabContent.removeFromLeft (knobW).reduced (3, 2));
                tabContent.removeFromLeft (6);

                // Per-stem FX grid: 4 columns (drums/bass/music/vocals) × 3 rows
                // (Filt / Send / Pitch). Column headers + row labels painted in paint().
                deckStemFxBounds[(size_t) deckIndex] = tabContent;
                auto grid = tabContent.reduced (0, 1);
                grid.removeFromTop (9);   // column-header strip (painted)
                const int colW = juce::jmax (18, grid.getWidth() / 4);
                auto filtRow  = grid.removeFromTop (grid.getHeight() / 3);
                auto sendRow  = grid.removeFromTop (grid.getHeight() / 2);
                auto pitchRow = grid;
                for (int s = 0; s < 4; ++s)
                {
                    controls.stemFilt[(size_t) s] .setBounds (filtRow.removeFromLeft (colW).reduced (2, 0));
                    controls.stemSend[(size_t) s] .setBounds (sendRow.removeFromLeft (colW).reduced (2, 0));
                    controls.stemPitch[(size_t) s].setBounds (pitchRow.removeFromLeft (colW).reduced (2, 0));
                }
                break;
            }
            case DeckControls::TabSync:
            {
                tabContent.removeFromTop (kLabelHeadroom);   // BPM / BEAT / DOWN labels
                auto r1 = tabContent.removeFromTop (22);
                const int w = juce::jmax (44, r1.getWidth() / 3);
                controls.bpm       .setBounds (r1.removeFromLeft (w).reduced (2, 1));
                controls.beatOffset.setBounds (r1.removeFromLeft (w).reduced (2, 1));
                controls.downbeat  .setBounds (r1.reduced (2, 1));
                tabContent.removeFromTop (2);
                auto r2 = tabContent.removeFromTop (20);
                const int cw = juce::jmax (44, r2.getWidth() / 3);
                controls.tempoInterpretation.setBounds (r2.removeFromLeft (cw).reduced (1, 0));
                controls.syncMode           .setBounds (r2.removeFromLeft (cw).reduced (1, 0));
                controls.keyTarget          .setBounds (r2.reduced (1, 0));
                tabContent.removeFromTop (2);
                auto r3 = tabContent.removeFromTop (18);
                controls.liveKey.setBounds (r3.removeFromLeft (r3.getWidth() / 2).reduced (1, 0));
                controls.keyLock.setBounds (r3.reduced (1, 0));
                break;
            }
            case DeckControls::TabSrc:
            {
                auto row = tabContent.removeFromTop (24);
                controls.load    .setBounds (row.removeFromLeft (juce::jmax (44, row.getWidth() * 22 / 100)).reduced (1, 0));
                controls.aiStems .setBounds (row.removeFromLeft (juce::jmax (60, row.getWidth() * 36 / 100)).reduced (1, 0));
                controls.loadStem.setBounds (row.removeFromLeft (juce::jmax (44, row.getWidth() * 38 / 100)).reduced (1, 0));
                controls.stemLane.setBounds (row.reduced (1, 0));
                tabContent.removeFromTop (2);
                // Full-mix trim knob (relocated from the old stem knob row) + audio→MIDI.
                controls.stemGains[(size_t) DeckPlayer::fullMixStem]
                    .setBounds (tabContent.removeFromLeft (46).reduced (2, 1));
                tabContent.removeFromLeft (4);
                controls.extractMidi.setBounds (tabContent.removeFromLeft (72).withHeight (22).translated (0, 4));
                tabContent.removeFromLeft (4);
                controls.extractDrums.setBounds (tabContent.removeFromLeft (80).withHeight (22).translated (0, 4));
                break;
            }
            default: break;
        }

        applyDeckToolTabVisibility (deckIndex);
        applyDeckControlWeights (deckIndex);
    }

    // The full deck already exposes transport, cues, loops, sync and key tools.
    // A second transport strip immediately below it duplicated those controls,
    // consumed vertical space, and left two competing places to perform the same
    // action. Keep DeckStrip available as a component/test seam, but retire the
    // duplicate from this assembled full-column surface.
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (auto* strip = deckStrips[(size_t) i].get())
        {
            deckStripBounds[(size_t) i] = {};
            strip->setVisible (false);
            strip->setBounds ({});
        }

    area.removeFromTop (10);

    // ── Master Control Band: five bounded panels ────────────────────────────────
    //   [ SCOPE ] [ SYNC ] [ CROSSFADER (flex) ] [ MASTER FX ] [ MASTER OUT ]
    // Each panel is a #131316 rounded box (frames painted in paint()); the crossfader
    // — the most-used control — takes all the remaining width.
    juce::Component* mixExtras[] = { &micGain, &hostSync, &beatSync, &micMonitor, &liveMidiButton,
                                     &camelotButton, &midiLearnButton, &snapToggleButton,
                                     &razorButton, &bounceEditsButton, &undoButton, &redoButton };
    // SCOPE and SYNC used to sit side by side, spending 180px of width between
    // them on a near-empty scope reading "-inf dB" and two oversized buttons that
    // read as unfinished placeholders — the exact inverse of the channel strips,
    // which were packing forty controls into 230px. Stacking them into one 128px
    // column halves that footprint and gives the width back to the FX rack.
    auto band = area.removeFromTop (kMasterBandH);
    const int panelGap = 8;
    {
        auto leftCol = band.removeFromLeft (128);
        band.removeFromLeft (panelGap);
        scopePanelBounds = leftCol.removeFromTop (leftCol.getHeight() * 52 / 100);
        leftCol.removeFromTop (stemdeck::tokens::gap4);
        syncPanelBounds = leftCol;
    }
    masterOutBounds     = band.removeFromRight (282);          band.removeFromRight (panelGap);
    masterFxPanelBounds = band.removeFromRight (100);          band.removeFromRight (panelGap);
    xfPanelBounds       = band;                                // the FX rack gets the rest

    // SCOPE — the master scope component fills the panel.
    mixerPanel.setBounds (scopePanelBounds.reduced (4));

    // SYNC — BPM and KEY side by side. They were full-height stacked buttons,
    // which made two occasional actions the largest controls in the region.
    {
        for (auto* c : { &syncBpmButton, &syncKeyButton }) c->setVisible (true);
        auto s = syncPanelBounds.reduced (7);
        s.removeFromTop (11);   // painted "SYNC" header
        const int pw = (s.getWidth() - stemdeck::tokens::gap4) / 2;
        syncBpmButton.setBounds (s.removeFromLeft (pw));
        s.removeFromLeft (stemdeck::tokens::gap4);
        syncKeyButton.setBounds (s.removeFromLeft (pw));
    }

    // MASTER FX — a single FILTER XY pad (option A: the panel fits the pad).
    masterFxPad.setBounds (masterFxPanelBounds.reduced (8).withTrimmedTop (12));

    // SATTARI FX RACK — takes the crossfader's flex slot. The crossfader is retired
    // (studio-first: blend with deck faders + stems), so its controls are hidden.
    {
        for (auto& mb : deckMeterBounds) mb = {};
        fxRack.setBounds (xfPanelBounds);
        for (juce::Component* c : { (juce::Component*) &crossfader, (juce::Component*) &crossfaderCurve,
                                    (juce::Component*) &crossfaderReverse })
        { c->setVisible (false); c->setBounds ({}); }
    }

    if (advancedVisible)
    {
        for (auto* c : mixExtras) c->setVisible (true);
        // Advanced utility chips live on the transport/keyRow area, not the mixer.
        auto util = area.removeFromTop (28);
        auto chip = [&util] (juce::Component& c, int w) { c.setBounds (util.removeFromLeft (w).withHeight (26)); util.removeFromLeft (3); };
        hostSync.setBounds (util.removeFromLeft (96)); util.removeFromLeft (3);
        beatSync.setBounds (util.removeFromLeft (96)); util.removeFromLeft (3);
        micMonitor.setBounds (util.removeFromLeft (104)); util.removeFromLeft (6);
        liveMidiButton.setBounds (util.removeFromLeft (94)); util.removeFromLeft (6);
        micGain.setBounds (util.removeFromLeft (84)); util.removeFromLeft (6);
        // Snap / razor / bounce / undo / redo moved to the ARRANGER toolbar — they
        // edit the arrangement, so hiding them behind TOOLS on the perform screen
        // meant the editing surface had no visible undo.
        chip (camelotButton, 44); chip (midiLearnButton, 48);
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : mixExtras) { c->setVisible (false); c->setBounds ({}); }
    }

    // MASTER OUT panel internals. Absorbs the LIMIT ceiling (an output-stage control)
    // and the Master-to-target control + selector (3.1), so they sit with the LUFS
    // readout they concern instead of floating over the crossfader.
    {
        for (juce::Component* c : { (juce::Component*) &aiMasterButton, (juce::Component*) &aiMasterMode,
                                    (juce::Component*) &aiMasterAmount, (juce::Component*) &limiter })
            c->setVisible (true);

        auto mo = masterOutBounds.reduced (12, 8);
        mo.removeFromTop (15);   // painted "MASTER OUTPUT" title
        masterMeterBounds = mo.removeFromLeft (38).reduced (0, 2);
        mo.removeFromLeft (8);
        auto col = mo;   // right stack

        // Row 1: MASTER GAIN knob + GLOBAL TEMPO box (painted, small). The knob cell is
        // ≥60px so its "-14.0 dB" readout isn't clipped to "-14.0...".
        auto row1 = col.removeFromTop (40);
        master.setBounds (row1.removeFromLeft (60));
        row1.removeFromLeft (8);
        masterTempoBounds = row1;
        masterBpm.setVisible (false); masterBpm.setBounds ({});

        // Row 2: Master-to-target button + target selector + depth rotary.
        col.removeFromTop (4);
        auto row2 = col.removeFromTop (22);
        aiMasterButton.setBounds (row2.removeFromLeft (74));
        row2.removeFromLeft (5);
        aiMasterAmount.setBounds (row2.removeFromRight (22));   // compact depth knob
        row2.removeFromRight (5);
        aiMasterMode.setBounds (row2);

        // Row 3: LIMIT ceiling + TAP + Key Lock.
        col.removeFromTop (4);
        auto row3 = col.removeFromTop (24);
        limiter.setBounds (row3.removeFromLeft (66));   // "LIMIT" label painted alongside
        row3.removeFromLeft (6);
        tapTempoButton.setBounds (row3.removeFromLeft (juce::jmax (40, row3.getWidth() / 2 - 3)));
        row3.removeFromLeft (5);
        keyLockAllButton.setBounds (row3);

        // Row 4: live integrated LUFS (primary) + limiter GR (painted).
        col.removeFromTop (4);
        masterLufsBounds = col.removeFromTop (30);

        // Reveal / Reload are advanced-only; park them on the utility row when shown.
        revealRecordingButton.setVisible (false); revealRecordingButton.setBounds ({});
        reloadIntoDecksButton.setVisible (false); reloadIntoDecksButton.setBounds ({});
    }

    area.removeFromTop (stemdeck::tokens::gap6);

    // ── PERFORM footer: the performance pads ─────────────────────────────────
    // This strip used to be the arrangement-capture timeline. That put the
    // product's central claim — perform it, then edit it — in a footer, at the
    // smallest and least developed size on the screen, while the pads (a
    // hands-on perform control) were squeezed into a 24px collapsed header
    // beneath it. Both were in the wrong place.
    //
    // The arrangement now owns the whole ARRANGER surface (see the overlay block
    // below). The pads get the perform footer, at a size you can actually hit.
    {
        bool anyPad = false;
        for (int i = 0; i < PadBank::numPads; ++i)
            if (processorRef.getPadPath (i).isNotEmpty()) { anyPad = true; break; }
        padsCollapsed = ! anyPad;

        // Bounded: the pads are a footer, not the subject. Left unbounded they took
        // whatever the channels did not, and eight 200px pads with hollow centres is
        // the same low-density mistake the old SCOPE panel was making.
        auto padArea = area.removeFromTop (juce::jmin (area.getHeight(), kPadStripH));
        auto labelRow = padArea.removeFromTop (20);
        samplerLabel.setBounds (labelRow.removeFromLeft (150));
        labelRow.removeFromLeft (stemdeck::tokens::gap5);
        recordStatus.setBounds (labelRow.removeFromRight (190).reduced (2, 2));
        labelRow.removeFromRight (stemdeck::tokens::gap5);
        libraryMetaLabel.setBounds (labelRow);   // MIDI-learn / status feedback
        padArea.removeFromTop (stemdeck::tokens::gap4);

        // Pads are HERO-tier: one of the few things struck under pressure, so they
        // get real height rather than the leftover sliver they used to live in.
        const int padGap = stemdeck::tokens::gap5;
        const int padW = juce::jmax (72,
            (padArea.getWidth() - padGap * (PadBank::numPads - 1)) / PadBank::numPads);
        for (int i = 0; i < PadBank::numPads; ++i)
        {
            auto cell = padArea.removeFromLeft (padW);
            if (i < PadBank::numPads - 1)
                padArea.removeFromLeft (padGap);

            for (juce::Component* c : { (juce::Component*) &pads[(size_t) i].trigger,
                                        (juce::Component*) &pads[(size_t) i].load,
                                        (juce::Component*) &pads[(size_t) i].gain })
                c->setVisible (true);

            // The trigger keeps the bulk; load/gain are recessed beneath it.
            auto top = cell.removeFromTop (juce::jmax (28, cell.getHeight() * 64 / 100));
            pads[(size_t) i].trigger.setBounds (top.reduced (1, 1));
            cell.removeFromTop (stemdeck::tokens::gap1);
            pads[(size_t) i].load.setBounds (cell.removeFromLeft (cell.getWidth() * 42 / 100).reduced (1, 0));
            cell.removeFromLeft (stemdeck::tokens::gap2);
            pads[(size_t) i].gain.setBounds (cell.reduced (1, 0));
        }
    }

    // Piano-roll / mixer overlays cover the decks + footer when toggled on.
    // Anchor to the actual deck-strip top (not a hardcoded offset) so header/nav
    // stay visible even if the upper sections change height.
    const int overlayTop = deckPanelBounds[0].isEmpty() ? 252 : deckPanelBounds[0].getY();
    const auto overlay = getLocalBounds().withTrimmedTop (overlayTop).reduced (8, 6);
    pianoRoll.setBounds (overlay);
    mixerView.setBounds (overlay);

    // ── ARRANGER: the arrangement is the surface, not a footer ───────────────
    // Energy arc across the top, the editing toolbar under it, and the capture
    // timeline taking everything that remains. mixArrangeView (a read-only
    // snapshot of the same material) is no longer shown here — arrangementView
    // is the editable one, and two timelines stacked was the confusion.
    layoutArrangerSurface (overlay);

    // "One primary surface at a time" has to be ENFORCED, not just intended.
    // The overlays are opaque only where a component actually sits, so the gaps
    // between them — the arranger's toolbar row, for instance — were showing the
    // deck waveforms straight through from the perform screen underneath.
    if (pianoRollButton.getToggleState() || mixerButton.getToggleState() || pianoRoll.isVisible())
        hidePerformSurface();
}

// Hide every perform-screen control so an overlay surface reads as its own
// screen. resized() lays the perform surface out unconditionally and then calls
// this, which keeps the layout logic in one place at the cost of one extra pass.
void MainPluginEditor::hidePerformSurface()
{
    auto hide = [] (juce::Component& c) { c.setVisible (false); c.setBounds ({}); };

    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        auto& c = deckControls[(size_t) d];
        for (juce::Component* comp : { (juce::Component*) &c.play,     (juce::Component*) &c.cue,
                                       (juce::Component*) &c.volume,   (juce::Component*) &c.fader,
                                       (juce::Component*) &c.pitch,    (juce::Component*) &c.low,
                                       (juce::Component*) &c.mid,      (juce::Component*) &c.high,
                                       (juce::Component*) &c.filter,   (juce::Component*) &c.bpm,
                                       (juce::Component*) &c.beatOffset, (juce::Component*) &c.downbeat,
                                       (juce::Component*) &c.reverbMix, (juce::Component*) &c.echoMix,
                                       (juce::Component*) &c.macroKnob, (juce::Component*) &c.load,
                                       (juce::Component*) &c.loadStem,  (juce::Component*) &c.aiStems,
                                       (juce::Component*) &c.stemLane,  (juce::Component*) &c.keyInfo,
                                       (juce::Component*) &c.stemStatus, (juce::Component*) &c.extractMidi,
                                       (juce::Component*) &c.extractDrums, (juce::Component*) &c.keyLock,
                                       (juce::Component*) &c.liveKey,   (juce::Component*) &c.loopToggle,
                                       (juce::Component*) &c.loopIn,    (juce::Component*) &c.loopOut,
                                       (juce::Component*) &c.loopHalf,  (juce::Component*) &c.loopDouble,
                                       (juce::Component*) &c.slipButton, (juce::Component*) &c.introEndBtn,
                                       (juce::Component*) &c.outroStartBtn, (juce::Component*) &c.tempoInterpretation,
                                       (juce::Component*) &c.syncMode,  (juce::Component*) &c.keyTarget })
            hide (*comp);

        for (auto& b : c.hotCues)      hide (b);
        for (auto& b : c.toolTabs)     hide (b);
        for (auto& b : c.beatRollBtns) hide (b);
        for (auto& b : c.beatJumpBack) hide (b);
        for (auto& b : c.beatJumpFwd)  hide (b);
        for (auto& b : c.cfSideBtns)   hide (b);
        for (auto& s : c.stemGains)    hide (s);
        for (auto& s : c.stemFilt)     hide (s);
        for (auto& s : c.stemSend)     hide (s);
        for (auto& s : c.stemPitch)    hide (s);

        if (auto* w = deckWaves[(size_t) d].get())       hide (*w);
        if (auto* r = deckWaveRegions[(size_t) d].get()) hide (*r);
        if (auto* s = deckStrips[(size_t) d].get())      hide (*s);

        // Painted regions are keyed off these rectangles, so clearing them stops
        // paint() drawing deck panels, chips and stem cells behind the overlay.
        deckPanelBounds[(size_t) d]    = {};
        deckHeaderBounds[(size_t) d]   = {};
        deckStemBounds[(size_t) d]     = {};
        deckStemFxBounds[(size_t) d]   = {};
        deckTabStripBounds[(size_t) d] = {};
        deckTabPanelBounds[(size_t) d] = {};
        deckMeterBounds[(size_t) d]    = {};
        layoutDeckHeaderChips (d);
    }

    for (auto& strip : channelStrips)
        if (auto* s = strip.get()) hide (*s);

    for (auto& p : pads) { hide (p.trigger); hide (p.load); hide (p.gain); }

    for (juce::Component* comp : { (juce::Component*) &mixerPanel,   (juce::Component*) &fxRack,
                                   (juce::Component*) &masterFxPad,  (juce::Component*) &syncBpmButton,
                                   (juce::Component*) &syncKeyButton, (juce::Component*) &master,
                                   (juce::Component*) &limiter,      (juce::Component*) &aiMasterButton,
                                   (juce::Component*) &aiMasterMode, (juce::Component*) &aiMasterAmount,
                                   (juce::Component*) &tapTempoButton, (juce::Component*) &keyLockAllButton,
                                   (juce::Component*) &samplerLabel, (juce::Component*) &recordStatus })
        hide (*comp);

    addChannelTileBounds = {};
    scopePanelBounds = syncPanelBounds = xfPanelBounds = {};
    masterFxPanelBounds = masterOutBounds = masterMeterBounds = {};
    masterTempoBounds = masterLufsBounds = {};
}

// Lays out the ARRANGER surface inside `bounds`, and hides every one of its
// controls when the ARRANGER view is not the active one. Split out of resized()
// because it owns a self-contained set of ~14 components whose visibility is
// driven by the nav toggle rather than by the perform layout.
void MainPluginEditor::layoutArrangerSurface (juce::Rectangle<int> bounds)
{
    namespace T = stemdeck::tokens;

    juce::Component* arrangerControls[] = {
        &arrangementLabel, &launchAllButton, &playArrButton, &arrRecordButton, &arrLoopButton,
        &arrZoomOutButton, &arrZoomInButton, &arrZoomFitButton,
        &snapToggleButton, &razorButton, &overlapModeButton,
        &undoButton, &redoButton, &bounceEditsButton };

    const bool showing = pianoRollButton.getToggleState();
    if (! showing || bounds.isEmpty())
    {
        for (auto* c : arrangerControls) { c->setVisible (false); c->setBounds ({}); }
        arrangementView.setVisible (false);
        arrangementView.setBounds ({});
        return;
    }

    for (auto* c : arrangerControls) c->setVisible (true);
    arrangementView.setVisible (true);

    // Energy arc is context, not the subject: cap it so the timeline keeps the
    // room. It was taking up to 190px of a footer-sized region before.
    auto arcArea = bounds.removeFromTop (juce::jlimit (84, 150, bounds.getHeight() * 22 / 100));
    energyArc.setBounds (arcArea);
    bounds.removeFromTop (T::gap5);

    // Toolbar: title left, transport centred, edit tools + zoom right.
    auto bar = bounds.removeFromTop (28);
    arrangementLabel.setBounds (bar.removeFromLeft (juce::jmin (200, bar.getWidth() / 4)));

    auto zoomBox = bar.removeFromRight (112);
    arrZoomFitButton.setBounds (zoomBox.removeFromRight (36).reduced (1, 2));
    zoomBox.removeFromRight (T::gap2);
    arrZoomInButton .setBounds (zoomBox.removeFromRight (30).reduced (1, 2));
    arrZoomOutButton.setBounds (zoomBox.removeFromRight (30).reduced (1, 2));
    bar.removeFromRight (T::gap6);

    // Edit tools were TOOLS-only before, which meant undo/redo for the
    // arrangement were hidden behind a toggle on a surface dedicated to editing.
    auto tools = bar.removeFromRight (juce::jmin (330, bar.getWidth() / 2));
    auto chip = [&tools] (juce::Component& c, int w)
    {
        c.setBounds (tools.removeFromLeft (w).withSizeKeepingCentre (w, 24));
        tools.removeFromLeft (T::gap2);
    };
    chip (snapToggleButton, 46); chip (razorButton, 48); chip (overlapModeButton, 52);
    chip (undoButton, 44); chip (redoButton, 44); chip (bounceEditsButton, 62);

    // 56px each so "LAUNCH" / "Record" fit without ellipsizing (was 44 → "Rec...").
    auto cluster = bar.withSizeKeepingCentre (juce::jmin (bar.getWidth(), 56 * 4 + T::gap5 * 3), 26);
    auto tbtn = [&cluster] (juce::Component& c) { c.setBounds (cluster.removeFromLeft (56)); cluster.removeFromLeft (T::gap5); };
    tbtn (launchAllButton);
    tbtn (playArrButton);
    tbtn (arrRecordButton);
    tbtn (arrLoopButton);

    bounds.removeFromTop (T::gap4);
    arrangementView.setBounds (bounds);   // the timeline takes everything left
}

void MainPluginEditor::refreshEnergyArc()
{
    // Auto Director: build the arc from the focused deck's structure analysis.
    const int deck = focusedDeck;
    const auto ana = processorRef.getDeckActiveAnalysis (deck);
    const double total = processorRef.getDeckTotalSeconds (deck);
    const double bpm = ana.detectedBpm;
    std::vector<std::tuple<float, float, juce::String>> structNorm;
    if (total > 0.0 && bpm > 1.0)
    {
        const auto audio = processorRef.getDeckLoadedAudio (deck);
        if (audio && audio->audio.getNumSamples() > 0)
        {
            const auto sections = stemdeck::segmentStructure (audio->audio, audio->sourceSampleRate, bpm);
            double lastEnd = 0.0;
            for (const auto& s : sections) lastEnd = juce::jmax (lastEnd, s.endBeat);
            if (lastEnd > 0.0)
                for (const auto& s : sections)
                    structNorm.emplace_back ((float) (s.startBeat / lastEnd), (float) (s.endBeat / lastEnd), s.label);
        }
    }
    energyArc.setArc (stemdeck::buildEnergyArc (structNorm));
    energyArc.setPlayhead (total > 0.0 ? (float) (processorRef.getDeckPlayheadSeconds (deck) / total) : 0.0f);
}

void MainPluginEditor::refreshMixArrangement()
{
    using namespace stemdeck::session;
    SessionModel s;
    const double bpm = juce::jmax (60.0, processorRef.getTransportSnapshot().bpm);
    s.masterTempo = bpm;
    s.sampleRate = juce::jmax (8000.0, processorRef.getRecordingSampleRate());

    auto paramVal = [this] (const juce::String& id) -> double
    { if (auto* p = processorRef.parameters().getRawParameterValue (id)) return p->load(); return 0.0; };

    int placed = 0; double firstDeckStart = 0.0;
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        if (processorRef.getDeckPath (d).isEmpty()) continue;
        const double at = placed * 32.0;                       // stagger clips by 8 bars
        if (placed == 0) firstDeckStart = at;

        TrackRef t; t.id = juce::String (d); t.path = processorRef.getDeckPath (d);
        if (auto a = processorRef.getDeckLoadedAudio (d))
        {
            t.durationSec = a->numSourceSamples() / juce::jmax (1.0, a->sourceSampleRate);
            t.bpm = a->analysis.detectedBpm; t.keyPitchClass = a->analysis.keyPitchClass; t.keyIsMinor = a->analysis.keyIsMinor;
        }
        s.addTrack (t);

        Event load; load.type = EventType::deckLoad; load.deck = d; load.atBeat = at; load.trackId = t.id; s.addEvent (load);
        Event fad;  fad.type = EventType::param; fad.deck = d; fad.atBeat = at; fad.paramId = "fader";
        fad.value = juce::jlimit (0.0, 1.0, paramVal (deckPrefix (d) + "Fader")); s.addEvent (fad);
        Event cs;   cs.type = EventType::param; cs.deck = d; cs.atBeat = at; cs.paramId = "cfSide";
        cs.value = processorRef.getDeckCfSide (d); s.addEvent (cs);
        Event lo;   lo.type = EventType::param; lo.deck = d; lo.atBeat = at; lo.paramId = "low";
        lo.value = paramVal (deckPrefix (d) + "Low"); s.addEvent (lo);
        if (paramVal (deckPrefix (d) + "Play") > 0.5)
        { Event pl; pl.type = EventType::transport; pl.deck = d; pl.transport = TransportKind::play; pl.atBeat = at; s.addEvent (pl); }
        ++placed;
    }

    Event cf; cf.type = EventType::param; cf.deck = 0; cf.atBeat = 0.0; cf.paramId = "crossfader";
    cf.value = juce::jlimit (0.0, 1.0, paramVal ("crossfader")); s.addEvent (cf);

    // Fold the retroactive tempo lane into the session (normalised beat -> beats), so
    // it re-times this arrangement and anything exported from it (§5.4).
    if (! masterTempoLaneNorm.empty())
    {
        double lastBeat = 0.0;
        for (const auto& e : s.events) lastBeat = juce::jmax (lastBeat, e.atBeat);
        const double totalBeats = juce::jmax (16.0, lastBeat);
        for (const auto& p : masterTempoLaneNorm)
            s.setTempoPoint ((double) p.first * totalBeats, p.second);
    }

    mixArrangeView.setSession (s);
    // Playhead from the first loaded deck's position.
    if (placed > 0)
        mixArrangeView.setPlayheadBeat (firstDeckStart + processorRef.getDeckPlayheadSeconds (0) * bpm / 60.0);
}

void MainPluginEditor::startAutomix()
{
    auto isPlaying = [this] (int d) {
        auto* p = processorRef.parameters().getRawParameterValue (deckPrefix (d) + "Play");
        return p != nullptr && p->load() > 0.5f;
    };
    auto hasAudio = [this] (int d) { return processorRef.getDeckPath (d).isNotEmpty(); };

    // Outgoing = the first playing deck with a track; incoming = the next loaded,
    // not-yet-playing deck.
    int outDeck = -1, inDeck = -1;
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (isPlaying (d) && hasAudio (d)) { outDeck = d; break; }
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (d != outDeck && hasAudio (d) && ! isPlaying (d)) { inDeck = d; break; }

    if (outDeck < 0 || inDeck < 0)
    {
        automixStatusLabel.setText ("AUTO needs a playing deck + a second loaded track",
                                    juce::dontSendNotification);
        return;
    }

    const auto plan = processorRef.beginAutomix (outDeck, inDeck);
    if (! plan.valid)
    {
        automixStatusLabel.setText ("AUTO: not enough phrase structure to mix", juce::dontSendNotification);
        return;
    }

    const double bpm = juce::jmax (60.0, (double) processorRef.getDeckActiveAnalysis (outDeck).detectedBpm);
    automixDurationMs = (double) plan.lengthBeats * 60000.0 / bpm;
    automixOutDeck    = outDeck;
    automixCfFrom     = processorRef.parameters().getRawParameterValue ("crossfader")->load();
    automixCfTo       = MainPluginProcessor::automixCrossfaderTo();   // ramp toward the incoming (B/right) bus
    automixStartMs    = juce::Time::getMillisecondCounter();
    automixRamping    = true;
    automixStatusLabel.setText (plan.description, juce::dontSendNotification);
}

void MainPluginEditor::updateAutomixRamp()
{
    if (! automixRamping)
        return;

    const double elapsed = (double) (juce::Time::getMillisecondCounter() - automixStartMs);
    const double t = automixDurationMs > 1.0 ? juce::jlimit (0.0, 1.0, elapsed / automixDurationMs) : 1.0;
    const float cf = automixCfFrom + (automixCfTo - automixCfFrom) * (float) t;
    if (auto* p = processorRef.parameters().getParameter ("crossfader"))
        p->setValueNotifyingHost (cf);

    if (t >= 1.0)
    {
        automixRamping = false;
        // Stop the outgoing deck once fully faded out.
        if (automixOutDeck >= 0)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (automixOutDeck) + "Play"))
                p->setValueNotifyingHost (0.0f);
        automixOutDeck = -1;
        automixStatusLabel.setText ("AUTO: mix complete", juce::dontSendNotification);
    }
}

void MainPluginEditor::timerCallback()
{
    ++uiTimerTick;
    const bool underPressure = processorRef.getAudioCpuLoad() > 0.85f;
    if (underPressure != cpuPressureMode)
    {
        cpuPressureMode = underPressure;
        recordStatus.setText (
            underPressure
                ? "Performance protection · reducing visual refresh while CPU is high"
                : "Performance protection cleared · full visual refresh restored",
            juce::dontSendNotification);
    }
    if (processorRef.isIntentCaptureActive())
        processorRef.pollHostedPluginParameterChanges();
    if (mixerView.isVisible() && (! cpuPressureMode || uiTimerTick % 3 == 0))
    {
        mixerView.numChannels = processorRef.getNumChannels();
        mixerView.repaint();
    }

    // Service deferred audio-thread requests (e.g. TE transport auto-stop).
    processorRef.serviceDeferredActions();
    if ((autosaveTick++ % 150) == 0)
        scheduleRecoveryAutosave();

    if (pendingRecordingStartMs > 0.0)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now >= pendingRecordingStartMs)
        {
            const auto file = pendingRecordingFile;
            pendingRecordingFile = juce::File();
            pendingRecordingStartMs = 0.0;
            startRecordingNow (file);
            if (! restoreMetronomeAfterCountIn)
                processorRef.setMetronomeEnabled (false);
        }
        else
        {
            const double bpm = juce::jlimit (40.0, 240.0,
                (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
            const double beatsLeft = (pendingRecordingStartMs - now) * bpm / 60000.0;
            recordStatus.setText ("Count-in: " + juce::String ((int) std::ceil (beatsLeft))
                                  + " beats", juce::dontSendNotification);
        }
    }

    updateAutomixRamp();
    if (mixArrangeView.isVisible()) refreshMixArrangement();   // live mix-timeline
    if (energyArc.isVisible())      refreshEnergyArc();        // ScenePilot energy arc
    refreshDeckStrips();                                        // per-deck control strips
    bool relayout = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const int loaded = deckHasContent (i) ? 1 : 0;
        if (deckDimState[(size_t) i] != loaded)
        {
            deckDimState[(size_t) i] = loaded;
            // Loaded<->empty flips the waveform region, the docked knobs AND the
            // column width (an idle channel is a narrow drop target), so this needs
            // a full relayout — which re-applies the control weights on its way out.
            relayout = true;
        }
    }
    // Pads strip collapses/expands when the first pad is loaded or the last cleared.
    bool anyPad = false;
    for (int i = 0; i < PadBank::numPads; ++i)
        if (processorRef.getPadPath (i).isNotEmpty()) { anyPad = true; break; }
    if (padsCollapsed == anyPad) relayout = true;   // state disagrees with last layout

    bool widthMotion = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const float delta = deckWidthTarget[(size_t) i] - deckWidthCurrent[(size_t) i];
        if (std::abs (delta) <= 0.5f)
        {
            deckWidthCurrent[(size_t) i] = deckWidthTarget[(size_t) i];
            continue;
        }
        // ~220 ms interruptible expansion at the editor's 30 Hz timer cadence.
        deckWidthCurrent[(size_t) i] += delta * 0.28f;
        widthMotion = true;
    }
    if (relayout || widthMotion) resized();
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)    // two-zone waveform (§2)
    {
        auto* region = deckWaveRegions[(size_t) i].get();
        if (region == nullptr || ! region->isVisible()) continue;
        // Reload peaks when the deck's content changes (also picks up stems that
        // finish separating a moment after the full mix loads).
        const juce::String sig = processorRef.getDeckPath (i) + "|" + juce::String (processorRef.deckHasNeuralStems (i) ? 1 : 0);
        const int h = sig.hashCode();
        if (waveDataState[(size_t) i] != h
            && (! cpuPressureMode || uiTimerTick % 3 == 0))
        {
            waveDataState[(size_t) i] = h;
            refreshDeckWaveData (i);
        }
        // Live playhead.
        const double total = processorRef.getDeckTotalSeconds (i);
        region->setPlayheadNorm (total > 0 ? (float) (processorRef.getDeckPlayheadSeconds (i) / total) : 0.0f);
    }
    {   // 3.1 Master button names the outcome (target LUFS from the adjacent selector).
        const int mode = aiMasterMode.getSelectedId();
        const char* tgt = mode == 2 ? "-9" : (mode == 3 ? "-16" : "-14");
        const juce::String want = juce::String ("Master ") + tgt;
        if (aiMasterButton.getButtonText() != want) aiMasterButton.setButtonText (want);
    }
    {   // 2.1 SCOPE recedes when there's no signal instead of shouting "-inf dB".
        const bool sig = juce::jmax (processorRef.getMasterPeak (0), processorRef.getMasterPeak (1)) > 0.0015f;
        const float a = sig ? 1.0f : 0.45f;
        if (std::abs (mixerPanel.getAlpha() - a) > 0.01f) mixerPanel.setAlpha (a);
    }
    {   // Keep the BEAT SYNC pill reflecting the real beatSync state.
        const bool bs = processorRef.parameters().getRawParameterValue ("beatSync")->load() > 0.5f;
        if (syncBpmButton.getToggleState() != bs) syncBpmButton.setToggleState (bs, juce::dontSendNotification);
    }

    // ── Motion: nav underline slides toward the active view's cell ──────────
    {
        const juce::Button* navBtns[4] = { &toggleLibraryButton, &navDecksButton,
                                           &mixerButton, &pianoRollButton };
        int active = browserVisible ? 0 : -1;
        for (int i = 0; i < 4; ++i)
            if (active < 0 && navBtns[i]->getToggleState()) { active = i; break; }

        float tx = navUnderlineX, tw = 0.0f;
        if (active >= 0 && ! navRects[(size_t) active].isEmpty())
        {
            tx = (float) navRects[(size_t) active].getX() + 8.0f;
            tw = (float) navRects[(size_t) active].getWidth() - 16.0f;
        }
        const float k  = 0.35f;   // ease ~120ms at 30Hz
        const float nx = navUnderlineX < 0.0f ? tx : navUnderlineX + (tx - navUnderlineX) * k;
        const float nw = navUnderlineW + (tw - navUnderlineW) * k;
        if (std::abs (nx - navUnderlineX) > 0.2f || std::abs (nw - navUnderlineW) > 0.2f)
        {
            navUnderlineX = nx;
            navUnderlineW = nw;
            if (! navRects[0].isEmpty())
                repaint (navRects[0].getX() - 4, navRects[0].getBottom() - 5,
                         navRects[3].getRight() - navRects[0].getX() + 8, 8);
        }
    }
    // ── Motion: repaint decaying pad flashes ────────────────────────────────
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < PadBank::numPads; ++i)
            if (padFlashMs[(size_t) i] > 0.0 && nowMs - padFlashMs[(size_t) i] < 360.0)
                repaint (pads[(size_t) i].trigger.getBounds().expanded (8));
    }

    // MIDI learn feedback — reflect bindings as they happen.
    {
        auto& midi = processorRef.getMidiMapping();
        if (auto res = midi.pollLearnResult(); res.valid)
        {
            const auto name = res.isCc ? midi.ccName (res.targetIdx) : midi.noteName (res.targetIdx);
            libraryMetaLabel.setText ((res.isCc ? "Mapped CC " : "Mapped Note ")
                                      + juce::String (res.controller) + u8 ("  \xe2\x86\x92  ") + name,
                                      juce::dontSendNotification);
        }
        midiLearnButton.setButtonText (midi.isLearning() ? u8 ("LEARN\xe2\x80\xa6") : juce::String ("MIDI"));
        midiLearnButton.setColour (juce::TextButton::buttonColourId,
                                   midi.isLearning() ? juce::Colour (sattari::brand::kAccent).withAlpha (0.55f)
                                                     : juce::Colour (sattari::brand::kPanelRaised));
    }

    // Live input meters / instrument activity on the per-type strips.
    if (! cpuPressureMode || uiTimerTick % 3 == 0)
        refreshChannelStrips();

    processorRef.refreshLiveKeyAnalysis();
    auto snapshot = processorRef.getTransportSnapshot();
    tempoLabel.setText (juce::String (snapshot.bpm, 1) + (snapshot.hostPlaying ? u8 ("  \xe2\x96\xb6") : u8 ("  \xe2\x97\xbc")),
                        juce::dontSendNotification);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    const bool recording = processorRef.isMasterRecording();
    arrRecordButton.setToggleState (recording, juce::dontSendNotification);
    // topRecordButton self-paints its active (red) state from isActive(); label pulses.
    topRecordButton.setLabel (recording ? "REC \xe2\x80\xa2" : "REC");
    topRecordButton.repaint();

    // Persistent recording/system-health model. Every field is sourced from the
    // corresponding engine or recorder; no badge is driven by showcase/demo state.
    {
        stemdeck::RecordingHealthBar::State hs;
        hs.recording = recording;
        const auto samples = processorRef.getRecordedSamples();
        const auto sr = processorRef.getRecordingSampleRate();
        hs.elapsedSeconds = sr > 0.0 ? (double) samples / sr : 0.0;
        hs.bpm = juce::jlimit (40.0, 240.0,
            (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        hs.takeNumber = juce::jmax (1, processorRef.getCurrentTakeNumber()
                                           + (recording ? 0 : 1));
        hs.barNumber = (int) std::floor (hs.elapsedSeconds * hs.bpm / 240.0) + 1;
        hs.captureCoverage = processorRef.getIntentCaptureCoverage();
        hs.captureTarget = processorRef.getIntentCaptureTargetCount();
        hs.captureEvents = processorRef.getIntentCaptureEventCount();
        hs.channelCount = juce::jmin (stemdeck::RecordingHealthBar::kMaxChannels,
                                     processorRef.getNumChannels());
        const bool captureWriting = recording && processorRef.isIntentCaptureActive();
        for (int i = 0; i < hs.channelCount; ++i)
        {
            auto& ch = hs.channels[(size_t) i];
            const auto type = processorRef.getChannelType (i);
            ch.label = type == ChannelType::input ? "IN"
                     : type == ChannelType::midiInstrument ? "M"
                     : juce::String::charToString ((juce_wchar) ('A' + i));
            ch.accent = deckAccent (i);
            ch.armed = true;
            ch.writing = captureWriting;
        }
        hs.integratedLufs = processorRef.getLiveIntegratedLufs();
        hs.limiterGrDb = processorRef.getLimiterGrDb();
        hs.peakDb = processorRef.getLivePeakDb();
        hs.bufferMs = sr > 0.0 ? 1000.0 * processorRef.getRecordingBlockSize() / sr : 0.0;
        hs.droppedBuffers = processorRef.getDroppedBufferCount();
        hs.droppedEvents = processorRef.getDroppedCaptureEventCount();
        hs.cpuLoad = processorRef.getAudioCpuLoad();
        hs.midiLinked = processorRef.hasMidiBindings();
        hs.countInBars = recordingHealthBar.getState().countInBars;

        if ((healthRefreshTick++ % 30) == 0)
        {
            auto volume = processorRef.getMasterRecordingTarget().getParentDirectory();
            if (volume == juce::File())
                volume = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
            const auto freeBytes = volume.getBytesFreeOnVolume();
            const double bytesPerSecond = juce::jmax (1.0, sr) * 2.0 * 3.0
                                        * (double) (MainPluginProcessor::numDecks + 2);
            cachedDiskMinutes = freeBytes > 0
                              ? (int) ((double) freeBytes / bytesPerSecond / 60.0) : -1;
        }
        hs.diskMinutes = cachedDiskMinutes;
        recordingHealthBar.setState (std::move (hs));
    }

    // Meters live in the MASTER OUTPUT panel — refresh continuously.
    if (! masterOutBounds.isEmpty())
        repaint (masterOutBounds);

    // Feed the live master-output scope + refresh the console channel meters.
    mixerPanel.pushLevel (processorRef.getMasterPeak (0), processorRef.getMasterPeak (1));
    for (const auto& mb : deckMeterBounds)
        if (! mb.isEmpty())
            repaint (mb);

    // DECKS nav chip reads active when no overlay view is open.
    navDecksButton.setToggleState (! pianoRollButton.getToggleState()
                                     && ! pianoRoll.isVisible() && ! mixerView.isVisible()
                                     && ! libraryVisible && ! browserVisible,
                                   juce::dontSendNotification);

    // Newly-loaded deck files pick up imported Rekordbox cues automatically.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto path = processorRef.getDeckPath (d);
        if (path != lastCuePaths[(size_t) d])
        {
            lastCuePaths[(size_t) d] = path;
            applyImportedCues (d);
        }
    }

    {
        // Project tempo (MASTER BPM) drives the arrangement grid + the piano roll.
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        // Keep the arranger's lane count tied to the real channel count, so adding
        // or removing a channel is reflected on both surfaces.
        arrangementView.setVisibleLanes (processorRef.getNumTrackDecks());
        arrangementView.setSnap (snapToggleButton.getToggleState(), 0.25, bpm);
        pianoRoll.setBpm (bpm);
    }

    // Push live playback state into each deck's waveform display so the playhead
    // line + play/pause glyph track the actual deck transport in real time.
    bool deckPlayChanged = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const bool playing = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        if (auto* wave = deckWaves[(size_t) i].get())
            wave->setPlaybackState (processorRef.getDeckPlayheadSeconds (i),
                                    processorRef.getDeckTotalSeconds (i),
                                    playing);
        if (playing != lastDeckPlayingUI[(size_t) i])
        {
            // Play/stop icon follows the toggle state (PlayIconLookAndFeel), no text.
            deckControls[(size_t) i].play.repaint();
            // Auto-follow: the last deck to START playing becomes the sync master.
            if (playing)
                setMasterDeck (i);
            lastDeckPlayingUI[(size_t) i] = playing;
            deckPlayChanged = true;
        }
    }
    if (deckPlayChanged)
    {
        // Universal transport follows the fleet: STOP ALL (square) if anything plays,
        // else PLAY ALL (triangle). ToolButton self-paints the active/red-cyan state.
        bool anyPlaying = false;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            anyPlaying = anyPlaying || lastDeckPlayingUI[(size_t) i];
        masterPlayButton.setIcon (anyPlaying ? stemdeck::ToolIcon::stop : stemdeck::ToolIcon::play);
        masterPlayButton.setLabel (anyPlaying ? "STOP ALL" : "PLAY ALL");
        repaint();   // refresh the deck-card active glow
    }

    undoButton.setEnabled (arrangementView.canUndo());
    redoButton.setEnabled (arrangementView.canRedo());

    // Arrangement transport feedback: moving playhead + button state.
    const bool arrPlaying = processorRef.isArrangementPlaying();
    arrangementView.setPlaybackPosition (processorRef.getArrangementPositionSeconds(), arrPlaying);

    playArrButton.setToggleState (arrPlaying, juce::dontSendNotification);
    playArrButton.setButtonText (arrPlaying ? u8 ("\xe2\x96\xa0") : u8 ("\xe2\x96\xb6"));   // ■ / ▶
    if (recording)
    {
        const auto samples = processorRef.getRecordedSamples();
        const auto sr = processorRef.getRecordingSampleRate();
        const double seconds = sr > 0.0 ? (double) samples / sr : 0.0;
        arrangementView.setRecordingProgress (seconds, processorRef.getMasterRecordingTarget().getFullPathName());
    }
    else if (! arrangementView.hasArrangement())
    {
        arrangementView.setIdle ("Hit Record to capture your set across every channel lane.");
    }

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        // Update slip button visual state
        updateSlipButtonState (i);

        // Beat roll: auto-release after one loop cycle if loopRollActive
        auto& dc = deckControls[(size_t) i];
        if (dc.loopRollActive && ! processorRef.isDeckLoopActive (i))
        {
            dc.loopRollActive = false;
            for (auto& b : dc.beatRollBtns)
                b.setToggleState (false, juce::dontSendNotification);
        }

        deckControls[(size_t) i].keyInfo.setText (processorRef.getDeckKeyInfo (i) + u8 (" \xe2\x80\xa2 ") + processorRef.getDeckTimePitchStatus (i) + u8 (" \xe2\x80\xa2 ") + processorRef.getDeckBeatSyncStatus (i),
                                                   juce::dontSendNotification);
        deckControls[(size_t) i].stemStatus.setText (processorRef.getDeckStemProviderStatus (i), juce::dontSendNotification);

        const auto path = processorRef.getDeckPath (i);
        if (path != deckPaths[(size_t) i])
        {
            deckPaths[(size_t) i] = path;
            if (path.isNotEmpty())
                if (auto* wave = deckWaves[(size_t) i].get())
                    wave->setFile (juce::File (path));
        }

        // Keep stem thumbnails in sync when AI stem separation completes or on reload.
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto stemPath = processorRef.getDeckStemPath (i, lane);
            if (stemPath != deckStemPaths[(size_t) i][(size_t) lane])
            {
                deckStemPaths[(size_t) i][(size_t) lane] = stemPath;
                if (auto* wave = deckWaves[(size_t) i].get())
                    wave->setStemFile (lane, stemPath.isNotEmpty() ? juce::File (stemPath) : juce::File());
            }
        }

        // RTSS badge — illuminates when real-time separation is running
        if (auto* wave = deckWaves[(size_t) i].get())
        {
            wave->setRtssActive (processorRef.isDeckRtssActive (i));
            wave->setIntroEnd   (processorRef.getIntroEnd   (i));
            wave->setOutroStart (processorRef.getOutroStart (i));

            // Beat-match grid: recompute the grid only when a grid-defining
            // parameter (file / BPM / offset / downbeat) actually changes.
            const auto pfx = deckPrefix (i);
            auto pval = [&] (const char* id) { return processorRef.parameters().getRawParameterValue (pfx + id)->load(); };
            const juce::String gridKey = deckPaths[(size_t) i]
                + "|" + juce::String (pval ("Bpm"), 2)
                + "|" + juce::String (pval ("BeatOffset"), 3)
                + "|" + juce::String (pval ("Downbeat"), 3);
            if (gridKey != lastBeatGridKey[(size_t) i])
            {
                lastBeatGridKey[(size_t) i] = gridKey;
                wave->setBeatGridSeconds (processorRef.getDeckBeatGridSeconds (i));
            }

            // Locked (green grid) when this deck's tempo matches the sync master's
            // — i.e. it is beat-matched. The master deck is always its own reference.
            const double myBpm      = (double) pval ("Bpm");
            const double masterDeckBpm = juce::isPositiveAndBelow (masterDeckIndex, MainPluginProcessor::numDecks)
                ? (double) processorRef.parameters().getRawParameterValue (deckPrefix (masterDeckIndex) + "Bpm")->load()
                : 0.0;
            const bool matched = myBpm > 1.0 && masterDeckBpm > 1.0
                && (i == masterDeckIndex || std::abs (myBpm - masterDeckBpm) < 0.15);
            wave->setBeatSyncState (matched, 0);
        }

        // Intro/Outro button toggle states
        deckControls[(size_t) i].introEndBtn  .setToggleState (processorRef.getIntroEnd   (i) >= 0.0, juce::dontSendNotification);
        deckControls[(size_t) i].outroStartBtn.setToggleState (processorRef.getOutroStart (i) >= 0.0, juce::dontSendNotification);
    }

    // ── Beat-quantized clip launcher ──────────────────────────────────────────
    {
        const double bpm = juce::jlimit (40.0, 240.0,
            (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        const double beatLenMs = (60.0 / bpm) * 1000.0;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        if (launcherBeatOriginMs < 0.0) launcherBeatOriginMs = nowMs;
        const double elapsedMs  = nowMs - launcherBeatOriginMs;
        const double beatPhase  = std::fmod (elapsedMs, beatLenMs) / beatLenMs;   // 0..1
        const bool newBeat = (beatPhase < 0.15 && launcherBeatPhase > 0.75);
        launcherBeatPhase = beatPhase;

        if (newBeat)
        {
            // Fire all lanes waiting for the next beat
            for (int lane = 0; lane < ArrangementRecorderView::numLanes; ++lane)
            {
                if (arrangementView.getSlotState (lane) != ArrangementRecorderView::SlotState::WaitingToStart)
                    continue;

                const int clipIdx = arrangementView.getLauncherClip (lane);
                const auto& allClips = arrangementView.getClips();

                if (juce::isPositiveAndBelow (lane, (int) allClips.size())
                    && juce::isPositiveAndBelow (clipIdx, (int) allClips[(size_t) lane].size()))
                {
                    const auto& clip = allClips[(size_t) lane][(size_t) clipIdx];
                    if (clip.file.existsAsFile())
                    {
                        processorRef.loadDeckFileAsync (lane, clip.file);
                        pendingSlotSeekSec[(size_t) lane] = clip.trimStartSeconds;
                    }
                }
                arrangementView.setSlotState (lane, ArrangementRecorderView::SlotState::Running);
            }
        }

        // Complete pending seek+play once the deck has finished loading
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            if (pendingSlotSeekSec[(size_t) lane] >= 0.0
                && processorRef.getDeckTotalSeconds (lane) > 0.0)
            {
                processorRef.seekDeckSeconds (lane, pendingSlotSeekSec[(size_t) lane]);
                if (auto* param = dynamic_cast<juce::AudioParameterBool*> (
                        processorRef.parameters().getParameter (deckPrefix (lane) + "Play")))
                    param->setValueNotifyingHost (1.0f);
                pendingSlotSeekSec[(size_t) lane] = -1.0;
            }
        }

        // Clear Running state when the deck stops; fire follow action if set
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            if (arrangementView.getSlotState (lane) == ArrangementRecorderView::SlotState::Running)
            {
                const bool playing = processorRef.parameters()
                    .getRawParameterValue (deckPrefix (lane) + "Play")->load() > 0.5f;
                if (! playing)
                {
                    const int clipIdx = arrangementView.getLauncherClip (lane);
                    applyFollowAction (lane, clipIdx);
                }
            }
        }
    }
}

// ── Toolbox tabs (declutter) ──────────────────────────────────────────────────

void MainPluginEditor::setupToolTabs (int deckIndex)
{
    static const char* const kLabels[] = { "CUE", "LOOP", "FX", "SYNC", "SRC" };
    static const char* const kTips[] = {
        "CUE - hot cues (1-8) + Intro End / Outro Start markers",
        "LOOP - loop in/out, halve/double, slip, beat-roll and beat-jump",
        "FX - this deck's echo/macro + per-stem filter, send and pitch",
        "SYNC - source BPM, beat-grid offset, tempo/sync mode, key match",
        "SRC - load a track, AI stem separation, and MIDI/drum extraction" };
    auto& dc = deckControls[(size_t) deckIndex];
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
    {
        auto& btn = dc.toolTabs[(size_t) t];
        btn.setButtonText (kLabels[t]);
        setupButton (btn);
        btn.setTooltip (kTips[t]);
        // Reference §3: these are segments of ONE control, not five buttons. The
        // LookAndFeel's segTab branch draws the active chip; text carries the state
        // (fg-primary active / fg-tertiary inactive) rather than an accent fill.
        btn.getProperties().set ("segTab", true);
        btn.setColour (juce::TextButton::textColourOnId,  stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
        btn.setColour (juce::TextButton::textColourOffId, stemdeck::tokens::c (stemdeck::tokens::fgTertiary));
        btn.onClick = [this, deckIndex, t] { setDeckToolTab (deckIndex, t); };
        addAndMakeVisible (btn);
    }
    setDeckToolTab (deckIndex, DeckControls::TabCue);
}

void MainPluginEditor::selectDeckToolTab (int deckIndex, int tab)
{
    if (juce::isPositiveAndBelow (deckIndex, (int) deckControls.size())
        && juce::isPositiveAndBelow (tab, (int) DeckControls::NumToolTabs))
        setDeckToolTab (deckIndex, tab);
}

// Reference §5: the scope selector offers master, the focused CHANNEL, and that
// channel's stems — not all 22 engine chains. Rebuilt whenever focus moves.
void MainPluginEditor::refreshFxScopes()
{
    const int deck = juce::jlimit (0, HostGraph::kNumDecks - 1, focusedDeck);
    const juce::String letter = juce::String::charToString ((juce_wchar) ('A' + deck));

    juce::StringArray names;
    juce::Array<int>  chains;

    names.add ("Master");                 chains.add ((int) HostGraph::Chain::master);
    names.add ("Deck " + letter);         chains.add (deck);

    // The stem scopes belong to the FOCUSED deck, so they must SAY which deck.
    // Labelled "▸ VOX" alone, the rack silently re-points at a different channel
    // when focus changes and the tab strip looks identical either way — which is
    // most of why "put a filter on the vocal of channel B" is hard to answer.
    // See docs/fx-signal-path.md.
    static const char* const stemTag[4] = { "DRM", "BAS", "MUS", "VOX" };
    for (int st = 0; st < HostGraph::kNumStemsPerDeck; ++st)
    {
        names.add (letter + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xb8 ")) + stemTag[st]);
        chains.add ((int) HostGraph::stemChain (deck, st));
    }

    fxRack.setScopes (names, chains);
    fxRack.setAccent (deckAccent (deck));
}

void MainPluginEditor::setDeckToolTab (int deckIndex, int tab)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const bool changed = dc.activeToolTab != tab;
    dc.activeToolTab = tab;
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
        dc.toolTabs[(size_t) t].setToggleState (t == tab, juce::dontSendNotification);
    applyDeckToolTabVisibility (deckIndex);
    resized();   // populates deckTabPanelBounds for the fade below

    if (! changed)
        return;

    // Reference §3: the swap is a 160ms crossfade, interruptible — "that transition is
    // where the Apple-clean feel lives". Rather than enumerate every control per tab a
    // second time (the enumeration in applyDeckToolTabVisibility is already long and
    // would drift), fade whatever now occupies the tab's well. New controls added to a
    // tab inherit the transition for free.
    const auto well = deckTabPanelBounds[(size_t) deckIndex];
    if (well.isEmpty())
        return;

    auto& anim = juce::Desktop::getInstance().getAnimator();
    for (auto* c : getChildren())
    {
        if (c == nullptr || ! c->isVisible()) continue;
        const auto b = c->getBounds();
        if (b.isEmpty() || ! well.contains (b.getCentre())) continue;

        anim.cancelAnimation (c, false);   // interruptible: pick up from where it is
        c->setAlpha (0.0f);
        anim.fadeIn (c, stemdeck::tokens::durState);
    }
}

void MainPluginEditor::applyDeckToolTabVisibility (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const int tab = dc.activeToolTab;

    const bool cue  = tab == DeckControls::TabCue;
    const bool loop = tab == DeckControls::TabLoop;
    const bool fx   = tab == DeckControls::TabFx;
    const bool sync = tab == DeckControls::TabSync;
    const bool src  = tab == DeckControls::TabSrc;

    for (auto& b : dc.hotCues)        b.setVisible (cue);
    dc.introEndBtn.setVisible (cue);
    dc.outroStartBtn.setVisible (cue);
    dc.loopIn.setVisible (loop); dc.loopOut.setVisible (loop); dc.loopToggle.setVisible (loop);
    dc.loopHalf.setVisible (loop); dc.loopDouble.setVisible (loop);
    dc.slipButton.setVisible (loop);
    for (auto& b : dc.beatRollBtns)  b.setVisible (loop);
    for (auto& b : dc.beatJumpBack)  b.setVisible (loop);
    for (auto& b : dc.beatJumpFwd)   b.setVisible (loop);
    dc.reverbMix.setVisible (true);   // lives in the stem strip's FX cell now
    dc.echoMix.setVisible (fx); dc.macroKnob.setVisible (fx);
    for (auto& k : dc.stemFilt)  k.setVisible (fx);
    for (auto& k : dc.stemSend)  k.setVisible (fx);
    for (auto& k : dc.stemPitch) k.setVisible (fx);
    dc.stemGains[(size_t) DeckPlayer::fullMixStem].setVisible (src);   // FULL trim in SRC tab
    dc.extractMidi.setVisible (src);
    dc.extractDrums.setVisible (src);
    dc.bpm.setVisible (sync); dc.beatOffset.setVisible (sync); dc.downbeat.setVisible (sync);
    dc.tempoInterpretation.setVisible (sync); dc.syncMode.setVisible (sync); dc.keyTarget.setVisible (sync);
    dc.load.setVisible (src); dc.aiStems.setVisible (src); dc.loadStem.setVisible (src); dc.stemLane.setVisible (src);
    dc.liveKey.setVisible (sync); dc.keyLock.setVisible (sync);   // key controls live in the SYNC tab
}

// ── Hot cue helpers ───────────────────────────────────────────────────────────

static const std::array<juce::Colour, 8> kHotCueColours {{
    juce::Colour (0xFFE03030), juce::Colour (0xFF3090E0), juce::Colour (0xFF30C050),
    juce::Colour (0xFFE0B020), juce::Colour (0xFFB040E0), juce::Colour (0xFFE06020),
    juce::Colour (0xFF20C8C8), juce::Colour (0xFFE040A0),
}};

void MainPluginEditor::setupHotCueButtons (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
    {
        auto& btn = dc.hotCues[(size_t) c];
        btn.setButtonText (juce::String (c + 1));
        btn.setClickingTogglesState (false);
        // Empty by default: calm neutral pad. refreshHotCueButtonColours lights it
        // up in its cue colour only once a cue is actually stored there.
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (sattari::brand::kPanelRaised));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (sattari::brand::kTextMuted));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btn.setTooltip ("Hot cue " + juce::String (c + 1)
                        + " - click to drop a cue at the playhead, or jump to it. "
                          "Alt-click clears; Cmd-click jumps + sets a 1-bar loop.");
        btn.onClick = [this, deckIndex, c]
        {
            handleHotCuePress (deckIndex, c, juce::ModifierKeys::getCurrentModifiers());
        };
        addAndMakeVisible (btn);
    }

    // Intro end / Outro start buttons (CUE tab, second row)
    auto setupDJMarker = [&] (juce::TextButton& btn, juce::Colour col, bool isIntro)
    {
        setupButton (btn);
        btn.setClickingTogglesState (true);
        btn.setColour (juce::TextButton::buttonOnColourId, col.withAlpha (0.65f));
        btn.setTooltip (isIntro
            ? "Set Intro End: marks where the intro finishes (click again to clear)"
            : "Set Outro Start: marks where the outro begins (click again to clear)");
        btn.onClick = [this, &btn, deckIndex, isIntro]
        {
            if (btn.getToggleState())
            {
                const double pos = processorRef.getDeckPlayheadSeconds (deckIndex);
                if (isIntro) processorRef.setIntroEnd   (deckIndex, pos);
                else         processorRef.setOutroStart (deckIndex, pos);
            }
            else
            {
                if (isIntro) processorRef.clearIntroEnd   (deckIndex);
                else         processorRef.clearOutroStart (deckIndex);
            }
        };
        addAndMakeVisible (btn);
    };
    setupDJMarker (dc.introEndBtn,   juce::Colour (0xff26d9ff), true);
    setupDJMarker (dc.outroStartBtn, juce::Colour (0xffffb020), false);
}

void MainPluginEditor::handleHotCuePress (int deckIndex, int cueIndex, juce::ModifierKeys mods)
{
    if (mods.isAltDown())
    {
        // Alt+click = clear cue
        processorRef.clearHotCue (deckIndex, cueIndex);
    }
    else if (mods.isShiftDown() || processorRef.getHotCue (deckIndex, cueIndex) < 0.0)
    {
        // Shift+click or unset cue = set at current playhead
        processorRef.setHotCue (deckIndex, cueIndex, processorRef.getDeckPlayheadSeconds (deckIndex));
    }
    else if (mods.isCtrlDown() || mods.isCommandDown())
    {
        // Ctrl/Cmd+click = Goto+Loop: jump to cue and set a 1-bar loop from there
        const double cuePos = processorRef.getHotCue (deckIndex, cueIndex);
        if (cuePos >= 0.0)
        {
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
            processorRef.jumpToHotCue (deckIndex, cueIndex);
            processorRef.setDeckLoopSeconds (deckIndex, cuePos, cuePos + barSeconds);
            processorRef.setDeckLoopActive (deckIndex, true);
            updateLoopButtonStates (deckIndex);
            updateWaveformLoopRegion (deckIndex);
        }
    }
    else
    {
        // Normal click = jump to cue
        processorRef.jumpToHotCue (deckIndex, cueIndex);
    }
    refreshHotCueButtonColours (deckIndex);
    updateWaveformHotCues (deckIndex);
}

void MainPluginEditor::refreshHotCueButtonColours (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
    {
        const bool isSet = processorRef.getHotCue (deckIndex, c) >= 0.0;
        // Set cue = its colour + white number; empty = neutral pad + muted number.
        dc.hotCues[(size_t) c].setColour (juce::TextButton::buttonColourId,
            isSet ? kHotCueColours[(size_t) c].withAlpha (0.55f) : juce::Colour (sattari::brand::kPanelRaised));
        dc.hotCues[(size_t) c].setColour (juce::TextButton::textColourOffId,
            isSet ? juce::Colours::white : juce::Colour (sattari::brand::kTextMuted));
    }
}

void MainPluginEditor::updateWaveformHotCues (int deckIndex)
{
    auto* wave = deckWaves[(size_t) deckIndex].get();
    std::array<WaveformDisplay::HotCue, WaveformDisplay::numHotCues> cues {};
    for (int c = 0; c < WaveformDisplay::numHotCues; ++c)
    {
        cues[(size_t) c].seconds = processorRef.getHotCue (deckIndex, c);
        cues[(size_t) c].colour  = kHotCueColours[(size_t) c];
    }
    if (wave != nullptr)
        wave->setHotCues (cues);

    if (auto* region = deckWaveRegions[(size_t) deckIndex].get())
    {
        std::array<stemdeck::WaveformRegion::HotCue, MainPluginProcessor::numHotCues> regionCues {};
        const double total = processorRef.getDeckTotalSeconds (deckIndex);
        for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
        {
            const double seconds = processorRef.getHotCue (deckIndex, c);
            regionCues[(size_t) c] = {
                seconds >= 0.0 && total > 0.0 ? (float) (seconds / total) : -1.0f,
                kHotCueColours[(size_t) c], c + 1
            };
        }
        region->setHotCues (regionCues);
    }
}

// ── Loop helpers ─────────────────────────────────────────────────────────────

void MainPluginEditor::setupLoopControls (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const auto accent = deckAccent (deckIndex);

    setupButton (dc.loopIn);
    dc.loopIn.onClick = [this, deckIndex]
    {
        // Set loop-in at current playhead; if loop-out already set keep it, else set 2-bar ahead
        const double pos = processorRef.getDeckPlayheadSeconds (deckIndex);
        double loopOut   = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (loopOut < pos)
        {
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            loopOut = pos + (60.0 / juce::jmax (1.0f, bpm)) * 8.0;  // 2 bars default
        }
        processorRef.setDeckLoopSeconds (deckIndex, pos, loopOut);
        processorRef.setDeckLoopActive (deckIndex, true);
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopIn);

    setupButton (dc.loopOut);
    dc.loopOut.onClick = [this, deckIndex]
    {
        const double pos    = processorRef.getDeckPlayheadSeconds (deckIndex);
        const double loopIn = juce::jmax (0.0, processorRef.getDeckLoopInSeconds (deckIndex));
        processorRef.setDeckLoopSeconds (deckIndex, loopIn, pos);
        processorRef.setDeckLoopActive (deckIndex, true);
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopOut);

    setupButton (dc.loopToggle);
    dc.loopToggle.setClickingTogglesState (false);
    dc.loopToggle.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.6f));
    dc.loopToggle.onClick = [this, deckIndex]
    {
        processorRef.setDeckLoopActive (deckIndex, ! processorRef.isDeckLoopActive (deckIndex));
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopToggle);

    setupButton (dc.loopHalf);
    dc.loopHalf.onClick = [this, deckIndex]
    {
        const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
        const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (in >= 0.0 && out > in)
        {
            const double mid = in + (out - in) * 0.5;
            processorRef.setDeckLoopSeconds (deckIndex, in, mid);
            updateWaveformLoopRegion (deckIndex);
        }
    };
    addAndMakeVisible (dc.loopHalf);

    setupButton (dc.loopDouble);
    dc.loopDouble.onClick = [this, deckIndex]
    {
        const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
        const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (in >= 0.0 && out > in)
        {
            processorRef.setDeckLoopSeconds (deckIndex, in, out + (out - in));
            updateWaveformLoopRegion (deckIndex);
        }
    };
    addAndMakeVisible (dc.loopDouble);
}

void MainPluginEditor::updateLoopButtonStates (int deckIndex)
{
    const bool active = processorRef.isDeckLoopActive (deckIndex);
    deckControls[(size_t) deckIndex].loopToggle.setToggleState (active, juce::dontSendNotification);
}

// ── Slip mode + loop roll ─────────────────────────────────────────────────────

void MainPluginEditor::setupSlipAndRoll (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const auto accent = deckAccent (deckIndex);

    // Slip button: toggles slip mode on/off
    setupButton (dc.slipButton);
    dc.slipButton.setClickingTogglesState (true);
    dc.slipButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff26d9ff).withAlpha (0.5f));
    dc.slipButton.setTooltip ("Slip mode: a shadow playhead advances at 1x while you loop/scratch. Disable to snap back.");
    dc.slipButton.onClick = [this, deckIndex]
    {
        processorRef.setDeckSlipEnabled (deckIndex, deckControls[(size_t) deckIndex].slipButton.getToggleState());
    };
    addAndMakeVisible (dc.slipButton);

    // Beat roll buttons: hold = loop+slip for that beat-length, release = snap back
    static constexpr std::array<const char*, 6> kLabels { "1/4","1/2","1","2","4","8" };
    static constexpr std::array<double, 6>      kLen    { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    for (int b = 0; b < 6; ++b)
    {
        auto& btn = dc.beatRollBtns[(size_t) b];
        btn.setButtonText (kLabels[(size_t) b]);
        setupButton (btn);
        btn.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
        btn.onClick = [this, deckIndex, b]
        {
            // Single click (no hold tracking in TextButton): just arm a 1-shot loop
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
            const double rollLen = kLen[(size_t) b] * barSeconds;
            processorRef.beginDeckLoopRoll (deckIndex, rollLen);
            // Schedule end after rollLen seconds via the timer; set flag
            deckControls[(size_t) deckIndex].loopRollActive = true;
            deckControls[(size_t) deckIndex].beatRollBtns[(size_t) b].setToggleState (true, juce::dontSendNotification);
        };
        addAndMakeVisible (btn);
    }
}

void MainPluginEditor::updateSlipButtonState (int deckIndex)
{
    const bool slip = processorRef.isDeckSlipEnabled (deckIndex);
    deckControls[(size_t) deckIndex].slipButton.setToggleState (slip, juce::dontSendNotification);
}

// ── Superknob / FX macro ──────────────────────────────────────────────────────

void MainPluginEditor::setupMacroKnob (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    setupSlider (dc.macroKnob, true);
    dc.macroKnob.setRange (0.0, 1.0);
    dc.macroKnob.setValue (0.0, juce::dontSendNotification);
    dc.macroKnob.setTooltip ("FX Macro: drives Reverb + Echo simultaneously");
    dc.macroKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dc.macroKnob.setPopupDisplayEnabled (true, true, this);
    dc.macroKnob.onValueChange = [this, deckIndex]
    {
        const float v = (float) deckControls[(size_t) deckIndex].macroKnob.getValue();
        // Soft-link: macro drives both reverb and echo params
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                processorRef.parameters().getParameter (deckPrefix (deckIndex) + "RevMix")))
            p->setValueNotifyingHost (v);
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                processorRef.parameters().getParameter (deckPrefix (deckIndex) + "EchoMix")))
            p->setValueNotifyingHost (v);
    };
    addAndMakeVisible (dc.macroKnob);
}

// ── Follow actions ────────────────────────────────────────────────────────────

void MainPluginEditor::showFollowActionMenu (int laneIndex, int clipIndex)
{
    const auto& allClips = arrangementView.getClips();
    if (! juce::isPositiveAndBelow (laneIndex, (int) allClips.size())) return;
    const auto& lane = allClips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) lane.size())) return;

    juce::PopupMenu m;
    m.addSectionHeader ("Follow Action");
    m.addItem (1, "None",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::None);
    m.addItem (2, "Next clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Next);
    m.addItem (3, "Prev clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Prev);
    m.addItem (4, "Random other",true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::RandomOther);
    m.addItem (5, "Stop",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Stop);

    m.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (this),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         laneIndex, clipIndex] (int r)
    {
        if (safe == nullptr || r < 1) return;
        const auto& allClips2 = safe->arrangementView.getClips();
        if (! juce::isPositiveAndBelow (laneIndex, (int) allClips2.size())) return;
        auto snap = safe->arrangementView.captureLaneState (laneIndex);
        auto& lane2 = const_cast<std::vector<ArrangementRecorderView::LaneClip>&> (
            safe->arrangementView.getClips()[(size_t) laneIndex]);
        if (juce::isPositiveAndBelow (clipIndex, (int) lane2.size()))
        {
            switch (r)
            {
                case 1: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::None; break;
                case 2: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Next; break;
                case 3: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Prev; break;
                case 4: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::RandomOther; break;
                case 5: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Stop; break;
                default: break;
            }
            safe->arrangementView.commitEdit (snap);
        }
    });
}

void MainPluginEditor::applyFollowAction (int laneIndex, int clipIndex)
{
    using FA = ArrangementRecorderView::FollowAction;
    const auto& allClips = arrangementView.getClips();

    // Always clear running state; follow action may re-queue
    arrangementView.setSlotState (laneIndex, ArrangementRecorderView::SlotState::Idle);

    if (! juce::isPositiveAndBelow (laneIndex, (int) allClips.size())) return;
    const auto& lane = allClips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) lane.size())) return;

    const auto fa   = lane[(size_t) clipIndex].followAction;
    const float prob = lane[(size_t) clipIndex].followProbability;
    const int numClips = (int) lane.size();

    if (fa == FA::None || fa == FA::Stop) return;

    // Probability gate
    if (juce::Random::getSystemRandom().nextFloat() > prob) return;

    switch (fa)
    {
        case FA::Next:
            arrangementView.triggerSlot (laneIndex, (clipIndex + 1) % numClips);
            break;
        case FA::Prev:
            arrangementView.triggerSlot (laneIndex, (clipIndex - 1 + numClips) % numClips);
            break;
        case FA::RandomOther:
            if (numClips > 1)
            {
                int next;
                do { next = juce::Random::getSystemRandom().nextInt (numClips); } while (next == clipIndex);
                arrangementView.triggerSlot (laneIndex, next);
            }
            break;
        default: break;
    }
}

void MainPluginEditor::updateWaveformLoopRegion (int deckIndex)
{
    auto* wave = deckWaves[(size_t) deckIndex].get();
    auto* region = deckWaveRegions[(size_t) deckIndex].get();
    const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
    const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
    const double total = processorRef.getDeckTotalSeconds (deckIndex);
    const bool active = processorRef.isDeckLoopActive (deckIndex);
    if (in >= 0.0 && out > in)
    {
        if (wave != nullptr) wave->setLoopRegion (in, out, active);
        if (region != nullptr && total > 0.0)
            region->setLoopRegion ((float) (in / total), (float) (out / total), active);
    }
    else
    {
        if (wave != nullptr) wave->clearLoopRegion();
        if (region != nullptr) region->clearLoopRegion();
    }
}

// ── Camelot wheel popup ───────────────────────────────────────────────────────

void MainPluginEditor::showCamelotWheel (int deckIndex)
{
    // Interactive Camelot wheel (§6.1): the geometry IS the control. The focused
    // deck's key is highlighted, other decks are marked, and clicking a wedge
    // transposes the focused deck there (Auto Pitch, via its KeyTarget).
    const int focus = juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)
                    ? deckIndex : focusedDeck;

    auto slotFor = [] (const TrackAnalysis& a, stemdeck::CamelotSlot& out) -> bool
    {
        if (! a.hasKey()) return false;
        out.number = stemdeck::camelotNumberFor (a.keyPitchClass, a.keyIsMinor);
        out.minor  = a.keyIsMinor;
        return true;
    };

    auto wheel = std::make_unique<stemdeck::CamelotWheelComponent>();
    wheel->setSize (320, 360);

    const auto anaF = processorRef.getDeckActiveAnalysis (focus);
    stemdeck::CamelotSlot cur;
    const bool haveCur = slotFor (anaF, cur);
    wheel->setCurrent (cur, haveCur);

    std::vector<stemdeck::CamelotDeckMark> marks;
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (d != focus)
        {
            stemdeck::CamelotSlot s;
            if (slotFor (processorRef.getDeckActiveAnalysis (d), s))
                marks.push_back ({ s, deckAccent (d), d, true });
        }
    wheel->setOthers (marks);

    // Suggestion with reasoning (§6.1): pick the compatible move that costs the
    // fewest semitones to line up with another loaded deck.
    if (haveCur)
    {
        const int curPc = stemdeck::pitchClassForCamelot (cur.number, cur.minor);
        int bestCost = 99; juce::String best;
        for (const auto& m : marks)
        {
            const int mpc = stemdeck::pitchClassForCamelot (m.slot.number, m.slot.minor);
            const int cost = std::abs (stemdeck::semitoneDistance (curPc, mpc));
            if (cost > 0 && cost < bestCost)
            {
                bestCost = cost;
                best = "Deck " + juce::String::charToString ((juce::juce_wchar) ('A' + m.deckIndex))
                     + " is " + stemdeck::camelotLabel (m.slot) + " — transpose "
                     + juce::String (cost) + " semitone" + (cost == 1 ? "" : "s") + " to match.";
            }
        }
        wheel->setSuggestion (best.isNotEmpty() ? best
                              : "Adjacent wedges are harmonically compatible; across the wheel clashes.");
    }
    else
    {
        wheel->setSuggestion ("No key detected for this deck yet.");
    }

    const int curPcForPick = haveCur ? stemdeck::pitchClassForCamelot (cur.number, cur.minor) : -1;
    wheel->onPick = [safe = juce::Component::SafePointer<MainPluginEditor> (this), focus, curPcForPick]
                    (stemdeck::CamelotSlot picked, bool perStem)
    {
        if (safe == nullptr) return;
        const int pc = stemdeck::pitchClassForCamelot (picked.number, picked.minor);
        const juce::String deckL = juce::String::charToString ((juce::juce_wchar) ('A' + focus));

        if (perStem)
        {
            // Per-stem key lock (§7 tier-2): pitch the TONAL stems (bass/music/vocals)
            // by the semitone delta; leave DRUMS unpitched (pitched drums sound wrong).
            // This is the "polytuning" tier — only possible because we have separation.
            if (curPcForPick < 0) return;
            const int delta = TrackAnalyzer::nearestSemitoneDelta (curPcForPick, pc);
            safe->applyPerStemKeyLock (focus, delta);
            safe->recordStatus.setText ("Deck " + deckL + " per-stem key lock -> "
                                        + stemdeck::camelotLabel (picked) + "  (" + (delta >= 0 ? "+" : "")
                                        + juce::String (delta) + " st, drums untouched)", juce::dontSendNotification);
        }
        else
        {
            // Whole-track: KeyTarget drives Auto Pitch on the full deck (choice = pc+1).
            if (auto* p = safe->processorRef.parameters().getParameter (deckPrefix (focus) + "KeyTarget"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (pc + 1)));
            safe->recordStatus.setText ("Deck " + deckL + " transpose target " + stemdeck::camelotLabel (picked),
                                        juce::dontSendNotification);
        }
    };

    auto anchor = ! deckKeyChipBounds[(size_t) focus].isEmpty() ? deckKeyChipBounds[(size_t) focus]
                                                                : camelotButton.getBounds();
    juce::CallOutBox::launchAsynchronously (std::move (wheel), anchor, this);
}

// ── Library meta info ─────────────────────────────────────────────────────────

void MainPluginEditor::showLibraryMeta (const juce::File& file)
{
    if (! file.existsAsFile()) { libraryMetaLabel.setText ({}, juce::dontSendNotification); return; }
    // Show what we know without full analysis (duration via thumbnail if loaded)
    libraryMetaLabel.setText (file.getFileName() + "  |  " + file.getFileExtension().toUpperCase(),
                               juce::dontSendNotification);
}

// ── MIDI learn — real controller mapping ───────────────────────────────────────

void MainPluginEditor::updateMidiLearnUI()
{
    auto& midi = processorRef.getMidiMapping();

    // If a learn is already in progress, a second click cancels it.
    if (midi.isLearning())
    {
        midi.cancelLearn();
        libraryMetaLabel.setText ("MIDI learn cancelled", juce::dontSendNotification);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ("MIDI Learn  (enable controller in \xe2\x9a\x99 Options first)"));

    constexpr int kCcBase   = 1;
    constexpr int kNoteBase = 1000;
    constexpr int kClearAll = 9999;

    juce::PopupMenu cont;
    for (int i = 0; i < midi.numCc(); ++i)
    {
        const int cc = midi.ccForTarget (i);
        cont.addItem (kCcBase + i,
                      midi.ccName (i) + (cc >= 0 ? "   [CC " + juce::String (cc) + "]" : u8 ("   [\xe2\x80\x94]")));
    }
    menu.addSubMenu ("Continuous  (move a knob / fader)", cont);

    juce::PopupMenu trig;
    for (int i = 0; i < midi.numNote(); ++i)
    {
        const int n = midi.noteForTarget (i);
        trig.addItem (kNoteBase + i,
                      midi.noteName (i) + (n >= 0 ? "   [Note " + juce::String (n) + "]" : u8 ("   [\xe2\x80\x94]")));
    }
    menu.addSubMenu ("Triggers  (press a pad / button)", trig);

    menu.addSeparator();
    menu.addItem (kClearAll, "Clear all MIDI mappings");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiLearnButton),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         kCcBase, kNoteBase, kClearAll] (int result)
        {
            if (safe == nullptr || result <= 0) return;
            auto& m = safe->processorRef.getMidiMapping();

            if (result == kClearAll)
            {
                m.clearAll();
                safe->libraryMetaLabel.setText ("All MIDI mappings cleared",
                                                juce::dontSendNotification);
                return;
            }

            if (result >= kNoteBase)
            {
                const int idx = result - kNoteBase;
                if (! juce::isPositiveAndBelow (idx, m.numNote())) return;
                m.beginLearnNote (idx);
                safe->libraryMetaLabel.setText (
                    "MIDI learn: press a pad for \"" + m.noteName (idx)
                        + u8 ("\"\xe2\x80\xa6"),
                    juce::dontSendNotification);
            }
            else
            {
                const int idx = result - kCcBase;
                if (! juce::isPositiveAndBelow (idx, m.numCc())) return;
                m.beginLearnCc (idx);
                safe->libraryMetaLabel.setText (
                    "MIDI learn: move a control for \"" + m.ccName (idx)
                        + u8 ("\"\xe2\x80\xa6"),
                    juce::dontSendNotification);
            }
            safe->midiLearnButton.setButtonText (u8 ("LEARN\xe2\x80\xa6"));
        });
}
