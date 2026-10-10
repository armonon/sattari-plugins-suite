#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include "PluginProcessor.h"
#include "MixController.h"

class AutoMixPanel final : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit AutoMixPanel (SattariMixAudioProcessor&);
    ~AutoMixPanel() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int, int, int, int, bool) override;
    juce::String getCellTooltip (int, int) override;
    void selectedRowsChanged (int) override;
    void refreshSelection();
    void editProposal();
    const sattari::automix::Track* selectedTrack() const;
    sattari::automix::Suggestion* selectedSuggestion();
    SattariMixAudioProcessor& processor;
    MixController& controller;
    juce::Colour accent { sattari::brand::productAccent ("mix") };
    sattari::releaseui::Header header { "Sattari Mix", "Session balance", accent };
    juce::TextEditor group, name;
    juce::TextButton newGroup { "New group" }, join { "Join" }, leave { "Leave" };
    juce::TextButton analyze { "Analyze group" }, propose { "Build proposal" }, stop { "Stop" };
    juce::TextButton original { "Original" }, proposed { "Proposal" }, keep { "Keep mix" }, undo { "Undo mix" };
    juce::ToggleButton match { "Level match" }, protect { "Lock track" }, enabled { "Auto Mix" };
    juce::ToggleButton busInsert { "Summing bus" }, showHistory { "History" };
    juce::TextEditor historyView;
    juce::ComboBox duration, role;
    juce::Slider gain, low, presence, compression, amount, localGain;
    std::array<juce::Label, 11> labels;
    juce::Label status, reason, routingWarning;
    juce::TableListBox table { "Mix group", this };
    juce::Rectangle<int> footerArea, detailArea;
    juce::String selectedId, displayedGroup, displayedName;
    bool refreshing = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> busAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment, gainAttachment;
    juce::TooltipWindow tooltip { this, 600 };
};

class SattariMixAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SattariMixAudioProcessorEditor (SattariMixAudioProcessor&);
    ~SattariMixAudioProcessorEditor() override;
    void resized() override;
private:
    sattari::releaseui::LookAndFeel look { sattari::brand::productAccent ("mix") };
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    AutoMixPanel autoMix;
    sattari::ui::SattariRackEditor rack;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessorEditor)
};
