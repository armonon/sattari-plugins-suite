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

    presetBox.addItemList (juce::StringArray {
        "Industry Lead", "Hard Tune Hook", "Silk Lead", "Radio Lead", "Persian Pop Lead",
        "Podcast Clean", "Demo Vocal Fix", "Noisy Room Rescue", "Thin Vocal Repair", "Harsh Mic Smooth", "Phone Recording Polish" }, 1);
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
    struct Preset
    {
        int style;
        float input, output, clean, tune, control, tone, air, smooth, glow, width, space, royal;
    };

    static constexpr Preset presets[] = {
        { 0, 0.0f, -0.5f, 0.58f, 0.00f, 0.64f, 0.52f, 0.55f, 0.44f, 0.38f, 0.28f, 0.22f, 0.60f },
        { 7, 0.0f, -1.0f, 0.45f, 0.35f, 0.70f, 0.45f, 0.60f, 0.38f, 0.44f, 0.34f, 0.20f, 0.72f },
        { 1, 0.0f, -0.5f, 0.62f, 0.00f, 0.52f, 0.58f, 0.48f, 0.52f, 0.30f, 0.20f, 0.28f, 0.52f },
        { 2, 0.0f, -0.8f, 0.58f, 0.00f, 0.66f, 0.50f, 0.70f, 0.48f, 0.36f, 0.24f, 0.22f, 0.64f },
        { 3, 0.0f, -0.7f, 0.55f, 0.00f, 0.58f, 0.54f, 0.56f, 0.50f, 0.34f, 0.20f, 0.30f, 0.56f },
        { 5, 0.0f, -1.5f, 0.72f, 0.00f, 0.42f, 0.44f, 0.30f, 0.60f, 0.10f, 0.00f, 0.06f, 0.30f },
        { 8, 1.5f, -1.0f, 0.78f, 0.00f, 0.60f, 0.38f, 0.48f, 0.56f, 0.22f, 0.12f, 0.10f, 0.46f },
        { 8, 2.0f, -1.5f, 0.90f, 0.00f, 0.50f, 0.40f, 0.36f, 0.62f, 0.12f, 0.06f, 0.04f, 0.36f },
        { 8, 1.0f, -0.8f, 0.52f, 0.00f, 0.48f, 0.72f, 0.62f, 0.38f, 0.28f, 0.16f, 0.12f, 0.42f },
        { 8, 0.0f, -1.2f, 0.64f, 0.00f, 0.52f, 0.36f, 0.28f, 0.82f, 0.08f, 0.08f, 0.08f, 0.34f },
        { 8, 2.5f, -1.8f, 0.76f, 0.00f, 0.55f, 0.42f, 0.40f, 0.70f, 0.18f, 0.10f, 0.05f, 0.40f }
    };

    if (! juce::isPositiveAndBelow (presetIndex, (int) std::size (presets)))
        return;

    const auto& pr = presets[presetIndex];
    auto& state = processor.parameters();
    MacroController::setParameterValue (state, "style", (float) pr.style);
    MacroController::setParameterValue (state, "input", pr.input);
    MacroController::setParameterValue (state, "output", pr.output);
    MacroController::setParameterValue (state, "clean", pr.clean);
    MacroController::setParameterValue (state, "tune", pr.tune);
    MacroController::setParameterValue (state, "control", pr.control);
    MacroController::setParameterValue (state, "tone", pr.tone);
    MacroController::setParameterValue (state, "air", pr.air);
    MacroController::setParameterValue (state, "smooth", pr.smooth);
    MacroController::setParameterValue (state, "glow", pr.glow);
    MacroController::setParameterValue (state, "width", pr.width);
    MacroController::setParameterValue (state, "space", pr.space);
    MacroController::setParameterValue (state, "royal", pr.royal);
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
    deckComp.setBounds (16, 588, 1068, 156);
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
    setResizeLimits (935, 646, 1540, 1064);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) RoyalChainView::kWidth / (double) RoyalChainView::kHeight);

    bool compact = false;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        compact = d->userArea.getHeight() < 860;
    if (compact)
        setSize (935, 646);
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
