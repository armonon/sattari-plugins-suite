#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "PluginProcessor.h"

class SattariAutoPitchAudioProcessorEditor;

// -----------------------------------------------------------------------------
// NoteDisplay: large stylised note ("F#" / "C") + cents-off bar (live-driven).
// -----------------------------------------------------------------------------
class NoteDisplay final : public juce::Component
{
public:
    NoteDisplay() = default;

    void setSnapshot (const SattariAutoPitchAudioProcessor::LiveSnapshot& s) noexcept
    {
        snapshot = s;
        repaint();
    }

    void paint (juce::Graphics&) override;

private:
    SattariAutoPitchAudioProcessor::LiveSnapshot snapshot;
};

// -----------------------------------------------------------------------------
// KeyPillRow: 12 chromatic note pills. Pills lit when the note is in the
// current key/scale; the currently-detected pitch is highlighted.
// -----------------------------------------------------------------------------
class KeyPillRow final : public juce::Component
{
public:
    KeyPillRow() = default;

    // Click handlers. Plain click toggles the note's bit in the enabled-notes
    // mask (block / unblock for correction). Shift+click sets the key root to
    // that pitch class. Editor wires both to APVTS params.
    std::function<void(int)> onPillClicked;       // plain click -> toggle enabled bit
    std::function<void(int)> onPillShiftClicked;  // shift-click -> set key root

    void setScale (int keyRoot, int scaleIndex) noexcept
    {
        keyRootIdx = keyRoot;
        scaleIdx   = scaleIndex;
        repaint();
    }

    void setEnabledMask (int mask) noexcept
    {
        enabledMask = mask;
        repaint();
    }

    void setDetectedMidi (int midi) noexcept
    {
        detectedMidi = midi;
        repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int hitTestPill (juce::Point<float> p) const;

    int keyRootIdx = 0;
    int scaleIdx   = 0;
    int detectedMidi = -1;
    int enabledMask = 4095;   // all 12 bits on by default
    juce::Rectangle<float> sharpRects[5];
    juce::Rectangle<float> naturalRects[7];
};

// -----------------------------------------------------------------------------
// KeyButton: ONE tactile control for the whole key story. The face shows the key
// currently in effect + how it's being driven (MANUAL / AUTO / FOLLOW). Clicking
// opens a menu to link AutoKey (auto-detect from the track), Follow Song Key
// (from the master bus), or set any key by hand. Replaces the separate AutoKey /
// Follow toggles + status label with a single button.
// -----------------------------------------------------------------------------
class KeyButton final : public juce::Component
{
public:
    enum class Mode { Manual, Auto, Follow, Map };

    KeyButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onLinkAuto;                   // engage AutoKey (and drop Follow)
    std::function<void()> onLinkFollow;                 // engage Follow Song Key (and drop AutoKey)
    std::function<void(int root, int scaleIdx)> onManualKey;  // set key by hand (drops Auto/Follow)

    void setState (Mode m, int root, int scaleIdx) noexcept
    {
        if (m == mode && root == rootIdx && scaleIdx == scaleIndex) return;
        mode = m; rootIdx = root; scaleIndex = scaleIdx; repaint();
    }

    // Live "lock" strength 0..1 (detection confidence). Drawn as a 5-dot meter in
    // the readout while auto-detecting so the user can see the key settle.
    void setConfidence (float c) noexcept
    {
        c = juce::jlimit (0.0f, 1.0f, c);
        if (std::abs (c - confidence) < 0.03f) return;
        confidence = c; repaint();
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override { pressed = false; repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    Mode mode = Mode::Manual;
    int  rootIdx = 9, scaleIndex = 1;
    float confidence = 0.0f;
    bool hover = false, pressed = false;
};

// -----------------------------------------------------------------------------
// Editor proper.
// -----------------------------------------------------------------------------
class SattariAutoPitchAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor&);
    ~SattariAutoPitchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider& s, bool feature = false);
    void styleCaption (juce::Label& l, const juce::String& text);
    void registerFactoryPresets();
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void loadTraceFromClipboard();
    void showTab (int idx);
    // Set an APVTS param by its real value (handles the 0-1 conversion + gesture).
    void setKeyParam (const juce::String& id, float value);

    SattariAutoPitchAudioProcessor& audioProcessor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("auto-pitch") };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    // --- Tab strip ------------------------------------------------------------
    juce::TextButton tabBasic { "BASIC" }, tabPro { "PRO" };
    int currentTab = 0;
    float keyLock = 0.0f;   // smoothed detection-confidence for the readout meter
    juce::Component basicPanel, proPanel;

    // --- Header (shared by both tabs) ----------------------------------------
    juce::Label title;
    juce::ComboBox vocalRange, keyRoot, scale;
    juce::Label vocalRangeCaption, keyScaleCaption;
    juce::ComboBox factoryPresets, userPresets;
    juce::TextButton savePresetButton { "Save" };

    // --- Basic tab widgets ---------------------------------------------------
    juce::ToggleButton autoKey { "AutoKey (this track)" };
    juce::ToggleButton followKey { "Follow Song Key (Sattari Key)" };
    NoteDisplay noteDisplay;
    KeyPillRow pillRow;
    juce::Label editNotesLabel, detuneLabel, modeLabel;
    // Key control: a big live readout (KeyButton) + a row of three explicit
    // action buttons under it. The readout shows the key + how it's driven +
    // a confidence lock meter; the buttons make every mode a single visible click.
    KeyButton keyButton;
    juce::TextButton autoDetectBtn { "AUTO-DETECT" }, followSongBtn { "FOLLOW SONG" }, setKeyBtn { "SET KEY" };
    juce::ToggleButton lowLatency { "Low Latency" };

    juce::Label retuneSpeedCaption, flexTuneCaption, humanizeCaption;
    juce::Slider retuneSpeed, flexTune, humanize;
    juce::ToggleButton style;        // off = Modern, on = Classic
    juce::Label modernLabel, classicLabel;

    // --- Pro tab widgets -----------------------------------------------------
    juce::ToggleButton enableCorrection { "Correction On" }, doNoHarm { "Do No Harm" },
                       formantPreserve  { "Formant Preserve" },
                       polyphonic       { "Polyphonic" };
    juce::Slider inputGain, outputGain, formantShift;
    juce::Label inputCaption, outputCaption, formantCaption;

    // --- Antares-class behaviour controls (Pro tab) --------------------------
    juce::ToggleButton classicMode { "Classic Mode" };
    juce::Slider flexAmt, transition, throat, vibratoKnob;
    juce::Label flexAmtCaption, transitionCaption, throatCaption, vibratoCaption;

    // --- Transpose to Key (Pro tab): whole-mix musical transpose for beats/instruments
    juce::ToggleButton transposeMode { "Transpose to Key (whole mix)" };
    juce::Slider transposeSemis;
    juce::Label transposeCaption, transposeReadout;
    juce::TextButton matchKeyButton { "Match Key" };

    // --- New engine / detector controls (Pro tab) ----------------------------
    juce::ToggleButton liveEngine  { "Live Engine (PSOLA)" };
    juce::ToggleButton neuralPitch { "Neural Pitch" };
    juce::TextButton loadTraceButton { "Load trace JSON" };
    juce::TextButton midiLearnButton { "MIDI Learn..." };
    juce::Label traceStatus, keyDetectionStatus;

    // Track Key Map (Pro tab) — scan a song to build a key timeline that the
    // live correction follows.
    juce::TextButton scanTrackButton { "Scan Track..." };
    juce::TextButton clearMapButton  { "Clear Map" };
    juce::ToggleButton useKeyMap     { "Use Key Map" };
    juce::Label keyMapStatusLabel;
    std::unique_ptr<juce::FileChooser> trackFileChooser;
    std::unique_ptr<ButtonAttachment> useKeyMapAttach;

    void onScanTrackClicked();

    // --- APVTS attachments ---------------------------------------------------
    std::unique_ptr<ComboAttachment> vocalRangeAttach, keyRootAttach, scaleAttach;
    std::unique_ptr<SliderAttachment> retuneSpeedAttach, flexTuneAttach, humanizeAttach,
                                       inputGainAttach, outputGainAttach, formantShiftAttach;
    std::unique_ptr<ButtonAttachment> autoKeyAttach, followKeyAttach, lowLatencyAttach,
                                       enableCorrectionAttach, doNoHarmAttach, formantPreserveAttach,
                                       polyphonicAttach, classicModeAttach;
    std::unique_ptr<SliderAttachment> flexAmtAttach, transitionAttach, throatAttach, vibratoAttach;
    std::unique_ptr<ButtonAttachment> liveEngineAttach, neuralPitchAttach, transposeModeAttach;
    std::unique_ptr<SliderAttachment> transposeSemisAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessorEditor)
};
