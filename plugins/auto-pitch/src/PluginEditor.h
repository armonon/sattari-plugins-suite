#pragma once

#include <JuceHeader.h>
#include <array>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariABRow.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include <sattari_audio_core/SattariKeyMapTimeline.h>
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
// PitchEditTimeline: captured note objects on the host timeline. Drag a block
// vertically to retarget it; right-click toggles correction for that region.
// -----------------------------------------------------------------------------
class PitchEditTimeline final : public juce::Component, public juce::SettableTooltipClient
{
public:
    using Entry = SattariAutoPitchAudioProcessor::PitchEditEntry;
    using HarmonyOverlay = SattariAutoPitchAudioProcessor::HarmonyEditOverlay;
    std::function<void(const Entry&)> onEntryChanged;
    std::function<void(juce::int64, int, int)> onHarmonyChanged;
    std::function<void(juce::int64)> onSelectionChanged;
    std::function<void()> onGestureBegin, onGestureEnd;

    void setData (std::vector<Entry> newEntries, double newPlayhead);
    void setHarmonyOverlays (std::vector<HarmonyOverlay> newOverlays);
    void zoomIn();
    void zoomOut();
    void fitAll();
    juce::int64 selectedId() const noexcept { return selectedEntryId; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> entryRect (int index) const;
    int hitEntry (juce::Point<float>) const;
    juce::Rectangle<float> harmonyRect (const HarmonyOverlay&) const;
    int minMidi() const;
    int maxMidi() const;

    std::vector<Entry> entries;
    std::vector<HarmonyOverlay> harmonyOverlays;
    double playheadSec = 0.0;
    double timelineDurationSec = 10.0;
    double viewStartSec = 0.0, viewDurationSec = 10.0;
    bool fitToContent = true;
    int selected = -1;
    juce::int64 selectedEntryId = 0;
    int dragStartTarget = 60;
    float dragStartY = 0.0f;
    int selectedHarmony = -1;
    juce::int64 harmonySourceId = 0;
    float dragRowHeight = 1.0f;
};

// -----------------------------------------------------------------------------
// Editor proper.
// -----------------------------------------------------------------------------
class SattariAutoPitchAudioProcessorEditor final : public juce::AudioProcessorEditor,
#if JucePlugin_Enable_ARA
                                                   public juce::AudioProcessorEditorARAExtension,
#endif
                                                   public juce::FileDragAndDropTarget,
                                                   private juce::Timer
{
public:
    explicit SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor&);
    ~SattariAutoPitchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    //  Drop a whole song anywhere on the editor: the absorbed Sattari Key analyses
    //  it (key + tempo + detune) and the verdict outranks live detection.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void styleKnob (juce::Slider& s, bool feature = false);
    void styleCaption (juce::Label& l, const juce::String& text);
    // Dim + disable a control when the current mode makes it inert (idempotent — safe
    // to call every timer tick; JUCE only repaints when the state actually changes).
    void setControlActive (juce::Component& c, bool on);
    void registerFactoryPresets();
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void loadTraceFromClipboard();
    // Set an APVTS param by its real value (handles the 0-1 conversion + gesture).
    void setKeyParam (const juce::String& id, float value);

    SattariAutoPitchAudioProcessor& audioProcessor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("auto-pitch") };

    sattari::presets::SattariPresetManager presetManager;
    sattari::ui::SattariABRow abRow { presetManager };

    // --- Workspaces ---------------------------------------------------------------
    // KEY | TUNE | HARMONY | EDIT. One primary workspace at a time; the header
    // and its musical summary persist across all of them.
    enum class Workspace { key, tune, harmony, edit };
    Workspace workspace = Workspace::tune;
    void setWorkspace (Workspace w);
    //  Lays out the graph band inside editPanel's current bounds. editMode gates
    //  the surgical chrome: command bar and per-object sliders live in EDIT, while
    //  TUNE keeps the always-visible graph with just the capture toggles.
    void layoutEditPanel (bool editMode);
    juce::TextButton wsKeyButton { "KEY" }, wsTuneButton { "TUNE" },
                     wsHarmonyButton { "HARMONY" }, wsEditButton { "EDIT" };
    juce::Label headerSummary;             // key - bpm - tuning - confidence, always honest

    //  KEY workspace: the musical-analysis centre. Reads ONLY the shared song
    //  model (songAnalysisCopy) — no analysis logic lives in the UI.
    juce::Component keyPanel;
    juce::Label keyBigLabel, keyMetaLabel, keyAltLabel, keySectionsLabel,
                keyInspectorLabel, keyHintLabel;
    sattari::ui::KeyMapTimeline songMapView;
    juce::Label keyChordsLabel, keyModsLabel;
    juce::TextButton followSongMapButton { "FOLLOW SONG MAP" };
    std::unique_ptr<ButtonAttachment> followSongMapAttachment;
    //  Region editing: the user's assertion outranks detection, permanently.
    juce::ComboBox regionRootBox, regionScaleBox;
    juce::TextButton regionApplyButton { "SET REGION KEY" };
    int selectedRegionIndex = -1;
    juce::String pendingRegionConfirm;   // survives the model rebuild it triggers

    //  HARMONY workspace.
    juce::Component harmonyPanel;
    juce::ComboBox harmonyModeBox;
    std::unique_ptr<ComboAttachment> harmonyModeAttachment;
    juce::Label harmonyHeaderLabel, harmonyStatusLabel;
    juce::Slider harmonyMix, harmonyLead, harmonyGlide, harmonyHumanPitch,
                 harmonyHumanTime, harmonyVibratoRate, harmonyEnsemble, harmonyEnsembleDepth,
                 harmonyEnsembleRate, harmonyEnsembleSpread,
                 harmonyLowCut, harmonyHighCut;
    juce::TextButton harmonyVoiceOnly { "VOICES ONLY" };
    std::array<juce::Label, 12> harmonyMacroLabels;
    std::array<std::unique_ptr<SliderAttachment>, 12> harmonyMacroAttachments;
    std::unique_ptr<ButtonAttachment> harmonyVoiceOnlyAttachment;
    struct HarmonyVoiceStrip
    {
        juce::TextButton enable, solo;
        juce::ComboBox   role;
        juce::Slider     degrees, octave, level, pan, delay, detune, vibrato, formant;
        juce::Label      caption, readout, levelLabel, panLabel, delayLabel,
                         detuneLabel, vibratoLabel, formantLabel;
        std::unique_ptr<ButtonAttachment> enableAttach, soloAttach;
        std::unique_ptr<ComboAttachment>  roleAttach;
        std::unique_ptr<SliderAttachment> degreesAttach, octaveAttach,
                                          levelAttach, panAttach, delayAttach,
                                          detuneAttach, vibratoAttach, formantAttach;
    };
    std::array<HarmonyVoiceStrip, 4> harmonyVoices;
    double songModelSeenDuration = -1.0;   // repopulate the map only on change
    int    songModelSeenRegions = -1;

    // --- Single-view chrome -----------------------------------------------------
    //  The BASIC/PRO/EDIT tab strip is gone: everything lives on one view, with the
    //  former PRO panel as a collapsible ADVANCED drawer. The three container
    //  components survive as layout bands so no child is reparented.
    juce::TextButton liveModeButton { "LIVE" };    // the low-latency engine, at last in the UI
    std::unique_ptr<ButtonAttachment> liveModeAttachment;
    juce::TextButton extremeLiveButton { "2MS" };
    std::unique_ptr<ButtonAttachment> extremeLiveAttachment;
    juce::ComboBox liveProfile;
    std::unique_ptr<ComboAttachment> liveProfileAttachment;
    juce::Label latencyReadout;                    // reported delay of the ACTIVE engine
    juce::TextButton advancedToggle { "ADVANCED" };
    juce::Label songStatusLabel;                   // drop-analysis progress / verdict
    juce::TextButton scanClipButton { "Scan DAW clip" };
    int songModelSeenRevision = -1;
    bool advancedOpen = false;
    bool dragActive = false;

    float keyLock = 0.0f;   // smoothed detection-confidence for the readout meter
    juce::Component basicPanel, proPanel, editPanel;

    // --- Header (shared by both tabs) ----------------------------------------
    juce::Label title;
    juce::ComboBox vocalRange, keyRoot, scale, scaleMode;
    juce::Label vocalRangeCaption;
    juce::ComboBox factoryPresets, userPresets;
    juce::TextButton savePresetButton { "Save" };

    // --- Basic tab widgets ---------------------------------------------------
    juce::ToggleButton autoKey { "AutoKey (this track)" };
    juce::ToggleButton followKey { "Follow Song Key (Sattari Key)" };
    NoteDisplay noteDisplay;
    KeyPillRow pillRow;
    juce::Label editNotesLabel, detuneLabel;
    // Key control: a big live readout (KeyButton) + a row of three explicit
    // action buttons under it. The readout shows the key + how it's driven +
    // a confidence lock meter; the buttons make every mode a single visible click.
    KeyButton keyButton;
    juce::TextButton autoDetectBtn { "AUTO-DETECT" }, followSongBtn { "FOLLOW SONG" }, setKeyBtn { "SET KEY" };
    // Quality-locked Studio selector. Legacy live parameters remain in the
    // processor so old sessions still deserialize safely.

    juce::Label retuneSpeedCaption, flexTuneCaption, humanizeCaption, styleCaptionLabel;
    juce::Slider retuneSpeed, flexTune, humanize;
    // Five real DSP styles are exposed directly. The previous two-position
    // Modern/Classic switch could display "Modern" while a preset had actually
    // selected Natural, Robot, or Big Harmony.
    juce::ComboBox styleSelect;

    // --- Pro tab widgets -----------------------------------------------------
    juce::ToggleButton enableCorrection { "Correction On" }, doNoHarm { "Do No Harm" },
                       formantPreserve  { "Formant Preserve" },
                       polyphonic       { "Polyphonic" };
    // Per-peak polyphonic macro; keeps the selected manual/suite key authority.
    // This is not the whole-beat transpose workflow.
    juce::ToggleButton scaleLock { "Poly Scale Lock" };
    juce::Slider inputGain, outputGain, formantShift;
    juce::Label inputCaption, outputCaption, formantCaption;

    // --- Antares-class behaviour controls (Pro tab) --------------------------
    juce::ToggleButton classicMode { "Hard Tune" };   // instant/robotic retune — distinct from the Style=Classic character
    juce::Slider flexAmt, transition, throat, vibratoKnob;
    juce::Slider addVibrato, vibratoRate, vibratoOnset, referenceDetune;
    juce::Label addVibratoCaption, vibratoRateCaption, vibratoOnsetCaption, referenceDetuneCaption;
    juce::ToggleButton followReference { "Follow song reference" };
    juce::ToggleButton midiTarget { "Target notes from MIDI" };
    // Poly Harmony amount — adds an in-key diatonic-3rd voice in Polyphonic mode.
    juce::Slider polyHarmonyKnob;
    juce::Label  polyHarmonyCaption;
    // Poly high-end (instrumentals): follow tonal overtones up into the top end so the
    // highs land in key, and protect percussive/noise content while doing it.
    juce::Slider highTrackKnob, tonalFocusKnob;
    juce::Label  highTrackCaption, tonalFocusCaption;
    juce::Label flexAmtCaption, transitionCaption, throatCaption, vibratoCaption;

    // --- Transpose to Key (Pro tab): whole-mix musical transpose for beats/instruments
    juce::ToggleButton transposeMode { "Transpose to Key (whole mix)" };
    juce::Slider transposeSemis;
    juce::Label transposeCaption, transposeReadout;
    juce::TextButton matchKeyButton { "Match Key" };

    // --- New engine / detector controls (Pro tab) ----------------------------
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
    // Visual key map: the song's waveform with its detected key sections. Refetched
    // only when the map changes (tracked by these three), playhead moved every tick.
    sattari::ui::KeyMapTimeline keyMapTimeline;
    bool         keyMapShown     = false;
    int          keyMapShownSegs = -1;
    juce::String keyMapShownFile;
    std::unique_ptr<juce::FileChooser> trackFileChooser;
    std::unique_ptr<ButtonAttachment> useKeyMapAttach;

    // --- Edit tab ------------------------------------------------------------
    PitchEditTimeline pitchEditTimeline;
    juce::ToggleButton capturePitchEdits { "Capture notes" };
    juce::ToggleButton usePitchEdits { "Use edited notes" };
    juce::ToggleButton graphTimeCorrection { "Time correction" };
    juce::TextButton clearPitchEdits { "Clear" }, undoPitchEdit { "Undo" }, redoPitchEdit { "Redo" },
                     splitPitchEdit { "Split" }, joinPitchEdit { "Join" }, deletePitchEdit { "Delete" },
                     zoomInPitchEdit { "+" }, zoomOutPitchEdit { "-" }, fitPitchEdit { "Fit" };
    juce::Slider objectAmount, objectRetune, objectTransition, objectHumanize, objectVibrato,
                 objectFormant, objectTime, objectGain;
    juce::Label objectAmountLabel, objectRetuneLabel, objectTransitionLabel, objectHumanizeLabel, objectVibratoLabel,
                objectFormantLabel, objectTimeLabel, objectGainLabel;
    juce::Label pitchEditHelp, pitchEditStatus;
    int pitchEditShownRevision = -1;
    juce::int64 selectedPitchEditId = 0;
    PitchEditTimeline::Entry selectedPitchEdit;
    bool syncingPitchEditControls = false;
    std::unique_ptr<ButtonAttachment> capturePitchEditsAttach, usePitchEditsAttach, graphTimeCorrectionAttach;
    void selectPitchEdit (juce::int64 id);
    void updateSelectedPitchEdit();

    void onScanTrackClicked();

    // --- APVTS attachments ---------------------------------------------------
    std::unique_ptr<ComboAttachment> vocalRangeAttach, keyRootAttach, scaleAttach, scaleModeAttach, styleAttach;
    std::unique_ptr<SliderAttachment> retuneSpeedAttach, flexTuneAttach, humanizeAttach,
                                       inputGainAttach, outputGainAttach, formantShiftAttach;
    std::unique_ptr<ButtonAttachment> autoKeyAttach, followKeyAttach,
                                       enableCorrectionAttach, doNoHarmAttach, formantPreserveAttach,
                                       polyphonicAttach, classicModeAttach;
    std::unique_ptr<SliderAttachment> flexAmtAttach, transitionAttach, throatAttach, vibratoAttach,
                                      addVibratoAttach, vibratoRateAttach, vibratoOnsetAttach,
                                      referenceDetuneAttach;
    std::unique_ptr<ButtonAttachment> neuralPitchAttach, transposeModeAttach,
                                      followReferenceAttach, midiTargetAttach;
    std::unique_ptr<SliderAttachment> transposeSemisAttach;
    std::unique_ptr<SliderAttachment> polyHarmonyAttach, highTrackAttach, tonalFocusAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariAutoPitchAudioProcessorEditor)
};
