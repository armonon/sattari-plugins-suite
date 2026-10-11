#include "RoyalChainEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>
#include "MacroController.h"

namespace th = rc::theme;

//==============================================================================
RoyalChainView::RoyalChainView (RoyalChainAudioProcessor& p, sattari::presets::SattariPresetManager& pm)
    : processor (p),
      presetManager (pm),
      abRow (pm),
      compassComp (p),
      spineComp (p),
      deckComp (p)
{
    setOpaque (true);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Royal Chain");
    addAndMakeVisible (title);

    presetPrev.setButtonText (th::utf8 ("\xe2\x80\xb9"));
    presetNext.setButtonText (th::utf8 ("\xe2\x80\xba"));
    presetPrev.setTooltip ("Previous factory preset");
    presetNext.setTooltip ("Next factory preset");
    presetPrev.setTitle ("Previous factory preset");
    presetNext.setTitle ("Next factory preset");
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (+1); };
    addAndMakeVisible (presetPrev);
    addAndMakeVisible (presetNext);

    for (int i = 0; i < processor.getNumPrograms(); ++i) presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("FACTORY PRESETS");
    presetBox.setTitle ("Factory presets");
    presetBox.setTooltip ("Factory vocal chains");
    presetBox.onChange = [this] { applyPreset (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    userPresets.setTextWhenNothingSelected ("USER PRESETS");
    userPresets.setTitle ("User presets");
    userPresets.setTooltip ("Your saved Royal Chain presets");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    addAndMakeVisible (userPresets);
    refreshUserPresets();

    savePresetButton.setTooltip ("Save the current settings as a user preset");
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);

    midiLearnButton.setTooltip (th::utf8 ("MIDI Learn\xe2\x80\xa6"));
    midiLearnButton.setTitle ("MIDI Learn");
    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };
    addAndMakeVisible (midiLearnButton);
    original.setTitle ("Audition Original"); original.setTooltip ("Audition the latency-aligned original input");
    levelMatch.setTitle ("Level Match"); levelMatch.setTooltip ("Attenuate processed audio toward the original RMS level; does not boost or measure LUFS");
    addAndMakeVisible (original); addAndMakeVisible (levelMatch);
    originalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters(), "auditionOriginal", original);
    matchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters(), "levelMatch", levelMatch);

    addAndMakeVisible (compassComp);
    addAndMakeVisible (spineComp);
    addAndMakeVisible (deckComp);

    compassComp.onFocusMacro = [this] (th::MacroId id)
    {
        spineComp.setFocusedMacro (id);
        deckComp.setHighlightedMacro (id);
    };
    deckComp.onFocusMacro = [this] (th::MacroId id) { focusMacro (id); };
    deckComp.onHoverEnd = [this] { compassComp.setHighlightedNode (-1); };
    spineComp.onFocusMacro = [this] (th::MacroId id) { focusMacro (id); };

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

RoyalChainView::~RoyalChainView()
{
    stopTimer();
}

void RoyalChainView::focusMacro (th::MacroId id)
{
    spineComp.setFocusedMacro (id);
    deckComp.setHighlightedMacro (id);
    compassComp.setHighlightedNode (th::macro (id).node);
}

void RoyalChainView::poll()
{
    spineComp.refresh();
    deckComp.refresh();
}

void RoyalChainView::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void RoyalChainView::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void RoyalChainView::stepPreset (int delta)
{
    const int n = presetBox.getNumItems();
    if (n <= 0)
        return;
    const int i = presetBox.getSelectedItemIndex();
    const int next = i < 0 ? (delta > 0 ? 0 : n - 1) : ((i + delta) % n + n) % n;
    presetBox.setSelectedItemIndex (next, juce::sendNotification);
}

void RoyalChainView::applyPreset (int presetIndex)
{
    processor.setCurrentProgram (presetIndex);
}

void RoyalChainView::paint (juce::Graphics& g)
{
    g.fillAll (th::col (th::canvas));
    g.setColour (th::col (th::mint).withAlpha (0.6f));
    g.fillRect (16, 0, 1068, 2);
    g.setColour (th::col (th::hairline));
    g.fillRect (16, 60, 1068, 1);
}

void RoyalChainView::resized()
{
    title.setBounds (20, 12, 300, 36);
    presetPrev.setBounds (336, 15, 30, 30);
    presetBox.setBounds (370, 15, 220, 30);
    presetNext.setBounds (594, 15, 30, 30);
    userPresets.setBounds (640, 15, 170, 30);
    savePresetButton.setBounds (816, 15, 64, 30);
    abRow.setBounds (888, 15, 94, 30);
    midiLearnButton.setBounds (990, 15, 90, 30);

    compassComp.setBounds (16, 68, VocalCrownCompass::kWidth, VocalCrownCompass::kHeight);
    spineComp.setBounds (772, 68, 312, 508);
    original.setBounds (772, 588, 130, 28); levelMatch.setBounds (910, 588, 160, 28);
    deckComp.setBounds (16, 628, 1068, 156);
}

void RoyalChainView::appendCaptionChecks (std::vector<th::CaptionCheck>& out) const
{
    compassComp.appendCaptionChecks (out);
    spineComp.appendCaptionChecks (out);
    deckComp.appendCaptionChecks (out);
}

//==============================================================================
RoyalChainAudioProcessorEditor::RoyalChainAudioProcessorEditor (RoyalChainAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (processor.parameters(), "royal-chain"),
      view (processor, presetManager)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    setResizable (true, true);
    setResizeLimits (935, 680, 1540, 1120);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) RoyalChainView::kWidth / (double) RoyalChainView::kHeight);

    bool compact = false;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        compact = d->userArea.getHeight() < 860;
    if (compact)
        setSize (935, 680);
    else
        setSize (RoyalChainView::kWidth, RoyalChainView::kHeight);
}

RoyalChainAudioProcessorEditor::~RoyalChainAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RoyalChainAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (th::col (th::canvas));
}

void RoyalChainAudioProcessorEditor::resized()
{
    view.setBounds (0, 0, RoyalChainView::kWidth, RoyalChainView::kHeight);
    view.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) RoyalChainView::kWidth));
}
