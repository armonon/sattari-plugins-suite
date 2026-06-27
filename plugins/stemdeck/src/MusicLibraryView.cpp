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
    cache = new juce::DynamicObject();

    table.setColour (juce::TableListBox::backgroundColourId, juce::Colour (sattari::brand::kPanelDeep));
    table.setRowHeight (22);
    table.setHeaderHeight (24);
    auto& header = table.getHeader();
    header.addColumn ("#",      colIndex,  34,  28,  48,  juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn ("Title",  colTitle,  220, 120, 600, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("BPM",    colBpm,    56,  44,  80,  juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Key",    colKey,    74,  56,  100, juce::TableHeaderComponent::defaultFlags);
    header.addColumn ("Length", colLength, 64,  52,  90,  juce::TableHeaderComponent::defaultFlags);
    header.setSortColumnId (colTitle, true);
    addAndMakeVisible (table);

    searchBox.setTextToShowWhenEmpty (u8 ("Search title / BPM / key\xe2\x80\xa6"), juce::Colour (sattari::brand::kTextMuted));
    searchBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (sattari::brand::kPanel));
    searchBox.setColour (juce::TextEditor::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    searchBox.onTextChange = [this]
    {
        filterText = searchBox.getText().trim().toLowerCase();
        rebuildView();
    };
    addAndMakeVisible (searchBox);

    addFolderButton.onClick = [this] { addFolderInteractive(); };
    addAndMakeVisible (addFolderButton);

    clearButton.onClick = [this]
    {
        entries.clear();
        crateFolders.clear();
        saveCrateList();
        rebuildView();
    };
    addAndMakeVisible (clearButton);

    countLabel.setFont (juce::FontOptions (10.0f));
    countLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    addAndMakeVisible (countLabel);

    loadCache();
}

MusicLibraryView::~MusicLibraryView()
{
    analysisPool.removeAllJobs (true, 2000);
    if (cacheDirty.load())
        saveCacheSoon();
}

// ── Crates / scanning ─────────────────────────────────────────────────────────

void MusicLibraryView::addFolderInteractive()
{
    folderChooser = std::make_unique<juce::FileChooser> ("Add a music folder to the library",
                                                         juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
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
    scanFolder (folder);
}

void MusicLibraryView::restorePersistedCrates()
{
    loadCrateList();
    for (const auto& path : crateFolders)
    {
        const juce::File folder (path);
        if (folder.isDirectory())
            scanFolder (folder);
    }
}

void MusicLibraryView::scanFolder (const juce::File& folder)
{
    juce::StringArray known;
    for (const auto& e : entries)
        known.add (e.file.getFullPathName());

    const auto files = folder.findChildFiles (juce::File::findFiles, true,
                                              "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    for (const auto& file : files)
    {
        if (known.contains (file.getFullPathName()))
            continue;

        Entry e;
        e.file  = file;
        e.title = file.getFileNameWithoutExtension();

        // Cache hit?
        if (auto* obj = cache->getProperty (file.getFullPathName()).getDynamicObject())
        {
            const auto mod = (double) file.getLastModificationTime().toMilliseconds();
            if (std::abs ((double) obj->getProperty ("mod") - mod) < 1.0)
            {
                e.bpm           = (float) (double) obj->getProperty ("bpm");
                e.keyPitchClass = (int) obj->getProperty ("kpc");
                e.keyMinor      = (bool) obj->getProperty ("min");
                e.keyConfidence = (float) (double) obj->getProperty ("conf");
                e.lengthSeconds = (double) obj->getProperty ("len");
                e.analyzed = true;
            }
        }

        entries.push_back (e);
        if (! e.analyzed)
            enqueueAnalysis (file);
    }

    rebuildView();
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
    cache->setProperty (file.getFullPathName(), juce::var (obj));
    cacheDirty.store (true);

    if (analysesInFlight.fetch_sub (1) - 1 <= 0)
        saveCacheSoon();

    rebuildView();
}

// ── View model ────────────────────────────────────────────────────────────────

void MusicLibraryView::rebuildView()
{
    view.clear();
    view.reserve (entries.size());

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        if (filterText.isNotEmpty())
        {
            const auto& e = entries[(size_t) i];
            const auto hay = (e.title + " " + juce::String (e.bpm, 0) + " "
                              + camelotCode (e.keyPitchClass, e.keyMinor)).toLowerCase();
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
    table.repaint();

    const int inFlight = analysesInFlight.load();
    countLabel.setText (juce::String ((int) entries.size()) + " tracks"
                        + (inFlight > 0 ? u8 ("  \xc2\xb7  analyzing ") + juce::String (inFlight) + u8 ("\xe2\x80\xa6") : juce::String()),
                        juce::dontSendNotification);
}

int MusicLibraryView::getNumRows() { return (int) view.size(); }

void MusicLibraryView::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (sattari::brand::kAccent).withAlpha (0.22f));
    else if (row % 2)
        g.fillAll (juce::Colour (sattari::brand::kPanel).withAlpha (0.35f));
}

void MusicLibraryView::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    const auto& e = entries[(size_t) view[(size_t) row]];

    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (juce::FontOptions (12.0f));
    const auto area = juce::Rectangle<int> (4, 0, width - 8, height);

    switch (columnId)
    {
        case colIndex:
            g.setColour (juce::Colour (sattari::brand::kTextMuted));
            g.drawText (juce::String (row + 1), area, juce::Justification::centredLeft);
            break;
        case colTitle:
            g.drawText (e.title, area, juce::Justification::centredLeft, true);
            break;
        case colBpm:
            g.setColour (e.analyzed ? juce::Colour (sattari::brand::kTextPrimary)
                                    : juce::Colour (sattari::brand::kTextMuted));
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
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (code, chip, juce::Justification::centred);
                g.setColour (juce::Colour (sattari::brand::kTextMuted));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (e.keyPitchClass >= 0 ? TrackAnalyzer::pitchClassName (e.keyPitchClass) + (e.keyMinor ? "m" : "")
                                                 : juce::String(),
                            juce::Rectangle<int> (34, 0, width - 38, height), juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (juce::Colour (sattari::brand::kTextMuted));
                g.drawText (e.analyzed ? u8 ("\xe2\x80\x94") : u8 ("\xe2\x80\xa6"), area, juce::Justification::centredLeft);
            }
            break;
        }
        case colLength:
            g.setColour (juce::Colour (sattari::brand::kTextMuted));
            g.drawText (formatLength (e.lengthSeconds), area, juce::Justification::centredLeft);
            break;
        default: break;
    }
}

void MusicLibraryView::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    const auto& e = entries[(size_t) view[(size_t) row]];
    if (onLoadFile)
        onLoadFile (e.file);
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
    auto area = getLocalBounds();
    auto top = area.removeFromTop (26);
    addFolderButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (4);
    clearButton.setBounds (top.removeFromLeft (52));
    top.removeFromLeft (6);
    searchBox.setBounds (top);
    area.removeFromTop (4);
    countLabel.setBounds (area.removeFromBottom (16));
    table.setBounds (area);
}

void MusicLibraryView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}
