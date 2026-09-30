#pragma once

// =============================================================================
//  Royal Chain — spine (style, key, readout, ROYAL, meters) and module deck
// -----------------------------------------------------------------------------
//  Both panels are opaque, painted once per change, and refreshed by the view's
//  30 Hz timer; they repaint only the rects whose content changed. Every one of
//  the 12 macros is a real juce::Slider (full slider accessibility), styled as a
//  Portal knob through the "rc.accent" property.
// =============================================================================

#include <JuceHeader.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "RoyalChainTheme.h"

#include <array>
#include <functional>
#include <vector>

class RoyalChainAudioProcessor;

namespace rc
{
// Formats a macro / trim value the same way everywhere (knob box, readout).
juce::String formatMacroValue (const theme::MacroSpec&, double plainValue, bool withUnit);
void configurePortalSlider (juce::Slider&, const theme::MacroSpec&, juce::AudioProcessorValueTreeState&);
} // namespace rc

//==============================================================================
class ModuleDeck final : public juce::Component
{
public:
    explicit ModuleDeck (RoyalChainAudioProcessor&);
    ~ModuleDeck() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void refresh();
    void setHighlightedMacro (rc::theme::MacroId);
    std::function<void (rc::theme::MacroId)> onFocusMacro;
    std::function<void()> onHoverEnd;

    juce::Slider& knob (rc::theme::MacroId id) noexcept { return knobs[(size_t) id]; }
    void appendCaptionChecks (std::vector<rc::theme::CaptionCheck>&) const;

    static constexpr int kUnits = 11;
    static int unitX (int unit) noexcept;

private:
    struct ChipSpec { const char* name; int unit0, unit1; std::uint32_t colour; int node; };
    static const ChipSpec kChips[10];

    juce::Rectangle<int> chipRect (int chip) const noexcept;
    float chipActivity (int chip) const noexcept;
    bool chipDimmed (int chip) const noexcept;

    RoyalChainAudioProcessor& processor;
    std::array<juce::Slider, kUnits> knobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::array<float, 10> led {}, ledShown {};
    std::array<bool, 10> dimShown {};
    int highlighted = (int) rc::theme::MacroId::Tune;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleDeck)
};

//==============================================================================
class RoyalSpine final : public juce::Component
{
public:
    explicit RoyalSpine (RoyalChainAudioProcessor&);
    ~RoyalSpine() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;

    void refresh();
    void setFocusedMacro (rc::theme::MacroId);
    rc::theme::MacroId focusedMacro() const noexcept { return focused; }
    std::function<void (rc::theme::MacroId)> onFocusMacro;

    sattari::ui::KeyButton& keyControl() noexcept { return keyButton; }
    juce::Slider& royalKnob() noexcept { return royalSlider; }
    juce::ComboBox& styleCombo() noexcept { return styleBox; }
    void appendCaptionChecks (std::vector<rc::theme::CaptionCheck>&) const;

private:
    void stepStyle (int delta);
    void paintReadout (juce::Graphics&, juce::Rectangle<float> card) const;
    void paintMeters (juce::Graphics&, juce::Rectangle<float> card) const;

    struct Slot
    {
        float db = -100.0f, peakDb = -100.0f, peakAge = 0.0f, fill = 0.0f;
        float px[14] {}, py[14] {}, alpha[14] {};
    };

    RoyalChainAudioProcessor& processor;
    juce::TextButton stylePrev, styleNext;
    juce::ComboBox styleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment;
    sattari::ui::KeyButton keyButton;
    juce::Slider royalSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> royalAttachment;

    rc::theme::MacroId focused = rc::theme::MacroId::Tune;
    float focusedValueShown = -1.0e9f;
    Slot slots[2];
    float rows[4] {}, rowsShown[4] { -1, -1, -1, -1 };

    static const juce::Rectangle<int> readoutCard, royalCard, meterCard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoyalSpine)
};
