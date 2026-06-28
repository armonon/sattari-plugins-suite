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
#include "StemDeckLookAndFeel.h"
#include "WaveformDisplay.h"

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

        // ── Beat jump (back: bars[3]=32 [2]=16 [1]=8 [0]=4, fwd mirror) ──────
        std::array<juce::TextButton, 4> beatJumpBack;
        std::array<juce::TextButton, 4> beatJumpFwd;

        // ── Built-in FX ───────────────────────────────────────────────────────
        juce::Slider reverbMix, echoMix;
        std::unique_ptr<SliderAttachment> reverbMixAttachment, echoMixAttachment;

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
                              std::array<std::vector<MidiClipData>, numEditableLanes> allMidi; };
        LaneSnapshot captureLaneState (int laneIndex) const;
        LaneSnapshot captureFullState() const;   // whole arrangement (for cross-lane moves)
        void commitEdit (LaneSnapshot before);
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }
        void undo();
        void redo();

        std::function<void(int)> onClearDeckRequest;
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
        bool  isLaneMuted  (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) && laneMuted[(size_t) lane]; }
        bool  isLaneSoloed (int lane) const noexcept { return juce::isPositiveAndBelow (lane, numEditableLanes) && laneSoloed[(size_t) lane]; }
        void  toggleLaneMute (int lane) { if (juce::isPositiveAndBelow (lane, numEditableLanes)) { laneMuted[(size_t) lane]  = ! laneMuted[(size_t) lane];  repaint(); } }
        void  toggleLaneSolo (int lane) { if (juce::isPositiveAndBelow (lane, numEditableLanes)) { laneSoloed[(size_t) lane] = ! laneSoloed[(size_t) lane]; repaint(); } }

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
    juce::Rectangle<int> recordModuleBounds {};
    juce::Rectangle<int> headerBarBounds {};
    juce::Rectangle<int> tempoChipBounds {};
    MixerSection mixerPanel;
    juce::Slider crossfader, masterBpm, master, limiter, micGain;
    juce::ToggleButton hostSync, beatSync, micMonitor;
    juce::TextButton recordButton { "Record WAV" };
    juce::TextButton tapTempoButton { "TAP" };
    juce::Label tapTempoLabel;
    juce::TextButton camelotButton { "Key" };
    juce::TextButton midiLearnButton { "MIDI" };
    juce::Label libraryMetaLabel;   // shows BPM / key / duration for hovered file
    juce::TextButton revealRecordingButton { "Reveal" };
    juce::TextButton reloadIntoDecksButton { "Reload to Decks" };
    juce::TextButton bounceEditsButton { "Bounce Edits" };
    juce::TextButton snapToggleButton { "Snap" };
    juce::TextButton razorButton { "Razor" };
    juce::TextButton arrZoomOutButton { "-" }, arrZoomInButton { "+" }, arrZoomFitButton { "Fit" };
    juce::TextButton arrLoopButton { "Loop" };
    juce::TextButton playArrButton { "Play Arr" };
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
    double arrangementCursorSeconds = 0.0;   // play-start cursor set by clicking the ruler
    int    pianoRollLane = -1;                // which lane/clip the piano roll edits
    int    pianoRollClip = -1;
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

    juce::TimeSliceThread browserThread { "StemDeck file browser" };
    juce::WildcardFileFilter browserFilter { "*.wav;*.aif;*.aiff;*.flac;*.mp3", "*", "Audio files" };
    juce::DirectoryContentsList browserContents { &browserFilter, browserThread };
    std::unique_ptr<juce::FileTreeComponent> fileBrowser;
    std::unique_ptr<MusicLibraryView> libraryView;
    juce::TextButton toggleLibraryButton { "Library" };
    bool libraryVisible = false;
    juce::TextButton toggleBrowserButton { "Files" };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPluginEditor)
};
