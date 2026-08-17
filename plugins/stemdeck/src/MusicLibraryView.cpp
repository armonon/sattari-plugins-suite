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

MusicLibraryView::MusicLibraryView (juce::AudioFormatManager& fm)
    : formatManager (fm)
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

    refreshPreparationControls();

    loadCache();
    startTimerHz (20);
}

MusicLibraryView::~MusicLibraryView()
{
    stopTimer();
    analysisPool.removeAllJobs (true, 2000);
    if (cacheDirty.load())
        saveCacheSoon();
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
    const auto files = folder.findChildFiles (juce::File::findFiles, true,
                                              "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    for (const auto& file : files)
        addAudioFile (file);

    rebuildView();
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
            const int n = (int) juce::jmin ((juce::int64) (sr * 90.0), reader->lengthInSamples);
            juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), juce::jmax (1, n));
            reader->read (&buf, 0, n, 0, true, true);
            analysis = TrackAnalyzer::analyze (buf, sr);
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
    previewButton.setEnabled (valid);
    refreshPreparationControls();
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
    auto footer = area.removeFromBottom (32);
    loadButton.setBounds (footer.removeFromRight (126));
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

void MusicLibraryView::fileDragExit (const juce::StringArray&)
{
    fileDragOver = false;
    repaint();
}

void MusicLibraryView::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    juce::Array<juce::File> imported;
    for (const auto& path : files)
        imported.add (juce::File (path));
    addFiles (imported);
    repaint();
}

void MusicLibraryView::paint (juce::Graphics& g)
{
    using namespace stemdeck;
    const auto panel = getLocalBounds().toFloat();
    g.setColour (tokens::c (tokens::bgPanel));
    g.fillRoundedRectangle (panel, tokens::radiusPanel);
    g.setColour (tokens::c (tokens::borderSubtle));
    g.drawRoundedRectangle (panel.reduced (0.5f), tokens::radiusPanel, 1.0f);

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
