#include "MusicLibraryView.h"
#include <sattari_audio_core/SattariBranding.h>

namespace
{
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

juce::File appDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Sattari").getChildFile ("StemDeck");
    dir.createDirectory();
    return dir;
}

juce::String formatLength (double seconds)
{
    if (seconds <= 0.0) return "--:--";
    const int total = (int) std::round (seconds);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

// Camelot numbers indexed by pitch class (0=C .. 11=B)
const int kMajorCamelot[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
const int kMinorCamelot[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
}

MusicLibraryView::MusicLibraryView (juce::AudioFormatManager& fm, bool persistQueue)
    : formatManager (fm), queuePersistenceEnabled (persistQueue)
{
    using namespace stemdeck;
    cache = new juce::DynamicObject();

    table.setColour (juce::TableListBox::backgroundColourId, tokens::c (tokens::bgWell));
    table.setColour (juce::TableListBox::outlineColourId, tokens::c (tokens::borderSubtle));
    table.setMultipleSelectionEnabled (true);
    table.setRowHeight (30);
    table.setHeaderHeight (30);
    auto& header = table.getHeader();
    header.addColumn ("#",      colIndex,  34,  28,  48,  juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn ("Title",  colTitle,  150, 100, 600, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("BPM",    colBpm,     52,  44,  80, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Key",    colKey,     65,  54, 100, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Length", colLength,  58,  50,  90, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Status", colStatus,  72,  64, 120, juce::TableHeaderComponent::defaultFlags);
    header.setSortColumnId (colTitle, true);
    addAndMakeVisible (table);

    searchBox.setTextToShowWhenEmpty (u8 ("Search title, BPM, key\xe2\x80\xa6"), tokens::c (tokens::fgMuted));
    searchBox.setColour (juce::TextEditor::backgroundColourId, tokens::c (tokens::bgControl));
    searchBox.setColour (juce::TextEditor::textColourId, tokens::c (tokens::fgPrimary));
    searchBox.onTextChange = [this]
    {
        filterText = searchBox.getText().trim().toLowerCase();
        rebuildView();
    };
    addAndMakeVisible (searchBox);

    smartFilter.addItem ("All Tracks", 1);
    smartFilter.addItem ("Ready", 2);
    smartFilter.addItem ("Analyzing", 3);
    smartFilter.addItem ("Missing", 4);
    smartFilter.addItem ("Played Recently", 5);
    smartFilter.addItem ("Minor Keys", 6);
    smartFilter.addItem ("Major Keys", 7);
    smartFilter.addItem ("60-100 BPM", 8);
    smartFilter.addItem ("100-130 BPM", 9);
    smartFilter.addItem ("130+ BPM", 10);
    smartFilter.addItem ("Favourites", 11);
    smartFilter.addItem ("4-5 Stars", 12);
    smartFilter.addItem ("Possible Duplicates", 13);
    smartFilter.addItem ("Needs Prep", 14);
    smartFilter.setSelectedId (1, juce::dontSendNotification);
    smartFilter.onChange = [this]
    {
        smartFilterId = smartFilter.getSelectedId();
        rebuildView();
    };
    addAndMakeVisible (smartFilter);

    crateSelector.addItem ("All Libraries", 1);
    crateSelector.setSelectedId (1, juce::dontSendNotification);
    crateSelector.setTooltip ("Show every added music folder or focus one library");
    crateSelector.onChange = [this]
    {
        removeFolderButton.setEnabled (crateSelector.getSelectedId() > 1);
        rebuildView();
    };
    addAndMakeVisible (crateSelector);

    promptBox.setTextToShowWhenEmpty (u8 ("Describe a set: \"dark, 124 BPM, A minor\"\xe2\x80\xa6"),
                                      tokens::c (tokens::fgMuted));
    promptBox.setColour (juce::TextEditor::backgroundColourId, tokens::c (tokens::bgControl));
    promptBox.setColour (juce::TextEditor::textColourId, tokens::c (tokens::fgPrimary));
    promptBox.onReturnKey = [this] { buildSetFromPrompt (promptBox.getText()); };
    addAndMakeVisible (promptBox);

    buildSetButton.onClick = [this] { buildSetFromPrompt (promptBox.getText()); };
    addAndMakeVisible (buildSetButton);

    bpmKeyArrangeButton.setTooltip (
        "Stage a four-deck proposal ordered by tempo and compatible Camelot keys");
    bpmKeyArrangeButton.onClick = [this] { arrangeByBpmAndKey(); };
    addAndMakeVisible (bpmKeyArrangeButton);

    loadSetButton.setEnabled (false);
    loadSetButton.setTooltip ("Load the staged A-D proposal after deck-safety checks");
    loadSetButton.onClick = [this] { loadProposedSet(); };
    addAndMakeVisible (loadSetButton);

    addFolderButton.setButtonText ("+ Library");
    addFolderButton.setTooltip ("Add a folder of WAV, AIFF, FLAC, or MP3 files");
    addFolderButton.onClick = [this] { addFolderInteractive(); };
    addAndMakeVisible (addFolderButton);

    removeFolderButton.setEnabled (false);
    removeFolderButton.setTooltip ("Forget this library folder without deleting any audio files");
    removeFolderButton.onClick = [this] { removeSelectedCrate(); };
    addAndMakeVisible (removeFolderButton);

    clearButton.onClick = [this]
    {
        const auto before = entries.size();
        std::erase_if (entries, [] (const Entry& entry)
        {
            return ! entry.file.existsAsFile();
        });
        rebuildView();
        countLabel.setText (
            before == entries.size()
                ? u8 ("Library is clean  \xe2\x80\xa2  no missing entries")
                : "Removed " + juce::String ((int) (before - entries.size()))
                    + " missing entries",
            juce::dontSendNotification);
    };
    addAndMakeVisible (clearButton);

    loadButton.setEnabled (false);
    loadButton.onClick = [this]
    {
        const auto row = table.getSelectedRow();
        if (juce::isPositiveAndBelow (row, (int) view.size()))
        {
            auto& entry = entries[(size_t) view[(size_t) row]];
            if (entry.file.existsAsFile() && onLoadFile)
            {
                entry.lastLoadedMs = juce::Time::currentTimeMillis();
                if (auto* object = cache->getProperty (
                        entry.file.getFullPathName()).getDynamicObject())
                    object->setProperty ("last", (double) entry.lastLoadedMs);
                cacheDirty.store (true);
                if (onStopPreview) onStopPreview();
                onLoadFile (entry.file);
                if (setProposalActive) table.repaint();
                else rebuildView();
            }
        }
    };
    addAndMakeVisible (loadButton);

    queueButton.setEnabled (false);
    queueButton.setTooltip ("Add the selected track to Up Next without changing a deck");
    queueButton.onClick = [this] { enqueueSelected(); };
    addAndMakeVisible (queueButton);

    previewButton.setEnabled (false);
    previewButton.setTooltip ("Audition the selected track without loading a deck");
    previewButton.onClick = [this] { toggleSelectedPreview(); };
    addAndMakeVisible (previewButton);

    stopPreviewButton.setEnabled (false);
    stopPreviewButton.setTooltip ("Stop the library audition and return to its start");
    stopPreviewButton.onClick = [this]
    {
        if (onStopPreview) onStopPreview();
    };
    addAndMakeVisible (stopPreviewButton);

    previewPosition.setSliderStyle (juce::Slider::LinearHorizontal);
    previewPosition.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewPosition.setRange (0.0, 1.0, 0.001);
    previewPosition.setColour (juce::Slider::trackColourId, tokens::c (tokens::deckA));
    previewPosition.setColour (juce::Slider::backgroundColourId, tokens::c (tokens::bgControl));
    previewPosition.setColour (juce::Slider::thumbColourId, tokens::c (tokens::fgPrimary));
    previewPosition.onDragEnd = [this]
    {
        if (! updatingPreviewUi && onSeekPreviewSeconds)
            onSeekPreviewSeconds (previewPosition.getValue());
    };
    addAndMakeVisible (previewPosition);

    previewTitle.setText (u8 ("Select a track  \xe2\x80\xa2  Preview or double-click to audition"),
                          juce::dontSendNotification);
    previewTitle.setFont (juce::FontOptions (10.0f));
    previewTitle.setColour (juce::Label::textColourId, tokens::c (tokens::fgTertiary));
    previewTitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (previewTitle);

    previewTime.setText ("--:-- / --:--", juce::dontSendNotification);
    previewTime.setFont (juce::FontOptions (9.5f));
    previewTime.setColour (juce::Label::textColourId, tokens::c (tokens::fgMuted));
    previewTime.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (previewTime);

    countLabel.setFont (juce::FontOptions (10.0f));
    countLabel.setColour (juce::Label::textColourId, tokens::c (tokens::fgTertiary));
    addAndMakeVisible (countLabel);

    favouriteButton.setTooltip ("Mark the selected track as a favourite");
    favouriteButton.onClick = [this] { saveSelectedPreparationMetadata(); };
    addAndMakeVisible (favouriteButton);

    ratingBox.addItem ("Unrated", 1);
    for (int stars = 1; stars <= 5; ++stars)
        ratingBox.addItem (juce::String::repeatedString (u8 ("\xe2\x98\x85"), stars), stars + 1);
    ratingBox.setTooltip ("Preparation rating for the selected track");
    ratingBox.onChange = [this] { saveSelectedPreparationMetadata(); };
    addAndMakeVisible (ratingBox);

    tagsBox.setTextToShowWhenEmpty ("Tags / notes", tokens::c (tokens::fgMuted));
    tagsBox.setTooltip ("Searchable preparation tags, energy notes, or transition ideas");
    tagsBox.onReturnKey = [this] { saveSelectedPreparationMetadata(); };
    tagsBox.onFocusLost = [this] { saveSelectedPreparationMetadata(); };
    addAndMakeVisible (tagsBox);

    queueTitle.setText ("UP NEXT", juce::dontSendNotification);
    queueTitle.setFont (tokens::section (13.0f));
    queueTitle.setColour (juce::Label::textColourId, tokens::c (tokens::fgPrimary));
    queueTitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (queueTitle);

    queueCount.setFont (tokens::technical (9.0f, true));
    queueCount.setColour (juce::Label::textColourId, tokens::c (tokens::deckA));
    queueCount.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (queueCount);

    queueDisclosureButton.setTooltip ("Show or hide the Up Next song queue");
    queueDisclosureButton.onClick = [this]
    {
        compactQueueExpanded = ! compactQueueExpanded;
        resized();
        repaint();
    };
    addAndMakeVisible (queueDisclosureButton);

    queueList.setRowHeight (38);
    queueList.setColour (juce::ListBox::backgroundColourId, tokens::c (tokens::bgWell));
    queueList.setColour (juce::ListBox::outlineColourId, tokens::c (tokens::borderSubtle));
    queueList.setOutlineThickness (1);
    queueList.setMultipleSelectionEnabled (false);
    addAndMakeVisible (queueList);

    queueEmptyLabel.setText ("DROP TRACKS HERE\nBuild the next few moves without leaving Sets",
                             juce::dontSendNotification);
    queueEmptyLabel.setFont (tokens::technical (9.0f, true));
    queueEmptyLabel.setColour (juce::Label::textColourId, tokens::c (tokens::fgMuted));
    queueEmptyLabel.setJustificationType (juce::Justification::centred);
    queueEmptyLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (queueEmptyLabel);

    loadNextButton.setTooltip ("Load the first Up Next track using normal deck-safety checks");
    loadNextButton.onClick = [this] { loadNextQueued(); };
    addAndMakeVisible (loadNextButton);
    queuePreviewButton.setTooltip ("Audition the selected queued track");
    queuePreviewButton.onClick = [this] { previewQueuedSelection(); };
    addAndMakeVisible (queuePreviewButton);
    queueUpButton.onClick = [this]
    {
        const int row = queueList.getSelectedRow();
        if (moveQueuedTrack (row, row - 1)) queueList.selectRow (row - 1);
    };
    queueDownButton.onClick = [this]
    {
        const int row = queueList.getSelectedRow();
        if (moveQueuedTrack (row, row + 1)) queueList.selectRow (row + 1);
    };
    queueRemoveButton.onClick = [this]
    {
        const int row = queueList.getSelectedRow();
        if (removeQueuedTrack (row) && ! playQueue.empty())
            queueList.selectRow (juce::jmin (row, (int) playQueue.size() - 1));
    };
    queueClearButton.onClick = [this] { clearQueue(); };

    static constexpr std::array<juce::uint32, 4> deckColours {
        tokens::deckA, tokens::deckB, tokens::deckC, tokens::deckD
    };
    for (int deck = 0; deck < 4; ++deck)
    {
        auto& button = deckLoadButtons[(size_t) deck];
        button.setButtonText (juce::String::charToString ((juce_wchar) ('A' + deck)));
        button.setTooltip ("Load the selected track into Deck "
                           + juce::String::charToString ((juce_wchar) ('A' + deck)));
        button.getProperties().set ("padColour", (int) deckColours[(size_t) deck]);
        button.setColour (juce::TextButton::buttonOnColourId,
                          tokens::c (deckColours[(size_t) deck]));
        button.setEnabled (false);
        button.onClick = [this, deck]
        {
            const auto file = selectedFile();
            if (file.existsAsFile() && onLoadDeck) onLoadDeck (file, deck);
        };
        addAndMakeVisible (button);
    }
    addAndMakeVisible (queueUpButton);
    addAndMakeVisible (queueDownButton);
    addAndMakeVisible (queueRemoveButton);
    addAndMakeVisible (queueClearButton);

    refreshPreparationControls();

    loadCache();
    loadQueue();
    refreshQueueControls();
    startTimerHz (20);
}

MusicLibraryView::~MusicLibraryView()
{
    stopTimer();
    analysisPool.removeAllJobs (true, 2000);
    if (cacheDirty.load())
        saveCacheSoon();
}

int MusicLibraryView::QueueListModel::getNumRows()
{
    return (int) owner.playQueue.size();
}

void MusicLibraryView::QueueListModel::paintListBoxItem (int row, juce::Graphics& g,
                                                          int width, int height,
                                                          bool selected)
{
    using namespace stemdeck;
    if (! juce::isPositiveAndBelow (row, (int) owner.playQueue.size())) return;
    const auto& file = owner.playQueue[(size_t) row];

    if (selected)
    {
        g.setColour (tokens::c (tokens::selected).withAlpha (0.18f));
        g.fillRect (0, 0, width, height);
    }
    else if (row % 2 != 0)
    {
        g.setColour (tokens::c (tokens::bgPanel2).withAlpha (0.35f));
        g.fillRect (0, 0, width, height);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (7, 3);
    auto number = area.removeFromLeft (26);
    g.setColour (row == 0 ? tokens::c (tokens::deckA) : tokens::c (tokens::fgMuted));
    g.setFont (tokens::technical (9.5f, true));
    g.drawText (juce::String (row + 1), number, juce::Justification::centredLeft);

    juce::String detail;
    for (const auto& entry : owner.entries)
        if (entry.file == file)
        {
            if (entry.bpm > 0.0f) detail << juce::String (entry.bpm, 1) << " BPM";
            const auto key = owner.camelotCode (entry.keyPitchClass, entry.keyMinor);
            if (key.isNotEmpty()) detail << (detail.isEmpty() ? "" : "  ") << key;
            break;
        }

    g.setColour (file.existsAsFile() ? tokens::c (tokens::fgPrimary)
                                     : tokens::c (tokens::meterHot));
    g.setFont (tokens::technical (10.0f, true));
    g.drawFittedText (file.getFileNameWithoutExtension(), area.removeFromTop (18),
                      juce::Justification::centredLeft, 1);
    g.setColour (tokens::c (tokens::fgMuted));
    g.setFont (tokens::technical (8.5f));
    g.drawText (file.existsAsFile() ? (detail.isNotEmpty() ? detail : "Ready to load") : "Missing file",
                area, juce::Justification::centredLeft);
}

void MusicLibraryView::QueueListModel::selectedRowsChanged (int)
{
    owner.refreshQueueControls();
}

void MusicLibraryView::QueueListModel::listBoxItemDoubleClicked (int row,
                                                                 const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) owner.playQueue.size()))
    {
        owner.queueList.selectRow (row);
        owner.previewQueuedSelection();
    }
}

juce::var MusicLibraryView::QueueListModel::getDragSourceDescription (
    const juce::SparseSet<int>& rows)
{
    juce::Array<juce::File> files;
    for (int i = 0; i < rows.size(); ++i)
        if (juce::isPositiveAndBelow (rows[i], (int) owner.playQueue.size()))
            files.add (owner.playQueue[(size_t) rows[i]]);
    return files.isEmpty() ? juce::var() : stemdeck::drag::makeAudioFiles (files);
}

bool MusicLibraryView::enqueueFile (const juce::File& file)
{
    if (! stemdeck::drag::isSupportedAudioFile (file)) return false;
    playQueue.push_back (file);
    queueList.updateContent();
    queueList.selectRow ((int) playQueue.size() - 1);
    saveQueue();
    refreshQueueControls();
    repaint (queueBounds);
    return true;
}

juce::File MusicLibraryView::getQueuedFile (int index) const
{
    return juce::isPositiveAndBelow (index, (int) playQueue.size())
        ? playQueue[(size_t) index] : juce::File();
}

bool MusicLibraryView::loadNextQueued()
{
    if (queueLoadPending) return false;
    while (! playQueue.empty() && ! playQueue.front().existsAsFile())
        playQueue.erase (playQueue.begin());
    if (playQueue.empty())
    {
        saveQueue();
        queueList.updateContent();
        refreshQueueControls();
        return false;
    }

    const auto file = playQueue.front();
    auto completion = [safe = juce::Component::SafePointer<MusicLibraryView> (this), file]
                      (bool loaded)
    {
        if (safe == nullptr) return;
        safe->queueLoadPending = false;
        if (loaded && ! safe->playQueue.empty() && safe->playQueue.front() == file)
        {
            safe->markFileLoaded (file);
            safe->playQueue.erase (safe->playQueue.begin());
            safe->saveQueue();
            safe->queueList.updateContent();
            if (! safe->playQueue.empty()) safe->queueList.selectRow (0);
        }
        safe->refreshQueueControls();
        safe->repaint (safe->queueBounds);
    };

    queueLoadPending = true;
    refreshQueueControls();
    if (onStopPreview) onStopPreview();
    if (onLoadQueuedFile)
        onLoadQueuedFile (file, std::move (completion));
    else if (onLoadFile)
    {
        onLoadFile (file);
        completion (true);
    }
    else
        completion (false);
    return true;
}

bool MusicLibraryView::removeQueuedTrack (int index)
{
    if (queueLoadPending || ! juce::isPositiveAndBelow (index, (int) playQueue.size())) return false;
    playQueue.erase (playQueue.begin() + index);
    saveQueue();
    queueList.updateContent();
    refreshQueueControls();
    repaint (queueBounds);
    return true;
}

bool MusicLibraryView::moveQueuedTrack (int fromIndex, int toIndex)
{
    if (queueLoadPending || ! juce::isPositiveAndBelow (fromIndex, (int) playQueue.size())
        || ! juce::isPositiveAndBelow (toIndex, (int) playQueue.size()) || fromIndex == toIndex)
        return false;
    auto file = playQueue[(size_t) fromIndex];
    playQueue.erase (playQueue.begin() + fromIndex);
    playQueue.insert (playQueue.begin() + toIndex, std::move (file));
    saveQueue();
    queueList.updateContent();
    refreshQueueControls();
    return true;
}

void MusicLibraryView::clearQueue()
{
    if (queueLoadPending) return;
    playQueue.clear();
    queueList.deselectAllRows();
    queueList.updateContent();
    saveQueue();
    refreshQueueControls();
    repaint (queueBounds);
}

void MusicLibraryView::enqueueSelected()
{
    enqueueFile (selectedFile());
}

void MusicLibraryView::previewQueuedSelection()
{
    const auto file = getQueuedFile (queueList.getSelectedRow());
    if (file.existsAsFile() && onTogglePreview) onTogglePreview (file);
}

void MusicLibraryView::refreshQueueControls()
{
    const int selected = queueList.getSelectedRow();
    const bool valid = juce::isPositiveAndBelow (selected, (int) playQueue.size());
    const bool canEdit = valid && ! queueLoadPending;
    queueCount.setText (playQueue.empty() ? "QUEUE EMPTY"
                                         : juce::String ((int) playQueue.size()) + " SONG"
                                           + (playQueue.size() == 1 ? "" : "S"),
                        juce::dontSendNotification);
    loadNextButton.setButtonText (queueLoadPending ? "Loading..." : "Load Next");
    loadNextButton.setEnabled (! playQueue.empty() && ! queueLoadPending);
    queuePreviewButton.setEnabled (valid && getQueuedFile (selected).existsAsFile());
    queueUpButton.setEnabled (canEdit && selected > 0);
    queueDownButton.setEnabled (canEdit && selected + 1 < (int) playQueue.size());
    queueRemoveButton.setEnabled (canEdit);
    queueClearButton.setEnabled (! playQueue.empty() && ! queueLoadPending);
    queueEmptyLabel.setVisible (playQueue.empty() && queueList.isVisible());
}

void MusicLibraryView::markFileLoaded (const juce::File& file)
{
    for (auto& entry : entries)
        if (entry.file == file)
        {
            entry.lastLoadedMs = juce::Time::currentTimeMillis();
            if (auto* object = cache->getProperty (file.getFullPathName()).getDynamicObject())
                object->setProperty ("last", (double) entry.lastLoadedMs);
            cacheDirty.store (true);
            break;
        }
}

void MusicLibraryView::loadQueue()
{
    if (! queuePersistenceEnabled) return;
    const auto file = appDataDir().getChildFile ("library-queue.json");
    if (! file.existsAsFile()) return;
    const auto parsed = juce::JSON::parse (file);
    if (auto* array = parsed.getArray())
        for (const auto& item : *array)
        {
            const juce::File queued (item.toString());
            if (stemdeck::drag::isSupportedAudioFile (queued))
                playQueue.push_back (queued);
        }
    queueList.updateContent();
    if (! playQueue.empty()) queueList.selectRow (0);
}

void MusicLibraryView::saveQueue() const
{
    if (! queuePersistenceEnabled) return;
    juce::Array<juce::var> paths;
    for (const auto& file : playQueue) paths.add (file.getFullPathName());
    appDataDir().getChildFile ("library-queue.json")
        .replaceWithText (juce::JSON::toString (juce::var (paths), true));
}

// ── Crates / scanning ─────────────────────────────────────────────────────────

void MusicLibraryView::addFolderInteractive()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Add a music folder to the library",
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            for (const auto& folder : fc.getResults())
                if (folder.isDirectory())
                    addFolder (folder, true);
        });
}

void MusicLibraryView::addFolder (const juce::File& folder, bool persist)
{
    if (! folder.isDirectory()) return;
    if (persist && ! crateFolders.contains (folder.getFullPathName()))
    {
        crateFolders.add (folder.getFullPathName());
        saveCrateList();
    }
    refreshCrateSelector();
    scanFolder (folder);
}

void MusicLibraryView::restorePersistedCrates()
{
    loadCrateList();
    refreshCrateSelector();
    for (const auto& path : crateFolders)
    {
        const juce::File folder (path);
        if (folder.isDirectory())
            scanFolder (folder);
    }
}

void MusicLibraryView::refreshCrateSelector()
{
    const auto previousPath = crateSelector.getSelectedId() > 1
        && juce::isPositiveAndBelow (crateSelector.getSelectedId() - 2, crateFolders.size())
            ? crateFolders[crateSelector.getSelectedId() - 2]
            : juce::String();

    crateSelector.clear (juce::dontSendNotification);
    crateSelector.addItem ("All Libraries", 1);
    for (int i = 0; i < crateFolders.size(); ++i)
    {
        const juce::File folder (crateFolders[i]);
        auto name = folder.getFileName();
        if (name.isEmpty()) name = folder.getFullPathName();
        crateSelector.addItem (name, i + 2);
    }

    const int previousIndex = previousPath.isNotEmpty()
        ? crateFolders.indexOf (previousPath) : -1;
    crateSelector.setSelectedId (previousIndex >= 0 ? previousIndex + 2 : 1,
                                 juce::dontSendNotification);
    removeFolderButton.setEnabled (crateSelector.getSelectedId() > 1);
}

void MusicLibraryView::removeSelectedCrate()
{
    const int index = crateSelector.getSelectedId() - 2;
    if (! juce::isPositiveAndBelow (index, crateFolders.size()))
        return;

    crateFolders.remove (index);
    saveCrateList();

    // Remove only library references which are no longer covered by another
    // added folder. Never touch the user's files on disk.
    std::erase_if (entries, [this] (const Entry& entry)
    {
        for (const auto& path : crateFolders)
            if (entry.file.isAChildOf (juce::File (path)))
                return false;
        return true;
    });

    refreshCrateSelector();
    rebuildView();
    countLabel.setText (u8 ("Library removed  \xe2\x80\xa2  audio files were not deleted"),
                        juce::dontSendNotification);
}

void MusicLibraryView::scanFolder (const juce::File& folder)
{
    folderScansInFlight.fetch_add (1, std::memory_order_relaxed);
    countLabel.setText ("Scanning " + folder.getFileName() + "...",
                        juce::dontSendNotification);
    juce::Component::SafePointer<MusicLibraryView> safe (this);
    analysisPool.addJob ([safe, folder]
    {
        auto discovered = std::make_shared<juce::Array<juce::File>> (
            folder.findChildFiles (
                juce::File::findFiles, true,
                "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg"));
        juce::MessageManager::callAsync ([safe, discovered]
        {
            if (auto* self = safe.getComponent())
                self->importDiscoveredFiles (discovered, 0);
        });
    });
}

void MusicLibraryView::importDiscoveredFiles (
    std::shared_ptr<juce::Array<juce::File>> files, int startIndex)
{
    if (files == nullptr)
        return;

    // Publish bounded batches so a 50,000-track drive never monopolises the message
    // thread. Analysis jobs are queued by addAudioFile after each batch is visible.
    constexpr int batchSize = 128;
    const int end = juce::jmin (files->size(), startIndex + batchSize);
    for (int index = startIndex; index < end; ++index)
        addAudioFile (files->getReference (index));
    rebuildView();

    if (end < files->size())
    {
        juce::Component::SafePointer<MusicLibraryView> safe (this);
        juce::MessageManager::callAsync ([safe, files, end]
        {
            if (auto* self = safe.getComponent())
                self->importDiscoveredFiles (files, end);
        });
        return;
    }

    folderScansInFlight.fetch_sub (1, std::memory_order_relaxed);
    countLabel.setText (
        "Library ready  •  " + juce::String (entries.size()) + " tracks",
        juce::dontSendNotification);
}

void MusicLibraryView::addFiles (const juce::Array<juce::File>& files)
{
    int added = 0;
    for (const auto& file : files)
    {
        if (file.isDirectory())
        {
            addFolder (file, true);
            continue;
        }
        added += addAudioFile (file) ? 1 : 0;
    }

    if (added > 0)
    {
        rebuildView();
        countLabel.setText ("Imported " + juce::String (added)
                                + (added == 1 ? " audio file" : " audio files")
                                + u8 ("  \xe2\x80\xa2  drag a row to a deck or pad"),
                            juce::dontSendNotification);
    }
}

bool MusicLibraryView::addAudioFile (const juce::File& file)
{
    if (! stemdeck::drag::isSupportedAudioFile (file))
        return false;

    for (const auto& existing : entries)
        if (existing.file == file)
            return false;

    Entry e;
    e.file  = file;
    e.title = file.getFileNameWithoutExtension();

    // Cache hit?
    if (auto* obj = cache->getProperty (file.getFullPathName()).getDynamicObject())
    {
        e.favourite = (bool) obj->getProperty ("fav");
        e.rating = juce::jlimit (0, 5, (int) obj->getProperty ("rating"));
        e.tags = obj->getProperty ("tags").toString();
        const auto mod = (double) file.getLastModificationTime().toMilliseconds();
        if (std::abs ((double) obj->getProperty ("mod") - mod) < 1.0)
        {
            e.bpm           = (float) (double) obj->getProperty ("bpm");
            e.keyPitchClass = (int) obj->getProperty ("kpc");
            e.keyMinor      = (bool) obj->getProperty ("min");
            e.keyConfidence = (float) (double) obj->getProperty ("conf");
            e.lengthSeconds = (double) obj->getProperty ("len");
            e.lastLoadedMs   = (juce::int64) (double) obj->getProperty ("last");
            e.analyzed = true;
        }
    }

    entries.push_back (e);
    if (! e.analyzed)
        enqueueAnalysis (file);
    return true;
}

void MusicLibraryView::enqueueAnalysis (const juce::File& file)
{
    analysesInFlight.fetch_add (1);
    juce::Component::SafePointer<MusicLibraryView> safe (this);

    analysisPool.addJob ([this, safe, file]
    {
        double lengthSeconds = 0.0;
        TrackAnalysis analysis;

        if (std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
        {
            const double sr = reader->sampleRate;
            lengthSeconds = (double) reader->lengthInSamples / sr;
            const int channels = (int) juce::jmax (1u, reader->numChannels);
            const juce::int64 ninetySeconds = (juce::int64) (sr * 90.0);
            if (reader->lengthInSamples <= ninetySeconds)
            {
                const int n = (int) reader->lengthInSamples;
                juce::AudioBuffer<float> buf (channels, juce::jmax (1, n));
                reader->read (&buf, 0, n, 0, true, true);
                analysis = TrackAnalyzer::analyze (buf, sr);
            }
            else
            {
                // Long tracks are sampled across their structure rather than only
                // from the intro. Independent windows avoid inventing transients at
                // concatenation boundaries; the median tempo rejects one ambient or
                // breakdown window and the strongest key result wins.
                constexpr double windowSeconds = 30.0;
                constexpr std::array<double, 3> centres { 0.18, 0.50, 0.82 };
                const int windowSamples = (int) std::round (sr * windowSeconds);
                std::array<TrackAnalysis, centres.size()> windows;
                std::vector<float> bpms;
                float bestKeyConfidence = -1.0f;
                for (size_t index = 0; index < centres.size(); ++index)
                {
                    const auto centre = (juce::int64) std::llround (
                        centres[index] * (double) reader->lengthInSamples);
                    const auto start = juce::jlimit<juce::int64> (
                        0, reader->lengthInSamples - windowSamples,
                        centre - windowSamples / 2);
                    juce::AudioBuffer<float> buf (channels, windowSamples);
                    reader->read (&buf, 0, windowSamples, start, true, true);
                    windows[index] = TrackAnalyzer::analyze (buf, sr);
                    if (windows[index].detectedBpm > 0.0f)
                        bpms.push_back (windows[index].detectedBpm);
                    if (windows[index].hasKey()
                        && windows[index].keyConfidence > bestKeyConfidence)
                    {
                        analysis = windows[index];
                        bestKeyConfidence = windows[index].keyConfidence;
                    }
                }
                if (bestKeyConfidence < 0.0f)
                    analysis = windows[1];
                if (! bpms.empty())
                {
                    std::sort (bpms.begin(), bpms.end());
                    analysis.detectedBpm = bpms[bpms.size() / 2];
                }
                analysis.durationSeconds = lengthSeconds;
            }
        }

        juce::MessageManager::callAsync ([safe, file, analysis, lengthSeconds]
        {
            if (auto* self = safe.getComponent())
                self->applyAnalysis (file, analysis, lengthSeconds);
        });
    });
}

void MusicLibraryView::applyAnalysis (const juce::File& file, const TrackAnalysis& analysis, double lengthSeconds)
{
    for (auto& e : entries)
    {
        if (e.file != file) continue;
        e.bpm           = analysis.detectedBpm;
        e.keyPitchClass = analysis.keyPitchClass;
        e.keyMinor      = analysis.keyIsMinor;
        e.keyConfidence = analysis.keyConfidence;
        e.lengthSeconds = lengthSeconds;
        e.analyzed      = true;
        break;
    }

    // Update cache
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("bpm",  (double) analysis.detectedBpm);
    obj->setProperty ("kpc",  analysis.keyPitchClass);
    obj->setProperty ("min",  analysis.keyIsMinor);
    obj->setProperty ("conf", (double) analysis.keyConfidence);
    obj->setProperty ("len",  lengthSeconds);
    obj->setProperty ("mod",  (double) file.getLastModificationTime().toMilliseconds());
    for (const auto& entry : entries)
        if (entry.file == file)
        {
            obj->setProperty ("last", (double) entry.lastLoadedMs);
            obj->setProperty ("fav", entry.favourite);
            obj->setProperty ("rating", entry.rating);
            obj->setProperty ("tags", entry.tags);
            break;
        }
    cache->setProperty (file.getFullPathName(), juce::var (obj));
    cacheDirty.store (true);

    if (analysesInFlight.fetch_sub (1) - 1 <= 0)
        saveCacheSoon();

    rebuildView();
}

// ── View model ────────────────────────────────────────────────────────────────

void MusicLibraryView::rebuildView()
{
    setProposalActive = false;
    proposedSetSize = 0;
    loadSetButton.setEnabled (false);
    view.clear();
    view.reserve (entries.size());

    // Fast duplicate triage without hashing entire music libraries on the UI
    // thread. Equal byte size plus duration-to-100 ms is a strong review signal;
    // the label deliberately says "possible" so no file is removed automatically.
    std::map<juce::String, int> duplicateCounts;
    for (const auto& entry : entries)
        if (entry.file.existsAsFile())
        {
            const auto signature = juce::String (entry.file.getSize()) + ":"
                                 + juce::String ((int) std::round (entry.lengthSeconds * 10.0));
            ++duplicateCounts[signature];
        }

    juce::File selectedCrate;
    const int crateIndex = crateSelector.getSelectedId() - 2;
    if (juce::isPositiveAndBelow (crateIndex, crateFolders.size()))
        selectedCrate = juce::File (crateFolders[crateIndex]);

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& entry = entries[(size_t) i];
        if (selectedCrate.getFullPathName().isNotEmpty()
            && ! entry.file.isAChildOf (selectedCrate))
            continue;
        bool smartMatch = true;
        switch (smartFilterId)
        {
            case 2: smartMatch = entry.analyzed && entry.file.existsAsFile(); break;
            case 3: smartMatch = ! entry.analyzed; break;
            case 4: smartMatch = ! entry.file.existsAsFile(); break;
            case 5: smartMatch = entry.lastLoadedMs > 0
                              && juce::Time::currentTimeMillis() - entry.lastLoadedMs
                                   < 30LL * 24LL * 60LL * 60LL * 1000LL; break;
            case 6: smartMatch = entry.analyzed && entry.keyMinor; break;
            case 7: smartMatch = entry.analyzed && ! entry.keyMinor
                                              && entry.keyPitchClass >= 0; break;
            case 8: smartMatch = entry.bpm >= 60.0f && entry.bpm < 100.0f; break;
            case 9: smartMatch = entry.bpm >= 100.0f && entry.bpm < 130.0f; break;
            case 10: smartMatch = entry.bpm >= 130.0f; break;
            case 11: smartMatch = entry.favourite; break;
            case 12: smartMatch = entry.rating >= 4; break;
            case 13:
            {
                const auto signature = juce::String (entry.file.getSize()) + ":"
                                     + juce::String ((int) std::round (entry.lengthSeconds * 10.0));
                smartMatch = entry.file.existsAsFile()
                          && duplicateCounts[signature] > 1;
                break;
            }
            case 14: smartMatch = entry.analyzed && entry.rating == 0
                                   && entry.tags.trim().isEmpty(); break;
            default: break;
        }
        if (! smartMatch)
            continue;
        if (filterText.isNotEmpty())
        {
            const auto& e = entry;
            const auto hay = (e.title + " " + juce::String (e.bpm, 0) + " "
                              + camelotCode (e.keyPitchClass, e.keyMinor) + " "
                              + e.tags).toLowerCase();
            if (! hay.contains (filterText))
                continue;
        }
        view.push_back (i);
    }

    auto less = [this] (int a, int b)
    {
        const auto& ea = entries[(size_t) a];
        const auto& eb = entries[(size_t) b];
        int cmp = 0;
        switch (sortColumnId)
        {
            case colBpm:    cmp = (ea.bpm < eb.bpm) ? -1 : (ea.bpm > eb.bpm ? 1 : 0); break;
            case colLength: cmp = (ea.lengthSeconds < eb.lengthSeconds) ? -1 : (ea.lengthSeconds > eb.lengthSeconds ? 1 : 0); break;
            case colKey:
            {
                const auto ka = camelotCode (ea.keyPitchClass, ea.keyMinor);
                const auto kb = camelotCode (eb.keyPitchClass, eb.keyMinor);
                cmp = ka.compareNatural (kb);
                break;
            }
            case colTitle:
            default: cmp = ea.title.compareNatural (eb.title); break;
        }
        return sortForwards ? (cmp < 0) : (cmp > 0);
    };
    std::stable_sort (view.begin(), view.end(), less);

    table.updateContent();
    table.setVisible (! entries.empty());
    table.repaint();

    const int inFlight = analysesInFlight.load();
    countLabel.setText (juce::String ((int) view.size())
                        + (view.size() == entries.size() ? " tracks" : u8 (" shown  \xe2\x80\xa2  ") + juce::String ((int) entries.size()) + " total")
                        + (inFlight > 0 ? u8 ("  \xc2\xb7  analyzing ") + juce::String (inFlight) + u8 ("\xe2\x80\xa6") : juce::String()),
                        juce::dontSendNotification);
}

void MusicLibraryView::arrangeByBpmAndKey()
{
    // Capture the anchor before rebuildView potentially changes table selection.
    const auto anchor = selectedFile();
    rebuildView();

    std::vector<int> remaining;
    std::vector<int> unresolved;
    remaining.reserve (view.size());
    unresolved.reserve (view.size());
    for (const int index : view)
    {
        const auto& entry = entries[(size_t) index];
        if (entry.file.existsAsFile() && entry.analyzed && entry.bpm > 0.0f
            && juce::isPositiveAndBelow (entry.keyPitchClass, 12))
            remaining.push_back (index);
        else
            unresolved.push_back (index);
    }

    if (remaining.empty())
    {
        countLabel.setText (analysesInFlight.load() > 0
                                ? "BPM / KEY: finish analyzing tracks first"
                                : "BPM / KEY: no analyzed tracks in this view",
                            juce::dontSendNotification);
        return;
    }

    auto normalizedTempo = [] (float bpm)
    {
        while (bpm > 0.0f && bpm < 80.0f) bpm *= 2.0f;
        while (bpm > 160.0f)              bpm *= 0.5f;
        return bpm;
    };

    auto keyCost = [] (const Entry& from, const Entry& to)
    {
        if (! juce::isPositiveAndBelow (from.keyPitchClass, 12)
            || ! juce::isPositiveAndBelow (to.keyPitchClass, 12))
            return 0.85f;

        const int fromNumber = from.keyMinor ? kMinorCamelot[from.keyPitchClass]
                                             : kMajorCamelot[from.keyPitchClass];
        const int toNumber = to.keyMinor ? kMinorCamelot[to.keyPitchClass]
                                         : kMajorCamelot[to.keyPitchClass];
        const int rawDistance = std::abs (fromNumber - toNumber);
        const int wheelDistance = juce::jmin (rawDistance, 12 - rawDistance);

        if (wheelDistance == 0 && from.keyMinor == to.keyMinor) return 0.0f;
        if (wheelDistance == 0)                                 return 0.12f;
        if (wheelDistance == 1 && from.keyMinor == to.keyMinor) return 0.20f;
        if (wheelDistance == 2 && from.keyMinor == to.keyMinor) return 0.48f;
        return 0.95f + 0.08f * (float) wheelDistance;
    };

    // Start from the selected song when it is eligible. Otherwise begin at the
    // lowest half/double-time-normalized BPM so the set naturally builds energy.
    int firstPosition = -1;
    if (anchor.existsAsFile())
        for (int i = 0; i < (int) remaining.size(); ++i)
            if (entries[(size_t) remaining[(size_t) i]].file == anchor)
            {
                firstPosition = i;
                break;
            }
    if (firstPosition < 0)
        firstPosition = (int) std::distance (
            remaining.begin(),
            std::min_element (remaining.begin(), remaining.end(),
                [&] (int a, int b)
                {
                    const auto ta = normalizedTempo (entries[(size_t) a].bpm);
                    const auto tb = normalizedTempo (entries[(size_t) b].bpm);
                    if (std::abs (ta - tb) > 0.001f) return ta < tb;
                    return entries[(size_t) a].title.compareNatural (
                               entries[(size_t) b].title) < 0;
                }));

    std::vector<int> sequence;
    sequence.reserve (view.size());
    sequence.push_back (remaining[(size_t) firstPosition]);
    remaining.erase (remaining.begin() + firstPosition);

    // Greedy route: tempo distance is measured after half/double-time folding;
    // Camelot-compatible moves receive a strong advantage. A small downward-tempo
    // penalty keeps the energy arc rising without blocking a great key mix.
    while (! remaining.empty())
    {
        const auto& current = entries[(size_t) sequence.back()];
        const float currentTempo = normalizedTempo (current.bpm);
        int bestPosition = 0;
        float bestScore = std::numeric_limits<float>::max();

        for (int i = 0; i < (int) remaining.size(); ++i)
        {
            const auto& candidate = entries[(size_t) remaining[(size_t) i]];
            const float candidateTempo = normalizedTempo (candidate.bpm);
            const float tempoDistance = std::abs (candidateTempo - currentTempo);
            const float downwardPenalty = candidateTempo + 0.5f < currentTempo
                                            ? (tempoDistance > 6.0f ? 0.38f : 0.16f)
                                            : 0.0f;
            const float confidencePenalty = candidate.keyConfidence < 0.25f ? 0.18f : 0.0f;
            const float score = tempoDistance / 6.0f + keyCost (current, candidate)
                              + downwardPenalty + confidencePenalty;
            if (score < bestScore - 0.0001f
                || (std::abs (score - bestScore) <= 0.0001f
                    && candidate.title.compareNatural (
                           entries[(size_t) remaining[(size_t) bestPosition]].title) < 0))
            {
                bestScore = score;
                bestPosition = i;
            }
        }

        sequence.push_back (remaining[(size_t) bestPosition]);
        remaining.erase (remaining.begin() + bestPosition);
    }

    sequence.insert (sequence.end(), unresolved.begin(), unresolved.end());
    view = std::move (sequence);
    const int readyCount = (int) (view.size() - unresolved.size());
    setProposalActive = true;
    proposedSetSize = juce::jmin (4, readyCount);
    loadSetButton.setEnabled (proposedSetSize > 0);
    table.updateContent();
    table.repaint();
    if (! view.empty()) table.selectRow (0);

    countLabel.setText (u8 ("BPM / Key sequence  \xe2\x80\xa2  ") + juce::String (readyCount)
                        + u8 (" harmonic tracks  \xe2\x80\xa2  A\xe2\x80\x93" "D proposal ready"),
                        juce::dontSendNotification);
}

// ── Generative set builder ────────────────────────────────────────────────────
// Deterministic "AI" — parses BPM / key / mood from the prompt and ranks the
// analyzed library by tempo proximity (incl. half/double time), Camelot-wheel
// harmonic compatibility, and a minor/major mood bias. No external service.

void MusicLibraryView::buildSetFromPrompt (const juce::String& promptRaw)
{
    setProposalActive = false;
    proposedSetSize = 0;
    loadSetButton.setEnabled (false);
    const auto prompt = promptRaw.trim().toLowerCase();
    if (prompt.isEmpty())
        return;

    // ── Parse target BPM: first standalone number in 60..200 ────────────────
    float targetBpm = 0.0f;
    auto tokens = juce::StringArray::fromTokens (prompt, " ,;-/", "");
    for (const auto& t : tokens)
    {
        const float v = t.removeCharacters ("bpm").getFloatValue();
        if (v >= 60.0f && v <= 200.0f) { targetBpm = v; break; }
    }

    // ── Parse target key: Camelot code ("8a") or note name + quality ────────
    int targetPc = -1;
    bool targetMinor = false, haveKey = false;
    static const std::pair<const char*, int> noteNames[] = {
        { "c#", 1 }, { "db", 1 }, { "d#", 3 }, { "eb", 3 }, { "f#", 6 }, { "gb", 6 },
        { "g#", 8 }, { "ab", 8 }, { "a#", 10 }, { "bb", 10 },
        { "c", 0 }, { "d", 2 }, { "e", 4 }, { "f", 5 }, { "g", 7 }, { "a", 9 }, { "b", 11 } };
    for (int ti = 0; ti < tokens.size() && ! haveKey; ++ti)
    {
        const auto& tok = tokens[ti];
        // Camelot form: 1..12 followed by a/b
        if (tok.length() >= 2 && tok.length() <= 3
            && (tok.endsWithChar ('a') || tok.endsWithChar ('b')))
        {
            const int num = tok.dropLastCharacters (1).getIntValue();
            if (num >= 1 && num <= 12 && tok.dropLastCharacters (1).containsOnly ("0123456789"))
            {
                const bool minor = tok.endsWithChar ('a');
                for (int pc = 0; pc < 12; ++pc)
                    if ((minor ? kMinorCamelot[pc] : kMajorCamelot[pc]) == num)
                    { targetPc = pc; targetMinor = minor; haveKey = true; break; }
            }
        }
        // Note-name form: "a minor", "f# maj", "c min"
        if (! haveKey && ti + 1 < tokens.size())
        {
            for (const auto& [name, pc] : noteNames)
            {
                if (tok != name) continue;
                const auto& q = tokens[ti + 1];
                if (q.startsWith ("min") || q == "m") { targetPc = pc; targetMinor = true;  haveKey = true; }
                else if (q.startsWith ("maj"))        { targetPc = pc; targetMinor = false; haveKey = true; }
                break;
            }
        }
    }

    // ── Mood bias: dark/heavy → prefer minor; chill/warm → prefer major ─────
    int moodBias = 0;
    for (const char* w : { "dark", "heavy", "hard", "aggressive", "banger", "peak" })
        if (prompt.contains (w)) { moodBias = 1; break; }
    if (moodBias == 0)
        for (const char* w : { "chill", "mellow", "deep", "soft", "ambient", "warm", "sunset" })
            if (prompt.contains (w)) { moodBias = -1; break; }

    // ── Score every analyzed track ───────────────────────────────────────────
    struct Scored { int idx; float score; };
    std::vector<Scored> scored;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.analyzed || e.bpm <= 0.0f)
            continue;

        float bpmScore = 0.5f;
        if (targetBpm > 0.0f)
        {
            float best = 1.0e9f;   // half/double-time counts as compatible
            for (const float b : { e.bpm, e.bpm * 2.0f, e.bpm * 0.5f })
                best = juce::jmin (best, std::abs (b - targetBpm) / targetBpm);
            bpmScore = juce::jmax (0.0f, 1.0f - best / 0.08f);   // 8% window
        }

        float keyScore = 0.5f;
        if (haveKey && e.keyPitchClass >= 0)
        {
            const int tn = targetMinor ? kMinorCamelot[targetPc] : kMajorCamelot[targetPc];
            const int en = e.keyMinor  ? kMinorCamelot[e.keyPitchClass] : kMajorCamelot[e.keyPitchClass];
            const int dist = juce::jmin (std::abs (tn - en), 12 - std::abs (tn - en));
            const bool sameWheel = e.keyMinor == targetMinor;
            keyScore = (dist == 0 && sameWheel) ? 1.0f
                     : (dist == 0)              ? 0.85f    // relative major/minor
                     : (dist == 1 && sameWheel) ? 0.75f    // adjacent on the wheel
                     : (dist == 2 && sameWheel) ? 0.35f
                                                : 0.10f;
        }

        float moodScore = 0.8f;
        if (moodBias > 0) moodScore = e.keyMinor ? 1.0f : 0.55f;
        if (moodBias < 0) moodScore = e.keyMinor ? 0.60f : 1.0f;

        const float total = 0.5f * bpmScore + 0.35f * keyScore + 0.15f * moodScore;
        if (total > 0.15f)
            scored.push_back ({ i, total });
    }
    std::stable_sort (scored.begin(), scored.end(),
                      [] (const Scored& a, const Scored& b) { return a.score > b.score; });

    // ── Show the ranked set in the table + hand the top 4 to the decks ──────
    view.clear();
    for (size_t k = 0; k < scored.size() && k < 24; ++k)
        view.push_back (scored[k].idx);
    setProposalActive = ! view.empty();
    proposedSetSize = juce::jmin (4, (int) view.size());
    loadSetButton.setEnabled (proposedSetSize > 0);
    table.updateContent();
    table.repaint();
    countLabel.setText (view.empty()
                          ? juce::String ("AI Set: no analyzed tracks matched \"" + promptRaw.trim() + "\"")
                          : u8 ("AI Set: ") + juce::String ((int) view.size())
                                + u8 (" picks  \xe2\x80\xa2  A\xe2\x80\x93" "D proposal ready for \"")
                                + promptRaw.trim() + "\"",
                        juce::dontSendNotification);
}

void MusicLibraryView::loadProposedSet()
{
    if (! setProposalActive || proposedSetSize <= 0 || onLoadSet == nullptr)
        return;

    std::array<juce::File, 4> set {};
    for (int deck = 0; deck < proposedSetSize && deck < (int) view.size(); ++deck)
        set[(size_t) deck] = entries[(size_t) view[(size_t) deck]].file;
    if (onStopPreview) onStopPreview();
    onLoadSet (set);
}

std::vector<MusicLibraryView::ExportEntry> MusicLibraryView::getAnalyzedEntries() const
{
    std::vector<ExportEntry> out;
    out.reserve (entries.size());
    for (const auto& e : entries)
        if (e.analyzed)
            out.push_back ({ e.file, e.bpm, e.keyPitchClass, e.keyMinor });
    return out;
}

int MusicLibraryView::getNumRows() { return (int) view.size(); }

void MusicLibraryView::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (stemdeck::tokens::c (stemdeck::tokens::selected).withAlpha (0.18f));
    else if (row % 2)
        g.fillAll (stemdeck::tokens::c (stemdeck::tokens::bgPanel2).withAlpha (0.35f));
}

void MusicLibraryView::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    const auto& e = entries[(size_t) view[(size_t) row]];

    g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgPrimary));
    g.setFont (stemdeck::tokens::technical (10.5f));
    const auto area = juce::Rectangle<int> (4, 0, width - 8, height);

    switch (columnId)
    {
        case colIndex:
        {
            if (setProposalActive && row < proposedSetSize)
            {
                static constexpr std::array<juce::uint32, 4> deckTokens {
                    stemdeck::tokens::deckA, stemdeck::tokens::deckB,
                    stemdeck::tokens::deckC, stemdeck::tokens::deckD };
                g.setColour (stemdeck::tokens::c (deckTokens[(size_t) row]));
                g.setFont (stemdeck::tokens::technical (10.0f, true));
                g.drawText (juce::String::charToString ((juce_wchar) ('A' + row)), area,
                            juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgMuted));
                g.drawText (juce::String (row + 1), area, juce::Justification::centredLeft);
            }
            break;
        }
        case colTitle:
            g.drawText ((e.favourite ? u8 ("\xe2\x98\x85 ") : juce::String()) + e.title
                        + (e.rating > 0 ? "  " + juce::String (e.rating) + u8 ("\xe2\x98\x85") : juce::String()),
                        area, juce::Justification::centredLeft, true);
            break;
        case colBpm:
            g.setColour (e.analyzed ? stemdeck::tokens::c (stemdeck::tokens::fgPrimary)
                                    : stemdeck::tokens::c (stemdeck::tokens::fgMuted));
            g.drawText (e.bpm > 0.0f ? juce::String (e.bpm, 1) : (e.analyzed ? u8 ("\xe2\x80\x94") : u8 ("\xe2\x80\xa6")),
                        area, juce::Justification::centredLeft);
            break;
        case colKey:
        {
            const auto code = camelotCode (e.keyPitchClass, e.keyMinor);
            if (code.isNotEmpty())
            {
                const auto chip = juce::Rectangle<int> (4, height / 2 - 8, 26, 16);
                g.setColour (camelotColour (e.keyPitchClass, e.keyMinor).withAlpha (0.85f));
                g.fillRoundedRectangle (chip.toFloat(), 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.85f));
                g.setFont (stemdeck::tokens::technical (9.5f, true));
                g.drawText (code, chip, juce::Justification::centred);
                g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgMuted));
                g.setFont (stemdeck::tokens::technical (10.0f));
                g.drawText (e.keyPitchClass >= 0 ? TrackAnalyzer::pitchClassName (e.keyPitchClass) + (e.keyMinor ? "m" : "")
                                                 : juce::String(),
                            juce::Rectangle<int> (34, 0, width - 38, height), juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgMuted));
                g.drawText (e.analyzed ? u8 ("\xe2\x80\x94") : u8 ("\xe2\x80\xa6"), area, juce::Justification::centredLeft);
            }
            break;
        }
        case colLength:
            g.setColour (stemdeck::tokens::c (stemdeck::tokens::fgMuted));
            g.drawText (formatLength (e.lengthSeconds), area, juce::Justification::centredLeft);
            break;
        case colStatus:
        {
            const bool exists = e.file.existsAsFile();
            const auto status = ! exists ? "MISSING" : e.analyzed ? "READY" : "ANALYZING";
            const auto colour = ! exists ? stemdeck::tokens::c (stemdeck::tokens::meterHot)
                              : e.analyzed ? stemdeck::tokens::c (stemdeck::tokens::signal)
                                           : stemdeck::tokens::c (stemdeck::tokens::fgMuted);
            auto chip = area.reduced (2, 4);
            g.setColour (colour.withAlpha (0.13f));
            g.fillRoundedRectangle (chip.toFloat(), 3.0f);
            g.setColour (colour);
            g.setFont (stemdeck::tokens::technical (8.0f, true));
            g.drawText (status, chip, juce::Justification::centred);
            break;
        }
        default: break;
    }
}

void MusicLibraryView::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    auto& e = entries[(size_t) view[(size_t) row]];
    if (e.file.existsAsFile() && onTogglePreview)
    {
        e.lastLoadedMs = juce::Time::currentTimeMillis();
        if (auto* object = cache->getProperty (e.file.getFullPathName()).getDynamicObject())
            object->setProperty ("last", (double) e.lastLoadedMs);
        cacheDirty.store (true);
        onTogglePreview (e.file);
        if (setProposalActive) table.repaint();
        else rebuildView();
    }
}

void MusicLibraryView::selectedRowsChanged (int lastRowSelected)
{
    const bool valid = juce::isPositiveAndBelow (lastRowSelected, (int) view.size())
                    && entries[(size_t) view[(size_t) lastRowSelected]].file.existsAsFile();
    loadButton.setEnabled (valid);
    queueButton.setEnabled (valid);
    previewButton.setEnabled (valid);
    for (auto& target : deckLoadButtons) target.setEnabled (valid);
    refreshPreparationControls();
    resized();
    repaint();
}

int MusicLibraryView::deckTargetAt (juce::Point<int> point) const
{
    for (int deck = 0; deck < (int) deckTargetBounds.size(); ++deck)
        if (deckTargetBounds[(size_t) deck].contains (point)) return deck;
    return -1;
}

juce::var MusicLibraryView::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() <= 0)
        return {};
    juce::Array<juce::File> files;
    for (int i = 0; i < rows.size(); ++i)
    {
        const int row = rows[i];
        if (! juce::isPositiveAndBelow (row, (int) view.size()))
            continue;
        const auto& file = entries[(size_t) view[(size_t) row]].file;
        if (stemdeck::drag::isSupportedAudioFile (file))
            files.add (file);
    }
    return files.isEmpty() ? juce::var() : stemdeck::drag::makeAudioFiles (files);
}

void MusicLibraryView::refreshPreparationControls()
{
    const auto row = table.getSelectedRow();
    const bool valid = juce::isPositiveAndBelow (row, (int) view.size());
    updatingPreparationUi = true;
    favouriteButton.setEnabled (valid);
    ratingBox.setEnabled (valid);
    tagsBox.setEnabled (valid);
    if (valid)
    {
        const auto& entry = entries[(size_t) view[(size_t) row]];
        favouriteButton.setToggleState (entry.favourite, juce::dontSendNotification);
        ratingBox.setSelectedId (entry.rating + 1, juce::dontSendNotification);
        tagsBox.setText (entry.tags, false);
    }
    else
    {
        favouriteButton.setToggleState (false, juce::dontSendNotification);
        ratingBox.setSelectedId (1, juce::dontSendNotification);
        tagsBox.clear();
    }
    updatingPreparationUi = false;
}

void MusicLibraryView::saveSelectedPreparationMetadata()
{
    if (updatingPreparationUi) return;
    const auto row = table.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    auto& entry = entries[(size_t) view[(size_t) row]];
    entry.favourite = favouriteButton.getToggleState();
    entry.rating = juce::jlimit (0, 5, ratingBox.getSelectedId() - 1);
    entry.tags = tagsBox.getText().trim();

    auto property = cache->getProperty (entry.file.getFullPathName());
    auto* object = property.getDynamicObject();
    if (object == nullptr)
    {
        object = new juce::DynamicObject();
        cache->setProperty (entry.file.getFullPathName(), juce::var (object));
    }
    object->setProperty ("fav", entry.favourite);
    object->setProperty ("rating", entry.rating);
    object->setProperty ("tags", entry.tags);
    cacheDirty.store (true);
    table.repaintRow (row);
}

juce::File MusicLibraryView::selectedFile() const
{
    const auto row = table.getSelectedRow();
    if (juce::isPositiveAndBelow (row, (int) view.size()))
        return entries[(size_t) view[(size_t) row]].file;
    return {};
}

void MusicLibraryView::toggleSelectedPreview()
{
    const auto file = selectedFile();
    if (! file.existsAsFile() || ! onTogglePreview)
        return;

    for (auto& entry : entries)
        if (entry.file == file)
        {
            entry.lastLoadedMs = juce::Time::currentTimeMillis();
            if (auto* object = cache->getProperty (file.getFullPathName()).getDynamicObject())
                object->setProperty ("last", (double) entry.lastLoadedMs);
            cacheDirty.store (true);
            break;
        }
    onTogglePreview (file);
}

void MusicLibraryView::timerCallback()
{
    // The library lives for the lifetime of the editor, but most performance time
    // is spent on another surface. Avoid 20 Hz file/string/UI churn while hidden;
    // the first tick after it is shown catches the preview state up immediately.
    if (! isShowing() || ! getPreviewState)
        return;

    const auto state = getPreviewState();
    const auto selection = selectedFile();
    const bool selectedIsPreview = selection.existsAsFile() && selection == state.file;

    previewButton.setButtonText (selectedIsPreview && state.playing ? "Pause" : "Preview");
    stopPreviewButton.setEnabled (state.loading || state.file.existsAsFile());

    updatingPreviewUi = true;
    const double duration = juce::jmax (0.0, state.durationSeconds);
    if (! previewPosition.isMouseButtonDown())
    {
        if (std::abs (previewPosition.getMaximum() - juce::jmax (1.0, duration)) > 0.01)
            previewPosition.setRange (0.0, juce::jmax (1.0, duration), 0.01);
        previewPosition.setValue (juce::jlimit (0.0, juce::jmax (1.0, duration),
                                                state.positionSeconds),
                                  juce::dontSendNotification);
    }
    updatingPreviewUi = false;

    if (state.loading)
        previewTitle.setText (u8 ("Loading preview  \xe2\x80\xa2  ") + state.file.getFileNameWithoutExtension(),
                              juce::dontSendNotification);
    else if (state.failed)
        previewTitle.setText (u8 ("Could not preview  \xe2\x80\xa2  unsupported or unreadable audio"),
                              juce::dontSendNotification);
    else if (state.file.existsAsFile())
        previewTitle.setText ((state.playing ? "Now playing" : "Preview paused")
                              + juce::String (state.routedToDedicatedCue
                                                ? u8 (" in Headphones  \xe2\x80\xa2  ")
                                                : u8 (" on Master  \xe2\x80\xa2  "))
                              + state.file.getFileNameWithoutExtension(),
                              juce::dontSendNotification);
    else
        previewTitle.setText (u8 ("Select a track  \xe2\x80\xa2  Preview or double-click to audition"),
                              juce::dontSendNotification);

    previewTime.setText (formatLength (state.positionSeconds) + " / "
                         + formatLength (state.durationSeconds),
                         juce::dontSendNotification);
}

void MusicLibraryView::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    sortColumnId = newSortColumnId;
    sortForwards = isForwards;
    rebuildView();
}

// ── Camelot helpers ───────────────────────────────────────────────────────────

juce::String MusicLibraryView::camelotCode (int pitchClass, bool minor)
{
    if (! juce::isPositiveAndBelow (pitchClass, 12)) return {};
    const int n = minor ? kMinorCamelot[pitchClass] : kMajorCamelot[pitchClass];
    return juce::String (n) + (minor ? "A" : "B");
}

juce::Colour MusicLibraryView::camelotColour (int pitchClass, bool minor)
{
    if (! juce::isPositiveAndBelow (pitchClass, 12)) return juce::Colours::grey;
    const int n = minor ? kMinorCamelot[pitchClass] : kMajorCamelot[pitchClass];
    const float hue = (float) ((n - 1) % 12) / 12.0f;
    return juce::Colour::fromHSV (hue, 0.55f, minor ? 0.78f : 0.92f, 1.0f);
}

// ── Persistence ───────────────────────────────────────────────────────────────

juce::File MusicLibraryView::cacheFile()  const { return appDataDir().getChildFile ("library-cache.json"); }
juce::File MusicLibraryView::cratesFile() const { return appDataDir().getChildFile ("library-crates.json"); }

void MusicLibraryView::loadCache()
{
    const auto f = cacheFile();
    if (! f.existsAsFile()) return;
    auto parsed = juce::JSON::parse (f);
    if (auto* obj = parsed.getDynamicObject())
        cache = obj;
}

void MusicLibraryView::saveCacheSoon()
{
    juce::var wrapper (cache.get());
    cacheFile().replaceWithText (juce::JSON::toString (wrapper, true));
    cacheDirty.store (false);
}

void MusicLibraryView::loadCrateList()
{
    const auto f = cratesFile();
    if (! f.existsAsFile()) return;
    auto parsed = juce::JSON::parse (f);
    crateFolders.clear();
    if (auto* arr = parsed.getArray())
        for (const auto& v : *arr)
            crateFolders.add (v.toString());
}

void MusicLibraryView::saveCrateList()
{
    juce::Array<juce::var> arr;
    for (const auto& p : crateFolders)
        arr.add (p);
    cratesFile().replaceWithText (juce::JSON::toString (juce::var (arr), true));
}

// ── Layout ────────────────────────────────────────────────────────────────────

void MusicLibraryView::resized()
{
    using namespace stemdeck;
    auto area = getLocalBounds().reduced (12);

    headerBounds = area.removeFromTop (58);
    area.removeFromTop (tokens::gap5);

    auto tools = area.removeFromTop (34);
    addFolderButton.setBounds (tools.removeFromLeft (92));
    tools.removeFromLeft (tokens::gap4);
    removeFolderButton.setBounds (tools.removeFromLeft (62));
    tools.removeFromLeft (tokens::gap4);
    crateSelector.setBounds (tools.removeFromLeft (juce::jlimit (112, 170, tools.getWidth() / 4)));
    tools.removeFromLeft (tokens::gap4);
    clearButton.setBounds (tools.removeFromLeft (54));
    tools.removeFromLeft (tokens::gap4);
    smartFilter.setBounds (tools.removeFromRight (132));
    tools.removeFromRight (tokens::gap5);
    searchBox.setBounds (tools);

    area.removeFromTop (tokens::gap6);
    auto promptRow = area.removeFromTop (38);
    buildSetButton.setBounds (promptRow.removeFromRight (92));
    promptRow.removeFromRight (tokens::gap5);
    loadSetButton.setBounds (promptRow.removeFromRight (88));
    promptRow.removeFromRight (tokens::gap5);
    bpmKeyArrangeButton.setBounds (promptRow.removeFromRight (104));
    promptRow.removeFromRight (tokens::gap5);
    promptBox.setBounds (promptRow);

    area.removeFromTop (tokens::gap6);

    // Keep Up Next continuously visible. Wide layouts use a proper right rail;
    // compact layouts use a shallow bottom drawer so the track table remains usable.
    compactQueueLayout = area.getHeight() < 500;
    const bool showQueueContents = ! compactQueueLayout || compactQueueExpanded;
    if (area.getWidth() >= 820)
    {
        queueBounds = area.removeFromRight (juce::jlimit (240, 292, area.getWidth() / 3));
        area.removeFromRight (tokens::gap6);

        // Selection intelligence earns space only when it has content. With no
        // selected track the queue expands to use the rail, keeping an empty
        // Preparation card from becoming decorative chrome.
        preparationBounds = {};
        if (selectedFile().existsAsFile())
        {
            auto rail = queueBounds;
            const int prepHeight = juce::jlimit (184, 248, rail.getHeight() * 34 / 100);
            preparationBounds = rail.removeFromTop (prepHeight);
            rail.removeFromTop (tokens::gap6);
            queueBounds = rail;
        }
    }
    else
    {
        preparationBounds = {};
        const int queueHeight = showQueueContents
            ? juce::jlimit (112, 168, area.getHeight() / 3) : 34;
        queueBounds = area.removeFromBottom (queueHeight);
        area.removeFromBottom (tokens::gap6);
    }

    auto queueArea = queueBounds.reduced (9);
    auto queueHeader = queueArea.removeFromTop (24);
    queueDisclosureButton.setVisible (compactQueueLayout);
    if (compactQueueLayout)
    {
        queueDisclosureButton.setButtonText (compactQueueExpanded ? "Hide" : "Show");
        queueDisclosureButton.setBounds (queueHeader.removeFromRight (46));
        queueHeader.removeFromRight (tokens::gap3);
    }
    queueCount.setBounds (queueHeader.removeFromRight (88));
    queueTitle.setBounds (queueHeader);
    queueArea.removeFromTop (tokens::gap4);
    deckTargetsBounds = {};
    deckTargetBounds.fill ({});
    if (showQueueContents)
    {
        deckTargetsBounds = queueArea.removeFromTop (72);
        auto targets = deckTargetsBounds.withTrimmedTop (24);
        const int gap = tokens::gap3;
        const int width = (targets.getWidth() - gap * 3) / 4;
        for (int deck = 0; deck < 4; ++deck)
        {
            auto cell = targets.removeFromLeft (deck == 3 ? targets.getWidth() : width);
            deckTargetBounds[(size_t) deck] = cell;
            deckLoadButtons[(size_t) deck].setVisible (true);
            deckLoadButtons[(size_t) deck].setBounds (cell.reduced (1, 1));
            if (deck < 3) targets.removeFromLeft (gap);
        }
        queueArea.removeFromTop (tokens::gap5);
    }
    else
        for (auto& button : deckLoadButtons) { button.setVisible (false); button.setBounds ({}); }
    queueList.setVisible (showQueueContents);
    loadNextButton.setVisible (showQueueContents);
    queuePreviewButton.setVisible (showQueueContents);
    queueUpButton.setVisible (showQueueContents);
    queueDownButton.setVisible (showQueueContents);
    queueRemoveButton.setVisible (showQueueContents);
    queueClearButton.setVisible (showQueueContents);
    if (! showQueueContents)
    {
        queueList.setBounds ({});
        loadNextButton.setBounds ({});
        queuePreviewButton.setBounds ({});
        queueUpButton.setBounds ({});
        queueDownButton.setBounds ({});
        queueRemoveButton.setBounds ({});
        queueClearButton.setBounds ({});
        queueEmptyLabel.setVisible (false);
        queueEmptyLabel.setBounds ({});
    }
    else
    {
    auto queueActions = queueArea.removeFromBottom (60);
    auto primaryQueueActions = queueActions.removeFromTop (28);
    const int primaryWidth = (primaryQueueActions.getWidth() - tokens::gap3) / 2;
    loadNextButton.setBounds (primaryQueueActions.removeFromLeft (primaryWidth));
    primaryQueueActions.removeFromLeft (tokens::gap3);
    queuePreviewButton.setBounds (primaryQueueActions);
    queueActions.removeFromTop (tokens::gap3);
    const int compactButton = juce::jmax (1, queueActions.getWidth() / 4);
    queueUpButton.setBounds (queueActions.removeFromLeft (compactButton));
    queueDownButton.setBounds (queueActions.removeFromLeft (compactButton));
    queueRemoveButton.setBounds (queueActions.removeFromLeft (compactButton));
    queueClearButton.setBounds (queueActions);
    queueArea.removeFromBottom (tokens::gap4);
    queueList.setBounds (queueArea);
    queueEmptyLabel.setBounds (queueArea.reduced (14));
    queueEmptyLabel.setVisible (playQueue.empty());
    }

    auto footer = area.removeFromBottom (32);
    loadButton.setBounds (footer.removeFromRight (126));
    footer.removeFromRight (tokens::gap5);
    queueButton.setBounds (footer.removeFromRight (118));
    footer.removeFromRight (tokens::gap5);
    countLabel.setBounds (footer);
    area.removeFromBottom (tokens::gap6);

    auto prep = area.removeFromBottom (30);
    favouriteButton.setBounds (prep.removeFromLeft (86));
    prep.removeFromLeft (tokens::gap4);
    ratingBox.setBounds (prep.removeFromLeft (92));
    prep.removeFromLeft (tokens::gap4);
    tagsBox.setBounds (prep);
    area.removeFromBottom (tokens::gap5);

    auto previewArea = area.removeFromBottom (54);
    auto previewHeading = previewArea.removeFromTop (19);
    previewTime.setBounds (previewHeading.removeFromRight (100));
    previewHeading.removeFromRight (tokens::gap4);
    previewTitle.setBounds (previewHeading);

    previewArea.removeFromTop (3);
    previewButton.setBounds (previewArea.removeFromLeft (76));
    previewArea.removeFromLeft (tokens::gap4);
    stopPreviewButton.setBounds (previewArea.removeFromLeft (50));
    previewArea.removeFromLeft (tokens::gap5);
    previewPosition.setBounds (previewArea);
    area.removeFromBottom (tokens::gap6);
    tableBounds = area;
    table.setBounds (tableBounds);
}

bool MusicLibraryView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.isDirectory() || stemdeck::drag::isSupportedAudioFile (file))
            return true;
    }
    return false;
}

void MusicLibraryView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragOver = true;
    repaint();
}

void MusicLibraryView::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int deck = deckTargetAt ({ x, y });
    if (deckDragOver != deck)
    {
        deckDragOver = deck;
        repaint (queueBounds);
    }
}

void MusicLibraryView::fileDragExit (const juce::StringArray&)
{
    fileDragOver = false;
    deckDragOver = -1;
    repaint();
}

void MusicLibraryView::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragOver = false;
    juce::Array<juce::File> imported;
    for (const auto& path : files)
        imported.add (juce::File (path));
    addFiles (imported);
    const int deck = deckTargetAt ({ x, y });
    if (deck >= 0 && ! imported.isEmpty() && onLoadDeck)
        onLoadDeck (imported.getFirst(), deck);
    else if (queueBounds.contains (x, y))
        for (const auto& file : imported)
            enqueueFile (file);
    deckDragOver = -1;
    repaint();
}

bool MusicLibraryView::isInterestedInDragSource (const SourceDetails& details)
{
    return ! stemdeck::drag::audioFiles (details.description).isEmpty();
}

void MusicLibraryView::itemDragEnter (const SourceDetails& details)
{
    deckDragOver = deckTargetAt (details.localPosition);
    queueDragOver = deckDragOver < 0 && queueBounds.contains (details.localPosition);
    repaint (queueBounds);
}

void MusicLibraryView::itemDragMove (const SourceDetails& details)
{
    const int deck = deckTargetAt (details.localPosition);
    const bool over = deck < 0 && queueBounds.contains (details.localPosition);
    if (queueDragOver != over || deckDragOver != deck)
    {
        queueDragOver = over;
        deckDragOver = deck;
        repaint (queueBounds);
    }
}

void MusicLibraryView::itemDragExit (const SourceDetails&)
{
    queueDragOver = false;
    deckDragOver = -1;
    repaint (queueBounds);
}

void MusicLibraryView::itemDropped (const SourceDetails& details)
{
    queueDragOver = false;
    const int deck = deckTargetAt (details.localPosition);
    const auto files = stemdeck::drag::audioFiles (details.description);
    if (deck >= 0 && ! files.isEmpty() && onLoadDeck)
        onLoadDeck (files.getFirst(), deck);
    else if (queueBounds.contains (details.localPosition))
    {
        addFiles (files);
        for (const auto& file : files)
            enqueueFile (file);
    }
    deckDragOver = -1;
    repaint (queueBounds);
}

void MusicLibraryView::paint (juce::Graphics& g)
{
    using namespace stemdeck;
    const auto panel = getLocalBounds().toFloat();
    g.setColour (tokens::c (tokens::bgPanel));
    g.fillRoundedRectangle (panel, tokens::radiusPanel);
    g.setColour (tokens::c (tokens::borderSubtle));
    g.drawRoundedRectangle (panel.reduced (0.5f), tokens::radiusPanel, 1.0f);

    if (! preparationBounds.isEmpty())
    {
        const int row = table.getSelectedRow();
        const Entry* selected = juce::isPositiveAndBelow (row, (int) view.size())
                              ? &entries[(size_t) view[(size_t) row]] : nullptr;
        if (selected != nullptr)
        {
            auto prep = preparationBounds.reduced (10);
            g.setColour (tokens::c (tokens::bgPanel2));
            g.fillRoundedRectangle (preparationBounds.toFloat(), tokens::radiusWell);
            g.setColour (tokens::c (tokens::borderSubtle));
            g.drawRoundedRectangle (preparationBounds.toFloat().reduced (0.5f),
                                    tokens::radiusWell, 1.0f);

            auto heading = prep.removeFromTop (18);
            g.setColour (tokens::c (tokens::fgTertiary));
            g.setFont (tokens::technical (8.5f, true));
            g.drawText ("PREPARATION", heading, juce::Justification::centredLeft);
            g.setColour (selected->analyzed ? tokens::c (tokens::signal)
                                            : tokens::c (tokens::warn));
            g.drawText (selected->analyzed ? "READY" : "ANALYZING",
                        heading, juce::Justification::centredRight);

            prep.removeFromTop (6);
            g.setColour (tokens::c (tokens::fgPrimary));
            g.setFont (tokens::section (15.0f));
            g.drawFittedText (selected->title, prep.removeFromTop (24),
                              juce::Justification::centredLeft, 1);
            g.setColour (tokens::c (tokens::fgMuted));
            g.setFont (tokens::ui (9.0f));
            g.drawFittedText (selected->file.getParentDirectory().getFileName(),
                              prep.removeFromTop (15), juce::Justification::centredLeft, 1);

            prep.removeFromTop (9);
            auto metrics = prep.removeFromTop (48);
            const int gap = tokens::gap3;
            const int metricWidth = (metrics.getWidth() - gap * 2) / 3;
            const std::array<juce::String, 3> labels { "BPM", "KEY", "LENGTH" };
            const std::array<juce::String, 3> values {
                selected->bpm > 1.0f ? juce::String (selected->bpm, 1) : juce::String ("--"),
                selected->keyPitchClass >= 0
                    ? camelotCode (selected->keyPitchClass, selected->keyMinor)
                    : juce::String ("--"),
                formatLength (selected->lengthSeconds)
            };
            for (int metric = 0; metric < 3; ++metric)
            {
                auto cell = metrics.removeFromLeft (metric == 2 ? metrics.getWidth() : metricWidth);
                if (metric < 2) metrics.removeFromLeft (gap);
                g.setColour (tokens::c (tokens::bgWell));
                g.fillRoundedRectangle (cell.toFloat(), tokens::radiusInner);
                g.setColour (tokens::c (tokens::fgMuted));
                g.setFont (tokens::technical (7.5f, true));
                g.drawText (labels[(size_t) metric], cell.removeFromTop (16),
                            juce::Justification::centred);
                g.setColour (metric == 1 ? tokens::c (tokens::deckC)
                                         : tokens::c (tokens::fgPrimary));
                g.setFont (tokens::numeric (12.0f, true));
                g.drawText (values[(size_t) metric], cell, juce::Justification::centred);
            }

            prep.removeFromTop (10);
            g.setColour (tokens::c (tokens::fgTertiary));
            g.setFont (tokens::technical (8.0f, true));
            g.drawText ("PERFORMANCE NOTES", prep.removeFromTop (14),
                        juce::Justification::centredLeft);
            g.setColour (tokens::c (tokens::fgMuted));
            g.setFont (tokens::ui (9.0f));
            const auto notes = selected->tags.trim().isNotEmpty()
                             ? selected->tags
                             : juce::String ("Add tags below; cues and beat-grid edits stay with the track.");
            g.drawFittedText (notes, prep, juce::Justification::topLeft, 2);
        }
    }

    if (! queueBounds.isEmpty())
    {
        g.setColour (tokens::c (tokens::bgPanel2));
        g.fillRoundedRectangle (queueBounds.toFloat(), tokens::radiusWell);
        g.setColour (queueDragOver ? tokens::c (tokens::deckA)
                                  : tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (queueBounds.toFloat().reduced (0.5f), tokens::radiusWell,
                                queueDragOver ? 2.0f : 1.0f);
        if (playQueue.empty())
        {
            g.setColour (tokens::c (tokens::fgMuted));
            g.setFont (tokens::technical (9.5f));
            g.drawFittedText (queueDragOver ? "DROP TO ADD UP NEXT"
                                           : "Queue tracks here for your next move",
                              queueList.getBounds().reduced (14), juce::Justification::centred, 2);
        }
    }

    if (! deckTargetsBounds.isEmpty())
    {
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (tokens::technical (8.5f, true));
        g.drawText ("LOAD OR DROP INTO LIVE DECK",
                    deckTargetsBounds.withHeight (18), juce::Justification::centredLeft);
        if (juce::isPositiveAndBelow (deckDragOver, 4))
        {
            const auto target = deckTargetBounds[(size_t) deckDragOver].toFloat().expanded (2.0f);
            const auto colour = tokens::deckAccent (deckDragOver);
            g.setColour (colour.withAlpha (0.15f));
            g.fillRoundedRectangle (target, tokens::radiusControl);
            g.setColour (colour);
            g.drawRoundedRectangle (target.reduced (0.5f), tokens::radiusControl, 2.0f);
        }
    }

    auto header = headerBounds;
    auto rail = header.removeFromLeft (3).reduced (0, 4);
    g.setColour (tokens::c (tokens::deckA));
    g.fillRoundedRectangle (rail.toFloat(), 1.5f);
    header.removeFromLeft (13);
    g.setColour (tokens::c (tokens::fgPrimary));
    g.setFont (tokens::section (19.0f));
    g.drawText ("MUSIC LIBRARY", header.removeFromTop (29), juce::Justification::centredLeft);
    g.setColour (tokens::c (tokens::fgTertiary));
    g.setFont (tokens::technical (9.5f));
    g.drawFittedText ("Add folders, audition tracks, analyze keys and build compatible live sets",
                      header, juce::Justification::centredLeft, 1);

    if (entries.empty() && ! tableBounds.isEmpty())
    {
        auto empty = tableBounds.reduced (8).withSizeKeepingCentre (
            juce::jmin (360, tableBounds.getWidth() - 16),
            juce::jmin (190, tableBounds.getHeight() - 16));
        g.setColour (tokens::c (tokens::bgWell));
        g.fillRoundedRectangle (empty.toFloat(), tokens::radiusWell);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (empty.toFloat().reduced (0.5f), tokens::radiusWell, 1.0f);

        auto content = empty.reduced (24, 20);
        auto icon = content.removeFromTop (42).withSizeKeepingCentre (42, 42).toFloat();
        g.setColour (tokens::activeFill (tokens::c (tokens::deckA)));
        g.fillEllipse (icon);
        g.setColour (tokens::c (tokens::deckA));
        g.drawHorizontalLine ((int) icon.getCentreY(), icon.getX() + 12.0f, icon.getRight() - 12.0f);
        g.drawVerticalLine ((int) icon.getCentreX(), icon.getY() + 12.0f, icon.getBottom() - 12.0f);
        content.removeFromTop (12);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (tokens::section (15.0f));
        g.drawText ("BUILD YOUR PERFORMANCE LIBRARY", content.removeFromTop (25),
                    juce::Justification::centred);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (tokens::technical (9.0f));
        g.drawFittedText ("Add a Library folder. StemDeck will analyze tempo and key in the background.",
                          content, juce::Justification::centred, 2);
    }

    if (fileDragOver)
    {
        auto target = getLocalBounds().reduced (8).toFloat();
        g.setColour (tokens::c (tokens::deckA).withAlpha (0.12f));
        g.fillRoundedRectangle (target, tokens::radiusPanel);
        g.setColour (tokens::c (tokens::deckA));
        g.drawRoundedRectangle (target.reduced (1.0f), tokens::radiusPanel, 2.0f);
        g.setFont (tokens::section (15.0f));
        g.drawText ("ADD TO MUSIC LIBRARY", target.toNearestInt(), juce::Justification::centred);
    }
}
