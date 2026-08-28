#pragma once

#include <JuceHeader.h>
#include <map>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "DeckPlayer.h"
#include "MainPluginProcessor.h"
#include "MixerSection.h"
#include "MixerView.h"
#include "MusicLibraryView.h"
#include "PianoRollView.h"
#include "DrumExtractor.h"
#include "MelodyExtractor.h"
#include "NeuralTranscriber.h"
#include "RekordboxIO.h"
#include "StemDeckLookAndFeel.h"
#include "WaveformDisplay.h"
#include "XYPad.h"

class MainPluginEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer,
                               private juce::FileBrowserListener,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit MainPluginEditor (MainPluginProcessor&);
    ~MainPluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct DeckControls
    {
        juce::TextButton load { "Load" };
        juce::ComboBox stemLane;
        juce::TextButton loadStem { "Stem" };
        juce::TextButton aiStems { "AI Split" };
        juce::TextButton extractMidi;   // audio→MIDI melody extraction (SRC tab)
        juce::TextButton extractDrums;  // beatbox/percussion→drum pattern (SRC tab)
        juce::ToggleButton play;
        juce::TextButton cue { "Cue" };
        juce::ComboBox tempoInterpretation;
        juce::ComboBox syncMode;
        juce::ComboBox keyTarget;
        juce::ToggleButton keyLock;
        juce::ToggleButton liveKey;
        juce::Label keyInfo;
        juce::Label stemStatus;
        juce::Slider bpm, beatOffset, downbeat, volume, fader, pitch, low, mid, high, filter;
        std::array<juce::Slider, DeckPlayer::numStemLanes> stemGains;
        std::unique_ptr<ButtonAttachment> playAttachment, keyLockAttachment, liveKeyAttachment;
        std::unique_ptr<ComboBoxAttachment> keyTargetAttachment, tempoInterpretationAttachment, syncModeAttachment;
        std::unique_ptr<SliderAttachment> bpmAttachment, beatOffsetAttachment, downbeatAttachment, volumeAttachment, faderAttachment, pitchAttachment, lowAttachment, midAttachment, highAttachment, filterAttachment;
        std::array<std::unique_ptr<SliderAttachment>, DeckPlayer::numStemLanes> stemGainAttachments;

        // ── Hot cues ──────────────────────────────────────────────────────────
        std::array<juce::TextButton, MainPluginProcessor::numHotCues> hotCues;

        // ── Loop controls ─────────────────────────────────────────────────────
        juce::TextButton loopIn   { "IN" };
        juce::TextButton loopOut  { "OUT" };
        juce::TextButton loopToggle { "LOOP" };
        juce::TextButton loopHalf { "1/2" };
        juce::TextButton loopDouble { "x2" };

        // ── Intro / Outro DJ markers ──────────────────────────────────────────
        juce::TextButton introEndBtn   { "Intro End" };
        juce::TextButton outroStartBtn { "Outro Start" };

        // ── Slip mode + Loop Roll ─────────────────────────────────────────────
        juce::TextButton slipButton    { "Slip" };
        // Beat-size loop roll buttons: 1/4  1/2  1  2  4  8 bars
        std::array<juce::TextButton, 6> beatRollBtns;
        static constexpr std::array<double, 6> kBeatRollLengths { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
        static constexpr std::array<const char*, 6> kBeatRollLabels { "1/4","1/2","1","2","4","8" };
        bool loopRollActive = false;

        // ── Beat jump (back: bars[3]=32 [2]=16 [1]=8 [0]=4, fwd mirror) ──────
        std::array<juce::TextButton, 4> beatJumpBack;
        std::array<juce::TextButton, 4> beatJumpFwd;

        // ── Built-in FX ───────────────────────────────────────────────────────
        juce::Slider reverbMix, echoMix;
        std::unique_ptr<SliderAttachment> reverbMixAttachment, echoMixAttachment;
        // Superknob: drives reverb + echo simultaneously (not APVTS-attached)
        juce::Slider macroKnob;

        // ── Per-stem FX (drums/bass/music/vocals): filter + space/echo send ────
        std::array<juce::Slider, 4> stemFilt, stemSend, stemPitch;
        std::array<std::unique_ptr<SliderAttachment>, 4> stemFiltAttachments, stemSendAttachments, stemPitchAttachments;

        // ── Crossfader assign: A / Center / B ────────────────────────────────
        std::array<juce::TextButton, 3> cfSideBtns;   // [0]=A  [1]=Ctr  [2]=B

        // ── Toolbox tabs (declutter): CUE / LOOP / FX / SYNC / SRC ─────────────
        enum ToolTab { TabCue = 0, TabLoop, TabFx, TabSync, TabSrc, NumToolTabs };
        std::array<juce::TextButton, NumToolTabs> toolTabs;
        int activeToolTab = TabCue;
    };

    struct PadControls
    {
        juce::TextButton trigger;
        juce::TextButton load { "load" };
        juce::Slider gain;
        std::unique_ptr<SliderAttachment> gainAttachment;
    };

    class ArrangementRecorderView final : public juce::Component,
                                          public juce::FileDragAndDropTarget,
                                          private juce::ChangeListener
    {
    public:
        static constexpr int numLanes = MainPluginProcessor::numDecks;
        static constexpr int masterLane = numLanes;          // 4 — master is the 5th lane
        static constexpr int numEditableLanes = numLanes + 1;

        enum class Mode { Idle, Recording, Finished };

        struct AutomationCurve
        {
            juce::String label;
            juce::Colour colour;
            float minValue = 0.0f, maxValue = 1.0f;
            std::vector<std::pair<double, float>> points;
        };

        using DeckAutomation = std::vector<AutomationCurve>;

        // Follow actions: what happens when a clip finishes in the clip launcher
        enum class FollowAction { None, Next, Prev, RandomOther, Stop };

        // Clip-launcher slot state (beat-quantized transitions)
        enum class SlotState { Idle, WaitingToStart, Running };

        // Overlap resolution when a clip is dragged/dropped onto existing clips
        enum class OverlapMode { Keep, Push, Replace };

        // A clip is a region [trimStartSeconds, trimEndSeconds] of its source file,
        // shown in place on the timeline (timeline position == source region). A
        // lane holds one or more clips; razor-splitting one produces two adjacent
        // clips referencing the same file.
        struct LaneClip
        {
            juce::File file;
            double durationSeconds = 0.0;      // full source-file duration
            double trimStartSeconds = 0.0;     // source in-point
            double trimEndSeconds = 0.0;       // source out-point
            double timelineStartSeconds = 0.0; // free position on the timeline
            double fadeInSeconds = 0.0;
            double fadeOutSeconds = 0.0;
            bool selected = false;
            juce::String label;          // optional display name (overrides the filename)
            juce::uint32 colourArgb = 0; // 0 = use the lane colour; else an explicit clip colour

            // Clip launcher metadata
            FollowAction followAction  = FollowAction::None;
            float        followProbability = 1.0f;   // 0..1
            bool         locked        = false;      // prevent accidental drag

            double lengthSeconds() const noexcept { return juce::jmax (0.0, trimEndSeconds - trimStartSeconds); }
            double timelineEndSeconds() const noexcept { return timelineStartSeconds + lengthSeconds(); }
        };

        using LaneClips = std::array<std::vector<LaneClip>, numEditableLanes>;

        // A per-lane MIDI clip: notes positioned (in beats) from its timeline start.
        struct MidiNote { int pitch = 60; double startBeats = 0.0; double lengthBeats = 1.0; int velocity = 100; };
        struct MidiClipData { bool active = false; double timelineStartSeconds = 0.0; std::vector<MidiNote> notes; };

        ArrangementRecorderView();
        ~ArrangementRecorderView() override;

        void initialise (juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);
        void setIdle (juce::String hint);
        void setRecordingProgress (double elapsedSeconds, juce::String recordingPath);
        void setArrangement (const juce::File& masterFile,
                             const std::array<juce::File, numLanes>& deckFiles,
                             juce::String statusLine);
        void setDeckAutomation (std::array<DeckAutomation, numLanes> automation);
        void clearArrangement();
        bool hasArrangement() const noexcept { return mode == Mode::Finished; }
        const LaneClips& getClips() const noexcept { return clips; }
        int getSelectedLane() const noexcept { return selectedLane; }
        void updateClipAfterBounce (int laneIndex, const juce::File& newFile, double newDuration);
        void resetSelectedClipEdits();
        void resetClipEdits (int laneIndex);
        bool copyClip (int sourceLane, int destLane);
        void setSnap (bool enabled, double beatGrid, double bpm);
        void setPlaybackPosition (double seconds, bool active);
        void setRazorMode (bool shouldSplit);
        bool isRazorMode() const noexcept { return razorMode; }
        void splitClipAt (int laneIndex, int clipIndex, double timeSeconds);
        void deleteClip (int laneIndex, int clipIndex);
        void deleteSelectedClip();
        void duplicateClip (int laneIndex, int clipIndex);
        void setClipLabel (int laneIndex, int clipIndex, const juce::String& label);
        void setClipColour (int laneIndex, int clipIndex, juce::uint32 argb);
        void setClipLocked (int laneIndex, int clipIndex, bool shouldLock);
        void zoomIn();
        void zoomOut();
        void zoomToFit();

        juce::ValueTree toState() const;                  // serialize clips + lane state + loop
        void fromState (const juce::ValueTree& state);

        // Undo / redo. Callers should snapshot via captureLaneState() before any edit,
        // then call commitEdit() after the edit completes to push it onto the stack.
        // A snapshot captures the whole lane (its full clip vector) so split / delete
        // round-trip cleanly.
        struct LaneSnapshot { int lane = -1; std::vector<LaneClip> state; bool full = false; LaneClips allState;
                              std::array<std::vector<MidiClipData>, numEditableLanes> allMidi;
                              // Automation curves ride along so point edits are undoable too.
                              std::array<std::vector<std::pair<double, float>>, numLanes> allAutoPoints; };
        LaneSnapshot captureLaneState (int laneIndex) const;
        LaneSnapshot captureFullState() const;   // whole arrangement (for cross-lane moves)
        void commitEdit (LaneSnapshot before);
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }
        void undo();
        void redo();

        std::function<void(int)> onClearDeckRequest;
        std::function<void(int)> onStopDeckRequest;   // stop the deck's transport (launcher Stop)
        std::function<void()>    onLaneMixChanged;    // mute/solo/gain edited — apply live
        std::function<void(double)> onSeek;   // user clicked the ruler to move the play cursor
        struct MidiRef { int lane = -1; int clip = -1; bool valid() const noexcept { return lane >= 0 && clip >= 0; } };
        std::function<void(int,int)> onEditMidiClip;   // (lane, clip) double-clicked

        // ---- Per-lane MIDI clips (multiple per lane, draggable) ----
        int    numMidiClips (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) ? (int) midiClips[(size_t) lane].size() : 0; }
        double midiClipStart (int lane, int idx) const noexcept;
        const std::vector<MidiNote>& midiClipNotes (int lane, int idx) const;
        int    addMidiClip (int lane, double timeSec);
        void   setMidiClipNotes (int lane, int idx, std::vector<MidiNote> notes);
        MidiRef midiClipAt (juce::Point<int> p) const;
        std::function<void(double,double,bool)> onLoopChanged;   // loop region changed (start, end, enabled)
        void setLoopEnabled (bool shouldLoop);
        bool isLoopEnabled() const noexcept { return loopEnabled; }

        // Clip launcher (beat-quantized slot triggers)
        void triggerSlot (int lane, int clip) noexcept;
        void triggerAllSlots() noexcept;    // queues clip[0] of every occupied deck lane
        void setSlotState (int lane, SlotState state) noexcept;
        SlotState getSlotState (int lane) const noexcept;
        int getLauncherClip (int lane) const noexcept;

        // Callback: editor hooks this to show the follow-action popup menu
        std::function<void(int lane, int clip)> onFollowActionRequest;

        // Overlap resolution mode (public accessor for the editor toolbar button)
        OverlapMode getOverlapMode() const noexcept { return overlapMode; }
        void setOverlapMode (OverlapMode m) noexcept { overlapMode = m; }

        // Per-lane mute/solo/volume (applied when the arrangement playback schedule is built).
        bool laneAudible (int lane) const noexcept
        {
            if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return true;
            bool anySolo = false;
            for (int i = 0; i < numLanes; ++i) anySolo = anySolo || laneSoloed[(size_t) i];
            return ! laneMuted[(size_t) lane] && (! anySolo || laneSoloed[(size_t) lane]);
        }
        float laneGainLinear (int lane) const noexcept
        {
            return juce::isPositiveAndBelow (lane, numEditableLanes)
                       ? juce::Decibels::decibelsToGain (laneGainDb[(size_t) lane]) : 1.0f;
        }
        float getLaneGainDb (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) ? laneGainDb[(size_t) lane] : 0.0f; }
        void  setLaneGainDb (int lane, float db) { if (juce::isPositiveAndBelow (lane, numEditableLanes)) { laneGainDb[(size_t) lane] = juce::jlimit (-60.0f, 6.0f, db); repaint(); } }
        // ---- Editable automation: one draggable breakpoint curve per deck lane ----
        // Targets: 0 = channel fader, 1 = filter, 2 = pitch, 3 = vocals FX send.
        static constexpr int numAutoTargets = 4;
        struct EditAutomation
        {
            bool visible = false;
            int  target  = 0;
            std::vector<std::pair<double, float>> points;   // (seconds, normalized 0..1)
        };
        void  setShowDemoBlocks (bool on) { showDemoBlocks = on; repaint(); }   // showcase placeholder clips
        bool  isAutomationVisible (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numLanes) && editAuto[(size_t) lane].visible; }
        // Interpolated value at a play position; returns -1 when the lane's curve
        // is hidden or empty. targetOut receives the curve's parameter target.
        float automationValueAt (int lane, double seconds, int& targetOut) const;

        bool  isLaneMuted  (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) && laneMuted[(size_t) lane]; }
        bool  isLaneSoloed (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) && laneSoloed[(size_t) lane]; }
        void  toggleLaneMute (int lane) { if (juce::isPositiveAndBelow (lane, numEditableLanes)) { laneMuted[(size_t) lane]  = ! laneMuted[(size_t) lane];  if (onLaneMixChanged) onLaneMixChanged(); repaint(); } }
        void  toggleLaneSolo (int lane) { if (juce::isPositiveAndBelow (lane, numEditableLanes)) { laneSoloed[(size_t) lane] = ! laneSoloed[(size_t) lane]; if (onLaneMixChanged) onLaneMixChanged(); repaint(); } }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

        // Free audio import: drag an audio file onto a lane to place a clip.
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void fileDragEnter (const juce::StringArray& files, int x, int y) override;
        void fileDragMove  (const juce::StringArray& files, int x, int y) override;
        void fileDragExit  (const juce::StringArray& files) override;
        void filesDropped  (const juce::StringArray& files, int x, int y) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void loadThumbnail (juce::AudioThumbnail& thumb, const juce::File& file);
        double computeMaxLengthSeconds() const;
        void drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds,
                        const AutomationCurve& curve, double timeScale) const;

        Mode mode = Mode::Idle;
        juce::String status { "Hit Record to capture your set across four deck lanes." };
        double elapsedSeconds = 0.0;
        juce::String recordingPath;

        juce::AudioFormatManager* formatManager = nullptr;
        juce::AudioThumbnailCache* thumbnailCache = nullptr;
        std::unique_ptr<juce::AudioThumbnail> masterThumb;
        std::array<std::unique_ptr<juce::AudioThumbnail>, numLanes> deckThumbs;
        std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> clipThumbs;   // per imported file
        juce::AudioThumbnail* thumbForClip (int laneIndex, const LaneClip& clip) const;
        void ensureClipThumb (const juce::File& file);
        int laneAtY (int y) const;
        int  fileDragLane = -1;   // lane currently under a file drag (for highlight)
        std::array<bool, numEditableLanes> laneMuted {}, laneSoloed {};
        std::array<float, numEditableLanes> laneGainDb {};   // 0 dB default
        std::array<juce::Rectangle<int>, numEditableLanes> muteBtnRects {}, soloBtnRects {};
        std::array<juce::Rectangle<int>, numLanes> autoBtnRects {}, autoTargetRects {};
        std::array<EditAutomation, numLanes> editAuto;
        int dragAutoLane = -1, dragAutoPointIdx = -1;
        bool showDemoBlocks = false;   // showcase placeholder timeline clips (opt-in via Settings)
        std::array<std::vector<MidiClipData>, numEditableLanes> midiClips {};
        MidiRef activeMidiDrag;
        double  dragStartMidiStart = 0.0;
        juce::File masterFile;
        std::array<juce::File, numLanes> deckFiles {};
        LaneClips clips {};
        std::array<juce::Rectangle<int>, numEditableLanes> laneRects {};
        std::array<DeckAutomation, numLanes> deckAutomation;
        int selectedLane = -1;
        int selectedClip = -1;
        bool razorMode = false;
        juce::Point<int> lastContextPos;

        enum class Handle { None, Body, TrimStart, TrimEnd, FadeIn, FadeOut };
        struct HitTest { int lane = -1; int clip = -1; Handle handle = Handle::None; };
        HitTest hitTestAt (juce::Point<int> p) const;
        double timeAtX (int laneIndex, int x) const;
        HitTest activeDrag;
        int dragOrigLane = -1;   // lane a Body-drag started in (for cross-lane move detection)
        double dragStartValue = 0.0;
        juce::Point<int> dragStartPoint;
        juce::String dragReadout;
        LaneSnapshot pendingEditSnapshot;
        juce::MouseCursor cursorForHandle (Handle h) const;
        double snapTime (double seconds) const;
        juce::AudioThumbnail* thumbForLane (int laneIndex) const;

        bool snapEnabled = false;
        double snapBeatGrid = 0.25;
        double snapBpm = 120.0;
        double playbackPositionSeconds = -1.0;
        bool   playbackActive = false;
        double cursorSeconds = 0.0;                       // play-start cursor (shown when stopped)
        juce::Rectangle<int> rulerBounds, timelineMapRect;   // computed in paint(), for click-to-seek
        double loopStartSeconds = 0.0, loopEndSeconds = 0.0;
        bool   loopEnabled = false;
        bool   rulerDragActive = false;
        double loopAnchorSeconds = 0.0;
        int    rulerDownX = 0;

        // ---- Timeline viewport: zoom + horizontal scroll ----
        double zoom = 1.0;                 // 1 = whole arrangement fits the width
        double viewStartSeconds = 0.0;     // left edge of the visible window
        juce::Rectangle<int> scrollTrackBounds, scrollThumbBounds;   // computed in paint()
        bool   draggingScrollBar = false;
        double scrollDragAnchorView = 0.0;
        int    scrollDragAnchorX = 0;
        double timelineContentSeconds() const;
        double visibleSeconds() const;
        void   clampView();
        void   setZoomAnchored (double newZoom, double anchorTime);
        int    timeToXIn (juce::Rectangle<int> track, double t) const;
        double xToTimeIn (juce::Rectangle<int> track, int x) const;

        std::vector<LaneSnapshot> undoStack;
        std::vector<LaneSnapshot> redoStack;

        // ---- Clip launcher state (beat-quantized transitions) ----
        std::array<SlotState, numEditableLanes> slotStates {};
        std::array<int, numLanes> launcherClipPerLane {};   // per-lane queued clip index (-1 = none)

        // ---- Overlap resolution mode ----
        OverlapMode overlapMode = OverlapMode::Keep;

        // Apply overlap resolution when a clip is placed at a given position on a lane.
        // Returns true if the operation changed surrounding clips (for undo).
        void applyOverlapResolution (int laneIndex, const LaneClip& incoming);
    };

    MainPluginProcessor& processorRef;

    // Shared suite UI + cross-plugin preset system.
    StemDeckLookAndFeel lookAndFeel { sattari::brand::productAccent ("stemdeck") };
    sattari::presets::SattariPresetManager presetManager;
    juce::ComboBox userPresets;
    juce::TextButton savePresetButton { "Save Preset" };

    juce::AudioFormatManager thumbnailFormatManager;
    juce::AudioThumbnailCache thumbnailCache { 16 };

    juce::Label title, subtitle, tempoLabel, standaloneWorkflowGuide, pluginHostStatus;
    juce::Label liveMixerLabel, arrangementLabel, samplerLabel;
    juce::TextButton scanPluginsButton { "Scan FX" };
    juce::TextButton reloadPluginCacheButton { "Reload Cache" };
    juce::TextButton loadPluginButton { "Load FX" };
    juce::TextButton clearChainButton { "Clear FX" };
    juce::ComboBox pluginChainSelector, pluginSelector;
    juce::Label pluginHostMessage;
    juce::Array<juce::PluginDescription> knownHostPlugins;
    std::array<std::unique_ptr<WaveformDisplay>, MainPluginProcessor::numDecks> deckWaves;
    std::array<DeckControls, MainPluginProcessor::numDecks> deckControls;
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckPanelBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckHeaderBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckStemBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckStemFxBounds {};
    juce::Rectangle<int> masterOutBounds {};    // MASTER OUTPUT panel (meters/gain/tempo)
    juce::Rectangle<int> masterMeterBounds {};  // stereo meter zone inside it
    juce::Rectangle<int> masterTempoBounds {};  // painted GLOBAL TEMPO box
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckMeterBounds {};  // console per-deck meters
    juce::TextButton keyLockAllButton { "Key Lock" };
    juce::TextButton aiMasterButton { "AI Master" };
    juce::ComboBox aiMasterMode;
    juce::TextButton rbxExportButton { "RBX Save" };
    juce::TextButton rbxImportButton { "RBX Load" };
    std::unique_ptr<juce::FileChooser> rbxChooser;
    RekordboxIO::CueMap importedCues;   // path → cues, persisted; applied on deck load
    std::array<juce::String, MainPluginProcessor::numDecks> lastCuePaths;
    void applyImportedCues (int deckIndex);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aiMasterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aiMasterModeAttachment;
    juce::Rectangle<int> headerBarBounds {};
    juce::Rectangle<int> tempoChipBounds {};
    MixerSection mixerPanel;
    XYPad masterFxPad;
    juce::Slider crossfader, masterBpm, master, limiter, micGain;
    juce::ToggleButton hostSync, beatSync, micMonitor;
    juce::TextButton tapTempoButton { "TAP" };
    juce::TextButton masterPlayButton;   // header: play/stop ALL decks at once
    juce::TextButton automixButton;      // header: phrase-aligned auto-transition
    juce::Label      automixStatusLabel; // shows the current transition plan / progress
    juce::TextButton camelotButton { "Key" };

    // Automix crossfader ramp (message-thread driven from timerCallback).
    bool          automixRamping   = false;
    int           automixOutDeck   = -1;
    float         automixCfFrom    = 0.0f;
    float         automixCfTo      = 1.0f;
    juce::uint32  automixStartMs   = 0;
    double        automixDurationMs = 0.0;
    void          startAutomix();
    void          updateAutomixRamp();
    juce::TextButton midiLearnButton { "MIDI" };
    juce::Label libraryMetaLabel;   // shows BPM / key / duration for hovered file
    juce::TextButton revealRecordingButton { "Reveal" };
    juce::TextButton reloadIntoDecksButton { "Reload to Decks" };
    juce::TextButton bounceEditsButton { "Bounce Edits" };
    juce::TextButton snapToggleButton { "Snap" };
    juce::TextButton razorButton { "Razor" };
    juce::TextButton arrZoomOutButton { "-" }, arrZoomInButton { "+" }, arrZoomFitButton { "Fit" };
    juce::TextButton arrLoopButton { "Loop" };
    juce::TextButton launchAllButton { ">>>" };
    juce::TextButton playArrButton { "Play Arr" };
    juce::TextButton arrRecordButton { "Record" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    void toggleArrangementPlayback();
    void saveProject();
    void loadProject();
    void openPianoRollForClip (int lane, int clip);
    juce::Label masterKeyLabel;
    juce::ComboBox masterKey;
    juce::TextButton syncAllDecksButton { "Sync All Decks" };
    juce::ToggleButton stemLinkToggle { "Link Stems" };
    std::unique_ptr<ComboBoxAttachment> masterKeyAttachment;
    std::unique_ptr<ButtonAttachment> stemLinkAttachment;
    bool stemLinkMirroring = false;
    juce::Label recordStatus;
    ArrangementRecorderView arrangementView;
    PianoRollView pianoRoll;
    juce::TextButton pianoRollButton { "Piano Roll" };
    MixerView mixerView;
    juce::TextButton mixerButton { "Mixer" };
    juce::TextButton saveProjButton { "Save Proj" }, openProjButton { "Open Proj" };
    juce::TextButton metronomeButton { "Click" };
    juce::TextButton talkoverButton  { "Tover" };    // talkover ducking toggle
    juce::TextButton overlapModeButton { "Keep" };   // cycles Keep → Push → Replace
    double arrangementCursorSeconds = 0.0;   // play-start cursor set by clicking the ruler
    int    pianoRollLane = -1;                // which lane/clip the piano roll edits
    int    pianoRollClip = -1;

    // Beat-quantized clip launcher
    double launcherBeatPhase   = 0.0;
    double launcherBeatOriginMs = -1.0;      // wall-clock ms of first beat reference
    std::array<double, MainPluginProcessor::numDecks> pendingSlotSeekSec {}; // -1 = no pending
    double arrangementLoopStart = 0.0, arrangementLoopEnd = 0.0;
    bool   arrangementLoopEnabled = false;
    std::array<PadControls, PadBank::numPads> pads;

    std::unique_ptr<SliderAttachment> crossfaderAttachment, masterBpmAttachment, masterAttachment, limiterAttachment, micGainAttachment;
    std::unique_ptr<ButtonAttachment> hostSyncAttachment, beatSyncAttachment, micMonitorAttachment;

    juce::FileChooser deckChooser { "Load a deck track" };
    juce::FileChooser padChooser { "Load a one-shot sample" };
    juce::FileChooser recordChooser { "Save StemDeck master recording", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("StemDeck-master.wav"), "*.wav" };
    std::array<juce::String, MainPluginProcessor::numDecks> deckPaths;
    std::array<std::array<juce::String, DeckPlayer::numStemLanes>, MainPluginProcessor::numDecks> deckStemPaths {};
    std::array<bool, MainPluginProcessor::numDecks> lastDeckPlayingUI {};
    std::array<juce::String, MainPluginProcessor::numDecks> lastBeatGridKey {};

    juce::TimeSliceThread browserThread { "StemDeck file browser" };
    juce::WildcardFileFilter browserFilter { "*.wav;*.aif;*.aiff;*.flac;*.mp3", "*", "Audio files" };
    juce::DirectoryContentsList browserContents { &browserFilter, browserThread };
    std::unique_ptr<juce::FileTreeComponent> fileBrowser;
    std::unique_ptr<MusicLibraryView> libraryView;
    juce::TextButton toggleLibraryButton { "LIBRARY" };
    juce::TextButton navDecksButton { "DECKS" };
    // Nav bar: icon-above-label cells (matches the mockup header).
    std::array<juce::Rectangle<int>, 5> navRects {};
    void drawNavIcon (juce::Graphics&, int which, juce::Rectangle<float> box, juce::Colour) const;
    // Advanced tools (plugin host, master key, AI master, rekordbox, project I/O,
    // limiter/mic/utility chips) are collapsed by default; the visible TOOLS button
    // in the header (plus title double-click) reveals them.
    bool advancedVisible = false;
    juce::TextButton advancedToggle { "TOOLS" };
    // Showcase demo content (fake titles / PLAY-on / demo waveforms / timeline
    // blocks). Off by default — real state only; opt-in via the Settings gear.
    bool showcaseMode = false;
    void setShowcaseMode (bool on);
    juce::TextButton settingsButton;   // gear — discoverable preferences (text set in ctor)
    void showSettingsMenu();
    void mouseDoubleClick (const juce::MouseEvent&) override;   // dbl-click title = reveal advanced
    void mouseDown (const juce::MouseEvent&) override;          // header BPM/Key chips
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;          // disarms BPM-chip drag
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    // Interactive deck header chips + all-deck sync.
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckBpmChipBounds {}, deckKeyChipBounds {};
    int    bpmDragDeck = -1;
    int    bpmDragStartY = 0;
    double bpmDragStartVal = 120.0;
    bool   masterBpmDragging = false;   // drag on the GLOBAL TEMPO box
    void setDeckBpm (int deckIndex, double bpm);
    void showKeyMenuForDeck (int deckIndex);
    void syncBpmAll();   // warp all decks to the master deck's tempo (hotkey B)
    void syncKeyAll();   // harmonically match all decks to the master deck (hotkey K)
    juce::TextButton syncBpmButton { "BPM SYNC" };
    juce::TextButton syncKeyButton { "KEY SYNC" };
    // Master deck = sync reference. Auto-follows the last deck to start playing;
    // click a deck's letter badge to override.
    int masterDeckIndex = 0;
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckBadgeBounds {};
    void setMasterDeck (int deckIndex);
    bool libraryVisible = false;
    juce::TextButton toggleBrowserButton { "PERFORMANCE" };
    juce::TextButton browserUpButton { "Up" };
    juce::TextButton browserHomeButton { "Home" };
    juce::Label browserPathLabel;
    bool browserVisible = false;

    static juce::String deckPrefix (int deckIndex);
    static juce::String deckLabel (int deckIndex);
    static juce::String padKeyLabel (int padIndex);
    static int padIndexForKeyPress (const juce::KeyPress& key);

    void setupDeck (DeckControls& controls, const juce::String& prefix, const juce::String& playText);
    void setupSlider (juce::Slider& slider, bool rotary, const juce::String& suffix = {});
    void setupButton (juce::Button& button);
    void setupComboBox (juce::ComboBox& comboBox);
    void openDeckChooser (int deckIndex);
    void openDeckStemChooser (int deckIndex, int stemLaneIndex);
    void applyExtractedMelody (int deckIndex, const std::vector<MelodyExtractor::Note>& notes, bool neural = false);
    void applyExtractedDrums (int deckIndex, const std::vector<MelodyExtractor::Note>& hits);
    void openPadChooser (int padIndex);

    // Toolbox tab helpers
    void setupToolTabs (int deckIndex);
    void setDeckToolTab (int deckIndex, int tab);
    void applyDeckToolTabVisibility (int deckIndex);

    // Hot cue helpers
    void setupHotCueButtons (int deckIndex);
    void handleHotCuePress (int deckIndex, int cueIndex, juce::ModifierKeys mods);
    void refreshHotCueButtonColours (int deckIndex);
    void updateWaveformHotCues (int deckIndex);

    // Loop helpers
    void setupLoopControls (int deckIndex);
    void updateLoopButtonStates (int deckIndex);
    void updateWaveformLoopRegion (int deckIndex);

    // Slip + loop roll helpers
    void setupSlipAndRoll (int deckIndex);
    void updateSlipButtonState (int deckIndex);

    // Superknob / FX macro helpers
    void setupMacroKnob (int deckIndex);

    // Follow action + launcher helpers
    void showFollowActionMenu (int laneIndex, int clipIndex);
    void applyFollowAction (int laneIndex, int clipIndex);

    // UI helpers
    void showCamelotWheel (int deckIndex);
    void showLibraryMeta (const juce::File& file);
    void updateMidiLearnUI();

    void toggleRecording();
    void revealLastRecording();
    void reloadLastRecordingIntoDecks();
    void loadLastArrangementIntoView (juce::String statusLine);
    void syncAllDecksTo (int sourceDeckIndex);
    void showSyncAllDecksMenu();
    void setupFileBrowser();
    void toggleFileBrowser();
    void setupLibrary();
    void toggleLibrary();
    void browserNavigateTo (const juce::File& folder);
    void browserNavigateUp();
    void bounceArrangementEdits();
    void refreshHostPluginChoices();
    void runPluginHostScan();
    void loadSelectedHostPlugin();
    void refreshUserPresets();
    void saveCurrentAsUserPreset();
    void timerCallback() override;
    juce::String supportedPattern() const;

    void selectionChanged() override {}
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    static int parseDeckIndexFromParamID (const juce::String& id);
    static int parseStemLaneFromParamID (const juce::String& id);

    // ── Motion state (eased in timerCallback @30Hz) ─────────────────────────
    float navUnderlineX = -1.0f, navUnderlineW = 0.0f;      // sliding nav underline
    std::array<double, 8> padFlashMs {};                    // pad-trigger flash stamps
    // Activates every setTooltip() in the app (they are inert without a window).
    juce::TooltipWindow tooltipWindow { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPluginEditor)
};
