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

    promptBox.setTextToShowWhenEmpty (u8 ("AI set: e.g. \"dark 124 bpm set in A minor\"\xe2\x80\xa6"),
                                      juce::Colour (sattari::brand::kTextMuted));
    promptBox.setColour (juce::TextEditor::backgroundColourId, juce::Colour (sattari::brand::kPanel));
    promptBox.setColour (juce::TextEditor::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    promptBox.onReturnKey = [this] { buildSetFromPrompt (promptBox.getText()); };
    addAndMakeVisible (promptBox);

    buildSetButton.onClick = [this] { buildSetFromPrompt (promptBox.getText()); };
    addAndMakeVisible (buildSetButton);

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

// ── Generative set builder ────────────────────────────────────────────────────
// Deterministic "AI" — parses BPM / key / mood from the prompt and ranks the
// analyzed library by tempo proximity (incl. half/double time), Camelot-wheel
// harmonic compatibility, and a minor/major mood bias. No external service.

void MusicLibraryView::buildSetFromPrompt (const juce::String& promptRaw)
{
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
    table.updateContent();
    table.repaint();
    countLabel.setText (view.empty()
                          ? juce::String ("AI SET: no analyzed tracks matched \"" + promptRaw.trim() + "\"")
                          : "AI SET: " + juce::String ((int) view.size()) + " picks for \"" + promptRaw.trim() + "\"",
                        juce::dontSendNotification);

    if (onLoadSet != nullptr && ! view.empty())
    {
        std::array<juce::File, 4> set {};
        for (int d = 0; d < 4 && d < (int) view.size(); ++d)
            set[(size_t) d] = entries[(size_t) view[(size_t) d]].file;
        onLoadSet (set);
    }
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
        g.fillAll (juce::Colour (sattari::brand::kAccent).withAlpha (0.22f));
    else if (row % 2)
        g.fillAll (juce::Colour (sattari::brand::kPanel).withAlpha (0.35f));
}

void MusicLibraryView::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) view.size())) return;
    const auto& e = entries[(size_t) view[(size_t) row]];

    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (juce::FontOptions (11.5f));
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
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (code, chip, juce::Justification::centred);
                g.setColour (juce::Colour (sattari::brand::kTextMuted));
                g.setFont (juce::FontOptions (11.5f));
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
    // Inset content so children clear the panel's rounded corners.
    auto area = getLocalBounds().reduced (8, 8);
    auto top = area.removeFromTop (26);
    addFolderButton.setBounds (top.removeFromLeft (72));
    top.removeFromLeft (4);
    clearButton.setBounds (top.removeFromLeft (52));
    top.removeFromLeft (6);
    searchBox.setBounds (top);
    area.removeFromTop (4);
    // AI set builder row: free-text prompt + Build Set.
    auto promptRow = area.removeFromTop (24);
    buildSetButton.setBounds (promptRow.removeFromRight (74));
    promptRow.removeFromRight (4);
    promptBox.setBounds (promptRow);
    area.removeFromTop (4);
    countLabel.setBounds (area.removeFromBottom (16));
    table.setBounds (area);
}

void MusicLibraryView::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}
