#pragma once

#include <JuceHeader.h>
#include "TrackAnalysis.h"

/**
 * Music library panel — a sortable, searchable track table with BPM / key
 * columns and harmonic (Camelot) codes, plus folder "crates".
 *
 * Heavy lifting reuses the suite's TrackAnalyzer (BPM + key detection), run on
 * a background pool so the UI never blocks. Results are cached to disk keyed by
 * path + modification time, so re-opening a folder is instant.
 *
 * Double-clicking a row fires onLoadFile, which the editor routes into the same
 * "load into next empty deck" path the file browser uses.
 */
class MusicLibraryView final : public juce::Component,
                               private juce::TableListBoxModel
{
public:
    explicit MusicLibraryView (juce::AudioFormatManager& formatManager);
    ~MusicLibraryView() override;

    std::function<void(const juce::File&)> onLoadFile;
    std::function<void(const std::array<juce::File, 4>&)> onLoadSet;   // AI set builder → decks A-D

    void addFolderInteractive();             // opens a chooser, then scans
    void addFolder (const juce::File& folder, bool persist = true);
    void restorePersistedCrates();           // reload saved crates on launch

    // Generative set builder: parse BPM / key / mood from a free-text prompt and
    // rank the analyzed library by BPM proximity + Camelot harmony + mood bias.
    void buildSetFromPrompt (const juce::String& prompt);

    // Read access for Rekordbox export (analyzed tracks only).
    struct ExportEntry { juce::File file; float bpm = 0.0f; int keyPitchClass = -1; bool keyMinor = false; };
    std::vector<ExportEntry> getAnalyzedEntries() const;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    enum ColumnId { colIndex = 1, colTitle, colBpm, colKey, colLength };

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
    };

    // TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int width, int height, bool selected) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;

    void rebuildView();
    void applyAnalysis (const juce::File& file, const TrackAnalysis& analysis, double lengthSeconds);
    void scanFolder (const juce::File& folder);
    void enqueueAnalysis (const juce::File& file);

    static juce::String camelotCode (int pitchClass, bool minor);
    static juce::Colour camelotColour (int pitchClass, bool minor);

    juce::File cacheFile()  const;
    juce::File cratesFile() const;
    void loadCache();
    void saveCacheSoon();
    void loadCrateList();
    void saveCrateList();

    juce::AudioFormatManager& formatManager;

    juce::TableListBox table { "library", this };
    juce::TextEditor    searchBox;
    juce::TextEditor    promptBox;                       // AI set builder prompt
    juce::TextButton    buildSetButton { "Build Set" };
    juce::TextButton    addFolderButton { "+ Folder" };
    juce::TextButton    clearButton     { "Clear" };
    juce::Label         countLabel;

    std::vector<Entry> entries;
    std::vector<int>   view;            // filtered + sorted indices into entries
    juce::StringArray  crateFolders;

    juce::String filterText;
    int  sortColumnId = colTitle;
    bool sortForwards = true;

    juce::ThreadPool analysisPool { 2 };
    std::atomic<int> analysesInFlight { 0 };

    juce::DynamicObject::Ptr cache;     // path -> {bpm,key,minor,conf,len,mod}
    std::atomic<bool> cacheDirty { false };

    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicLibraryView)
};
