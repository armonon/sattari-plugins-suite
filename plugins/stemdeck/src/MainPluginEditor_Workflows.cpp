#include "MainPluginEditor.h"
#include <algorithm>

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

struct PackageFile
{
    juce::File source;
    juce::String relativePath;
};

class SessionPackageJob final : public juce::ThreadPoolJob
{
public:
    SessionPackageJob (juce::File packageDirectory,
                       juce::String projectXml,
                       juce::String sourceManifest,
                       juce::String diagnostics,
                       std::vector<PackageFile> packageFiles,
                       std::function<void (bool, juce::File)> completionCallback)
        : ThreadPoolJob ("StemDeck session package"),
          directory (std::move (packageDirectory)),
          project (std::move (projectXml)),
          sources (std::move (sourceManifest)),
          report (std::move (diagnostics)),
          files (std::move (packageFiles)),
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

        for (const auto& item : files)
        {
            if (shouldExit()) return jobHasFinished;
            if (! item.source.existsAsFile()) continue;
            const auto target = directory.getChildFile (item.relativePath);
            target.getParentDirectory().createDirectory();
            ok = item.source.copyFileTo (target) && ok;
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
    std::vector<PackageFile> files;
    std::function<void (bool, juce::File)> completion;
};

struct MixdownClip
{
    juce::File file;
    double timelineStart = 0.0;
    double sourceStart = 0.0;
    double length = 0.0;
    double fadeIn = 0.0;
    double fadeOut = 0.0;
    float gain = 1.0f;
    std::vector<std::pair<double, float>> gainAutomation;
};

struct MixdownMasterSettings
{
    float gainDb = 0.0f;
    float fxX = 0.5f;
    float fxY = 0.0f;
    bool smartOn = false;
    int smartMode = 0;
    float smartAmount = 0.0f;
    float limiterCeilingDb = -1.0f;
};

class ArrangementMixdownJob final : public juce::ThreadPoolJob
{
public:
    ArrangementMixdownJob (
        juce::File destinationFile, double outputSampleRate,
        std::vector<MixdownClip> sourceClips,
        std::vector<MidiClipPlayer::SourceNote> sourceMidi,
        double sourceMidiBpm,
        std::array<float, MidiClipPlayer::numLanes> sourceMidiGains,
        MixdownMasterSettings sourceMaster,
        std::function<void (bool, juce::File, float)> completionCallback)
        : ThreadPoolJob ("StemDeck arrangement mixdown"),
          destination (std::move (destinationFile)),
          sampleRate (outputSampleRate > 0.0 ? outputSampleRate : 48000.0),
          clips (std::move (sourceClips)),
          midiNotes (std::move (sourceMidi)),
          midiBpm (sourceMidiBpm),
          midiGains (sourceMidiGains),
          master (sourceMaster),
          completion (std::move (completionCallback))
    {
    }

    JobStatus runJob() override
    {
        struct RuntimeClip
        {
            MixdownClip spec;
            std::unique_ptr<juce::AudioFormatReader> reader;
            juce::int64 timelineStart = 0, timelineLength = 0;
        };

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
       #if JUCE_USE_MP3AUDIOFORMAT
        formats.registerFormat (new juce::MP3AudioFormat(), true);
       #endif

        std::vector<RuntimeClip> runtime;
        runtime.reserve (clips.size());
        juce::int64 totalSamples = 0;
        for (const auto& clip : clips)
        {
            auto reader = std::unique_ptr<juce::AudioFormatReader> (
                formats.createReaderFor (clip.file));
            if (reader == nullptr || reader->sampleRate <= 0.0
                || clip.length <= 0.0)
                continue;
            RuntimeClip r;
            r.spec = clip;
            r.reader = std::move (reader);
            r.timelineStart = (juce::int64) std::llround (
                clip.timelineStart * sampleRate);
            r.timelineLength = (juce::int64) std::llround (
                clip.length * sampleRate);
            totalSamples = juce::jmax (
                totalSamples, r.timelineStart + r.timelineLength);
            runtime.push_back (std::move (r));
        }

        MidiClipPlayer midiPlayer;
        midiPlayer.prepare (sampleRate, blockSize);
        midiPlayer.setNotes (midiNotes, midiBpm);
        for (int lane = 0; lane < MidiClipPlayer::numLanes; ++lane)
            midiPlayer.setLaneGain (lane, midiGains[(size_t) lane]);
        totalSamples = juce::jmax (
            totalSamples,
            (juce::int64) std::llround (
                (midiPlayer.lengthSeconds() + (midiNotes.empty() ? 0.0 : 0.5))
                * sampleRate));

        if ((runtime.empty() && midiNotes.empty())
            || totalSamples <= 0 || shouldExit())
            return finish (false, 0.0f);

        juce::AudioBuffer<float> block (2, blockSize);
        MasterFx masterFx;
        SmartMaster smartMaster;
        masterFx.prepare (sampleRate, blockSize, 2);
        smartMaster.prepare (sampleRate);

        auto automationGainAt = [] (
            const std::vector<std::pair<double, float>>& points,
            double seconds)
        {
            if (points.empty()) return 1.0f;
            if (seconds <= points.front().first) return points.front().second;
            if (seconds >= points.back().first)  return points.back().second;
            const auto next = std::lower_bound (
                points.begin(), points.end(), seconds,
                [] (const auto& point, double time)
                {
                    return point.first < time;
                });
            const auto& b = *next;
            const auto& a = *(next - 1);
            const double span = juce::jmax (1.0e-9, b.first - a.first);
            const float alpha = (float) ((seconds - a.first) / span);
            return a.second + alpha * (b.second - a.second);
        };

        auto renderBlock = [&] (juce::int64 blockStart, int count)
        {
            block.clear();
            const juce::int64 blockEnd = blockStart + count;
            for (auto& r : runtime)
            {
                const juce::int64 clipEnd =
                    r.timelineStart + r.timelineLength;
                const juce::int64 from =
                    juce::jmax (blockStart, r.timelineStart);
                const juce::int64 to =
                    juce::jmin (blockEnd, clipEnd);
                if (to <= from) continue;

                const double sourceRate = r.reader->sampleRate;
                const double ratio = sourceRate / sampleRate;
                const juce::int64 sinceClip = from - r.timelineStart;
                const double firstSource =
                    r.spec.sourceStart * sourceRate
                    + (double) sinceClip * ratio;
                const int outputCount = (int) (to - from);
                const juce::int64 readStart =
                    juce::jmax<juce::int64> (
                        0, (juce::int64) std::floor (firstSource));
                const int readCount = juce::jmax (
                    2, (int) std::ceil (
                           (double) outputCount * ratio) + 3);
                const int sourceChannels =
                    juce::jlimit (1, 8, (int) r.reader->numChannels);
                juce::AudioBuffer<float> source (
                    sourceChannels, readCount);
                source.clear();
                if (! r.reader->read (
                        &source, 0, readCount, readStart, true, true))
                    continue;

                for (int n = 0; n < outputCount; ++n)
                {
                    const double absoluteSource =
                        firstSource + (double) n * ratio;
                    const double local = absoluteSource - (double) readStart;
                    const int s0 = juce::jlimit (
                        0, readCount - 1, (int) std::floor (local));
                    const int s1 = juce::jmin (readCount - 1, s0 + 1);
                    const float frac = (float) (local - (double) s0);
                    const double timeInClip =
                        (double) (sinceClip + n) / sampleRate;
                    float gain = r.spec.gain;
                    gain *= automationGainAt (
                        r.spec.gainAutomation,
                        (double) (from + n) / sampleRate);
                    if (r.spec.fadeIn > 0.0
                        && timeInClip < r.spec.fadeIn)
                        gain *= (float) (timeInClip / r.spec.fadeIn);
                    if (r.spec.fadeOut > 0.0
                        && timeInClip > r.spec.length - r.spec.fadeOut)
                        gain *= (float) juce::jlimit (
                            0.0, 1.0,
                            (r.spec.length - timeInClip)
                                / r.spec.fadeOut);

                    const int dest = (int) (from - blockStart) + n;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const int sourceCh =
                            juce::jmin (ch, sourceChannels - 1);
                        const float* data =
                            source.getReadPointer (sourceCh);
                        block.addSample (
                            ch, dest,
                            (data[s0] + frac * (data[s1] - data[s0]))
                                * gain);
                    }
                }
            }
            midiPlayer.process (block, blockStart, count);
            masterFx.process (block, master.fxX, master.fxY);
            block.applyGain (
                juce::Decibels::decibelsToGain (master.gainDb));
            smartMaster.process (
                block, master.smartMode,
                master.smartOn ? master.smartAmount : 0.0f);
        };

        // First pass measures the edited mix. Do not boost quiet material; only
        // protect exports that exceed a -1 dBFS ceiling.
        float peak = 0.0f;
        for (juce::int64 pos = 0; pos < totalSamples;
             pos += blockSize)
        {
            if (shouldExit()) return finish (false, peak);
            const int count = (int) juce::jmin<juce::int64> (
                blockSize, totalSamples - pos);
            renderBlock (pos, count);
            peak = juce::jmax (
                peak, juce::jmax (
                    block.getMagnitude (0, count),
                    block.getMagnitude (1, count)));
        }
        const float target = juce::Decibels::decibelsToGain (
            master.limiterCeilingDb);
        const float outputGain =
            peak > target ? target / peak : 1.0f;

        destination.getParentDirectory().createDirectory();
        destination.deleteFile();
        auto stream = destination.createOutputStream();
        if (stream == nullptr)
            return finish (false, peak);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (
                stream.get(), sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return finish (false, peak);
        stream.release();

        // The measurement and write passes must begin from identical state.
        masterFx.reset();
        smartMaster.reset();
        midiPlayer.allNotesOff();
        for (juce::int64 pos = 0; pos < totalSamples;
             pos += blockSize)
        {
            if (shouldExit()) return finish (false, peak);
            const int count = (int) juce::jmin<juce::int64> (
                blockSize, totalSamples - pos);
            renderBlock (pos, count);
            block.applyGain (0, count, outputGain);
            if (! writer->writeFromAudioSampleBuffer (
                    block, 0, count))
                return finish (false, peak);
        }
        writer.reset();
        return finish (destination.existsAsFile(), peak * outputGain);
    }

private:
    JobStatus finish (bool ok, float peak)
    {
        juce::MessageManager::callAsync (
            [callback = completion, ok, file = destination, peak]
            {
                if (callback) callback (ok, file, peak);
            });
        return jobHasFinished;
    }

    juce::File destination;
    double sampleRate = 48000.0;
    static constexpr int blockSize = 4096;
    std::vector<MixdownClip> clips;
    std::vector<MidiClipPlayer::SourceNote> midiNotes;
    double midiBpm = 120.0;
    std::array<float, MidiClipPlayer::numLanes> midiGains {};
    MixdownMasterSettings master;
    std::function<void (bool, juce::File, float)> completion;
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
    panelPtr->onMixdown = [safe, dismiss]
    {
        dismiss();
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr) safe->exportArrangementMixdown();
        });
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

void MainPluginEditor::exportArrangementMixdown()
{
    const auto& lanes = arrangementView.getClips();
    std::vector<MixdownClip> clips;
    for (int lane = 0; lane < ArrangementRecorderView::numLanes;
         ++lane)
    {
        if (! arrangementView.laneAudible (lane))
            continue;
        const float laneGain =
            arrangementView.laneGainLinear (lane);
        for (const auto& clip : lanes[(size_t) lane])
        {
            if (! clip.file.existsAsFile()
                || clip.durationSeconds <= 0.0)
                continue;
            const double start = juce::jlimit (
                0.0, clip.durationSeconds,
                clip.trimStartSeconds);
            const double end = juce::jlimit (
                start, clip.durationSeconds,
                clip.trimEndSeconds > 0.0
                    ? clip.trimEndSeconds : clip.durationSeconds);
            if (end - start <= 0.001)
                continue;
            MixdownClip mix {
                clip.file, clip.timelineStartSeconds, start,
                end - start, clip.fadeInSeconds,
                clip.fadeOutSeconds, laneGain, {}
            };
            if (const auto* automation =
                    arrangementView.editAutomationForLane (lane);
                automation != nullptr && automation->visible
                    && automation->target == 0)
                mix.gainAutomation = automation->points;
            clips.push_back (std::move (mix));
        }
    }

    const double midiBpm = juce::jlimit (
        40.0, 240.0,
        (double) processorRef.parameters()
            .getRawParameterValue ("masterBpm")->load());
    const double secsPerBeat = 60.0 / midiBpm;
    std::vector<MidiClipPlayer::SourceNote> midiNotes;
    std::array<float, MidiClipPlayer::numLanes> midiGains {};
    midiGains.fill (1.0f);
    for (int lane = 0;
         lane < ArrangementRecorderView::numEditableLanes; ++lane)
    {
        if (! arrangementView.laneAudible (lane))
        {
            midiGains[(size_t) lane] = 0.0f;
            continue;
        }
        midiGains[(size_t) lane] =
            arrangementView.laneGainLinear (lane);
        for (int clip = 0;
             clip < arrangementView.numMidiClips (lane); ++clip)
        {
            const double startBeats =
                arrangementView.midiClipStart (lane, clip)
                / secsPerBeat;
            for (const auto& note :
                 arrangementView.midiClipNotes (lane, clip))
                midiNotes.push_back ({
                    lane, note.pitch,
                    startBeats + note.startBeats,
                    note.lengthBeats, note.velocity
                });
        }
    }

    MixdownMasterSettings master;
    auto value = [this] (const juce::String& id, float fallback)
    {
        if (auto* parameter =
                processorRef.parameters().getRawParameterValue (id))
            return parameter->load();
        return fallback;
    };
    master.gainDb = value ("master", 0.0f);
    master.fxX = value ("masterFxX", 0.5f);
    master.fxY = value ("masterFxY", 0.0f);
    master.smartOn = value ("smartMasterOn", 0.0f) > 0.5f;
    master.smartMode = (int) std::round (
        value ("smartMasterMode", 0.0f));
    master.smartAmount = value ("smartMasterAmount", 0.0f);
    master.limiterCeilingDb = value ("limiterCeiling", -1.0f);

    if (clips.empty() && midiNotes.empty())
    {
        recordStatus.setText (
            "Nothing to export · record, import, or arrange audio first",
            juce::dontSendNotification);
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> (
        "Export final StemDeck mix",
        juce::File::getSpecialLocation (
            juce::File::userMusicDirectory)
            .getChildFile ("StemDeck Final Mix.wav"),
        "*.wav");
    chooser->launchAsync (
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<MainPluginEditor> (this),
         chooser, clips = std::move (clips),
         midiNotes = std::move (midiNotes), midiBpm, midiGains, master]
        (const juce::FileChooser& fc) mutable
        {
            if (safe == nullptr) return;
            auto destination = fc.getResult();
            if (destination == juce::File()) return;
            destination = destination.withFileExtension ("wav");
            safe->recordStatus.setText (
                "Rendering final mix…",
                juce::dontSendNotification);
            safe->uiWorkPool.addJob (
                new ArrangementMixdownJob (
                    destination,
                    safe->processorRef.getRecordingSampleRate(),
                    std::move (clips),
                    std::move (midiNotes),
                    midiBpm,
                    midiGains,
                    master,
                    [safe] (bool ok, juce::File file, float peak)
                    {
                        if (safe == nullptr) return;
                        safe->recordStatus.setText (
                            ok
                                ? "Exported final mix: "
                                      + file.getFileName()
                                      + " · peak "
                                      + juce::String (
                                          juce::Decibels::gainToDecibels (
                                              juce::jmax (
                                                  peak, 1.0e-9f)),
                                          1)
                                      + " dBFS"
                                : "Final mix export failed",
                            juce::dontSendNotification);
                        if (ok) file.revealToUser();
                    }),
                true);
        });
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
            const auto stamp = juce::Time::getCurrentTime().formatted (
                "%Y-%m-%d_%H-%M-%S");
            const auto destination = parent.getChildFile (
                "StemDeck Session " + stamp);

            juce::String manifest (
                "StemDeck Source Manifest\n"
                "========================\n"
                "Source audio is copied into Media and recordings into Recordings.\n"
                "Session.stemdeck contains an exact package media map, so the folder "
                "can be moved and reopened without repairing paths.\n\n");
            std::vector<juce::File> sourceMedia;
            auto addUniqueSource = [&sourceMedia] (
                const juce::File& file)
            {
                if (file.existsAsFile()
                    && std::find (
                           sourceMedia.begin(), sourceMedia.end(), file)
                           == sourceMedia.end())
                    sourceMedia.push_back (file);
            };
            for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
            {
                const auto path = safe->processorRef.getDeckPath (deck);
                manifest << "Deck " << juce::String::charToString (
                    (juce_wchar) ('A' + deck)) << ": "
                         << (path.isNotEmpty() ? path : "(empty)")
                         << (path.isNotEmpty() && ! juce::File (path).existsAsFile()
                                 ? " [MISSING]" : "")
                         << "\n";
                if (path.isNotEmpty())
                    addUniqueSource (juce::File (path));
                for (int stem = 1;
                     stem < DeckPlayer::numStemLanes; ++stem)
                {
                    const auto stemPath =
                        safe->processorRef.getDeckStemPath (
                            deck, stem);
                    if (stemPath.isNotEmpty())
                        addUniqueSource (juce::File (stemPath));
                }
            }
            const auto& processorState =
                safe->processorRef.parameters().state;
            for (int pad = 0; pad < PadBank::numPads; ++pad)
            {
                const auto path = processorState.getProperty (
                    "pad" + juce::String (pad + 1) + "File")
                                      .toString();
                if (path.isNotEmpty())
                    addUniqueSource (juce::File (path));
            }
            for (const auto& lane : safe->arrangementView.getClips())
                for (const auto& clip : lane)
                    addUniqueSource (clip.file);

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

            std::vector<PackageFile> packageFiles;
            juce::StringArray usedRelativePaths;
            auto plan = [&packageFiles, &usedRelativePaths] (
                const std::vector<juce::File>& sources,
                const juce::String& folder)
            {
                for (const auto& source : sources)
                {
                    juce::String name = source.getFileName();
                    juce::String relative = folder + "/" + name;
                    for (int copy = 2;
                         usedRelativePaths.contains (relative); ++copy)
                    {
                        name = source.getFileNameWithoutExtension()
                               + "-" + juce::String (copy)
                               + source.getFileExtension();
                        relative = folder + "/" + name;
                    }
                    usedRelativePaths.add (relative);
                    packageFiles.push_back ({ source, relative });
                }
            };
            plan (sourceMedia, "Media");
            plan (recordings, "Recordings");

            auto projectState = safe->makeProjectState();
            juce::ValueTree mediaMap ("PACKAGE_MEDIA");
            for (const auto& item : packageFiles)
            {
                juce::ValueTree entry ("FILE");
                entry.setProperty (
                    "original", item.source.getFullPathName(), nullptr);
                entry.setProperty (
                    "relative", item.relativePath, nullptr);
                mediaMap.appendChild (entry, nullptr);
            }
            projectState.appendChild (mediaMap, nullptr);
            auto xml = projectState.createXml();
            if (xml == nullptr)
            {
                safe->recordStatus.setText ("Could not serialize the session",
                                            juce::dontSendNotification);
                return;
            }

            safe->recordStatus.setText ("Packaging session…",
                                        juce::dontSendNotification);
            safe->uiWorkPool.addJob (
                new SessionPackageJob (
                    destination, xml->toString(), std::move (manifest),
                    safe->buildDiagnosticsReport(),
                    std::move (packageFiles),
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
