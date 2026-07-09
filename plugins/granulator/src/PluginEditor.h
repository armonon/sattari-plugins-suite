#pragma once

#include <JuceHeader.h>

#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>

#include "PluginProcessor.h"
#include "WaveformView.h"

#include <array>
#include <memory>
#include <vector>

// =============================================================================
//  Sattari Granulator — editor
// -----------------------------------------------------------------------------
//  ~980x640, non-resizable. Layout:
//
//    HEADER  — product title | shared sattari::ui::KeyButton (MANUAL / FOLLOW
//              SONG KEY, wired to keyRoot / keyScale / followKey + a bus-status
//              dot) | SattariABRow + preset browser | master Output knob.
//    SOURCE  — the always-visible WaveformView (drag-drop + Start/End handles +
//              live grain sparks) with a Load Sample / Reverse / Normalize /
//              Trim / Mono-fold / Undo edit strip and Reverse-grains + Loop +
//              Channels source controls.
//    RACK    — a tabbed control rack (GRAIN / PITCH / FILTER / ENV / LFO / MOD)
//              of styled rotaries + combos + toggles, every v1 param bound.
//    KEYS    — a juce::MidiKeyboardComponent so it plays without a controller.
//
//  A 24 Hz timer keeps the KeyButton reflecting the effective (bus / manual)
//  key and lights the follow-status dot. The WaveformView owns its own 30 Hz
//  timer for the grain visualisation.
// =============================================================================
class SattariGranulatorAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                    private juce::Timer
{
public:
    explicit SattariGranulatorAudioProcessorEditor (SattariGranulatorAudioProcessor&);
    ~SattariGranulatorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    // --- Small builders -------------------------------------------------------
    void styleKnob (juce::Slider& s, bool feature = false);
    void styleCaption (juce::Label& l, const juce::String& text);
    void styleCombo (juce::ComboBox& c);
    void styleToggle (juce::ToggleButton& t, const juce::String& text);

    // Attach a rotary + caption to an APVTS param and add it to a panel.
    void addKnob (juce::Component& panel, juce::Slider& s, juce::Label& cap,
                  const juce::String& text, const char* paramId,
                  std::vector<std::unique_ptr<SliderAttachment>>& store);
    void addCombo (juce::Component& panel, juce::ComboBox& c, juce::Label& cap,
                   const juce::String& text, const juce::StringArray& items,
                   const char* paramId,
                   std::vector<std::unique_ptr<ComboAttachment>>& store);
    void addToggle (juce::Component& panel, juce::ToggleButton& t,
                    const juce::String& text, const char* paramId,
                    std::vector<std::unique_ptr<ButtonAttachment>>& store);

    void showTab (int idx);
    void setKeyParam (const juce::String& id, float value);
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void openFileChooser();

    SattariGranulatorAudioProcessor& audioProcessor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("granulator") };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    // --- Header ---------------------------------------------------------------
    juce::Label title;
    sattari::ui::KeyButton keyButton;
    bool followingBus = false;   // drives the status dot in paint()

    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save" };

    juce::Slider outputKnob;
    juce::Label  outputCaption;
    std::unique_ptr<SliderAttachment> outputAttach;

    // --- Source strip (under the header, above the waveform) ------------------
    WaveformView waveform { audioProcessor };
    juce::Label  sourceNameLabel;
    juce::TextButton loadButton  { "Load Sample" };
    juce::TextButton reverseButton { "Reverse" };
    juce::TextButton normalizeButton { "Normalize" };
    juce::TextButton trimButton { "Trim" };
    juce::TextButton monoFoldButton { "Mono-fold" };
    juce::TextButton undoButton { "Undo" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ToggleButton reverseGrainsToggle;   // srcReverse (grain read direction)
    juce::ComboBox loopModeCombo;             // srcLoopMode
    juce::ComboBox channelsCombo;             // srcMono
    juce::Label    loopCaption, channelsCaption;
    std::unique_ptr<ButtonAttachment> reverseGrainsAttach;
    std::unique_ptr<ComboAttachment>  loopModeAttach, channelsAttach;

    // --- Tab strip ------------------------------------------------------------
    static constexpr int kNumTabs = 6;
    std::array<juce::TextButton, kNumTabs> tabButtons {
        juce::TextButton { "GRAIN" }, juce::TextButton { "PITCH" },
        juce::TextButton { "FILTER" }, juce::TextButton { "ENV" },
        juce::TextButton { "LFO" }, juce::TextButton { "MOD" } };
    int currentTab = 0;

    juce::Component grainPanel, pitchPanel, filterPanel, envPanel, lfoPanel, modPanel;
    std::array<juce::Component*, kNumTabs> panels {
        &grainPanel, &pitchPanel, &filterPanel, &envPanel, &lfoPanel, &modPanel };

    // --- GRAIN tab ------------------------------------------------------------
    juce::Slider grainSize, density, spray, posBase, posScanRate, grainPan;
    juce::Label  grainSizeCap, densityCap, sprayCap, posBaseCap, posScanRateCap, grainPanCap;
    juce::ComboBox densityMode, window, posScanDiv;
    juce::Label  densityModeCap, windowCap, posScanDivCap;
    juce::ToggleButton posScanSync;
    // Sub oscillator sub-cluster.
    juce::ToggleButton subOsc;
    juce::Slider subLevel;
    juce::Label  subLevelCap;
    juce::ComboBox subWave;
    juce::Label  subWaveCap;
    juce::Slider subOct;
    juce::Label  subOctCap;

    // --- PITCH tab ------------------------------------------------------------
    juce::Slider tuneCoarse, tuneFine, glide, pitchEnvAmt, pitchEnvAttack, pitchEnvDecay,
                 pitchLfoAmt, pitchLfoRate, pitchSpread;
    juce::Label  tuneCoarseCap, tuneFineCap, glideCap, pitchEnvAmtCap, pitchEnvAttackCap,
                 pitchEnvDecayCap, pitchLfoAmtCap, pitchLfoRateCap, pitchSpreadCap;
    juce::ComboBox glideMode;
    juce::Label  glideModeCap;
    juce::ToggleButton keySnap;

    // --- FILTER tab -----------------------------------------------------------
    juce::Slider cutoff, reso, filterKeyTrack, filterEnvAmt;
    juce::Label  cutoffCap, resoCap, filterKeyTrackCap, filterEnvAmtCap;
    juce::ComboBox filterType;
    juce::Label  filterTypeCap;

    // --- ENV tab --------------------------------------------------------------
    //  Amp ADSR + Filter ADSR + velocity sensitivity + shared Mod AD. (The pitch
    //  AD lives on the PITCH tab so its attack/decay bind exactly once.)
    juce::Slider ampAttack, ampDecay, ampSustain, ampRelease,
                 fltAttack, fltDecay, fltSustain, fltRelease,
                 envVelSens, modEnvAttack, modEnvDecay;
    juce::Label  ampAttackCap, ampDecayCap, ampSustainCap, ampReleaseCap,
                 fltAttackCap, fltDecayCap, fltSustainCap, fltReleaseCap,
                 envVelSensCap, modEnvAttackCap, modEnvDecayCap;

    // --- LFO tab --------------------------------------------------------------
    juce::Slider lfo1Rate, lfo1Amt, lfo2Rate, lfo2Amt;
    juce::Label  lfo1RateCap, lfo1AmtCap, lfo2RateCap, lfo2AmtCap;
    juce::ComboBox lfo1Shape, lfo1Div, lfo1Dest, lfo2Shape, lfo2Div, lfo2Dest;
    juce::Label  lfo1ShapeCap, lfo1DivCap, lfo1DestCap, lfo2ShapeCap, lfo2DivCap, lfo2DestCap;
    juce::ToggleButton lfo1Sync, lfo2Sync;

    // --- MOD tab --------------------------------------------------------------
    juce::Slider posEnvAmt, posLfoAmt, srcGain, width, tone, voices;
    juce::Label  posEnvAmtCap, posLfoAmtCap, srcGainCap, widthCap, toneCap, voicesCap;
    juce::ComboBox voiceMode;
    juce::Label  voiceModeCap;

    // --- Bottom keyboard ------------------------------------------------------
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState,
                                           juce::MidiKeyboardComponent::horizontalKeyboard };

    // --- Attachment stores (one flat vector per widget kind keeps it tidy) ----
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>>  comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGranulatorAudioProcessorEditor)
};
