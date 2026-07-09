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
//  Sattari Granulator — editor (TWO-LAYER A/B + MORPH + LIVE CAPTURE)
// -----------------------------------------------------------------------------
//  ~980x680, non-resizable. Layout:
//
//    HEADER  — product title | shared sattari::ui::KeyButton | SattariABRow +
//              preset browser | master Output knob.
//    LAYER   — an A|B layer-select toggle (drives the UI-only `layerSel` param),
//              a MORPH knob (layerMorph), the selected layer's LEVEL + PAN, and a
//              CAPTURE cluster (Arm / Length / Target A|B) with a progress bar.
//    SOURCE  — the WaveformView for the SELECTED layer (drag-drop + Start/End +
//              live grain sparks) with the Load / Reverse / Normalize / Trim /
//              Mono-fold / Undo edit strip + per-layer Reverse-grains / Loop /
//              Channels controls.
//    RACK    — a tabbed control rack. GRAIN / PITCH tabs are PER-LAYER (they
//              rebind to the selected layer's A_/B_ params when it flips). FILTER
//              / ENV / LFO / MOD tabs are GLOBAL (shared).
//    KEYS    — a juce::MidiKeyboardComponent.
//
//  A 24 Hz timer keeps the KeyButton in sync, reflects the selected layer's
//  source name / undo state, drains any completed Live Capture, and updates the
//  capture progress bar. Flipping the layer selector rebinds every per-layer
//  widget + points the WaveformView at the new layer.
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
    using Layer            = SattariGranulatorAudioProcessor::Layer;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;

    // --- Small builders -------------------------------------------------------
    void styleKnob (juce::Slider& s, bool feature = false);
    void styleCaption (juce::Label& l, const juce::String& text);
    void styleCombo (juce::ComboBox& c);
    void styleToggle (juce::ToggleButton& t, const juce::String& text);

    // GLOBAL widget binders (persistent attachments — never rebound).
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

    // PER-LAYER widget binders: create the widget/caption + record a base id so
    // the attachment can be (re)built for the currently selected layer.
    void addLayerKnob (juce::Component& panel, juce::Slider& s, juce::Label& cap,
                       const juce::String& text, const char* baseId);
    void addLayerCombo (juce::Component& panel, juce::ComboBox& c, juce::Label& cap,
                        const juce::String& text, const juce::StringArray& items,
                        const char* baseId);
    void addLayerToggle (juce::Component& panel, juce::ToggleButton& t,
                         const juce::String& text, const char* baseId);

    // Rebuild every per-layer attachment against `selectedLayer` (A_/B_ prefix).
    void rebindLayer (Layer layer);

    void showTab (int idx);
    void setKeyParam (const juce::String& id, float value);
    void selectLayer (Layer layer);          // flip layerSel + rebind + refresh
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void openFileChooser();

    SattariGranulatorAudioProcessor& audioProcessor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("granulator") };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    Layer selectedLayer = Layer::A;

    // --- Header ---------------------------------------------------------------
    juce::Label title;
    sattari::ui::KeyButton keyButton;
    bool followingBus = false;   // drives the status dot in paint()

    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save" };

    juce::Slider outputKnob;
    juce::Label  outputCaption;
    std::unique_ptr<SliderAttachment> outputAttach;

    // --- Layer / morph / capture bar ------------------------------------------
    juce::TextButton layerAButton { "A" }, layerBButton { "B" };
    juce::Label  layerCaption;
    juce::Slider morphKnob;                    // layerMorph (GLOBAL)
    juce::Label  morphCaption;
    std::unique_ptr<SliderAttachment> morphAttach;
    juce::Slider layerLevel, layerPan;         // PER-LAYER (rebound)
    juce::Label  layerLevelCap, layerPanCap;

    juce::ToggleButton captureArmButton;       // captureArm (momentary)
    juce::Slider captureSecs;                  // captureSecs
    juce::Label  captureSecsCap;
    juce::TextButton captureTargetA { "->A" }, captureTargetB { "->B" };
    juce::Label  captureCaption;
    std::unique_ptr<ButtonAttachment> captureArmAttach;
    std::unique_ptr<SliderAttachment> captureSecsAttach;
    double captureProgress = 0.0;              // 0..1 drawn as a bar in paint()

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

    // PER-LAYER source-shaping controls (rebound to the selected layer).
    juce::ToggleButton reverseGrainsToggle;   // srcReverse
    juce::ComboBox loopModeCombo;             // srcLoopMode
    juce::ComboBox channelsCombo;             // srcMono
    juce::Label    loopCaption, channelsCaption;

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

    // --- GRAIN tab (PER-LAYER) ------------------------------------------------
    juce::Slider grainSize, density, spray, posBase, posScanRate, grainPan;
    juce::Label  grainSizeCap, densityCap, sprayCap, posBaseCap, posScanRateCap, grainPanCap;
    juce::ComboBox densityMode, window, posScanDiv;
    juce::Label  densityModeCap, windowCap, posScanDivCap;
    juce::ToggleButton posScanSync;

    // --- PITCH tab (per-layer offsets + UNISON + GLOBAL pitch shaping) --------
    // Per-layer: coarse / fine / spread + unison (voices/detune/width).
    // Global: glide + pitch env/lfo + keySnap.
    juce::Slider tuneCoarse, tuneFine, pitchSpread,                       // per-layer
                 unisonVoices, unisonDetune, unisonWidth,                 // per-layer (unison)
                 glide, pitchEnvAmt, pitchEnvAttack, pitchEnvDecay,
                 pitchLfoAmt, pitchLfoRate;                                // global
    juce::Label  tuneCoarseCap, tuneFineCap, pitchSpreadCap,
                 unisonVoicesCap, unisonDetuneCap, unisonWidthCap,
                 glideCap, pitchEnvAmtCap, pitchEnvAttackCap,
                 pitchEnvDecayCap, pitchLfoAmtCap, pitchLfoRateCap;
    juce::ComboBox glideMode;
    juce::Label  glideModeCap;
    juce::ToggleButton keySnap;

    // --- FILTER tab (GLOBAL) --------------------------------------------------
    juce::Slider cutoff, reso, filterKeyTrack, filterEnvAmt;
    juce::Label  cutoffCap, resoCap, filterKeyTrackCap, filterEnvAmtCap;
    juce::ComboBox filterType;
    juce::Label  filterTypeCap;

    // --- ENV tab (GLOBAL) -----------------------------------------------------
    juce::Slider ampAttack, ampDecay, ampSustain, ampRelease,
                 fltAttack, fltDecay, fltSustain, fltRelease,
                 envVelSens, modEnvAttack, modEnvDecay;
    juce::Label  ampAttackCap, ampDecayCap, ampSustainCap, ampReleaseCap,
                 fltAttackCap, fltDecayCap, fltSustainCap, fltReleaseCap,
                 envVelSensCap, modEnvAttackCap, modEnvDecayCap;

    // --- LFO tab (GLOBAL) -----------------------------------------------------
    juce::Slider lfo1Rate, lfo1Amt, lfo2Rate, lfo2Amt;
    juce::Label  lfo1RateCap, lfo1AmtCap, lfo2RateCap, lfo2AmtCap;
    juce::ComboBox lfo1Shape, lfo1Div, lfo1Dest, lfo2Shape, lfo2Div, lfo2Dest;
    juce::Label  lfo1ShapeCap, lfo1DivCap, lfo1DestCap, lfo2ShapeCap, lfo2DivCap, lfo2DestCap;
    juce::ToggleButton lfo1Sync, lfo2Sync;

    // --- MOD tab (GLOBAL) -----------------------------------------------------
    //  Sub oscillator + width/tone + voices live here (shared).
    juce::Slider subLevel, subOct, width, tone, voices;
    juce::Label  subLevelCap, subOctCap, widthCap, toneCap, voicesCap;
    juce::ComboBox subWave, voiceMode;
    juce::Label  subWaveCap, voiceModeCap;
    juce::ToggleButton subOsc;

    // --- Bottom keyboard ------------------------------------------------------
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState,
                                           juce::MidiKeyboardComponent::horizontalKeyboard };

    // --- Attachment stores ----------------------------------------------------
    // GLOBAL attachments (persistent).
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>>  comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    // PER-LAYER attachments (torn down + rebuilt on layer flip) + their bindings.
    struct LayerSliderBind { juce::Slider* w; juce::String baseId; };
    struct LayerComboBind  { juce::ComboBox* w; juce::String baseId; };
    struct LayerToggleBind { juce::ToggleButton* w; juce::String baseId; };
    std::vector<LayerSliderBind> layerSliderBinds;
    std::vector<LayerComboBind>  layerComboBinds;
    std::vector<LayerToggleBind> layerToggleBinds;
    std::vector<std::unique_ptr<SliderAttachment>> layerSliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>>  layerComboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> layerToggleAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGranulatorAudioProcessorEditor)
};
