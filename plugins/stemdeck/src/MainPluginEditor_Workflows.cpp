#include "MainPluginEditor.h"

namespace
{
int countMissingSessionMedia (const MainPluginProcessor& processor)
{
    int missing = 0;
    const auto& state = processor.parameters().state;
    for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
    {
        const auto prefix = "deck" + juce::String::charToString (
            (juce_wchar) ('A' + deck));
        const auto full = state.getProperty (prefix + "File").toString();
        if (full.isNotEmpty() && ! juce::File (full).existsAsFile())
            ++missing;
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto path = state.getProperty (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "File").toString();
            if (path.isNotEmpty() && ! juce::File (path).existsAsFile())
                ++missing;
        }
    }
    for (int pad = 0; pad < PadBank::numPads; ++pad)
    {
        const auto path = state.getProperty ("pad" + juce::String (pad + 1) + "File").toString();
        if (path.isNotEmpty() && ! juce::File (path).existsAsFile())
            ++missing;
    }
    return missing;
}

juce::Rectangle<int> centredAnchor (juce::Component& owner)
{
    return owner.getLocalBounds().withSizeKeepingCentre (120, 40);
}

class SessionPackageJob final : public juce::ThreadPoolJob
{
public:
    SessionPackageJob (juce::File packageDirectory,
                       juce::String projectXml,
                       juce::String sourceManifest,
                       juce::String diagnostics,
                       std::vector<juce::File> recordings,
                       std::function<void (bool, juce::File)> completionCallback)
        : ThreadPoolJob ("StemDeck session package"),
          directory (std::move (packageDirectory)),
          project (std::move (projectXml)),
          sources (std::move (sourceManifest)),
          report (std::move (diagnostics)),
          files (std::move (recordings)),
          completion (std::move (completionCallback))
    {
    }

    JobStatus runJob() override
    {
        bool ok = directory.createDirectory();
        if (shouldExit())
            return jobHasFinished;

        ok = directory.getChildFile ("Session.stemdeck").replaceWithText (project) && ok;
        ok = directory.getChildFile ("Sources.txt").replaceWithText (sources) && ok;
        ok = directory.getChildFile ("Diagnostics.txt").replaceWithText (report) && ok;

        const auto recordingsDirectory = directory.getChildFile ("Recordings");
        for (const auto& source : files)
        {
            if (shouldExit())
                return jobHasFinished;
            if (! source.existsAsFile())
                continue;
            recordingsDirectory.createDirectory();
            auto destination = recordingsDirectory.getChildFile (source.getFileName());
            for (int copy = 2; destination.existsAsFile(); ++copy)
                destination = recordingsDirectory.getChildFile (
                    source.getFileNameWithoutExtension() + "-" + juce::String (copy)
                    + source.getFileExtension());
            ok = source.copyFileTo (destination) && ok;
        }

        juce::MessageManager::callAsync ([callback = completion, folder = directory, ok]
        {
            if (callback)
                callback (ok, folder);
        });
        return jobHasFinished;
    }

private:
    juce::File directory;
    juce::String project, sources, report;
    std::vector<juce::File> files;
    std::function<void (bool, juce::File)> completion;
};
} // namespace

juce::File MainPluginEditor::onboardingMarkerFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Sattari Audio")
        .getChildFile ("StemDeck")
        .getChildFile ("onboarding-v1.done");
}

juce::String MainPluginEditor::buildDiagnosticsReport() const
{
    juce::String report;
    report << "StemDeck Diagnostics\n"
           << "====================\n"
           << "Version: " << JucePlugin_VersionString << "\n"
           << "Mode: " << (juce::JUCEApplicationBase::isStandaloneApp()
                              ? "Standalone" : "Plugin") << "\n"
           << "Operating system: " << juce::SystemStats::getOperatingSystemName() << "\n"
           << "CPU: " << juce::SystemStats::getCpuModel() << "\n"
           << "Audio sample rate: " << juce::String (processorRef.getRecordingSampleRate(), 1) << " Hz\n"
           << "Audio block size: " << processorRef.getRecordingBlockSize() << " samples\n"
           << "Audio CPU load: " << juce::String (processorRef.getAudioCpuLoad() * 100.0f, 1) << "%\n"
           << "Dropped audio buffers: " << processorRef.getDroppedBufferCount() << "\n"
           << "Dropped capture events: " << processorRef.getDroppedCaptureEventCount() << "\n"
           << "Intent coverage: " << processorRef.getIntentCaptureCoverage() << "/"
           << processorRef.getIntentCaptureTargetCount() << "\n"
           << "MIDI mappings: " << (processorRef.hasMidiBindings() ? "Configured" : "None") << "\n"
           << "Channels: " << processorRef.getNumChannels() << "\n";

    for (int i = 0; i < processorRef.getNumChannels(); ++i)
        report << "  " << (i + 1) << ". " << processorRef.getChannelName (i)
               << " [" << channelTypeId (processorRef.getChannelType (i)) << "]\n";

    const auto target = processorRef.getMasterRecordingTarget();
    const auto volume = target.getFullPathName().isNotEmpty()
                            ? target.getParentDirectory()
                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto freeGb = (double) volume.getBytesFreeOnVolume()
                        / (1024.0 * 1024.0 * 1024.0);
    report << "Recording storage free: "
           << (freeGb > 0.0 ? juce::String (freeGb, 1) + " GB" : "Unavailable") << "\n"
           << "Missing session media: " << countMissingSessionMedia (processorRef) << "\n"
           << "Master recording active: " << (processorRef.isMasterRecording() ? "Yes" : "No") << "\n";
    return report;
}

void MainPluginEditor::checkpointSessionUndo (const juce::String& label)
{
    if (restoringSessionUndo)
        return;
    auto state = makeProjectState();
    state.setProperty ("undoLabel", label, nullptr);
    sessionUndoStack.push_back (std::move (state));
    if (sessionUndoStack.size() > 24)
        sessionUndoStack.erase (sessionUndoStack.begin());
    sessionRedoStack.clear();
}

void MainPluginEditor::undoSessionAction()
{
    if (sessionUndoStack.empty())
    {
        recordStatus.setText ("Nothing else to undo", juce::dontSendNotification);
        return;
    }
    auto current = makeProjectState();
    auto target = sessionUndoStack.back();
    sessionUndoStack.pop_back();
    sessionRedoStack.push_back (std::move (current));
    const auto label = target.getProperty ("undoLabel", "session change").toString();
    restoringSessionUndo = true;
    restoreProjectState (target, "undo: " + label);
    restoringSessionUndo = false;
}

void MainPluginEditor::redoSessionAction()
{
    if (sessionRedoStack.empty())
    {
        recordStatus.setText ("Nothing else to redo", juce::dontSendNotification);
        return;
    }
    auto current = makeProjectState();
    auto target = sessionRedoStack.back();
    sessionRedoStack.pop_back();
    sessionUndoStack.push_back (std::move (current));
    restoringSessionUndo = true;
    restoreProjectState (target, "redo session change");
    restoringSessionUndo = false;
}

void MainPluginEditor::showSessionHistory()
{
    auto panel = std::make_unique<stemdeck::SessionHistoryPanel> (
        sessionSnapshotsDirectory());
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    const juce::Component::SafePointer<stemdeck::SessionHistoryPanel> safePanel (panelPtr);

    panelPtr->onStatus = [safe] (juce::String status)
    {
        if (safe != nullptr)
            safe->recordStatus.setText (std::move (status), juce::dontSendNotification);
    };
    panelPtr->onCreate = [safe, safePanel]
    {
        if (safe == nullptr)
            return;
        safe->createSessionSnapshot();
        juce::Timer::callAfterDelay (350, [safePanel]
        {
            if (safePanel != nullptr)
                safePanel->refresh();
        });
    };
    panelPtr->onRestore = [safe, panelPtr] (juce::File file)
    {
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        juce::MessageManager::callAsync ([safe, file]
        {
            if (safe == nullptr)
                return;
            safe->checkpointSessionUndo ("Restore session history");
            safe->createSessionSnapshot();
            if (auto xml = juce::XmlDocument::parse (file))
                safe->restoreProjectState (
                    juce::ValueTree::fromXml (*xml),
                    file.getFileNameWithoutExtension());
            else
                safe->recordStatus.setText ("Snapshot could not be read",
                                            juce::dontSendNotification);
        });
    };
    panelPtr->onDelete = [safe, safePanel] (juce::File file)
    {
        juce::AlertWindow::showOkCancelBox (
            juce::AlertWindow::WarningIcon,
            "Delete Restore Point?",
            "Delete " + file.getFileNameWithoutExtension()
                + "? This removes only the snapshot, not its source audio.",
            "Delete", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([safe, safePanel, file] (int result)
            {
                if (result == 0)
                    return;
                const auto note = file.getSiblingFile (
                    file.getFileNameWithoutExtension() + ".note.txt");
                const bool removed = file.deleteFile();
                note.deleteFile();
                if (safe != nullptr)
                    safe->recordStatus.setText (
                        removed ? "Deleted restore point" : "Could not delete restore point",
                        juce::dontSendNotification);
                if (safePanel != nullptr)
                    safePanel->refresh();
            }));
    };

    juce::CallOutBox::launchAsynchronously (
        std::move (panel), centredAnchor (*this), this);
}

void MainPluginEditor::showDiagnostics()
{
    auto panel = std::make_unique<stemdeck::DiagnosticsPanel> (
        buildDiagnosticsReport());
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    panelPtr->onSave = [safe] (juce::String report)
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Save StemDeck diagnostics",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile ("StemDeck-Diagnostics.txt"),
            "*.txt");
        chooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safe, chooser, report = std::move (report)] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                file = file.withFileExtension ("txt");
                const bool ok = file.replaceWithText (report);
                if (safe != nullptr)
                    safe->recordStatus.setText (
                        ok ? "Saved diagnostics: " + file.getFileName()
                           : "Could not save diagnostics",
                        juce::dontSendNotification);
            });
    };
    juce::CallOutBox::launchAsynchronously (
        std::move (panel), centredAnchor (*this), this);
}

void MainPluginEditor::showOnboarding (bool force)
{
    const auto marker = onboardingMarkerFile();
    if (! force && marker.existsAsFile())
        return;

    auto panel = std::make_unique<stemdeck::OnboardingPanel>();
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    panelPtr->onFinish = [safe, panelPtr, marker]
    {
        marker.getParentDirectory().createDirectory();
        marker.replaceWithText ("StemDeck onboarding completed\n");
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
        if (safe != nullptr)
            safe->recordStatus.setText (
                "Welcome guide complete · press Command-K for any action",
                juce::dontSendNotification);
    };
    juce::CallOutBox::launchAsynchronously (
        std::move (panel), centredAnchor (*this), this);
}

void MainPluginEditor::showExportCenter()
{
    stemdeck::ExportCenterPanel::State state;
    for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
        state.hasProjectContent = state.hasProjectContent
                                  || processorRef.getDeckPath (deck).isNotEmpty();
    state.hasProjectContent = state.hasProjectContent
                              || ! arrangementView.getClips()[0].empty();
    const auto recording = processorRef.getMasterRecordingTarget();
    state.hasRecording = recording.existsAsFile();
    state.recordingName = state.hasRecording ? recording.getFileName() : juce::String();
    state.hasMissingMedia = countMissingSessionMedia (processorRef) > 0;
    const auto volume = recording.getFullPathName().isNotEmpty()
                            ? recording.getParentDirectory()
                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto freeGb = (double) volume.getBytesFreeOnVolume()
                        / (1024.0 * 1024.0 * 1024.0);
    state.freeSpace = freeGb > 0.0 ? juce::String (freeGb, 1) + " GB free"
                                   : "Storage unavailable";

    auto panel = std::make_unique<stemdeck::ExportCenterPanel> (std::move (state));
    auto* panelPtr = panel.get();
    const juce::Component::SafePointer<MainPluginEditor> safe (this);
    auto dismiss = [panelPtr]
    {
        if (auto* box = panelPtr->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    };
    panelPtr->onPackage = [safe, dismiss]
    {
        dismiss();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->exportSessionPackage();
        });
    };
    panelPtr->onSaveProject = [safe, dismiss]
    {
        dismiss();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->saveProject();
        });
    };
    panelPtr->onBounce = [safe, dismiss]
    {
        dismiss();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->bounceArrangementEdits();
        });
    };
    panelPtr->onReveal = [safe, dismiss]
    {
        dismiss();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->revealLastRecording();
        });
    };
    juce::CallOutBox::launchAsynchronously (
        std::move (panel), centredAnchor (*this), this);
}

void MainPluginEditor::exportSessionPackage()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Choose a folder for the StemDeck session package",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this), chooser]
        (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;
            const auto parent = fc.getResult();
            if (! parent.isDirectory())
                return;
            auto xml = safe->makeProjectState().createXml();
            if (xml == nullptr)
            {
                safe->recordStatus.setText ("Could not serialize the session",
                                            juce::dontSendNotification);
                return;
            }

            const auto stamp = juce::Time::getCurrentTime().formatted (
                "%Y-%m-%d_%H-%M-%S");
            const auto destination = parent.getChildFile (
                "StemDeck Session " + stamp);

            juce::String manifest (
                "StemDeck Source Manifest\n"
                "========================\n"
                "Source audio is referenced, not duplicated. Recordings are copied.\n\n");
            for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
            {
                const auto path = safe->processorRef.getDeckPath (deck);
                manifest << "Deck " << juce::String::charToString (
                    (juce_wchar) ('A' + deck)) << ": "
                         << (path.isNotEmpty() ? path : "(empty)")
                         << (path.isNotEmpty() && ! juce::File (path).existsAsFile()
                                 ? " [MISSING]" : "")
                         << "\n";
            }

            std::vector<juce::File> recordings;
            auto addUnique = [&recordings] (const juce::File& file)
            {
                if (file.existsAsFile()
                    && std::find (recordings.begin(), recordings.end(), file)
                           == recordings.end())
                    recordings.push_back (file);
            };
            addUnique (safe->processorRef.getMasterRecordingTarget());
            addUnique (safe->processorRef.getMicRecordingTarget());
            addUnique (safe->processorRef.getLastPerformanceEventLogFile());
            addUnique (safe->processorRef.getLastArrangementFile());
            for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
                addUnique (safe->processorRef.getLastDeckRecordingFile (deck));

            safe->recordStatus.setText ("Packaging session…",
                                        juce::dontSendNotification);
            safe->uiWorkPool.addJob (
                new SessionPackageJob (
                    destination, xml->toString(), std::move (manifest),
                    safe->buildDiagnosticsReport(), std::move (recordings),
                    [safe] (bool ok, juce::File folder)
                    {
                        if (safe == nullptr)
                            return;
                        safe->recordStatus.setText (
                            ok ? "Exported session package: " + folder.getFileName()
                               : "Session package finished with copy errors",
                            juce::dontSendNotification);
                        if (ok)
                            folder.revealToUser();
                    }),
                true);
        });
}
