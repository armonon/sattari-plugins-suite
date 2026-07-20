// MainPluginEditor, part 1 of 2: the main editor (decks, mixer, transport,
// library, automix). The ArrangementRecorderView nested class lives in
// MainPluginEditor_Arrangement.cpp; shared drawing helpers are in
// MainPluginEditorInternal.h.
#include "MainPluginEditor.h"
#include "MainPluginEditorInternal.h"

using namespace sdedit;

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
    title.setInterceptsMouseClicks (true, false);
    title.addMouseListener (this, false);

    // Discoverable Settings gear (advanced tools + showcase toggle live here).
    setupButton (settingsButton);
    settingsButton.setButtonText (u8 ("\xe2\x9a\x99"));   // gear glyph (UTF-8)
    settingsButton.setTooltip ("Settings");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    setupFileBrowser();
    setupLibrary();

    title.setText ("StemDeck PRO", juce::dontSendNotification);
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

    // All-deck BPM / Key sync (also on hotkeys B / K).
    setupButton (syncBpmButton);
    syncBpmButton.setTooltip ("Warp + downbeat phase-lock all decks to Deck A's tempo (hotkey: B)");
    syncBpmButton.onClick = [this] { syncBpmAll(); };
    addAndMakeVisible (syncBpmButton);
    setupButton (syncKeyButton);
    syncKeyButton.setTooltip ("Harmonically match all decks to Deck A's key, Camelot-style (hotkey: K)");
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
        processorRef.clearHostPluginChain (chainIndex);
        pluginHostMessage.setText ("Cleared " + pluginChainSelector.getText(), juce::dontSendNotification);
    };

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckName = deckLabel (i).toUpperCase();
        deckWaves[(size_t) i] = std::make_unique<WaveformDisplay> (thumbnailFormatManager, thumbnailCache, deckName);
        deckWaves[(size_t) i]->setOnFileDropped ([this, i] (const juce::File& f) { processorRef.loadDeckFileAsync (i, f); });
        // Empty decks show a real call to action; clicking it opens the file chooser.
        deckWaves[(size_t) i]->setShowDropHint (true);
        deckWaves[(size_t) i]->setOnBrowseRequest ([this, i] { openDeckChooser (i); });
        addAndMakeVisible (*deckWaves[(size_t) i]);

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
                juce::Thread::launch ([safe, file, i, neural]
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
                });
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
                juce::Thread::launch ([safe, file, i]
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
                });
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

    // Universal transport: one button to play / stop every deck at once.
    setupButton (masterPlayButton);
    masterPlayButton.setButtonText (u8 ("\xe2\x96\xb6 PLAY ALL"));
    masterPlayButton.setColour (juce::TextButton::buttonColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.22f));
    masterPlayButton.setTooltip ("Play or stop every deck at once");
    masterPlayButton.onClick = [this]
    {
        bool anyPlaying = false;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            anyPlaying = anyPlaying || processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        const float target = anyPlaying ? 0.0f : 1.0f;   // any playing -> stop all, else start all
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (i) + "Play"))
                p->setValueNotifyingHost (target);
    };
    addAndMakeVisible (masterPlayButton);

    setupButton (automixButton);
    automixButton.setButtonText ("AUTO");
    automixButton.setColour (juce::TextButton::buttonColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.14f));
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
    arrRecordButton.onClick = [this] { toggleRecording(); };
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
    arrLoopButton.setTooltip ("Loop the region (drag the ruler to set it)");
    arrLoopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    arrLoopButton.onClick = [this] { arrangementView.setLoopEnabled (arrLoopButton.getToggleState()); };
    addAndMakeVisible (arrLoopButton);

    setupButton (pianoRollButton);
    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    // ARRANGER nav → the mix-timeline overlay (the new arrangement surface). The
    // per-clip piano roll still opens separately via onEditMidiClip.
    pianoRollButton.onClick = [this]
    {
        const bool show = pianoRollButton.getToggleState();
        if (show) { refreshMixArrangement(); fadeOverlay (mixArrangeView, true); mixArrangeView.toFront (true); }
        else       fadeOverlay (mixArrangeView, false);
    };
    addAndMakeVisible (pianoRollButton);

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
        if (on) { pianoRollButton.setToggleState (false, juce::dontSendNotification); fadeOverlay (pianoRoll, false); fadeOverlay (mixArrangeView, false); }
        fadeOverlay (mixerView, on);
        if (on) mixerView.toFront (true);
    };
    addAndMakeVisible (mixerButton);

    // ── djay-Pro nav bar: LIBRARY / DECKS / MIXER / ARRANGER / PERFORMANCE ──
    // Icon + label are painted in paint(); buttons are naked click targets that
    // only show a subtle highlight when active.
    for (juce::Button* b : { (juce::Button*) &toggleLibraryButton, (juce::Button*) &navDecksButton,
                             (juce::Button*) &mixerButton, (juce::Button*) &pianoRollButton,
                             (juce::Button*) &toggleBrowserButton })
    {
        b->setButtonText ({});
        b->getProperties().set ("navNaked", true);
    }
    setupButton (navDecksButton);
    navDecksButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    navDecksButton.onClick = [this]
    {
        fadeOverlay (pianoRoll, false);
        fadeOverlay (mixArrangeView, false);
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
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
    aiMasterButton.setTooltip ("AI mastering: auto EQ balance + bus dynamics + loudness targeting");
    addAndMakeVisible (aiMasterButton);
    aiMasterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.parameters(), "smartMasterOn", aiMasterButton);

    aiMasterMode.addItem ("Streaming -14", 1);
    aiMasterMode.addItem ("Club -9", 2);
    aiMasterMode.addItem ("Warm -16", 3);
    addAndMakeVisible (aiMasterMode);
    aiMasterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.parameters(), "smartMasterMode", aiMasterMode);

    // ── Rekordbox XML interop: export library + cues / import cue points ────
    importedCues = RekordboxIO::loadCueStore();
    setupButton (rbxExportButton);
    rbxExportButton.setTooltip ("Export the analyzed library + hot cues as rekordbox.xml");
    rbxExportButton.onClick = [this]
    {
        rbxChooser = std::make_unique<juce::FileChooser> ("Export rekordbox XML",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("rekordbox.xml"), "*.xml");
        rbxChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto dest = fc.getResult();
                if (dest == juce::File() || libraryView == nullptr)
                    return;
                const auto entries = libraryView->getAnalyzedEntries();
                const bool ok = RekordboxIO::exportXml (dest, entries,
                    [this] (const juce::String& path, std::vector<RekordboxIO::Cue>& cues)
                    {
                        for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // loaded decks: live cues
                            if (processorRef.getDeckPath (d) == path)
                                for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
                                    if (const double s = processorRef.getHotCue (d, c); s >= 0.0)
                                        cues.push_back ({ c, s });
                        if (cues.empty())                                          // else: previously imported
                            if (const auto found = importedCues.find (path); found != importedCues.end())
                                cues = found->second;
                    });
                recordStatus.setText (ok ? "Exported " + juce::String ((int) entries.size()) + " tracks to " + dest.getFileName()
                                         : "Rekordbox export failed", juce::dontSendNotification);
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
            [this] (const juce::FileChooser& fc)
            {
                const auto src = fc.getResult();
                if (! src.existsAsFile())
                    return;
                int seen = 0;
                auto imported = RekordboxIO::importXml (src, &seen);
                for (auto& [path, cues] : imported)
                    importedCues[path] = std::move (cues);
                RekordboxIO::saveCueStore (importedCues);
                for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // apply to loaded decks now
                    applyImportedCues (d);
                recordStatus.setText ("Imported cues for " + juce::String ((int) imported.size())
                                        + " of " + juce::String (seen) + " tracks",
                                      juce::dontSendNotification);
            });
    };
    addAndMakeVisible (rbxImportButton);

    mixerView.numChannels = MainPluginProcessor::numDecks;
    mixerView.onClose    = [this] { fadeOverlay (mixerView, false); mixerButton.setToggleState (false, juce::dontSendNotification); };
    // These strips control the ARRANGEMENT lanes (gain/mute/solo of the timeline),
    // not the live deck channels — name them honestly.
    mixerView.getName    = [] (int i) { return "LANE " + juce::String::charToString ((juce_wchar) ('A' + i)); };
    mixerView.getColour  = [] (int i) { return deckAccent (i); };
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
    mixerView.getGainDb  = [this] (int i) { return arrangementView.getLaneGainDb (i); };
    mixerView.setGainDb  = [this] (int i, float db) { arrangementView.setLaneGainDb (i, db);
                                                      if (arrangementView.onLaneMixChanged) arrangementView.onLaneMixChanged(); };
    mixerView.isMuted    = [this] (int i) { return arrangementView.isLaneMuted (i); };
    mixerView.isSoloed   = [this] (int i) { return arrangementView.isLaneSoloed (i); };
    mixerView.toggleMute = [this] (int i) { arrangementView.toggleLaneMute (i); };
    mixerView.toggleSolo = [this] (int i) { arrangementView.toggleLaneSolo (i); };
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
    launchAllButton.setTooltip ("Queue all deck lanes to launch at the next beat");
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
        {
            processorRef.clearDeck (lane);
            if (auto* wave = deckWaves[(size_t) lane].get())
                wave->setFile (juce::File());
            recordStatus.setText ("Cleared Deck " + juce::String::charToString ((juce_wchar) ('A' + lane)),
                                  juce::dontSendNotification);
        }
    };
    recordStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    recordStatus.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    recordStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (recordStatus);
    arrangementView.initialise (thumbnailFormatManager, thumbnailCache);
    arrangementView.setIdle ("Hit Record to capture your set across four deck lanes.");
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
            safeThis->grabKeyboardFocus();
    });
}

bool MainPluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            arrangementView.redo();
        else
            arrangementView.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        arrangementView.redo();
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
    addAndMakeVisible (comboBox);
}

void MainPluginEditor::setupDeck (DeckControls& controls, const juce::String& prefix, const juce::String& playText)
{
    controls.play.setButtonText (playText);
    setupButton (controls.load);
    setupButton (controls.loadStem);
    setupButton (controls.aiStems);
    setupButton (controls.play);
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
        auto& slider = controls.stemGains[(size_t) lane];
        setupSlider (slider, true, " dB");
        slider.setRange (-60.0, 12.0);
        // Stem-knob fill tracks the deck's brand accent so each deck reads as a colour.
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setName (DeckPlayer::stemLaneLabel (lane));
        slider.setTooltip (DeckPlayer::stemLaneLabel (lane) + " stem gain");
        // djay-Pro cells: the four separated stems are vertical mini-faders;
        // the full-mix trim stays a knob (relocated to the SRC tab).
        if (lane != DeckPlayer::fullMixStem)
            slider.setSliderStyle (juce::Slider::LinearVertical);
        addAndMakeVisible (slider);
        controls.stemGainAttachments[(size_t) lane] = std::make_unique<SliderAttachment> (
            processorRef.parameters(),
            prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
            slider);
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
        [this, deckIndex, stemLaneIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                processorRef.loadDeckStemFileAsync (deckIndex, stemLaneIndex, file);
                if (auto* wave = deckWaves[(size_t) deckIndex].get())
                {
                    if (stemLaneIndex == DeckPlayer::fullMixStem)
                        wave->setFile (file);
                    else
                        wave->setStemFile (stemLaneIndex, file);
                }
            }
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
        [this, deckIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                processorRef.loadDeckFileAsync (deckIndex, file);
                if (auto* wave = deckWaves[(size_t) deckIndex].get())
                    wave->setFile (file);
            }
        });
}

void MainPluginEditor::openPadChooser (int padIndex)
{
    padChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, padIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                processorRef.loadPadFileAsync (padIndex, file);
        });
}

void MainPluginEditor::toggleRecording()
{
    if (processorRef.isMasterRecording())
    {
        const auto file = processorRef.stopMasterRecording();
        const auto arrangement = processorRef.getLastArrangementFile();
        const auto savedMsg = file.existsAsFile()
                                  ? "Saved: " + file.getFileName() + " + arrangement " + arrangement.getFileName()
                                  : juce::String ("Recording stopped");
        recordStatus.setText (savedMsg, juce::dontSendNotification);
        loadLastArrangementIntoView (savedMsg);
        return;
    }

    // NOTE: do NOT clear the arrangement or disable Reveal/Reload/Bounce yet —
    // cancelling the save dialog must leave the current work untouched. Only wipe
    // once a destination is chosen and recording actually starts.
    recordChooser.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
                return;   // cancelled — arrangement + buttons preserved

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            const bool started = processorRef.beginMasterRecording (file);
            recordStatus.setText (started ? "Recording: " + file.getFileName() : "Could not start recording", juce::dontSendNotification);
            if (started)
            {
                arrangementView.clearArrangement();
                revealRecordingButton.setEnabled (false);
                reloadIntoDecksButton.setEnabled (false);
                bounceEditsButton.setEnabled (false);
                arrangementView.setRecordingProgress (0.0, file.getFullPathName());
            }
        });
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

void MainPluginEditor::saveProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension ("stemdeck");
            juce::ValueTree root ("STEMDECK_PROJECT");
            root.appendChild (arrangementView.toState(), nullptr);
            root.appendChild (pianoRoll.toState(), nullptr);
            if (auto xml = root.createXml())
                xml->writeTo (f);
            recordStatus.setText ("Saved project: " + f.getFileName(), juce::dontSendNotification);
        });
}

void MainPluginEditor::loadProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Open StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
            {
                const auto root = juce::ValueTree::fromXml (*xml);
                arrangementView.fromState (root.getChildWithName ("ARRANGEMENT"));
                pianoRoll.fromState (root.getChildWithName ("PIANOROLL"));
                recordStatus.setText ("Loaded project: " + f.getFileName(), juce::dontSendNotification);
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

        std::unique_ptr<juce::AudioFormatReader> reader (thumbnailFormatManager.createReaderFor (sourceFile));
        if (reader == nullptr)
        {
            errors << laneName (i) << ": read failed. ";
            continue;
        }

        const double sr = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        const int channels = (int) juce::jmax (1u, reader->numChannels);
        const int totalSamples = juce::jmax (1, (int) std::round (laneTimelineEnd * sr));

        // Flattened lane timeline: gaps are silence, each clip placed at its
        // source/timeline position with its fades applied.
        juce::AudioBuffer<float> out (channels, totalSamples);
        out.clear();

        bool ok = true;
        for (const auto& clip : laneClips)
        {
            const double startSec = juce::jlimit (0.0, clip.durationSeconds, clip.trimStartSeconds);
            const double endSec   = juce::jlimit (startSec, clip.durationSeconds,
                                                  clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
            const int regionSamples = (int) std::round ((endSec - startSec) * sr);
            if (regionSamples < 1) continue;

            juce::AudioBuffer<float> region (channels, regionSamples);
            if (! reader->read (&region, 0, regionSamples, (juce::int64) std::round (startSec * sr), true, true))
            {
                ok = false;
                break;
            }

            const double regionLen = endSec - startSec;
            const double fadeIn  = juce::jlimit (0.0, regionLen, clip.fadeInSeconds);
            const double fadeOut = juce::jlimit (0.0, regionLen - fadeIn, clip.fadeOutSeconds);
            const int fadeInSamples  = (int) std::round (fadeIn  * sr);
            const int fadeOutSamples = (int) std::round (fadeOut * sr);
            if (fadeInSamples > 1)
                region.applyGainRamp (0, juce::jmin (fadeInSamples, regionSamples), 0.0f, 1.0f);
            if (fadeOutSamples > 1)
            {
                const int n = juce::jmin (fadeOutSamples, regionSamples);
                region.applyGainRamp (regionSamples - n, n, 1.0f, 0.0f);
            }

            const int destOffset = juce::jlimit (0, totalSamples, (int) std::round (clip.timelineStartSeconds * sr));
            const int copyLen = juce::jmin (regionSamples, totalSamples - destOffset);
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, destOffset, region, ch, 0, copyLen);
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
        arrangementView.setArrangement (masterRec, decks, std::move (statusLine));
        revealRecordingButton.setEnabled (true);
        reloadIntoDecksButton.setEnabled (anyDeck);
        bounceEditsButton.setEnabled (anyDeck);

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

    juce::Thread::launch ([safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]
    {
        if (safeThis == nullptr)
            return;

        auto result = safeThis->processorRef.scanPluginHostDefaultLocations (48, true);
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
    });
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

MainPluginEditor::~MainPluginEditor()
{
    stopTimer();
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
        for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        {
            if (! set[(size_t) d].existsAsFile())
                continue;
            processorRef.loadDeckFileAsync (d, set[(size_t) d]);
            if (auto* wave = deckWaves[(size_t) d].get())
                wave->setFile (set[(size_t) d]);
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
        processorRef.loadDeckFileAsync (target, file);
        if (auto* wave = deckWaves[(size_t) target].get())
            wave->setFile (file);
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
        [this, file] (int choice)
        {
            if (choice <= 0 || choice >= 99) return;
            const int idx = choice - 1;
            processorRef.loadDeckFileAsync (idx, file);
            if (auto* wave = deckWaves[(size_t) idx].get())
                wave->setFile (file);
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
    }
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
    juce::Component::SafePointer<MainPluginEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
        [safe] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == 1) { safe->advancedVisible = ! safe->advancedVisible; safe->advancedToggle.setToggleState (safe->advancedVisible, juce::dontSendNotification); safe->resized(); safe->repaint(); }
            else if (choice == 2) safe->setShowcaseMode (! safe->showcaseMode);
            else if (choice == 3) safe->processorRef.getMidiMapping().resyncFeedback();
        });
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
        namespace sdp = stemdeck::palette;
        const auto r = headerBarBounds.toFloat();
        g.setColour (juce::Colour (sdp::kHairline).withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 1.0f, r.getWidth() - 8.0f, 1.0f));
    }

    // --- Nav bar: icon above label per cell (LIBRARY/DECKS/MIXER/ARRANGER/PERFORMANCE) ---
    {
        static const char* navLabels[5] = { "LIBRARY", "DECKS", "MIXER", "ARRANGER", "FILES" };
        const juce::Button* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                           &pianoRollButton, &toggleBrowserButton };
        for (int i = 0; i < 5; ++i)
        {
            const auto cell = navRects[(size_t) i];
            if (cell.isEmpty()) continue;
            const bool active = navBtns[i]->getToggleState();
            const auto col = active ? deckAccent (0) : juce::Colour (stemdeck::palette::kTextMuted);
            auto icon = cell.withHeight (22).translated (0, 3).toFloat().reduced ((cell.getWidth() - 22) * 0.5f, 0.0f);
            drawNavIcon (g, i, icon, col.withAlpha (active ? 0.95f : 0.7f));
            g.setColour (col.withAlpha (active ? 0.98f : 0.72f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            drawTracked (g, navLabels[i], cell.withTop (cell.getY() + 27).withHeight (12), 1.6f);
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
        namespace sdp = stemdeck::palette;
        const auto r = tempoChipBounds.toFloat();
        g.setColour (juce::Colour (sdp::kPanelDeep));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (deckAccent (0).withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (sdp::kTextMuted));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("HOST TEMPO", juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 2.0f, r.getWidth() - 16.0f, 10.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (stemdeck::palette::kHairline).withAlpha (0.5f));
    g.drawHorizontalLine (title.getBounds().getBottom() + 9, 22.0f, (float) getWidth() - 22.0f);

    // --- Deck panels: flat brand-palette card per deck, accent stripe at the top ---
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckBounds = deckPanelBounds[(size_t) i];
        if (deckBounds.isEmpty())
            continue;
        const bool deckPlaying = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        drawPanel (g, deckBounds, deckAccent (i), 10.0f, deckPlaying);
        g.setColour (deckAccent (i).withAlpha (0.85f));
        g.fillRoundedRectangle (deckBounds.removeFromTop (3).toFloat(), 1.5f);

        // --- Deck header strip: letter badge + filename + key chip ---
        auto headerBar = deckHeaderBounds[(size_t) i];
        if (! headerBar.isEmpty())
        {
            const juce::String letter = juce::String::charToString ((juce_wchar) ('A' + i));
            auto badge = headerBar.removeFromLeft (22);
            deckBadgeBounds[(size_t) i] = badge;   // click to set master deck
            g.setColour (deckAccent (i).withAlpha (0.95f));
            g.fillRoundedRectangle (badge.toFloat(), 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
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
            headerBar.removeFromLeft (8);

            // Right-aligned, interactive BPM + Key chips (drag BPM, click Key).
            const auto bpmValue   = deckControls[(size_t) i].bpm.getValue();
            const auto bpmText    = juce::String (bpmValue, 1);
            const auto detectKey  = deckControls[(size_t) i].keyInfo.getText().upToFirstOccurrenceOf (" ", false, false);
            const int  keyChoice  = (int) std::round (processorRef.parameters().getRawParameterValue (deckPrefix (i) + "KeyTarget")->load());
            const bool detectValid = detectKey.isNotEmpty() && detectKey != "Key:" && detectKey != "unknown";
            const auto keyText     = keyChoice > 0 ? processorRef.getKeyTargetChoices()[keyChoice]
                                                   : (detectValid ? detectKey : u8 ("\xe2\x80\x94"));

            auto drawChip = [&] (juce::Rectangle<int> chip, const juce::String& text)
            {
                g.setColour (juce::Colour (stemdeck::palette::kPanelDeep));
                g.fillRoundedRectangle (chip.toFloat(), 3.0f);
                g.setColour (deckAccent (i).withAlpha (0.7f));   // brighter = interactive
                g.drawRoundedRectangle (chip.toFloat(), 3.0f, 1.0f);
                g.setColour (deckAccent (i).withAlpha (0.95f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (text, chip.reduced (6, 0), juce::Justification::centredRight);
            };

            auto keyChip = headerBar.removeFromRight (juce::jmin (58, headerBar.getWidth() / 3));
            deckKeyChipBounds[(size_t) i] = keyChip;
            drawChip (keyChip, u8 ("\xe2\x99\xa9 ") + keyText);
            headerBar.removeFromRight (4);
            auto bpmChip = headerBar.removeFromRight (juce::jmin (80, headerBar.getWidth() / 3));
            deckBpmChipBounds[(size_t) i] = bpmChip;
            drawChip (bpmChip, bpmText + " BPM");
            headerBar.removeFromRight (6);

            // Filename in the middle slot. A/B/C carry fixed showcase titles to
            // match the mockup; D shows the real file or an empty placeholder.
            static const char* kDemoTitle[4] = {
                "Moneyplagg & MealThePred\xe2\x80\xa6",
                "Life is real controlle way",
                "Moneyplagg & MealThePred\xe2\x80\xa6",
                "" };
            const auto path = processorRef.getDeckPath (i);
            const auto file = juce::File (path).getFileName();
            const bool useDemo = showcaseMode && i < 4 && kDemoTitle[i][0] != '\0';
            const auto display = useDemo ? u8 (kDemoTitle[i])
                                         : (file.isNotEmpty() ? file : u8 ("\xe2\x80\x94 empty deck \xe2\x80\x94"));
            g.setColour (juce::Colour (stemdeck::palette::kTextPrimary).withAlpha ((useDemo || file.isNotEmpty()) ? 0.92f : 0.45f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (display, headerBar.reduced (2, 0), juce::Justification::centredLeft, true);
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
                    g.setColour (juce::Colour (stemdeck::palette::kTextMuted));
                    g.setFont (juce::FontOptions (8.5f));
                    g.drawText (i == 2 ? "Delay" : "Reverb", cell.withTop (cell.getBottom() - 11).translated (0, -1),
                                juce::Justification::centred);
                }
            }

            // Honest separation badge: NEURAL (demucs cache) vs DSP (real-time
            // heuristic), on the VOCALS cell corner.
            {
                const bool neural = processorRef.deckHasNeuralStems (i);
                const bool dsp    = processorRef.isDeckRtssActive (i);
                if (neural || dsp)
                {
                    const auto badgeCol = neural ? juce::Colour (0xff62f5c8) : deckAccent (i);
                    g.setColour (badgeCol.withAlpha (0.9f));
                    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                    g.drawText (neural ? "NEURAL" : "DSP",
                                juce::Rectangle<int> (stemBox.getX() + cellW * 4 - 40,
                                                      stemBox.getY() + 3, 36, 9),
                                juce::Justification::centredRight);
                }
            }
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
            auto r = component.getBounds().withHeight (12).translated (0, -13);
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (deckAccent (i).withAlpha (0.92f));
        drawAbove ("VOL",   controls.volume);
        drawAbove ("XF",    controls.fader);
        drawAbove ("PITCH", controls.pitch);
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
        g.setColour (juce::Colour (stemdeck::palette::kPanelDeep));
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

    // --- MASTER OUTPUT panel: title, stereo segment meters + dB scale ---
    if (! masterOutBounds.isEmpty())
    {
        namespace sdp = stemdeck::palette;
        const auto r = masterOutBounds.toFloat();
        g.setColour (juce::Colour (sdp::kPanel));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.35f).reduced (3.0f, 2.0f), 8.0f);
        g.setColour (juce::Colour (sdp::kHairline).withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (juce::Colour (kAccent));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("MASTER OUTPUT", masterOutBounds.reduced (12, 6).withHeight (14),
                    juce::Justification::centredLeft);

        // GLOBAL TEMPO box: label, value (120.0), and a small green "synced" square.
        if (! masterTempoBounds.isEmpty())
        {
            const auto tb = masterTempoBounds.toFloat();
            g.setColour (juce::Colour (sdp::kPanelDeep));
            g.fillRoundedRectangle (tb, 5.0f);
            g.setColour (juce::Colour (sdp::kHairline));
            g.drawRoundedRectangle (tb.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (juce::Colour (sdp::kTextMuted));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText ("GLOBAL TEMPO", tb.withHeight (11.0f).translated (0.0f, 3.0f), juce::Justification::centred);
            const float bpm = (float) processorRef.parameters().getRawParameterValue ("masterBpm")->load();
            auto valRow = tb.withTrimmedTop (13.0f);
            g.setColour (juce::Colour (sdp::kTextPrimary));
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
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

            // dB scale ticks (matches the mockup's 0 / -6 / -16 / -20 readout).
            g.setColour (juce::Colour (sdp::kTextMuted));
            g.setFont (juce::FontOptions (8.5f));
            for (const float tickDb : { 0.0f, -6.0f, -16.0f, -20.0f })
            {
                const float tn = juce::jmap (tickDb, -40.0f, 0.0f, 0.0f, 1.0f);
                const int y = meterArea.getBottom() - (int) (tn * (float) meterArea.getHeight()) - 4;
                g.drawText (juce::String ((int) tickDb), scaleCol.getX(), y, scaleCol.getWidth(), 8,
                            juce::Justification::centredLeft);
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
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (50);
    headerBarBounds = header;

    // Left cluster: djay-Pro nav — icon-above-label cells (icon + text painted
    // in paint(); the button itself is a naked click target).
    {
        juce::Component* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                        &pianoRollButton, &toggleBrowserButton };
        const int widths[5] = { 66, 58, 56, 66, 92 };
        for (int i = 0; i < 5; ++i)
        {
            auto cell = header.removeFromLeft (widths[i]).withSizeKeepingCentre (widths[i], 46);
            navRects[(size_t) i] = cell;
            navBtns[i]->setBounds (cell);
            header.removeFromLeft (4);
        }
    }
    subtitle.setBounds ({});   // mode line retired from the header (mockup layout)
    header.removeFromLeft (8);

    // Universal transport: play/stop every deck at once (prominent, left of title).
    masterPlayButton.setBounds (header.removeFromLeft (108).withSizeKeepingCentre (108, 34));
    header.removeFromLeft (6);
    automixButton.setBounds (header.removeFromLeft (52).withSizeKeepingCentre (52, 34));
    header.removeFromLeft (6);
    automixStatusLabel.setBounds (header.removeFromLeft (196).withSizeKeepingCentre (196, 30));
    header.removeFromLeft (10);

    // Right cluster: settings gear / TOOLS / save / user presets / tempo chip.
    settingsButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 30));
    header.removeFromRight (6);
    advancedToggle.setVisible (true);
    advancedToggle.setBounds (header.removeFromRight (64).withSizeKeepingCentre (64, 30));
    header.removeFromRight (6);
    savePresetButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, 30));
    header.removeFromRight (8);
    userPresets.setBounds (header.removeFromRight (160).withSizeKeepingCentre (160, 28));
    header.removeFromRight (10);
    tempoChipBounds = header.removeFromRight (150).withSizeKeepingCentre (150, 30);
    tempoLabel.setBounds (tempoChipBounds.reduced (10, 0));
    header.removeFromRight (12);

    // Centre: title fills whatever remains.
    title.setBounds (header);

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
        &metronomeButton, &talkoverButton, &overlapModeButton };

    // FX plugin-host controls: only shown when the hosting path is actually wired
    // (see STEMDECK_ENABLE_PLUGIN_HOST). Kept out of the array above so they never
    // leak into shipping/demo layout even when TOOLS is open.
    juce::Component* pluginHostComps[] = {
        &pluginHostStatus, &pluginChainSelector, &pluginSelector, &loadPluginButton,
        &clearChainButton, &scanPluginsButton, &reloadPluginCacheButton, &pluginHostMessage };
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
        pluginSelector.setBounds (fxRow.removeFromLeft (260));        fxRow.removeFromLeft (4);
        loadPluginButton.setBounds (fxRow.removeFromLeft (66));       fxRow.removeFromLeft (3);
        clearChainButton.setBounds (fxRow.removeFromLeft (66));       fxRow.removeFromLeft (3);
        scanPluginsButton.setBounds (fxRow.removeFromLeft (60));      fxRow.removeFromLeft (3);
        reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (80)); fxRow.removeFromLeft (8);
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
        talkoverButton.setBounds (keyRow.removeFromLeft (54));        keyRow.removeFromLeft (6);
        overlapModeButton.setBounds (keyRow.removeFromLeft (44));
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : advancedComps) { c->setVisible (false); c->setBounds ({}); }
    }

    // "LIVE MIXER / STEM DECKS" section title removed (mockup has no such label).
    liveMixerLabel.setBounds ({});
    liveMixerLabel.setVisible (false);
    area.removeFromTop (2);

    // Deck row gets a fixed band; the arrangement below takes ALL remaining height
    // (so it is large and grows as the window resizes).
    auto decksArea = area.removeFromTop (juce::jmax (378, area.getHeight() * 40 / 100));
    const int deckGap = 10;
    const int deckWidth = (decksArea.getWidth() - deckGap * (MainPluginProcessor::numDecks - 1)) / MainPluginProcessor::numDecks;
    for (int deckIndex = 0; deckIndex < MainPluginProcessor::numDecks; ++deckIndex)
    {
        auto laneOuter = decksArea.removeFromLeft (deckWidth);
        decksArea.removeFromLeft (deckGap);
        deckPanelBounds[(size_t) deckIndex] = laneOuter;
        auto lane = laneOuter.reduced (10, 10);

        auto& controls = deckControls[(size_t) deckIndex];

        // Header strip (badge + filename + chips — painted in paint()).
        auto headerStrip = lane.removeFromTop (22);
        deckHeaderBounds[(size_t) deckIndex] = headerStrip;
        lane.removeFromTop (6);

        // ---- Bottom-up fixed sections; the waveform claims the flexible middle ----

        // Status row (key / stem provider status).
        // Deck footer status text removed for the clean 1:1 view (advanced only).
        auto statusRow = lane.removeFromBottom (advancedVisible ? 13 : 0);
        if (advancedVisible)
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
        lane.removeFromBottom (4);

        // Toolbox content (the active tab's controls render here).
        auto tabContent = lane.removeFromBottom (66);   // fits LOOP (62) + SYNC (64) rows uncropped
        lane.removeFromBottom (3);

        // Toolbox tab strip (CUE / LOOP / FX / SYNC / SRC).
        auto tabStrip = lane.removeFromBottom (20);
        {
            const int tw = tabStrip.getWidth() / DeckControls::NumToolTabs;
            for (int t = 0; t < DeckControls::NumToolTabs; ++t)
                controls.toolTabs[(size_t) t].setBounds (tabStrip.removeFromLeft (tw).reduced (1, 0));
        }
        lane.removeFromBottom (8);

        // Mix band: EQ knobs (left) + VOL / FADER / PITCH vertical faders (right).
        auto mixBand = lane.removeFromBottom (78).withTrimmedTop (14);   // 14px headroom for the painted VOL/XF/PITCH labels
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
        lane.removeFromBottom (6);

        // Transport row — PLAY + CUE + 3 tiny CF-assign buttons on the right
        auto transport = lane.removeFromBottom (28);
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
        lane.removeFromBottom (6);

        // STEM SEPARATION — djay-Pro cells: DRUMS/BASS/MUSIC/VOCALS vertical
        // mini-faders + an FX cell (reverb knob). Painted cell boxes in paint().
        auto stemStrip = lane.removeFromBottom (72);
        deckStemBounds[(size_t) deckIndex] = stemStrip;
        {
            const int cellW = juce::jmax (28, stemStrip.getWidth() / 5);
            auto cells = stemStrip;
            for (int s = 0; s < 4; ++s)   // drums..vocals faders
            {
                auto cell = cells.removeFromLeft (cellW).reduced (3, 2);
                cell.removeFromTop (13);   // painted header
                controls.stemGains[(size_t) (DeckPlayer::drumsStem + s)].setBounds (cell.reduced (2, 0));
            }
            auto fxCell = cells.reduced (3, 2);
            fxCell.removeFromTop (13);
            fxCell.removeFromBottom (10);  // painted "Reverb" caption
            controls.reverbMix.setBounds (fxCell.reduced (2, 0));
        }
        lane.removeFromBottom (6);

        // Waveform — flexible: everything that's left between header and stems.
        if (auto* wave = deckWaves[(size_t) deckIndex].get())
            wave->setBounds (lane);

        // ---- Active toolbox tab content ----
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
                // Deck-wide FX knobs on the left (echo / macro); reverb lives in
                // the stem strip's FX cell now.
                const int knobW = juce::jmin (46, tabContent.getWidth() / 6);
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
    }

    area.removeFromTop (8);

    // ---- Mixer + GLOBAL MASTER EFFECTS + MASTER OUTPUT band ----
    auto mixerStrip = area.removeFromTop (150);
    masterOutBounds = mixerStrip.removeFromRight (juce::jmax (300, mixerStrip.getWidth() * 22 / 100));
    mixerStrip.removeFromRight (10);
    // Frosted-glass XY hero panel (full band height) between mix controls and recorder.
    masterFxPad.setBounds (mixerStrip.removeFromRight (juce::jmax (240, mixerStrip.getWidth() * 24 / 100)).reduced (2, 6));
    mixerStrip.removeFromRight (12);

    // MIXER block: master scope + surfaced power controls + crossfader flanked by
    // per-deck channel meters. Rarely-used utilities stay advanced-only.
    juce::Component* mixExtras[] = { &micGain, &hostSync, &beatSync, &micMonitor,
                                     &camelotButton, &midiLearnButton, &snapToggleButton,
                                     &razorButton, &bounceEditsButton, &undoButton, &redoButton };
    auto mixer = mixerStrip.reduced (2, 5);
    mixerPanel.setBounds (mixer.removeFromLeft (140));
    auto mixControls = mixer.reduced (10, 8);

    // Always-visible power row: BPM SYNC · KEY SYNC · AI MASTER (+mode) · LIMIT.
    {
        for (juce::Component* c : { (juce::Component*) &syncBpmButton, (juce::Component*) &syncKeyButton,
                                    (juce::Component*) &aiMasterButton, (juce::Component*) &aiMasterMode,
                                    (juce::Component*) &limiter })
            c->setVisible (true);
        auto powerRow = mixControls.removeFromTop (26);
        syncBpmButton.setBounds (powerRow.removeFromLeft (78));   powerRow.removeFromLeft (4);
        syncKeyButton.setBounds (powerRow.removeFromLeft (78));   powerRow.removeFromLeft (10);
        aiMasterButton.setBounds (powerRow.removeFromLeft (80));  powerRow.removeFromLeft (4);
        aiMasterMode.setBounds (powerRow.removeFromLeft (110));   powerRow.removeFromLeft (10);
        limiter.setBounds (powerRow);   // LIMIT ceiling — label painted alongside
    }
    mixControls.removeFromTop (6);

    // Crossfader flanked by per-deck meters: A/C feed the left, B/D the right.
    {
        auto xfRow = mixControls;
        auto leftMeters  = xfRow.removeFromLeft (40);
        auto rightMeters = xfRow.removeFromRight (40);
        deckMeterBounds[0] = leftMeters.removeFromTop (leftMeters.getHeight() / 2).reduced (3, 2);   // A
        deckMeterBounds[2] = leftMeters.reduced (3, 2);                                               // C
        deckMeterBounds[1] = rightMeters.removeFromTop (rightMeters.getHeight() / 2).reduced (3, 2);  // B
        deckMeterBounds[3] = rightMeters.reduced (3, 2);                                              // D

        // Curve selector + REV toggle sit on a compact strip above the fader, centred.
        auto ctrlStrip = xfRow.removeFromTop (22);
        auto ctrl = ctrlStrip.withSizeKeepingCentre (188, 22);
        crossfaderCurve.setBounds (ctrl.removeFromLeft (120));
        ctrl.removeFromLeft (8);
        crossfaderReverse.setBounds (ctrl);
        xfRow.removeFromTop (4);

        crossfader.setBounds (xfRow.reduced (8, 0).withSizeKeepingCentre (xfRow.getWidth() - 16, 40));
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
        micGain.setBounds (util.removeFromLeft (84)); util.removeFromLeft (6);
        chip (camelotButton, 44); chip (midiLearnButton, 48); chip (snapToggleButton, 48);
        chip (razorButton, 50); chip (bounceEditsButton, 64); chip (undoButton, 46); chip (redoButton, 46);
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : mixExtras) { c->setVisible (false); c->setBounds ({}); }
    }

    // MASTER OUTPUT panel internals: meters | MASTER GAIN | GLOBAL TEMPO/TAP/KeyLock.
    {
        auto mo = masterOutBounds.reduced (12, 8);
        mo.removeFromTop (16);   // painted "MASTER OUTPUT" title
        masterMeterBounds = mo.removeFromLeft (76).reduced (0, 4);
        mo.removeFromLeft (8);
        master.setBounds (mo.removeFromLeft (66).withTrimmedTop (10));
        mo.removeFromLeft (8);
        auto tempoCol = mo;
        // GLOBAL TEMPO is a painted box (label + 120.0 + green square); the slider
        // itself is hidden but kept for automation/state.
        masterTempoBounds = tempoCol.removeFromTop (42);
        masterBpm.setVisible (false);
        masterBpm.setBounds ({});
        tempoCol.removeFromTop (6);
        auto tapRow = tempoCol.removeFromTop (26);
        tapTempoButton.setBounds (tapRow.removeFromLeft (juce::jmax (44, tapRow.getWidth() / 2 - 3)));
        tapRow.removeFromLeft (6);
        keyLockAllButton.setBounds (tapRow);
        // Reveal / Reload are advanced-only (not in the mockup master panel).
        if (advancedVisible)
        {
            revealRecordingButton.setVisible (true); reloadIntoDecksButton.setVisible (true);
            auto recRow = tempoCol.withTrimmedTop (4).withHeight (20);
            revealRecordingButton.setBounds (recRow.removeFromLeft (juce::jmax (44, recRow.getWidth() / 2 - 3)));
            recRow.removeFromLeft (6);
            reloadIntoDecksButton.setBounds (recRow);
        }
        else
        {
            revealRecordingButton.setVisible (false); revealRecordingButton.setBounds ({});
            reloadIntoDecksButton.setVisible (false); reloadIntoDecksButton.setBounds ({});
        }
        // recordStatus is placed in the pads strip (Recording chip) further down.
    }

    area.removeFromTop (8);
    {
        // djay-Pro transport: label left, centred >>> ▶ ● Loop cluster, zoom right.
        auto labelRow = area.removeFromTop (28);
        arrangementLabel.setBounds (labelRow.removeFromLeft (labelRow.getWidth() * 28 / 100));
        auto zoomBox = labelRow.removeFromRight (128);
        arrZoomFitButton.setBounds (zoomBox.removeFromRight (40).reduced (1, 2));
        zoomBox.removeFromRight (4);
        arrZoomInButton .setBounds (zoomBox.removeFromRight (32).reduced (1, 2));
        arrZoomOutButton.setBounds (zoomBox.removeFromRight (32).reduced (1, 2));

        auto cluster = labelRow.withSizeKeepingCentre (44 * 4 + 6 * 3, 26);
        auto tbtn = [&cluster] (juce::Component& c) { c.setBounds (cluster.removeFromLeft (44)); cluster.removeFromLeft (6); };
        tbtn (launchAllButton);
        tbtn (playArrButton);
        tbtn (arrRecordButton);
        tbtn (arrLoopButton);

        labelRow.removeFromRight (8);
        libraryMetaLabel.setBounds (labelRow);   // MIDI-learn / status feedback
    }
    // Performance pads relocate to a compact full-width strip along the bottom,
    // so the arrangement timeline spans the ENTIRE width above them.
    auto bottom = area;
    auto samplerStrip = bottom.removeFromBottom (64);
    bottom.removeFromBottom (8);
    arrangementView.setBounds (bottom);   // full-width arrangement timeline

    samplerLabel.setBounds (samplerStrip.removeFromLeft (150));
    samplerStrip.removeFromLeft (6);
    // Recording status chip anchors the right end of the pads strip.
    recordStatus.setBounds (samplerStrip.removeFromRight (170).reduced (2, 14));
    samplerStrip.removeFromRight (6);
    const int padGap = 6;
    const int padW = juce::jmax (96,
        (samplerStrip.getWidth() - padGap * (PadBank::numPads - 1)) / PadBank::numPads);
    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto cell = samplerStrip.removeFromLeft (padW);
        if (i < PadBank::numPads - 1)
            samplerStrip.removeFromLeft (padGap);

        auto top = cell.removeFromTop (cell.getHeight() * 58 / 100);
        pads[(size_t) i].trigger.setBounds (top.reduced (1, 1));
        cell.removeFromTop (2);
        pads[(size_t) i].load.setBounds (cell.removeFromLeft (cell.getWidth() * 42 / 100).reduced (1, 0));
        cell.removeFromLeft (3);
        pads[(size_t) i].gain.setBounds (cell.reduced (1, 0));
    }

    // Piano-roll / mixer overlays cover the decks + arrangement when toggled on.
    // Anchor to the actual deck-strip top (not a hardcoded offset) so header/nav
    // stay visible even if the upper sections change height.
    const int overlayTop = deckPanelBounds[0].isEmpty() ? 252 : deckPanelBounds[0].getY();
    const auto overlay = getLocalBounds().withTrimmedTop (overlayTop).reduced (8, 6);
    pianoRoll.setBounds (overlay);
    mixerView.setBounds (overlay);
    mixArrangeView.setBounds (overlay);
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
            t.durationSec = a->audio.getNumSamples() / juce::jmax (1.0, a->sourceSampleRate);
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
    // Service deferred audio-thread requests (e.g. TE transport auto-stop).
    processorRef.serviceDeferredActions();
    updateAutomixRamp();
    if (mixArrangeView.isVisible()) refreshMixArrangement();   // live mix-timeline

    // ── Motion: nav underline slides toward the active view's cell ──────────
    {
        const juce::Button* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                           &pianoRollButton, &toggleBrowserButton };
        int active = -1;
        for (int i = 0; i < 5; ++i)
            if (navBtns[i]->getToggleState()) { active = i; break; }

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
                         navRects[4].getRight() - navRects[0].getX() + 8, 8);
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

    processorRef.refreshLiveKeyAnalysis();
    auto snapshot = processorRef.getTransportSnapshot();
    tempoLabel.setText (juce::String (snapshot.bpm, 1) + (snapshot.hostPlaying ? u8 ("  \xe2\x96\xb6") : u8 ("  \xe2\x97\xbc")),
                        juce::dontSendNotification);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    const bool recording = processorRef.isMasterRecording();
    arrRecordButton.setToggleState (recording, juce::dontSendNotification);

    // Meters live in the MASTER OUTPUT panel — refresh continuously.
    if (! masterOutBounds.isEmpty())
        repaint (masterOutBounds);

    // Feed the live master-output scope + refresh the console channel meters.
    mixerPanel.pushLevel (processorRef.getMasterPeak (0), processorRef.getMasterPeak (1));
    for (const auto& mb : deckMeterBounds)
        if (! mb.isEmpty())
            repaint (mb);

    // DECKS nav chip reads active when no overlay view is open.
    navDecksButton.setToggleState (! pianoRoll.isVisible() && ! mixerView.isVisible()
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
            // Transport button reflects the action: STOP while playing, PLAY while stopped.
            deckControls[(size_t) i].play.setButtonText (playing ? u8 ("\xe2\x96\xa0 STOP")
                                                                  : u8 ("\xe2\x96\xb6 PLAY"));
            // Auto-follow: the last deck to START playing becomes the sync master.
            if (playing)
                setMasterDeck (i);
            lastDeckPlayingUI[(size_t) i] = playing;
            deckPlayChanged = true;
        }
    }
    if (deckPlayChanged)
    {
        // Universal transport label follows the fleet: STOP ALL if anything plays.
        bool anyPlaying = false;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            anyPlaying = anyPlaying || lastDeckPlayingUI[(size_t) i];
        masterPlayButton.setButtonText (anyPlaying ? u8 ("\xe2\x96\xa0 STOP ALL") : u8 ("\xe2\x96\xb6 PLAY ALL"));
        masterPlayButton.setColour (juce::TextButton::buttonColourId,
            juce::Colour (sattari::brand::kAccent).withAlpha (anyPlaying ? 0.45f : 0.22f));
        repaint();   // refresh the deck-card active glow
    }

    undoButton.setEnabled (arrangementView.canUndo());
    redoButton.setEnabled (arrangementView.canRedo());

    // Arrangement transport feedback: moving playhead + button state.
    const bool arrPlaying = processorRef.isArrangementPlaying();
    arrangementView.setPlaybackPosition (processorRef.getArrangementPositionSeconds(), arrPlaying);

    // Apply editable automation curves to their target parameters while playing.
    if (arrPlaying)
    {
        const double arrPos = processorRef.getArrangementPositionSeconds();
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            int target = -1;
            const float v = arrangementView.automationValueAt (lane, arrPos, target);
            if (v < 0.0f || target < 0)
                continue;
            juce::String pid;
            switch (target)
            {
                case 0: pid = deckPrefix (lane) + "Fader";  break;
                case 1: pid = deckPrefix (lane) + "Filter"; break;
                case 2: pid = deckPrefix (lane) + "Pitch";  break;
                case 3: pid = deckPrefix (lane) + "Stem" + DeckPlayer::stemLaneId (DeckPlayer::vocalsStem) + "Send"; break;
                default: break;
            }
            if (auto* p = processorRef.parameters().getParameter (pid))
                p->setValueNotifyingHost (v);   // curve stores the normalized 0..1 value
        }
    }
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
        arrangementView.setIdle ("Hit Record to capture your set across four deck lanes.");
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
        btn.setColour (juce::TextButton::buttonOnColourId, deckAccent (deckIndex).withAlpha (0.55f));
        btn.onClick = [this, deckIndex, t] { setDeckToolTab (deckIndex, t); };
        addAndMakeVisible (btn);
    }
    setDeckToolTab (deckIndex, DeckControls::TabCue);
}

void MainPluginEditor::setDeckToolTab (int deckIndex, int tab)
{
    auto& dc = deckControls[(size_t) deckIndex];
    dc.activeToolTab = tab;
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
        dc.toolTabs[(size_t) t].setToggleState (t == tab, juce::dontSendNotification);
    applyDeckToolTabVisibility (deckIndex);
    resized();
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
    if (wave == nullptr) return;
    std::array<WaveformDisplay::HotCue, WaveformDisplay::numHotCues> cues {};
    for (int c = 0; c < WaveformDisplay::numHotCues; ++c)
    {
        cues[(size_t) c].seconds = processorRef.getHotCue (deckIndex, c);
        cues[(size_t) c].colour  = kHotCueColours[(size_t) c];
    }
    wave->setHotCues (cues);
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
    auto& lane = arrangementView.getClips()[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) lane.size())) return;

    juce::PopupMenu m;
    m.addSectionHeader ("Follow Action");
    m.addItem (1, "None",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::None);
    m.addItem (2, "Next clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Next);
    m.addItem (3, "Prev clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Prev);
    m.addItem (4, "Random other",true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::RandomOther);
    m.addItem (5, "Stop",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Stop);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, laneIndex, clipIndex] (int r)
    {
        if (r < 1) return;
        auto snap = arrangementView.captureLaneState (laneIndex);
        auto& lane2 = const_cast<std::vector<ArrangementRecorderView::LaneClip>&> (arrangementView.getClips()[(size_t) laneIndex]);
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
            arrangementView.commitEdit (snap);
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
    if (wave == nullptr) return;
    const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
    const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
    const bool active = processorRef.isDeckLoopActive (deckIndex);
    if (in >= 0.0 && out > in)
        wave->setLoopRegion (in, out, active);
    else
        wave->clearLoopRegion();
}

// ── Camelot wheel popup ───────────────────────────────────────────────────────

void MainPluginEditor::showCamelotWheel (int /*deckIndex*/)
{
    // 12 keys × 2 (major/minor) shown in a grid popup
    // Columns: C Db D Eb E F F# G Ab A Bb B
    static const char* const kKeys[] = {
        "C","Db","D","Eb","E","F","F#","G","Ab","A","Bb","B"
    };
    static const int kCamelotMaj[] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };   // camelot number
    static const int kCamelotMin[] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };

    // Selecting a key sets the suite-wide Master Key target (harmonic mixing):
    // every deck with key-match enabled transposes toward it.
    const int currentChoice = (int) std::round (
        processorRef.parameters().getRawParameterValue ("masterKey")->load());

    juce::PopupMenu menu;
    menu.addSectionHeader ("Master Key Target (Camelot)");
    menu.addItem (99, "Off - no key matching", true, currentChoice == 0);
    menu.addSeparator();
    for (int k = 0; k < 12; ++k)
    {
        menu.addItem (k + 1,
                      juce::String (kCamelotMaj[k]) + "B  " + kKeys[k] + " maj   |   "
                      + juce::String (kCamelotMin[k]) + "A  " + kKeys[k] + " min",
                      true, currentChoice == k + 1);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (camelotButton),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this)] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (auto* p = safe->processorRef.parameters().getParameter ("masterKey"))
            {
                const int choice = result == 99 ? 0 : result;   // 0 = Off, else pitch class + 1
                p->setValueNotifyingHost (p->convertTo0to1 ((float) choice));
                safe->recordStatus.setText (choice == 0 ? "Master key matching off"
                                                        : "Master key target set",
                                            juce::dontSendNotification);
            }
        });
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
        [this, kCcBase, kNoteBase, kClearAll] (int result)
        {
            if (result <= 0) return;
            auto& m = processorRef.getMidiMapping();

            if (result == kClearAll)
            {
                m.clearAll();
                libraryMetaLabel.setText ("All MIDI mappings cleared", juce::dontSendNotification);
                return;
            }

            if (result >= kNoteBase)
            {
                const int idx = result - kNoteBase;
                m.beginLearnNote (idx);
                libraryMetaLabel.setText ("MIDI learn: press a pad for \"" + m.noteName (idx) + u8 ("\"\xe2\x80\xa6"),
                                          juce::dontSendNotification);
            }
            else
            {
                const int idx = result - kCcBase;
                m.beginLearnCc (idx);
                libraryMetaLabel.setText ("MIDI learn: move a control for \"" + m.ccName (idx) + u8 ("\"\xe2\x80\xa6"),
                                          juce::dontSendNotification);
            }
            midiLearnButton.setButtonText (u8 ("LEARN\xe2\x80\xa6"));
        });
}
