#pragma once

#include <JuceHeader.h>
#include "TrackAnalysis.h"
#include "DesignTokens.h"
#include "StemDeckDragPayload.h"

/**
 * Music library panel — a sortable, searchable track table with BPM / key
 * columns and harmonic (Camelot) codes, plus folder "crates".
 *
 * Heavy lifting reuses the suite's TrackAnalyzer (BPM + key detection), run on
 * a background pool so the UI never blocks. Results are cached to disk keyed by
 * path + modification time, so re-opening a folder is instant.
 *
 * Double-clicking auditions a row. Explicit Load / set-builder actions route into
 * the editor's deck-loading path so browsing never changes the live set by accident.
 */
class MusicLibraryView final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::TableListBoxModel,
                               private juce::Timer
{
public:
    struct PreviewState
    {
        juce::File file;
        bool loading = false;
        bool playing = false;
        bool failed = false;
        bool routedToDedicatedCue = false;
        double positionSeconds = 0.0;
        double durationSeconds = 0.0;
    };

    explicit MusicLibraryView (juce::AudioFormatManager& formatManager);
    ~MusicLibraryView() override;

    std::function<void(const juce::File&)> onLoadFile;
    std::function<void(const std::array<juce::File, 4>&)> onLoadSet;   // confirmed set proposal → decks A-D
    std::function<void(const juce::File&)> onTogglePreview;
    std::function<void()> onStopPreview;
    std::function<void(double)> onSeekPreviewSeconds;
    std::function<PreviewState()> getPreviewState;

    void addFolderInteractive();             // opens a chooser, then scans
    void addFolder (const juce::File& folder, bool persist = true);
    void addFiles (const juce::Array<juce::File>& files);
    void restorePersistedCrates();           // reload saved crates on launch

    // Generative set builder: parse BPM / key / mood from a free-text prompt and
    // rank the analyzed library by BPM proximity + Camelot harmony + mood bias.
    void buildSetFromPrompt (const juce::String& prompt);
    // Build a playable sequence from the current filtered library. The selected
    // track is used as the anchor when possible; otherwise the lowest normalized
    // tempo starts the route. The first four picks become a protected A-D proposal.
    void arrangeByBpmAndKey();

    // Read access for Rekordbox export (analyzed tracks only).
    struct ExportEntry { juce::File file; float bpm = 0.0f; int keyPitchClass = -1; bool keyMinor = false; };
    std::vector<ExportEntry> getAnalyzedEntries() const;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum ColumnId { colIndex = 1, colTitle, colBpm, colKey, colLength, colStatus };

    struct Entry
    {
        juce::File   file;
        juce::String title;
        double       lengthSeconds = 0.0;
        float        bpm = 0.0f;
        int          keyPitchClass = -1;
        bool         keyMinor = false;
        float        keyConfidence = 0.0f;
        bool         analyzed = false;
        juce::int64  lastLoadedMs = 0;
        bool         favourite = false;
        int          rating = 0;
        juce::String tags;
    };

    // TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int width, int height, bool selected) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void timerCallback() override;

    void rebuildView();
    void applyAnalysis (const juce::File& file, const TrackAnalysis& analysis, double lengthSeconds);
    void scanFolder (const juce::File& folder);
    void importDiscoveredFiles (std::shared_ptr<juce::Array<juce::File>> files,
                                int startIndex);
    bool addAudioFile (const juce::File& file);
    void enqueueAnalysis (const juce::File& file);

    static juce::String camelotCode (int pitchClass, bool minor);
    static juce::Colour camelotColour (int pitchClass, bool minor);

    juce::File cacheFile()  const;
    juce::File cratesFile() const;
    void loadCache();
    void saveCacheSoon();
    void loadCrateList();
    void saveCrateList();
    void refreshCrateSelector();
    void removeSelectedCrate();
    juce::File selectedFile() const;
    void toggleSelectedPreview();
    void loadProposedSet();
    void refreshPreparationControls();
    void saveSelectedPreparationMetadata();

    juce::AudioFormatManager& formatManager;

    juce::TableListBox table { "library", this };
    juce::TextEditor    searchBox;
    juce::TextEditor    promptBox;                       // AI set builder prompt
    juce::TextButton    buildSetButton { "Build Set" };
    juce::TextButton    bpmKeyArrangeButton { "BPM / Key" };
    juce::TextButton    loadSetButton { "Load A-D" };
    juce::TextButton    addFolderButton { "+ Folder" };
    juce::TextButton    removeFolderButton { "Remove" };
    juce::TextButton    clearButton     { "Clean" };
    juce::TextButton    loadButton      { "Load Selected" };
    juce::TextButton    previewButton   { "Preview" };
    juce::TextButton    stopPreviewButton { "Stop" };
    juce::Slider        previewPosition;
    juce::Label         previewTitle;
    juce::Label         previewTime;
    juce::ComboBox      crateSelector;
    juce::ComboBox      smartFilter;
    juce::Label         countLabel;
    juce::ToggleButton  favouriteButton { "Favourite" };
    juce::ComboBox      ratingBox;
    juce::TextEditor    tagsBox;

    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> tableBounds;

    std::vector<Entry> entries;
    std::vector<int>   view;            // filtered + sorted indices into entries
    juce::StringArray  crateFolders;

    juce::String filterText;
    int smartFilterId = 1;
    int  sortColumnId = colTitle;
    bool sortForwards = true;
    bool setProposalActive = false;
    int proposedSetSize = 0;
    bool updatingPreparationUi = false;

    juce::ThreadPool analysisPool { 2 };
    std::atomic<int> analysesInFlight { 0 };
    std::atomic<int> folderScansInFlight { 0 };

    juce::DynamicObject::Ptr cache;     // path -> {bpm,key,minor,conf,len,mod}
    std::atomic<bool> cacheDirty { false };

    std::unique_ptr<juce::FileChooser> folderChooser;
    bool updatingPreviewUi = false;
    bool fileDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicLibraryView)
};
