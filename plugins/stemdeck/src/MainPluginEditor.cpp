#include "MainPluginEditor.h"

namespace
{
juce::Colour deckAccent (int deckIndex)
{
    static constexpr std::array<juce::uint32, 4> colours { 0xff26d9ff, 0xffff4c5f, 0xff50f06b, 0xffffcf4a };
    return juce::Colour (colours[(size_t) juce::jlimit (0, 3, deckIndex)]);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent, float corner = 12.0f)
{
    const auto r = bounds.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff080a10), r.getTopLeft(),
                                             juce::Colour (0xff151927), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.0f), corner, 1.0f);
    g.setColour (accent.withAlpha (0.36f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.1f);
}
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

void MainPluginEditor::ArrangementRecorderView::setRecordingState (bool isRecording, juce::String statusText)
{
    recording = isRecording;
    status = std::move (statusText);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff080a12));
    g.fillRoundedRectangle (bounds, 18.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff151a29), bounds.getTopLeft(),
                                             juce::Colour (0xff070911), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds.reduced (1.0f), 17.0f);

    const auto accent = recording ? juce::Colour (0xffff4c6a) : juce::Colour (0xff67f6d2);
    g.setColour (accent.withAlpha (0.75f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 18.0f, 1.5f);

    auto area = getLocalBounds().reduced (18, 14);
    auto header = area.removeFromTop (30);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.94f));
    g.drawText ("Pattern Arrangement", header.removeFromLeft (260), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (accent.withAlpha (0.95f));
    g.drawText (recording ? "● RECORDING MIX" : "○ READY TO CAPTURE MIX", header, juce::Justification::centredRight);

    area.removeFromTop (10);
    const int rulerHeight = 24;
    auto ruler = area.removeFromTop (rulerHeight);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (ruler.getBottom(), (float) ruler.getX(), (float) ruler.getRight());
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    for (int bar = 0; bar <= 8; ++bar)
    {
        const float x = (float) ruler.getX() + (float) bar * (float) ruler.getWidth() / 8.0f;
        g.setColour (juce::Colours::white.withAlpha (bar % 2 == 0 ? 0.34f : 0.18f));
        g.drawVerticalLine ((int) std::round (x), (float) ruler.getY(), (float) getHeight() - 18.0f);
        if (bar < 8)
            g.drawText (juce::String (bar + 1), (int) x + 6, ruler.getY(), 64, rulerHeight - 2, juce::Justification::centredLeft);
    }

    const juce::StringArray lanes { "Track 1", "Track 2", "Track 3", "Track 4", "Stem Moves", "FX + Pads" };
    const std::array<juce::Colour, 6> laneColours { juce::Colour (0xff26d9ff), juce::Colour (0xffff4c5f), juce::Colour (0xff50f06b),
                                                    juce::Colour (0xffffcf4a), juce::Colour (0xffc77dff), juce::Colour (0xff67f6d2) };
    const int laneGap = 6;
    const int laneHeight = juce::jmax (18, (area.getHeight() - laneGap * (lanes.size() - 1)) / lanes.size());

    for (int i = 0; i < lanes.size(); ++i)
    {
        auto lane = area.removeFromTop (laneHeight);
        area.removeFromTop (laneGap);
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        g.fillRoundedRectangle (lane.toFloat(), 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawRoundedRectangle (lane.toFloat(), 7.0f, 1.0f);

        auto label = lane.removeFromLeft (112);
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (lanes[i], label.reduced (4, 0), juce::Justification::centredLeft);

        auto track = lane.reduced (6, 5);
        const int clipCount = i == 0 ? 4 : 3;
        for (int clip = 0; clip < clipCount; ++clip)
        {
            const int x = track.getX() + 14 + clip * juce::jmax (82, track.getWidth() / 4);
            const int width = juce::jlimit (48, 180, track.getWidth() / (clip == 0 ? 3 : 4));
            auto clipRect = juce::Rectangle<int> (x, track.getY(), width, track.getHeight()).getIntersection (track);
            if (clipRect.getWidth() <= 20)
                continue;

            const auto laneColour = laneColours[(size_t) i];
            const bool hotClip = (i == 1 && clip == 1);
            g.setColour ((hotClip ? juce::Colour (0xffff2f4f) : laneColour).withAlpha (recording ? 0.48f : 0.28f));
            g.fillRoundedRectangle (clipRect.toFloat(), 6.0f);
            g.setColour (laneColour.withAlpha (0.62f));
            g.drawRoundedRectangle (clipRect.toFloat(), 6.0f, 1.0f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.setColour (juce::Colours::white.withAlpha (0.78f));
            g.drawText ("Pattern 1", clipRect.reduced (8, 0), juce::Justification::centredLeft);
        }
    }

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::white.withAlpha (0.64f));
    g.drawText (status, getLocalBounds().reduced (18, 10).removeFromBottom (18), juce::Justification::centredLeft);
}

MainPluginEditor::MainPluginEditor (MainPluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      presetManager (p.parameters(), "stemdeck")
{
    setLookAndFeel (&lookAndFeel);
    thumbnailFormatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    thumbnailFormatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif

    setSize (1500, 940);
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

    title.setText ("StemDeck PRO", juce::dontSendNotification);
    title.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText ("FOUR DECKS  •  STEM MIXER  •  ARRANGEMENT RECORDER", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    subtitle.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    standaloneWorkflowGuide.setText ("STANDALONE APP ALPHA QUICK START  Load or drag a full mix onto Deck A/B/C/D  •  AI Split after local Demucs setup  •  Mix stems/faders/FX/pads  •  Record WAV to capture the performed arrangement  •  Not notarized or sale-ready", juce::dontSendNotification);
    standaloneWorkflowGuide.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2).withAlpha (0.94f));
    standaloneWorkflowGuide.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    standaloneWorkflowGuide.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (standaloneWorkflowGuide);

    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    pluginHostStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.82f));
    pluginHostStatus.setFont (juce::FontOptions (11.0f));
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
    pluginHostMessage.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.88f));
    pluginHostMessage.setFont (juce::FontOptions (11.0f));
    pluginHostMessage.setJustificationType (juce::Justification::centredLeft);
    pluginHostMessage.setText ("FX host cache: " + processorRef.getPluginHostCacheFile().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (scanPluginsButton);
    addAndMakeVisible (reloadPluginCacheButton);
    addAndMakeVisible (loadPluginButton);
    addAndMakeVisible (clearChainButton);
    addAndMakeVisible (pluginHostMessage);

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
        addAndMakeVisible (*deckWaves[(size_t) i]);

        setupDeck (deckControls[(size_t) i], deckPrefix (i), juce::String::charToString ((juce_wchar) ('A' + i)) + " Play");
        deckControls[(size_t) i].load.onClick = [this, i] { openDeckChooser (i); };
        deckControls[(size_t) i].loadStem.onClick = [this, i]
        {
            const int selectedLane = deckControls[(size_t) i].stemLane.getSelectedId() - 1;
            openDeckStemChooser (i, selectedLane < 0 ? DeckPlayer::fullMixStem : selectedLane);
        };
        deckControls[(size_t) i].aiStems.onClick = [this, i] { processorRef.requestDeckAiStemsAsync (i); };
        deckControls[(size_t) i].cue.onClick = [this, i] { processorRef.rewindDeck (i); };
    }

    addAndMakeVisible (mixerPanel);
    setupSlider (crossfader, false);
    setupSlider (masterBpm, false, " BPM");
    setupSlider (master, true, " dB");
    setupSlider (limiter, true, " dB");
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
    setupButton (recordButton);
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);
    recordStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    recordStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (recordStatus);
    addAndMakeVisible (arrangementView);

    crossfaderAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "crossfader", crossfader);
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
        pad.trigger.setButtonText ("PAD " + juce::String (i + 1) + " [" + padKeyLabel (i) + "]");
        setupButton (pad.trigger);
        setupButton (pad.load);
        setupSlider (pad.gain, false, " dB");
        pad.trigger.onClick = [this, i] { processorRef.triggerPad (i); };
        pad.load.onClick = [this, i] { openPadChooser (i); };
        pad.gainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "pad" + juce::String (i + 1) + "Gain", pad.gain);
        addAndMakeVisible (pad.trigger);
        addAndMakeVisible (pad.load);
        addAndMakeVisible (pad.gain);
    }

    startTimerHz (8);

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]
    {
        if (safeThis != nullptr)
            safeThis->grabKeyboardFocus();
    });
}

bool MainPluginEditor::keyPressed (const juce::KeyPress& key)
{
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
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff121621));
    button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0f5664));
    button.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainPluginEditor::setupSlider (juce::Slider& slider, bool rotary, const juce::String& suffix)
{
    slider.setWantsKeyboardFocus (false);
    slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff67f6d2));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.12f));
    slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff67f6d2));
    slider.setColour (juce::Slider::backgroundColourId, juce::Colours::white.withAlpha (0.08f));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void MainPluginEditor::setupComboBox (juce::ComboBox& comboBox)
{
    comboBox.setWantsKeyboardFocus (false);
    comboBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff10141f));
    comboBox.setColour (juce::ComboBox::textColourId, juce::Colours::white.withAlpha (0.9f));
    comboBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff67f6d2).withAlpha (0.35f));
    comboBox.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff67f6d2));
    comboBox.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff101522));
    comboBox.setColour (juce::PopupMenu::textColourId, juce::Colours::white);
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
    controls.keyInfo.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.9f));
    controls.keyInfo.setJustificationType (juce::Justification::centredLeft);
    controls.keyInfo.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (controls.keyInfo);
    controls.stemStatus.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2).withAlpha (0.92f));
    controls.stemStatus.setJustificationType (juce::Justification::centredLeft);
    controls.stemStatus.setFont (juce::FontOptions (11.0f, juce::Font::bold));
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
                if (stemLaneIndex == DeckPlayer::fullMixStem)
                    if (auto* wave = deckWaves[(size_t) deckIndex].get())
                        wave->setFile (file);
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
        recordStatus.setText (file.existsAsFile()
                                  ? "Saved: " + file.getFileName() + " + arrangement " + arrangement.getFileName()
                                  : "Recording stopped",
                              juce::dontSendNotification);
        return;
    }

    recordChooser.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
                return;

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            const bool started = processorRef.beginMasterRecording (file);
            recordStatus.setText (started ? "Recording: " + file.getFileName() : "Could not start recording", juce::dontSendNotification);
        });
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
    pluginHostMessage.setText ("Scanning FX defaults…", juce::dontSendNotification);
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
                                 + " • known FX: " + juce::String (result.plugins.size());
            if (! result.blacklistedFiles.isEmpty())
                message += " • blacklisted: " + juce::String (result.blacklistedFiles.size());
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
    pluginHostMessage.setText ("Loading " + pluginSelector.getText() + " → " + pluginChainSelector.getText(), juce::dontSendNotification);
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
    setLookAndFeel (nullptr);
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

void MainPluginEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    g.setColour (juce::Colours::white.withAlpha (0.075f));
    g.drawHorizontalLine (title.getBounds().getBottom() + 7, 22.0f, (float) getWidth() - 22.0f);

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto deckBounds = deckWaves[(size_t) i] != nullptr ? deckWaves[(size_t) i]->getBounds().expanded (10, 12) : juce::Rectangle<int>{};
        deckBounds = deckBounds.getUnion (controls.pitch.getBounds().expanded (8, 12));
        if (! deckBounds.isEmpty())
        {
            drawPanel (g, deckBounds, deckAccent (i), 14.0f);
            g.setColour (deckAccent (i).withAlpha (0.72f));
            g.fillRoundedRectangle (deckBounds.removeFromTop (4).toFloat(), 2.0f);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.62f));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto drawControlLabel = [&g] (const juce::String& text, const juce::Component& component)
        {
            auto r = component.getBounds().withHeight (14).translated (0, -14);
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (deckAccent (i).withAlpha (0.92f));
        drawControlLabel ("GAIN", controls.volume);
        drawControlLabel ("BPM", controls.bpm);
        drawControlLabel ("GRID", controls.beatOffset);
        drawControlLabel ("DOWN", controls.downbeat);
        drawControlLabel ("FADER", controls.fader);
        drawControlLabel ("PITCH", controls.pitch);
        drawControlLabel ("LOW", controls.low);
        drawControlLabel ("MID", controls.mid);
        drawControlLabel ("HIGH", controls.high);
        drawControlLabel ("FILTER", controls.filter);
    }

    if (! crossfader.getBounds().isEmpty())
    {
        drawPanel (g, mixerPanel.getBounds().getUnion (recordStatus.getBounds()).expanded (10, 8), juce::Colour (0xff67f6d2), 14.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.68f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("Crossfader groups: A/C  ↔  B/D", crossfader.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Master BPM", masterBpm.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Master", master.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Limit", limiter.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Mic gain", micGain.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (48);
    subtitle.setBounds (header.removeFromLeft (420));
    title.setBounds (header.removeFromLeft (480));
    savePresetButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    userPresets.setBounds (header.removeFromRight (180));
    header.removeFromRight (12);
    tempoLabel.setBounds (header);

    standaloneWorkflowGuide.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);
    pluginHostStatus.setBounds (area.removeFromTop (16));
    area.removeFromTop (4);
    auto fxRow = area.removeFromTop (28);
    pluginChainSelector.setBounds (fxRow.removeFromLeft (148));
    fxRow.removeFromLeft (6);
    pluginSelector.setBounds (fxRow.removeFromLeft (300));
    fxRow.removeFromLeft (6);
    scanPluginsButton.setBounds (fxRow.removeFromLeft (76));
    fxRow.removeFromLeft (6);
    reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (100));
    fxRow.removeFromLeft (6);
    loadPluginButton.setBounds (fxRow.removeFromLeft (76));
    fxRow.removeFromLeft (6);
    clearChainButton.setBounds (fxRow.removeFromLeft (80));
    fxRow.removeFromLeft (8);
    pluginHostMessage.setBounds (fxRow);
    area.removeFromTop (8);

    liveMixerLabel.setBounds (area.removeFromTop (22));
    auto topMixer = area.removeFromTop (518);
    topMixer = topMixer.reduced (0, 4);

    auto decksArea = topMixer.removeFromTop (394);
    const int deckGap = 10;
    const int deckWidth = (decksArea.getWidth() - deckGap * (MainPluginProcessor::numDecks - 1)) / MainPluginProcessor::numDecks;
    for (int deckIndex = 0; deckIndex < MainPluginProcessor::numDecks; ++deckIndex)
    {
        auto lane = decksArea.removeFromLeft (deckWidth);
        decksArea.removeFromLeft (deckGap);
        lane = lane.reduced (10, 12);

        auto& controls = deckControls[(size_t) deckIndex];
        if (auto* wave = deckWaves[(size_t) deckIndex].get())
            wave->setBounds (lane.removeFromTop (166));

        lane.removeFromTop (8);
        auto transport = lane.removeFromTop (30);
        controls.load.setBounds (transport.removeFromLeft (48));
        transport.removeFromLeft (4);
        controls.aiStems.setBounds (transport.removeFromLeft (68));
        transport.removeFromLeft (5);
        controls.play.setBounds (transport.removeFromLeft (64));
        transport.removeFromLeft (5);
        controls.cue.setBounds (transport.removeFromLeft (42));
        transport.removeFromLeft (5);
        controls.liveKey.setBounds (transport.removeFromLeft (64));

        lane.removeFromTop (8);
        auto stemRow = lane.removeFromTop (24);
        controls.stemLane.setBounds (stemRow.removeFromLeft (86));
        stemRow.removeFromLeft (5);
        controls.loadStem.setBounds (stemRow.removeFromLeft (52));
        stemRow.removeFromLeft (5);
        controls.keyTarget.setBounds (stemRow.removeFromLeft (76));
        stemRow.removeFromLeft (5);
        controls.keyLock.setBounds (stemRow);

        lane.removeFromTop (7);
        auto syncRow = lane.removeFromTop (24);
        controls.tempoInterpretation.setBounds (syncRow.removeFromLeft (112));
        syncRow.removeFromLeft (5);
        controls.syncMode.setBounds (syncRow.removeFromLeft (116));

        lane.removeFromTop (5);
        controls.keyInfo.setBounds (lane.removeFromTop (18));
        controls.stemStatus.setBounds (lane.removeFromTop (18));

        lane.removeFromTop (10);
        auto faders = lane.removeFromTop (92);
        const int faderW = 42;
        controls.volume.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        controls.fader.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        controls.pitch.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        faders.removeFromLeft (5);
        controls.low.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.mid.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.high.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.filter.setBounds (faders.removeFromLeft (44).reduced (1));

        lane.removeFromTop (6);
        auto gridRow = lane.removeFromTop (44);
        controls.bpm.setBounds (gridRow.removeFromLeft (96).reduced (2));
        gridRow.removeFromLeft (6);
        controls.beatOffset.setBounds (gridRow.removeFromLeft (88).reduced (2));
        gridRow.removeFromLeft (6);
        controls.downbeat.setBounds (gridRow.reduced (2));
    }

    auto mixer = topMixer.reduced (0, 5);
    mixerPanel.setBounds (mixer.removeFromLeft (150));
    auto mixControls = mixer.reduced (12, 12);
    crossfader.setBounds (mixControls.removeFromLeft (250));
    mixControls.removeFromLeft (18);
    masterBpm.setBounds (mixControls.removeFromLeft (116));
    master.setBounds (mixControls.removeFromLeft (84));
    limiter.setBounds (mixControls.removeFromLeft (84));
    hostSync.setBounds (mixControls.removeFromLeft (112).withTrimmedTop (24));
    beatSync.setBounds (mixControls.removeFromLeft (112).withTrimmedTop (24));
    micMonitor.setBounds (mixControls.removeFromLeft (120).withTrimmedTop (24));
    micGain.setBounds (mixControls.removeFromLeft (110));
    recordButton.setBounds (mixControls.removeFromLeft (118).withTrimmedTop (20).withHeight (32));
    recordStatus.setBounds (mixControls.withTrimmedTop (20).withHeight (32));

    area.removeFromTop (8);
    arrangementLabel.setBounds (area.removeFromTop (22));
    auto bottom = area;
    auto samplerStrip = bottom.removeFromRight (270);
    bottom.removeFromRight (14);
    arrangementView.setBounds (bottom);

    samplerLabel.setBounds (samplerStrip.removeFromTop (22));
    const int padH = juce::jmax (58, samplerStrip.getHeight() / 8);
    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto cell = samplerStrip.removeFromTop (padH).reduced (4);
        pads[(size_t) i].trigger.setBounds (cell.removeFromLeft (92));
        cell.removeFromLeft (5);
        pads[(size_t) i].load.setBounds (cell.removeFromLeft (48));
        cell.removeFromLeft (5);
        pads[(size_t) i].gain.setBounds (cell);
    }
}

void MainPluginEditor::timerCallback()
{
    processorRef.refreshLiveKeyAnalysis();
    auto snapshot = processorRef.getTransportSnapshot();
    tempoLabel.setText (juce::String (snapshot.bpm, 1) + " BPM • " + (snapshot.hostPlaying ? "host playing" : "host stopped"), juce::dontSendNotification);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    const bool recording = processorRef.isMasterRecording();
    recordButton.setButtonText (recording ? "Stop Rec" : "Record WAV");
    arrangementView.setRecordingState (recording,
        recording ? "Recording master output plus performance moves into arrangement lanes"
                  : "Open space ready: hit Record to capture deck moves, stems, FX, pads, mic, and master mix");

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        deckControls[(size_t) i].keyInfo.setText (processorRef.getDeckKeyInfo (i) + " • " + processorRef.getDeckTimePitchStatus (i) + " • " + processorRef.getDeckBeatSyncStatus (i),
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
    }
}
