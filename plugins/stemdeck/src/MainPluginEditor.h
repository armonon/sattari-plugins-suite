#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariPresetManager.h>
#include "DeckPlayer.h"
#include "MainPluginProcessor.h"
#include "MixerSection.h"
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
    };

    struct PadControls
    {
        juce::TextButton trigger;
        juce::TextButton load { "load" };
        juce::Slider gain;
        std::unique_ptr<SliderAttachment> gainAttachment;
    };

    class ArrangementRecorderView final : public juce::Component,
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

        struct LaneClip
        {
            juce::File file;
            double durationSeconds = 0.0;
            double trimStartSeconds = 0.0;
            double trimEndSeconds = 0.0;
            double fadeInSeconds = 0.0;
            double fadeOutSeconds = 0.0;
            bool selected = false;
        };

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
        const std::array<LaneClip, numEditableLanes>& getClips() const noexcept { return clips; }
        int getSelectedLane() const noexcept { return selectedLane; }
        void updateClipAfterBounce (int laneIndex, const juce::File& newFile, double newDuration);
        void resetSelectedClipEdits();
        void resetClipEdits (int laneIndex);
        bool copyClip (int sourceLane, int destLane);
        void setSnap (bool enabled, double beatGrid, double bpm);

        // Undo / redo. Callers should snapshot via captureLaneState() before any edit,
        // then call commitEdit() after the edit completes to push it onto the stack.
        struct LaneSnapshot { int lane = -1; LaneClip state; };
        LaneSnapshot captureLaneState (int laneIndex) const;
        void commitEdit (LaneSnapshot before);
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }
        void undo();
        void redo();

        std::function<void(int)> onClearDeckRequest;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

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
        juce::File masterFile;
        std::array<juce::File, numLanes> deckFiles {};
        std::array<LaneClip, numEditableLanes> clips {};
        std::array<juce::Rectangle<int>, numEditableLanes> laneRects {};
        std::array<DeckAutomation, numLanes> deckAutomation;
        int selectedLane = -1;

        enum class Handle { None, Body, TrimStart, TrimEnd, FadeIn, FadeOut };
        struct HitTest { int lane = -1; Handle handle = Handle::None; };
        HitTest hitTestAt (juce::Point<int> p) const;
        HitTest activeDrag;
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

        std::vector<LaneSnapshot> undoStack;
        std::vector<LaneSnapshot> redoStack;
    };

    MainPluginProcessor& processorRef;

    // Shared suite UI + cross-plugin preset system.
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("stemdeck") };
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
    MixerSection mixerPanel;
    juce::Slider crossfader, masterBpm, master, limiter, micGain;
    juce::ToggleButton hostSync, beatSync, micMonitor;
    juce::TextButton recordButton { "Record WAV" };
    juce::TextButton revealRecordingButton { "Reveal" };
    juce::TextButton reloadIntoDecksButton { "Reload to Decks" };
    juce::TextButton bounceEditsButton { "Bounce Edits" };
    juce::TextButton snapToggleButton { "Snap" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::Label masterKeyLabel;
    juce::ComboBox masterKey;
    juce::TextButton syncAllDecksButton { "Sync All Decks" };
    juce::ToggleButton stemLinkToggle { "Link Stems" };
    std::unique_ptr<ComboBoxAttachment> masterKeyAttachment;
    std::unique_ptr<ButtonAttachment> stemLinkAttachment;
    bool stemLinkMirroring = false;
    juce::Label recordStatus;
    ArrangementRecorderView arrangementView;
    std::array<PadControls, PadBank::numPads> pads;

    std::unique_ptr<SliderAttachment> crossfaderAttachment, masterBpmAttachment, masterAttachment, limiterAttachment, micGainAttachment;
    std::unique_ptr<ButtonAttachment> hostSyncAttachment, beatSyncAttachment, micMonitorAttachment;

    juce::FileChooser deckChooser { "Load a deck track" };
    juce::FileChooser padChooser { "Load a one-shot sample" };
    juce::FileChooser recordChooser { "Save StemDeck master recording", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("StemDeck-master.wav"), "*.wav" };
    std::array<juce::String, MainPluginProcessor::numDecks> deckPaths;

    juce::TimeSliceThread browserThread { "StemDeck file browser" };
    juce::WildcardFileFilter browserFilter { "*.wav;*.aif;*.aiff;*.flac;*.mp3", "*", "Audio files" };
    juce::DirectoryContentsList browserContents { &browserFilter, browserThread };
    std::unique_ptr<juce::FileTreeComponent> fileBrowser;
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
    void toggleRecording();
    void revealLastRecording();
    void reloadLastRecordingIntoDecks();
    void loadLastArrangementIntoView (juce::String statusLine);
    void syncAllDecksTo (int sourceDeckIndex);
    void showSyncAllDecksMenu();
    void setupFileBrowser();
    void toggleFileBrowser();
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
