#pragma once

#include <JuceHeader.h>
#include "DesignTokens.h"

namespace stemdeck
{
namespace workflow_detail
{
inline void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, tokens::c (tokens::bgControl));
    button.setColour (juce::TextButton::buttonOnColourId, tokens::c (tokens::deckA).withAlpha (0.30f));
    button.setColour (juce::TextButton::textColourOffId, tokens::c (tokens::fgSecondary));
    button.setColour (juce::TextButton::textColourOnId, tokens::c (tokens::fgPrimary));
}

inline void drawPanelBackground (juce::Graphics& g, juce::Rectangle<int> bounds,
                                 const juce::String& title,
                                 const juce::String& subtitle = {})
{
    g.fillAll (tokens::c (tokens::bgApp));
    auto header = bounds.reduced (20).removeFromTop (58);
    g.setColour (tokens::c (tokens::fgPrimary));
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText (title, header.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (tokens::c (tokens::fgTertiary));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (subtitle, header, juce::Justification::centredLeft);
}
} // namespace workflow_detail

class SessionHistoryPanel final : public juce::Component,
                                  private juce::ListBoxModel
{
public:
    explicit SessionHistoryPanel (juce::File snapshotsDirectory)
        : directory (std::move (snapshotsDirectory)), list ("Session history", this)
    {
        setSize (680, 520);
        setTitle ("Session History");
        setDescription ("Browse restore points, keep notes, or restore a selected snapshot.");

        list.setRowHeight (50);
        list.setColour (juce::ListBox::backgroundColourId, tokens::c (tokens::bgWell));
        list.setColour (juce::ListBox::outlineColourId, tokens::c (tokens::borderSubtle));
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        note.setMultiLine (true);
        note.setReturnKeyStartsNewLine (true);
        note.setTextToShowWhenEmpty ("Add a note for this restore point...",
                                     tokens::c (tokens::fgMuted));
        note.setColour (juce::TextEditor::backgroundColourId, tokens::c (tokens::bgWell));
        note.setColour (juce::TextEditor::textColourId, tokens::c (tokens::fgPrimary));
        note.setColour (juce::TextEditor::outlineColourId, tokens::c (tokens::borderSubtle));
        note.setAccessible (true);
        note.setTitle ("Snapshot note");
        addAndMakeVisible (note);

        configure (create, "New Snapshot");
        configure (restore, "Restore");
        configure (saveNote, "Save Note");
        configure (reveal, "Reveal");
        configure (remove, "Delete");
        remove.setColour (juce::TextButton::textColourOffId, tokens::c (tokens::meterHot));

        create.onClick = [this] { if (onCreate) onCreate(); };
        restore.onClick = [this]
        {
            if (auto file = selectedFile(); file.existsAsFile() && onRestore)
                onRestore (file);
        };
        saveNote.onClick = [this]
        {
            if (auto file = selectedFile(); file.existsAsFile())
            {
                noteFileFor (file).replaceWithText (note.getText());
                if (onStatus) onStatus ("Saved snapshot note");
            }
        };
        reveal.onClick = [this]
        {
            if (auto file = selectedFile(); file.existsAsFile())
                file.revealToUser();
        };
        remove.onClick = [this]
        {
            if (auto file = selectedFile(); file.existsAsFile() && onDelete)
                onDelete (file);
        };
        refresh();
    }

    std::function<void()> onCreate;
    std::function<void (juce::File)> onRestore;
    std::function<void (juce::File)> onDelete;
    std::function<void (juce::String)> onStatus;

    void refresh()
    {
        files.clear();
        directory.findChildFiles (files, juce::File::findFiles, false,
                                  "Snapshot_*.stemdeck");
        std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });
        list.updateContent();
        if (! files.isEmpty() && list.getSelectedRow() < 0)
            list.selectRow (0);
        updateButtons();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        workflow_detail::drawPanelBackground (
            g, getLocalBounds(), "Session History",
            files.isEmpty() ? "No restore points yet"
                            : juce::String (files.size()) + " restore points | newest first");
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("RESTORE POINTS", list.getBounds().translated (0, -20),
                    juce::Justification::centredLeft);
        g.drawText ("NOTE", note.getBounds().translated (0, -20),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (20);
        bounds.removeFromTop (78);
        auto buttons = bounds.removeFromBottom (34);
        auto noteArea = bounds.removeFromRight (238);
        bounds.removeFromRight (12);
        list.setBounds (bounds);
        note.setBounds (noteArea);

        create.setBounds (buttons.removeFromLeft (118));
        buttons.removeFromLeft (8);
        restore.setBounds (buttons.removeFromLeft (90));
        buttons.removeFromLeft (8);
        saveNote.setBounds (buttons.removeFromLeft (94));
        buttons.removeFromLeft (8);
        reveal.setBounds (buttons.removeFromLeft (76));
        buttons.removeFromLeft (8);
        remove.setBounds (buttons.removeFromLeft (76));
    }

private:
    int getNumRows() override { return files.size(); }
    void selectedRowsChanged (int) override
    {
        loadSelectedNote();
        updateButtons();
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                           bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;
        const auto& file = files.getReference (row);
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (7, 5);
        if (selected)
        {
            g.setColour (tokens::activeFill (tokens::c (tokens::deckA)));
            g.fillRoundedRectangle (area.toFloat(), tokens::radiusControl);
            g.setColour (tokens::activeBorder (tokens::c (tokens::deckA)));
            g.drawRoundedRectangle (area.toFloat(), tokens::radiusControl, 1.0f);
        }
        g.setColour (tokens::c (selected ? tokens::fgPrimary : tokens::fgSecondary));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        auto text = area.reduced (10, 4);
        g.drawText (displayName (file), text.removeFromTop (21),
                    juce::Justification::centredLeft, true);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (9.5f));
        g.drawText (file.getLastModificationTime().formatted ("%b %d, %Y | %I:%M %p"),
                    text, juce::Justification::centredLeft);
    }

    void configure (juce::TextButton& button, const juce::String& label)
    {
        button.setButtonText (label);
        workflow_detail::styleButton (button);
        addAndMakeVisible (button);
    }

    static juce::String displayName (const juce::File& file)
    {
        return file.getFileNameWithoutExtension()
            .replaceCharacter ('_', ' ')
            .replaceFirstOccurrenceOf ("Snapshot ", "");
    }

    static juce::File noteFileFor (const juce::File& file)
    {
        return file.getSiblingFile (file.getFileNameWithoutExtension() + ".note.txt");
    }

    juce::File selectedFile() const
    {
        const auto row = list.getSelectedRow();
        return juce::isPositiveAndBelow (row, files.size())
                   ? files.getReference (row) : juce::File();
    }

    void loadSelectedNote()
    {
        const auto file = selectedFile();
        note.setText (file.existsAsFile() && noteFileFor (file).existsAsFile()
                          ? noteFileFor (file).loadFileAsString() : juce::String(),
                      false);
    }

    void updateButtons()
    {
        const bool selected = selectedFile().existsAsFile();
        restore.setEnabled (selected);
        saveNote.setEnabled (selected);
        reveal.setEnabled (selected);
        remove.setEnabled (selected);
    }

    juce::File directory;
    juce::Array<juce::File> files;
    juce::ListBox list;
    juce::TextEditor note;
    juce::TextButton create, restore, saveNote, reveal, remove;
};

class DiagnosticsPanel final : public juce::Component
{
public:
    explicit DiagnosticsPanel (juce::String reportText) : report (std::move (reportText))
    {
        setSize (680, 520);
        setTitle ("StemDeck Diagnostics");
        setDescription ("A privacy-safe technical report for troubleshooting.");
        text.setMultiLine (true);
        text.setReadOnly (true);
        text.setScrollbarsShown (true);
        text.setText (report, false);
        text.setColour (juce::TextEditor::backgroundColourId, tokens::c (tokens::bgWell));
        text.setColour (juce::TextEditor::textColourId, tokens::c (tokens::fgSecondary));
        text.setColour (juce::TextEditor::outlineColourId, tokens::c (tokens::borderSubtle));
        addAndMakeVisible (text);
        workflow_detail::styleButton (copy);
        workflow_detail::styleButton (save);
        copy.setButtonText ("Copy Report");
        save.setButtonText ("Save...");
        copy.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (report);
            copy.setButtonText ("Copied");
        };
        save.onClick = [this] { if (onSave) onSave (report); };
        addAndMakeVisible (copy);
        addAndMakeVisible (save);
    }

    std::function<void (juce::String)> onSave;

    void paint (juce::Graphics& g) override
    {
        workflow_detail::drawPanelBackground (
            g, getLocalBounds(), "Diagnostics",
            "No audio or personal file paths are included.");
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (20);
        area.removeFromTop (68);
        auto actions = area.removeFromBottom (34);
        text.setBounds (area);
        actions.removeFromTop (6);
        copy.setBounds (actions.removeFromLeft (110));
        actions.removeFromLeft (8);
        save.setBounds (actions.removeFromLeft (80));
    }

private:
    juce::String report;
    juce::TextEditor text;
    juce::TextButton copy, save;
};

class OnboardingPanel final : public juce::Component
{
public:
    OnboardingPanel()
    {
        setSize (640, 470);
        setTitle ("Welcome to StemDeck");
        setDescription ("From an empty set to a finished live recording in four steps.");
        workflow_detail::styleButton (back);
        workflow_detail::styleButton (next);
        workflow_detail::styleButton (skip);
        back.setButtonText ("Back");
        next.setButtonText ("Continue");
        skip.setButtonText ("Skip");
        back.onClick = [this] { page = juce::jmax (0, page - 1); update(); };
        next.onClick = [this]
        {
            if (page < 3) { ++page; update(); }
            else if (onFinish) onFinish();
        };
        skip.onClick = [this] { if (onFinish) onFinish(); };
        addAndMakeVisible (back);
        addAndMakeVisible (next);
        addAndMakeVisible (skip);
        update();
    }

    std::function<void()> onFinish;

    void paint (juce::Graphics& g) override
    {
        static const char* titles[] = {
            "Connect your sound", "Prepare a set",
            "Perform and capture", "Perfect the replay"
        };
        static const char* bodies[] = {
            "Choose your audio device and start at a 256-sample buffer. Add an Input channel for a mic or guitar, or a MIDI Instrument channel for a controller.",
            "Open Sets, add a music folder, then double-click or drag a track into the focused deck. StemDeck analyzes tempo, key, phrases, and stems in the background.",
            "Open Perform and follow the objective bar. Cues, Loop, Stems, and FX always control the focused deck. Press R or Record before the set; every musical move is captured.",
            "Open Replay to edit the recorded lanes, tighten timing, automate the mix, and export. Use Tools only when you need the deeper DAW controls. Command-K finds any action."
        };
        workflow_detail::drawPanelBackground (
            g, getLocalBounds(), "Welcome to StemDeck",
            "Step " + juce::String (page + 1) + " of 4");
        auto area = getLocalBounds().reduced (42);
        area.removeFromTop (102);
        g.setColour (tokens::deckAccent (page));
        g.fillRoundedRectangle (area.removeFromTop (5).toFloat(), 2.5f);
        area.removeFromTop (25);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawFittedText (titles[page], area.removeFromTop (54),
                          juce::Justification::centredLeft, 2);
        area.removeFromTop (12);
        g.setColour (tokens::c (tokens::fgSecondary));
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (bodies[page], area.removeFromTop (120),
                          juce::Justification::topLeft, 5);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("You can reopen this guide from Settings at any time.",
                    area.removeFromBottom (35), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto actions = getLocalBounds().reduced (22).removeFromBottom (34);
        skip.setBounds (actions.removeFromLeft (70));
        next.setBounds (actions.removeFromRight (110));
        actions.removeFromRight (8);
        back.setBounds (actions.removeFromRight (78));
    }

private:
    void update()
    {
        back.setEnabled (page > 0);
        next.setButtonText (page == 3 ? "Get Started" : "Continue");
        repaint();
    }

    int page = 0;
    juce::TextButton back, next, skip;
};

class ExportCenterPanel final : public juce::Component
{
public:
    struct State
    {
        bool hasProjectContent = false;
        bool hasRecording = false;
        bool hasMissingMedia = false;
        juce::String recordingName;
        juce::String freeSpace;
    };

    explicit ExportCenterPanel (State initial) : state (std::move (initial))
    {
        setSize (650, 470);
        setTitle ("Export Center");
        setDescription ("Render the finished set or package the complete StemDeck session.");
        configure (mixdown, "Export Final Mix...");
        configure (package, "Export Session Package...");
        configure (project, "Save Project...");
        configure (bounce, "Bounce Arrangement Edits");
        configure (reveal, "Reveal Last Recording");
        mixdown.onClick = [this] { if (onMixdown) onMixdown(); };
        package.onClick = [this] { if (onPackage) onPackage(); };
        project.onClick = [this] { if (onSaveProject) onSaveProject(); };
        bounce.onClick = [this] { if (onBounce) onBounce(); };
        reveal.onClick = [this] { if (onReveal) onReveal(); };
        reveal.setEnabled (state.hasRecording);
        package.setEnabled (state.hasProjectContent);
        mixdown.setEnabled (state.hasProjectContent);
    }

    std::function<void()> onMixdown, onPackage, onSaveProject, onBounce, onReveal;

    void paint (juce::Graphics& g) override
    {
        workflow_detail::drawPanelBackground (
            g, getLocalBounds(), "Export Center",
            "A portable package includes project state, sources, diagnostics, and recordings.");
        auto area = getLocalBounds().reduced (24);
        area.removeFromTop (82);
        drawCheck (g, area.removeFromTop (44), state.hasProjectContent,
                   "Project state", state.hasProjectContent ? "Ready" : "Nothing loaded yet");
        drawCheck (g, area.removeFromTop (44), state.hasRecording,
                   "Last recording", state.hasRecording ? state.recordingName : "Optional");
        drawCheck (g, area.removeFromTop (44), ! state.hasMissingMedia,
                   "Linked media", state.hasMissingMedia ? "Some source files are missing" : "All available");
        drawCheck (g, area.removeFromTop (44), state.freeSpace.isNotEmpty(),
                   "Storage", state.freeSpace);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24);
        auto actions = area.removeFromBottom (138);
        mixdown.setBounds (actions.removeFromTop (38));
        actions.removeFromTop (8);
        package.setBounds (actions.removeFromTop (38));
        actions.removeFromTop (8);
        project.setBounds (actions.removeFromLeft (116));
        actions.removeFromLeft (8);
        bounce.setBounds (actions.removeFromLeft (184));
        actions.removeFromLeft (8);
        reveal.setBounds (actions.removeFromLeft (164));
    }

private:
    void configure (juce::TextButton& button, const juce::String& label)
    {
        button.setButtonText (label);
        workflow_detail::styleButton (button);
        addAndMakeVisible (button);
    }

    static void drawCheck (juce::Graphics& g, juce::Rectangle<int> row, bool ok,
                           const juce::String& label, const juce::String& detail)
    {
        const auto colour = ok ? tokens::c (tokens::deckA) : tokens::c (tokens::warn);
        g.setColour (colour.withAlpha (0.16f));
        g.fillRoundedRectangle (row.toFloat(), tokens::radiusControl);
        g.setColour (colour);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (ok ? "OK" : "!",
                    row.removeFromLeft (38), juce::Justification::centred);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label, row.removeFromLeft (150), juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (detail, row.reduced (8, 0), juce::Justification::centredLeft);
    }

    State state;
    juce::TextButton mixdown, package, project, bounce, reveal;
};

} // namespace stemdeck
