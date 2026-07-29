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
#include "ArrangementView.h"
#include "ToolbarIcons.h"
#include "MusicLibraryView.h"
#include "PianoRollView.h"
#include "DrumExtractor.h"
#include "MelodyExtractor.h"
#include "NeuralTranscriber.h"
#include "RekordboxIO.h"
#include "StemDeckLookAndFeel.h"
#include "WaveformDisplay.h"
#include "DeckStrip.h"
#include "StemGainKnob.h"
#include "CamelotWheel.h"
#include "WaveformRegion.h"
#include "TempoPanel.h"
#include "SessionModel.h"
#include "EnergyArc.h"
#include "SattariFxRack.h"
#include "ChannelStrip.h"
#include "XYPad.h"
#include "RecordingHealthBar.h"
#include "CommandPalette.h"
#include "QuickInspector.h"
#include "WorkflowPanels.h"

// Plugin-FX hosting: HostGraph discovers + instantiates plugins AND its processChain
// is now wired into processBlock for every deck, the mic bus, and the master (empty
// chains pass through). So loaded FX are audible — the UI is enabled. The curated
// "Sattari FX" loader lives in this panel (showSattariFxMenu).
#ifndef STEMDECK_ENABLE_PLUGIN_HOST
 #define STEMDECK_ENABLE_PLUGIN_HOST 1
#endif

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

    // Select a deck's segmented tab (CUE/LOOP/FX/SYNC/SRC) from outside the editor.
    // Exists so StemDeckEditorRender can photograph every tab's panel — a tab whose
    // content is only reachable by clicking is a tab the PNG gate never checks.
    void selectDeckToolTab (int deckIndex, int tab);

    // Four workspaces are the product navigation. `files` remains available as a
    // command-palette utility; the legacy aliases keep render tools/source callers
    // compatible while making "performance" mean the actual Perform workspace.
    enum class PrimarySurface
    {
        perform, library, mixer, arranger, files,
        decks = perform,
        performance = perform
    };
    // Deterministic surface selection for render/interaction gates. This deliberately
    // uses the same callbacks as nav clicks, so tests cannot exercise a fake layout.
    void selectPrimarySurface (PrimarySurface surface);

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
        std::array<stemdeck::StemGainKnob, DeckPlayer::numStemLanes> stemGains;   // 0/100/200 knobs (§3)
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

        // How many channel lanes actually exist. numLanes is the POOL SIZE (8);
        // rendering all of it meant the arranger showed CH A..CH H while the header
        // said "6 CH" — the channel model never propagated down here. Lanes beyond
        // this are neither drawn nor hit-tested.
        void setVisibleLanes (int n)
        {
            const int clamped = juce::jlimit (1, numLanes, n);
            if (clamped == visibleLanes) return;
            visibleLanes = clamped;
            for (int i = clamped; i < numLanes; ++i)
                laneRects[(size_t) i] = {};
            repaint();
        }
        int getVisibleLanes() const noexcept { return visibleLanes; }
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
        int visibleLanes = numLanes;   // see setVisibleLanes()
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
    juce::TextButton sattariFxButton { "SATTARI FX" };   // curated one-click Sattari-suite loader
    stemdeck::SattariFxRack fxRack;                       // FX-rack panel (replaced the crossfader)
    juce::OwnedArray<stemdeck::PluginEditorWindow> pluginWindows;
    void addSattariFxToChain (int chainIndex);           // menu -> load into a specific chain
    bool pluginScanInFlight = false;
    void openChainPluginEditor (int chainIndex, int pluginIndex);
    // Plugin editors hold a raw reference to a processor OWNED BY THE GRAPH. Removing
    // or clearing a chain destroys that processor, so every open editor must be closed
    // FIRST or its next paint is a use-after-free.
    void closeAllPluginWindows() { pluginWindows.clear(); }
    juce::ComboBox pluginChainSelector, pluginSelector;
    juce::Label pluginHostMessage;
    juce::Array<juce::PluginDescription> knownHostPlugins;
    std::array<std::unique_ptr<WaveformDisplay>, MainPluginProcessor::numDecks> deckWaves;
    // Two-zone waveform (§2), shown for loaded decks in place of the flat WaveformDisplay.
    std::array<std::unique_ptr<stemdeck::WaveformRegion>, MainPluginProcessor::numDecks> deckWaveRegions;
    std::array<int, MainPluginProcessor::numDecks> waveDataState { { -1, -1, -1, -1 } };   // reload peaks on change
    void refreshDeckWaveData (int deckIndex);   // async peak extraction -> region
    static std::vector<float> extractPeaks (const juce::AudioBuffer<float>& buf, int bins);
    std::array<DeckControls, MainPluginProcessor::numDecks> deckControls;

    // ── Per-deck control strips (Row 1 always visible + Row 2 drawer) ────────────
    // Stacked full-width beneath the deck row. Wire live state each tick.
    std::array<std::unique_ptr<stemdeck::DeckStrip>, MainPluginProcessor::numDecks> deckStrips;
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckStripBounds {};
    std::array<int,    MainPluginProcessor::numDecks> stripJumpBars  { { 4, 4, 4, 4 } };
    std::array<double, MainPluginProcessor::numDecks> stripLoopBeats { { 4.0, 4.0, 4.0, 4.0 } };
    std::array<int,    MainPluginProcessor::numDecks> stripQuant     { { 3, 3, 3, 3 } };
    std::array<bool,   MainPluginProcessor::numDecks> stripCueHeld   { {} };
    void configureDeckStrip (int deckIndex);
    void refreshDeckStrips();
    bool deckHasContent (int deckIndex) const;   // has a loaded track (or showcase title)
    // Hero / standard / recessed rendering weight for a channel's ~40 controls.
    // Rendering only — hit targets and behaviour are unchanged.
    void applyDeckControlWeights (int deckIndex);

    // ── Focus model: one deck expands to full height, the rest collapse to strips ──
    int  focusedDeck = 0;
    void setFocusedDeck (int deckIndex);
    void layoutCollapsedDeck (int deckIndex, juce::Rectangle<int> strip);
    void setDeckDetailVisible (int deckIndex, bool visible);
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckCollapsedBounds {};
    std::array<int, MainPluginProcessor::numDecks> deckDimState { { -1, -1, -1, -1 } };
    std::array<float, MainPluginProcessor::numDecks> deckWidthCurrent {};
    std::array<float, MainPluginProcessor::numDecks> deckWidthTarget {};

    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckPanelBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckHeaderBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckStemBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckStemFxBounds {};
    // Segmented-control track and the well the active tab's panel sits in — painted
    // in paint() so the tabs read as one control over a recess (reference §3).
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckTabStripBounds {};
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckTabPanelBounds {};
    // Master Control Band — five bounded panels.
    juce::Rectangle<int> scopePanelBounds {};   // SCOPE
    juce::Rectangle<int> syncPanelBounds {};     // SYNC (stacked BPM/KEY pills)
    juce::Rectangle<int> xfPanelBounds {};       // CROSSFADER
    juce::Rectangle<int> masterFxPanelBounds {}; // MASTER FX (FILTER pad)
    juce::Rectangle<int> masterOutBounds {};    // MASTER OUTPUT panel (meters/gain/tempo)
    juce::Rectangle<int> masterMeterBounds {};  // stereo meter zone inside it
    juce::Rectangle<int> masterTempoBounds {};  // painted GLOBAL TEMPO box
    juce::Rectangle<int> masterLufsBounds {};   // live integrated LUFS + limiter GR readout
    std::array<float, 2> masterMeterHold { { 0.0f, 0.0f } };   // per-channel peak-hold (0..1)
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckMeterBounds {};  // console per-deck meters
    juce::TextButton keyLockAllButton { "Key Lock" };
    juce::TextButton aiMasterButton { "AI Master" };
    juce::ComboBox aiMasterMode;
    juce::Slider aiMasterAmount;   // depth of the auto-master (smartMasterAmount), was hardwired 0.8
    juce::TextButton rbxExportButton { "RBX Save" };
    juce::TextButton rbxImportButton { "RBX Load" };
    std::unique_ptr<juce::FileChooser> rbxChooser;
    RekordboxIO::CueMap importedCues;   // path → cues, persisted; applied on deck load
    std::array<juce::String, MainPluginProcessor::numDecks> lastCuePaths;
    void applyImportedCues (int deckIndex);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aiMasterAttachment;
    std::unique_ptr<SliderAttachment> aiMasterAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aiMasterModeAttachment;
    juce::Rectangle<int> headerBarBounds {};
    juce::Rectangle<int> tempoChipBounds {};
    MixerSection mixerPanel;
    XYPad masterFxPad;
    juce::Slider crossfader, masterBpm, master, limiter, micGain;
    juce::ComboBox crossfaderCurve;                 // Smooth / Linear / Sharp / Cut
    juce::TextButton crossfaderReverse { "REV" };   // hamster / reverse toggle
    juce::ToggleButton hostSync, beatSync, micMonitor, liveMidiButton;
    juce::TextButton tapTempoButton { "TAP" };
    stemdeck::ToolButton masterPlayButton { stemdeck::ToolIcon::play,  "PLAY ALL", juce::Colour (0xff2fd8f0) };   // header: play/stop ALL decks
    stemdeck::ToolButton topRecordButton  { stemdeck::ToolIcon::rec,   "REC",      juce::Colour (0xffff3b47) };   // header: arm/stop capturing
    stemdeck::ToolButton automixButton    { stemdeck::ToolIcon::magic, "AUTO",     juce::Colour (0xffe8c25c) };   // header: phrase-aligned auto-transition
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
    juce::TextButton launchAllButton { "LAUNCH" };   // trigger all clip slots at next beat (was cryptic ">>>")
    juce::TextButton playArrButton { "Play Arr" };
    juce::TextButton arrRecordButton { "Record" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    // The ARRANGER surface owns ~14 components (transport, edit tools, zoom, the
    // timeline itself) whose visibility follows the nav toggle rather than the
    // perform layout. Kept out of resized() so that stays about the perform screen.
    void layoutArrangerSurface (juce::Rectangle<int> bounds);
    // Enforces "one primary surface at a time": blanks the perform screen so an
    // overlay (ARRANGER / MIXER / piano roll) does not show decks through its gaps.
    void hidePerformSurface();
    void toggleArrangementPlayback();
    void saveProject();
    void loadProject();
    void createSessionSnapshot();
    void restoreLatestSnapshot();
    void repairMissingMedia();
    void showSessionHistory();
    void showDiagnostics();
    void showOnboarding (bool force = false);
    void showExportCenter();
    void exportSessionPackage();
    juce::String buildDiagnosticsReport() const;
    static juce::File onboardingMarkerFile();
    void checkpointSessionUndo (const juce::String& label);
    void undoSessionAction();
    void redoSessionAction();
    std::vector<juce::ValueTree> sessionUndoStack, sessionRedoStack;
    bool restoringSessionUndo = false;
    static juce::File sessionSnapshotsDirectory();
    juce::ValueTree makeProjectState() const;
    void restoreProjectState (const juce::ValueTree& root, const juce::String& sourceLabel);
    void scheduleRecoveryAutosave();
    void offerRecoveryAutosave();
    static juce::File recoveryAutosaveFile();
    static juce::File recoveryIntentFile();
    void openPianoRollForClip (int lane, int clip);
    juce::Label masterKeyLabel;
    juce::ComboBox masterKey;
    juce::TextButton syncAllDecksButton { "Sync All Decks" };
    juce::ToggleButton stemLinkToggle { "Link Stems" };
    std::unique_ptr<ComboBoxAttachment> masterKeyAttachment;
    std::unique_ptr<ButtonAttachment> stemLinkAttachment;
    bool stemLinkMirroring = false;
    juce::Label recordStatus;
    stemdeck::RecordingHealthBar recordingHealthBar;
    ArrangementRecorderView arrangementView;
    PianoRollView pianoRoll;
    juce::TextButton pianoRollButton { "Piano Roll" };
    MixerView mixerView;
    // Mix-timeline arrangement overlay (the ARRANGER surface) — draws a live
    // snapshot of the loaded decks: clips, stem-less waveforms, structure, xfader.
    ArrangementView mixArrangeView;
    stemdeck::EnergyArcArranger energyArc;   // ScenePilot energy-arc (top of the ARRANGER)
    int energyArcSig = 0;                     // rebuild the arc when the structure changes
    void refreshMixArrangement();   // rebuild the snapshot session from deck state
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
    std::unique_ptr<ComboBoxAttachment> crossfaderCurveAttachment;
    std::unique_ptr<ButtonAttachment> crossfaderReverseAttachment;
    std::unique_ptr<ButtonAttachment> hostSyncAttachment, beatSyncAttachment, micMonitorAttachment, liveMidiAttachment;

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
    // Four-workspace nav plus global, context-sensitive actions.
    std::array<juce::Rectangle<int>, 4> navRects {};
    void drawNavIcon (juce::Graphics&, int which, juce::Rectangle<float> box, juce::Colour) const;
    juce::TextButton commandButton { "Search" };
    juce::TextButton inspectorButton { "Inspect" };
    juce::TextButton focusModeButton { "Focus" };
    void showCommandPalette();
    void showQuickInspector();
    void performCommand (int commandId);
    void setFocusMode (bool enabled);
    bool focusMode = false;
    bool cpuPressureMode = false;
    int uiTimerTick = 0;
    // Advanced tools (plugin host, master key, AI master, rekordbox, project I/O,
    // limiter/mic/utility chips) are collapsed by default; the visible TOOLS button
    // in the header (plus title double-click) reveals them.
    bool advancedVisible = false;
    bool padsCollapsed = false;   // performance-pads strip collapsed when no pad is loaded
    stemdeck::PlayIconLookAndFeel playIconLnF;   // deck play/stop rendered as an icon
    juce::TextButton advancedToggle { "Tools" };
    // Channel add/remove (channel architecture). Adding/removing is zero-dropout —
    // the engine swaps an immutable graph snapshot (see GraphSnapshot.h).
    // Per-type strips for non-TrackDeck channels (Input / MIDI Instrument). Deck
    // channels keep their full stem panel; these render beside them (spec §6).
    std::array<std::unique_ptr<stemdeck::ChannelStrip>, MainPluginProcessor::getMaxChannels()> channelStrips;
    void refreshChannelStrips();
    // Shared "add channel" type picker. target=nullptr anchors to the in-layout tile.
    void showAddChannelMenu (juce::Component* target);
    // The always-visible "+" tile at the end of the channel row — makes the channel
    // model discoverable instead of hiding it behind a toolbar button.
    juce::Rectangle<int> addChannelTileBounds;

    juce::TextButton addChannelButton    { "+ Channel" };
    juce::TextButton removeChannelButton { "−" };
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
    // These are HIT TARGETS (mouseDown/mouseDrag/mouseWheelMove read them, and the
    // tempo panel and Camelot wheel anchor their popups to them), so they are
    // computed in the LAYOUT pass — see layoutDeckHeaderChips(). They used to be
    // assigned inside paint(), which meant a click could only land correctly if a
    // paint had already run at the current size: between a resize and the next
    // paint, every chip hit-test used stale geometry.
    void layoutDeckHeaderChips (int deckIndex);
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckBpmChipBounds {}, deckKeyChipBounds {};
    // Row 1 of the deck header: the track name owns the full column width. Row 2
    // carries the numerics + the NEURAL/DSP provenance chip.
    std::array<juce::Rectangle<int>, MainPluginProcessor::numDecks> deckNameBounds {}, deckSrcChipBounds {};
    int    bpmDragDeck = -1;
    int    bpmDragStartY = 0;
    double bpmDragStartVal = 120.0;
    bool   masterBpmDragging = false;   // drag on the GLOBAL TEMPO box
    void setDeckBpm (int deckIndex, double bpm);
    void showKeyMenuForDeck (int deckIndex);
    void syncBpmAll();   // warp all decks to the master deck's tempo (hotkey B)
    void syncKeyAll();   // harmonically match all decks to the master deck (hotkey K)
    juce::TextButton syncBpmButton { "BPM" };   // in the SYNC panel; header supplies "sync"
    juce::TextButton syncKeyButton { "KEY" };
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
    // .stemdeck project schema. v2 added the full processor state (params, channel
    // list, file assignments); v1 files carry only the arrangement + piano roll and
    // still load. Bump when a stored value's MEANING changes, and migrate on load.
    static constexpr int kProjectVersion = 2;

    void setupComboBox (juce::ComboBox& comboBox);
    // Enable/disable the +CH / -CH buttons at the pool limits and show the count.
    void refreshChannelButtons();
    void openDeckChooser (int deckIndex);
    void requestDeckLoad (int deckIndex, const juce::File& file);
    void requestDeckStemLoad (int deckIndex, int stemLaneIndex, const juce::File& file);
    void requestDeckClear (int deckIndex);
    void requestChannelRemoval (int channelIndex);
    void openDeckStemChooser (int deckIndex, int stemLaneIndex);
    void applyExtractedMelody (int deckIndex, const std::vector<MelodyExtractor::Note>& notes, bool neural = false);
    void applyExtractedDrums (int deckIndex, const std::vector<MelodyExtractor::Note>& hits);
    void openPadChooser (int padIndex);

    // Toolbox tab helpers
    void setupToolTabs (int deckIndex);
    void setDeckToolTab (int deckIndex, int tab);
    void refreshFxScopes();
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
    void showTempoPanel (int deckIndex);   // §5 unified tempo panel, opened from the BPM chip
    void refreshEnergyArc();               // Auto Director: build the arc from structure
    void applyPerStemKeyLock (int deckIndex, int semitones);   // §7 tier-2: tonal stems only, drums untouched
    // Retroactive MASTER tempo lane, normalised (beat 0..1, bpm). Edited in the tempo
    // panel; folded into the session (as beats) when the mix arrangement is built.
    std::vector<std::pair<float, double>> masterTempoLaneNorm;
    void showLibraryMeta (const juce::File& file);
    void updateMidiLearnUI();

    void toggleRecording();
    void requestRecording (int countInBars);
    void startRecordingNow (const juce::File& destination);
    bool recordingPreflight (const juce::File& destination);
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
    void showSattariFxMenu();   // curated Sattari-suite FX loader for the selected chain
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
    juce::File pendingRecordingFile;
    double pendingRecordingStartMs = 0.0;
    bool restoreMetronomeAfterCountIn = false;
    int healthRefreshTick = 0;
    int cachedDiskMinutes = -1;
    int autosaveTick = 0;
    bool recoveryDecisionMade = false;
    std::shared_ptr<std::atomic<bool>> autosaveInFlight {
        std::make_shared<std::atomic<bool>> (false)
    };
    // Own every editor background task. Detached workers could outlive both the
    // editor and processor during plugin-window teardown.
    juce::ThreadPool uiWorkPool { 2 };
    juce::ThreadPool autosavePool { 1 };
    // Activates every setTooltip() in the app (they are inert without a window).
    juce::TooltipWindow tooltipWindow { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPluginEditor)
};
