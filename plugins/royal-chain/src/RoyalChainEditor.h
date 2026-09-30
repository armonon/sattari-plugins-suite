#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "RoyalChainProcessor.h"
#include "RoyalChainLookAndFeel.h"
#include "RoyalChainPanels.h"
#include "VocalCrownCompass.h"

#include <vector>

// =============================================================================
//  RoyalChainView — every control at a fixed logical rect on a 1100 x 760 canvas.
//  The editor scales it with one AffineTransform, so the layout never reflows
//  and the Compass keeps its 1:1 field blit at the default size.
// =============================================================================
class RoyalChainView final : public juce::Component,
                             private juce::Timer
{
public:
    RoyalChainView (RoyalChainAudioProcessor&, sattari::presets::SattariPresetManager&);
    ~RoyalChainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void poll();
    VocalCrownCompass& compass() noexcept { return compassComp; }
    RoyalSpine& spine() noexcept { return spineComp; }
    ModuleDeck& deck() noexcept { return deckComp; }
    void appendCaptionChecks (std::vector<rc::theme::CaptionCheck>&) const;

    static constexpr int kWidth = 1100, kHeight = 760;

private:
    void timerCallback() override { poll(); }
    void focusMacro (rc::theme::MacroId);
    void applyPreset (int presetIndex);
    void stepPreset (int delta);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();

    RoyalChainAudioProcessor& processor;
    sattari::presets::SattariPresetManager& presetManager;

    juce::Label title;
    juce::TextButton presetPrev, presetNext;
    juce::ComboBox presetBox, userPresets;
    juce::TextButton savePresetButton { "SAVE" };
    sattari::ui::SattariABRow abRow;
    juce::TextButton midiLearnButton { "MIDI" };

    VocalCrownCompass compassComp;
    RoyalSpine spineComp;
    ModuleDeck deckComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainView)
};

class RoyalChainAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit RoyalChainAudioProcessorEditor (RoyalChainAudioProcessor&);
    ~RoyalChainAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    RoyalChainView& viewForTesting() noexcept { return view; }

private:
    // Deliberately named like AudioProcessorEditor::processor (typed access).
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wshadow-field")
    RoyalChainAudioProcessor& processor;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE

    RoyalChainLookAndFeel lookAndFeel;
    sattari::presets::SattariPresetManager presetManager;
    juce::TooltipWindow tooltips { this, 700 };
    RoyalChainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalChainAudioProcessorEditor)
};
