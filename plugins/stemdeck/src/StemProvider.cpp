#include "StemProvider.h"

namespace
{
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

juce::var objectWith (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* object = new juce::DynamicObject();
    for (const auto& [key, value] : properties)
        object->setProperty (key, value);
    return juce::var (object);
}
}

class StemProvider::StemJob final : public juce::ThreadPoolJob
{
public:
    StemJob (StemProvider& owner, juce::File source, Callback cb,
             juce::int64 playhead = 0, ProgressiveStemCallback progressive = {})
        : ThreadPoolJob ("StemDeck offline stem cache"), provider (owner), sourceFile (std::move (source)),
          callback (std::move (cb)), playheadSample (playhead), onProgressiveStem (std::move (progressive)) {}

    JobStatus runJob() override
    {
        // ThreadPool::removeAllJobs(interruptRunningJobs=true, ...) works by making
        // shouldExit() return true — a job that never asks is not interruptible, and
        // the pool simply waits for it. This job used to never ask, so the -1 (wait
        // forever) join in StemProvider::shutdown() could block for the whole
        // separation: up to the separator's 45-minute timeout, with the host frozen.
        if (shouldExit())
            return jobHasFinished;

        auto result = provider.inspectCache (sourceFile);
        provider.updateStatus (sourceFile, StemProvider::statusFromResult (result, false, true, provider.getSeparatorCommand().trim().isNotEmpty(), u8 ("Preparing premium offline stem cache\xe2\x80\xa6")));
        if (! result.allCoreStemsReady)
        {
            juce::String message;
            // Prefer in-process on-device neural (ONNX/CoreML) when a model is loaded
            // — faster, no subprocess; else fall back to the demucs CLI. When a
            // progressive callback is set, stream playhead-first so stems near the
            // playhead publish within a segment or two instead of at the very end.
            bool done = false;
            if (provider.neuralAvailable())
                done = onProgressiveStem
                     ? provider.runNeuralSeparationStreaming (sourceFile, result.cacheDirectory, playheadSample,
                                                              onProgressiveStem, message,
                                                              [this] { return shouldExit(); })
                     : provider.runNeuralSeparation (sourceFile, result.cacheDirectory, message,
                                                     [this] { return shouldExit(); });
            if (! done && ! shouldExit())
            {
                juce::String cliMsg;
                if (StemProvider::runSeparatorCommand (provider.getSeparatorCommand(), sourceFile,
                                                       result.cacheDirectory, cliMsg,
                                                       [this] { return shouldExit(); }))
                    done = true;
                message = message.isNotEmpty() ? message + u8 (" \xe2\x80\xa2 ") + cliMsg : cliMsg;
            }

            // A cancelled job publishes nothing: its cache is half-written and the
            // owning processor is being destroyed, so the completion callback must
            // not fire against it.
            if (shouldExit())
                return jobHasFinished;

            if (done)
                result = provider.inspectCache (sourceFile);

            if (message.isNotEmpty())
                result.message = message + u8 (" \xe2\x80\xa2 ") + result.message;
        }
        provider.updateStatus (sourceFile, StemProvider::statusFromResult (result, false, false, provider.getSeparatorCommand().trim().isNotEmpty()));
        provider.writeManifest (result);

        juce::MessageManager::callAsync ([result, cb = callback]() mutable
        {
            if (cb)
                cb (std::move (result));
        });
        return jobHasFinished;
    }

private:
    StemProvider& provider;
    juce::File sourceFile;
    Callback callback;
    juce::int64 playheadSample = 0;
    ProgressiveStemCallback onProgressiveStem;
};

StemProvider::StemProvider()
{
    setCacheRoot (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Sattari")
                    .getChildFile ("StemDeck")
                    .getChildFile ("StemCache"));
    separatorCommand = juce::SystemStats::getEnvironmentVariable ("SATTARI_STEMDECK_SEPARATOR_CMD", {});
    if (separatorCommand.trim().isEmpty())
    {
        auto configFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("Sattari")
                            .getChildFile ("StemDeck")
                            .getChildFile ("StemProvider")
                            .getChildFile ("separator-command.txt");
        if (configFile.existsAsFile())
            separatorCommand = configFile.loadFileAsString().trim();
    }

    // Auto-detect demucs if still not configured. An optional model override
    // (e.g. "htdemucs_ft" — the fine-tuned, higher-quality model) is read from
    // StemProvider/separator-model.txt and passed via demucs' -n flag.
    if (separatorCommand.trim().isEmpty())
    {
        juce::String modelArgs;
        {
            auto modelFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                               .getChildFile ("Sattari").getChildFile ("StemDeck")
                               .getChildFile ("StemProvider").getChildFile ("separator-model.txt");
            if (modelFile.existsAsFile())
            {
                const auto model = modelFile.loadFileAsString().trim();
                if (model.isNotEmpty() && ! model.containsAnyOf (" \t\r\n\"'"))
                    modelArgs = " -n " + model;
            }
        }

        const juce::StringArray candidatePaths {
            "/Users/" + juce::SystemStats::getLogonName() + "/Library/Python/3.9/bin/demucs",
            "/Users/" + juce::SystemStats::getLogonName() + "/Library/Python/3.10/bin/demucs",
            "/Users/" + juce::SystemStats::getLogonName() + "/Library/Python/3.11/bin/demucs",
            "/usr/local/bin/demucs",
            "/opt/homebrew/bin/demucs",
            "/opt/homebrew/opt/demucs/bin/demucs",
        };
        for (const auto& path : candidatePaths)
        {
            if (juce::File (path).existsAsFile())
            {
                separatorCommand = path + modelArgs + " --out {output} {input}";
                break;
            }
        }
        // Also search PATH directly. Spawning `which` from every processor
        // constructor was surprisingly expensive in DAWs restoring several
        // StemDeck instances and could block behind shell/process pressure. PATH is
        // already authoritative here, and direct file probes are deterministic.
        if (separatorCommand.trim().isEmpty())
        {
            const auto pathValue = juce::SystemStats::getEnvironmentVariable ("PATH", {});
            juce::StringArray pathEntries;
            pathEntries.addTokens (pathValue, ":", "");
            for (const auto& entry : pathEntries)
            {
                const auto executable = juce::File (entry.trim()).getChildFile ("demucs");
                if (executable.existsAsFile())
                {
                    separatorCommand = executable.getFullPathName()
                                     + modelArgs + " --out {output} {input}";
                    break;
                }
            }
        }
    }

    // Resolve the model now, but load it only when the user explicitly requests a
    // split. Model construction is both slow and memory-heavy.
    if (NeuralStemEngine::isCompiledIn())
        neuralModelFile = NeuralStemEngine::defaultModelFile();
}

StemProvider::~StemProvider()
{
    shutdown();
}

bool StemProvider::neuralAvailable()
{
    if (neuralEngine.isAvailable())
        return true;

    const juce::ScopedLock lock (neuralLoadLock);
    if (neuralEngine.isAvailable())
        return true;
    if (neuralLoadAttempted)
        return false;

    neuralLoadAttempted = true;
    if (! NeuralStemEngine::isCompiledIn() || ! neuralModelFile.existsAsFile())
        return false;

    juce::String message;
    return neuralEngine.loadModel (neuralModelFile, message);
}

void StemProvider::shutdown()
{
    {
        const juce::ScopedLock lock (lifecycleLock);
        if (shuttingDown)
            return;
        shuttingDown = true;
    }

    neuralEngine.cancelPending();
    // A timeout here used to let the owning processor continue tearing down while
    // a provider callback could still enqueue analysis work. Join fully: separation
    // is off the message thread, and lifetime safety takes priority during shutdown.
    pool.removeAllJobs (true, -1);
}

void StemProvider::cancelPending()
{
    neuralEngine.cancelPending();
    // Signal queued/running jobs without making the message thread wait for the
    // separator process to unwind. Jobs observe shouldExit()/the neural cancel
    // token and finish in the background; shutdown() still performs the full join.
    pool.removeAllJobs (true, 0);
    const juce::ScopedLock lock (statusLock);
    for (auto& [hash, status] : statusByHash)
    {
        juce::ignoreUnused (hash);
        if (status.jobQueued || status.jobRunning)
        {
            status.jobQueued = false;
            status.jobRunning = false;
            status.headline = "Stem separation cancelled";
            status.detail = "Existing completed stem cache remains available";
        }
    }
}

bool StemProvider::runNeuralSeparation (const juce::File& sourceFile, const juce::File& cacheDirectory,
                                        juce::String& message, const std::function<bool()>& shouldCancel)
{
    if (! neuralEngine.isAvailable()) { message = "neural model not loaded"; return false; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sourceFile));
    if (reader == nullptr) { message = "could not read source for neural separation"; return false; }

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    juce::AudioBuffer<float> input (2, juce::jmax (1, len));
    reader->read (&input, 0, len, 0, true, true);
    const double sr = reader->sampleRate;

    juce::AudioBuffer<float> d, b, m, v;
    if (! neuralEngine.separate (input, sr, d, b, m, v, nullptr, message, shouldCancel))
        return false;

    cacheDirectory.createDirectory();
    auto writeWav = [&] (const juce::AudioBuffer<float>& buf, const juce::String& name) -> bool
    {
        auto outFile = cacheDirectory.getChildFile (name);
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream());
        if (os == nullptr) return false;
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sr)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24);
        auto writer = wav.createWriterFor (os, options);
        if (writer == nullptr) return false;
        return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    };

    const bool ok = writeWav (d, "drums.wav") && writeWav (b, "bass.wav")
                 && writeWav (m, "music.wav") && writeWav (v, "vocals.wav");
    message = ok ? "on-device neural separation complete" : "neural stem write failed";
    return ok;
}

bool StemProvider::runNeuralSeparationStreaming (const juce::File& sourceFile, const juce::File& cacheDirectory,
                                                 juce::int64 playheadSample,
                                                 const ProgressiveStemCallback& onProgressiveStem, juce::String& message,
                                                 const std::function<bool()>& shouldCancel)
{
    if (! neuralEngine.isAvailable()) { message = "neural model not loaded"; return false; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (sourceFile));
    if (reader == nullptr) { message = "could not read source for neural separation"; return false; }

    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    juce::AudioBuffer<float> input (2, juce::jmax (1, len));
    reader->read (&input, 0, len, 0, true, true);
    const double sr = reader->sampleRate;

    // Full-length accumulators, silent until each playhead-first segment fills its
    // region. Neural output order drums/bass/music/vocals -> DeckPlayer stem lanes.
    const int laneOf[4] = { DeckPlayer::drumsStem, DeckPlayer::bassStem,
                            DeckPlayer::musicStem, DeckPlayer::vocalsStem };
    std::array<juce::AudioBuffer<float>, 4> acc;
    for (auto& a : acc) { a.setSize (2, juce::jmax (1, len)); a.clear(); }

    auto onSegment = [&] (juce::int64 startSample,
                          const juce::AudioBuffer<float>& d,
                          const juce::AudioBuffer<float>& b,
                          const juce::AudioBuffer<float>& m,
                          const juce::AudioBuffer<float>& v)
    {
        const juce::AudioBuffer<float>* segs[4] = { &d, &b, &m, &v };
        const int start = (int) juce::jlimit<juce::int64> (0, len, startSample);
        for (int s = 0; s < 4; ++s)
        {
            const int segLen = juce::jmin (segs[s]->getNumSamples(), len - start);
            if (segLen <= 0)
                continue;
            for (int ch = 0; ch < 2; ++ch)
                acc[(size_t) s].copyFrom (ch, start, *segs[s],
                                          juce::jmin (ch, segs[s]->getNumChannels() - 1), 0, segLen);

            if (onProgressiveStem)
            {
                // Immutable snapshot of what's separated so far for this lane; the deck
                // swaps it in lock-free (atomic shared_ptr) and drops the previous one.
                auto snap = std::make_shared<DeckPlayer::LoadedAudio>();
                snap->audio = acc[(size_t) s];
                snap->sourceSampleRate = sr;
                snap->path = sourceFile.getFullPathName();
                snap->displayName = DeckPlayer::stemLaneLabel (laneOf[s]);
                onProgressiveStem (laneOf[s], std::move (snap));
            }
        }
    };

    juce::String sepMsg;
    if (! neuralEngine.separateStreaming (input, sr, playheadSample, onSegment, nullptr, sepMsg,
                                          shouldCancel))
    {
        message = sepMsg.isNotEmpty() ? sepMsg : juce::String ("neural streaming separation failed");
        return false;
    }

    // Persist the completed stems to the WAV cache so the next load is instant.
    cacheDirectory.createDirectory();
    auto writeWav = [&] (const juce::AudioBuffer<float>& buf, const juce::String& name) -> bool
    {
        auto outFile = cacheDirectory.getChildFile (name);
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream());
        if (os == nullptr) return false;
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sr)
                                 .withNumChannels (2)
                                 .withBitsPerSample (24);
        auto writer = wav.createWriterFor (os, options);
        if (writer == nullptr) return false;
        return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    };

    const bool ok = writeWav (acc[0], "drums.wav") && writeWav (acc[1], "bass.wav")
                 && writeWav (acc[2], "music.wav") && writeWav (acc[3], "vocals.wav");
    message = ok ? "on-device neural streaming separation complete" : "neural stem write failed";
    return ok;
}

void StemProvider::setCacheRoot (const juce::File& root)
{
    cacheRoot = root;
    if (! cacheRoot.exists())
        cacheRoot.createDirectory();
}

juce::File StemProvider::getCacheRoot() const
{
    return cacheRoot;
}

void StemProvider::setSeparatorCommand (juce::String command)
{
    separatorCommand = std::move (command);
}

juce::String StemProvider::getSeparatorCommand() const
{
    return separatorCommand;
}

juce::String StemProvider::hashForFile (const juce::File& sourceFile)
{
    juce::String key = sourceFile.getFullPathName() + ":" + juce::String (sourceFile.getSize())
                     + ":" + juce::String (sourceFile.getLastModificationTime().toMilliseconds());
    return juce::String::toHexString ((int64) key.hashCode64());
}

juce::File StemProvider::getCacheDirectoryForSource (const juce::File& sourceFile) const
{
    return cacheRoot.getChildFile (hashForFile (sourceFile));
}

juce::String StemProvider::laneCacheName (int lane)
{
    return DeckPlayer::stemLaneId (lane).toLowerCase() + ".wav";
}

bool StemProvider::isCoreStemLane (int lane) noexcept
{
    return lane == DeckPlayer::drumsStem || lane == DeckPlayer::bassStem
        || lane == DeckPlayer::musicStem || lane == DeckPlayer::vocalsStem;
}

void StemProvider::requestStemsAsync (const juce::File& sourceFile, Callback callback)
{
    if (! sourceFile.existsAsFile())
        return;

    // Dedupe: if a job for this exact source is already queued or running, drop the
    // duplicate. Two jobs share the same cache + "-separating" temp dir and would
    // race on deleteRecursively/moveFileTo. A completed job clears both flags, so
    // re-requests after completion still work.
    {
        const juce::ScopedLock lock (statusLock);
        auto it = statusByHash.find (hashForFile (sourceFile));
        if (it != statusByHash.end() && (it->second.jobQueued || it->second.jobRunning))
            return;
    }

    const auto result = inspectCache (sourceFile);
    updateStatus (sourceFile, statusFromResult (result, true, false, getSeparatorCommand().trim().isNotEmpty(),
                                                result.allCoreStemsReady ? "Premium stems already cached." : "Queued premium AI stem split."));
    {
        const juce::ScopedLock lock (lifecycleLock);
        if (shuttingDown)
            return;
        pool.addJob (new StemJob (*this, sourceFile, std::move (callback)), true);
    }
}

void StemProvider::requestStemsStreamingAsync (const juce::File& sourceFile,
                                               juce::int64 playheadSample,
                                               ProgressiveStemCallback onProgressiveStem,
                                               Callback onComplete)
{
    if (! sourceFile.existsAsFile())
        return;

    {
        const juce::ScopedLock lock (statusLock);
        auto it = statusByHash.find (hashForFile (sourceFile));
        if (it != statusByHash.end() && (it->second.jobQueued || it->second.jobRunning))
            return;
    }

    const auto result = inspectCache (sourceFile);
    updateStatus (sourceFile, statusFromResult (result, true, false, getSeparatorCommand().trim().isNotEmpty(),
                                                result.allCoreStemsReady ? juce::String ("Premium stems already cached.") : u8 ("Splitting AI stems from the playhead\xe2\x80\xa6")));
    {
        const juce::ScopedLock lock (lifecycleLock);
        if (shuttingDown)
            return;
        pool.addJob (new StemJob (*this, sourceFile, std::move (onComplete),
                                  playheadSample, std::move (onProgressiveStem)), true);
    }
}

StemProvider::Result StemProvider::inspectCache (const juce::File& sourceFile) const
{
    Result result;
    result.sourceFile = sourceFile;
    result.cacheDirectory = getCacheDirectoryForSource (sourceFile);
    result.cacheDirectory.createDirectory();

    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
    {
        auto& stem = result.stems[(size_t) lane];
        stem.lane = lane;
        stem.file = lane == DeckPlayer::fullMixStem ? sourceFile : result.cacheDirectory.getChildFile (laneCacheName (lane));
        stem.ready = stem.file.existsAsFile();
        stem.status = stem.ready ? "ready" : (isCoreStemLane (lane) ? "missing_cached_offline_stem" : "source_full_mix");
    }

    result.allCoreStemsReady = true;
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        if (isCoreStemLane (lane))
            result.allCoreStemsReady = result.allCoreStemsReady && result.stems[(size_t) lane].ready;

    result.message = result.allCoreStemsReady
        ? "Cached offline stems ready."
        : "Offline stem cache inspected; separation backend not run yet, so only existing cached WAVs will be loaded.";
    return result;
}

int StemProvider::readyCoreStemCount (const Result& result) noexcept
{
    int ready = 0;
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        if (isCoreStemLane (lane) && result.stems[(size_t) lane].ready)
            ++ready;
    return ready;
}

StemProvider::Status StemProvider::statusFromResult (const Result& result,
                                                     bool queued,
                                                     bool running,
                                                     bool backendConfigured,
                                                     const juce::String& detailOverride)
{
    Status status;
    status.sourceFile = result.sourceFile;
    status.cacheDirectory = result.cacheDirectory;
    status.readyCoreStems = readyCoreStemCount (result);
    status.allCoreStemsReady = result.allCoreStemsReady;
    status.jobQueued = queued;
    status.jobRunning = running;
    status.backendConfigured = backendConfigured;

    if (status.allCoreStemsReady)
        status.headline = u8 ("AI stems ready \xe2\x80\x94 vocals, drums, bass, music");
    else if (running)
        status.headline = backendConfigured ? u8 ("Splitting AI stems offline\xe2\x80\xa6") : juce::String ("Waiting for AI stem backend");
    else if (queued)
        status.headline = "AI stem split queued";
    else if (status.readyCoreStems > 0)
        status.headline = "Partial AI stem cache: " + juce::String (status.readyCoreStems) + "/" + juce::String (status.totalCoreStems);
    else
        status.headline = backendConfigured ? "AI stems not cached yet" : "AI stem backend not configured";

    status.detail = detailOverride.isNotEmpty() ? detailOverride : result.message;
    return status;
}

StemProvider::Status StemProvider::getStatusForSource (const juce::File& sourceFile) const
{
    if (! sourceFile.existsAsFile())
    {
        Status status;
        status.backendConfigured = getSeparatorCommand().trim().isNotEmpty();
        status.headline = "Load a full mix to enable AI stems";
        status.detail = status.backendConfigured ? "Offline separator configured." : "No offline separator configured.";
        return status;
    }

    const auto key = hashForFile (sourceFile);
    {
        const juce::ScopedLock lock (statusLock);
        auto found = statusByHash.find (key);
        if (found != statusByHash.end())
            return found->second;
    }

    return statusFromResult (inspectCache (sourceFile), false, false, getSeparatorCommand().trim().isNotEmpty());
}

juce::var StemProvider::statusToManifest (const Status& status)
{
    return objectWith ({
        { "headline", status.headline },
        { "detail", status.detail },
        { "readyCoreStems", status.readyCoreStems },
        { "totalCoreStems", status.totalCoreStems },
        { "allCoreStemsReady", status.allCoreStemsReady },
        { "jobQueued", status.jobQueued },
        { "jobRunning", status.jobRunning },
        { "backendConfigured", status.backendConfigured }
    });
}

void StemProvider::updateStatus (const juce::File& sourceFile, const Status& status)
{
    const juce::ScopedLock lock (statusLock);
    statusByHash[hashForFile (sourceFile)] = status;
}

juce::var StemProvider::resultToManifest (const Result& result)
{
    juce::Array<juce::var> stemVars;
    for (const auto& stem : result.stems)
    {
        stemVars.add (objectWith ({
            { "lane", stem.lane },
            { "id", DeckPlayer::stemLaneId (stem.lane) },
            { "label", DeckPlayer::stemLaneLabel (stem.lane) },
            { "path", stem.file.getFullPathName() },
            { "ready", stem.ready },
            { "status", stem.status }
        }));
    }

    return objectWith ({
        { "schema", "SattariStemDeck.stemCache.v1" },
        { "source", result.sourceFile.getFullPathName() },
        { "cacheDirectory", result.cacheDirectory.getFullPathName() },
        { "allCoreStemsReady", result.allCoreStemsReady },
        { "message", result.message },
        { "premiumStatus", statusToManifest (statusFromResult (result, false, false, result.allCoreStemsReady)) },
        { "stems", stemVars }
    });
}

void StemProvider::writeManifest (const Result& result) const
{
    auto manifest = result.cacheDirectory.getChildFile ("manifest.json");
    manifest.replaceWithText (juce::JSON::toString (resultToManifest (result), true), false, false, "\n");
}

bool StemProvider::runSeparatorCommand (const juce::String& command,
                                        const juce::File& sourceFile,
                                        const juce::File& cacheDirectory,
                                        juce::String& message,
                                        const std::function<bool()>& shouldCancel)
{
    if (command.trim().isEmpty())
    {
        message = "No offline separator command configured. Set SATTARI_STEMDECK_SEPARATOR_CMD.";
        return false;
    }

    auto tempDirectory = cacheDirectory.getSiblingFile (cacheDirectory.getFileName() + "-separating");
    if (tempDirectory.exists())
        tempDirectory.deleteRecursively();
    tempDirectory.createDirectory();

    auto expanded = command;
    expanded = expanded.replace ("{input}", sourceFile.getFullPathName().quoted());
    expanded = expanded.replace ("{output}", tempDirectory.getFullPathName().quoted());
    expanded = expanded.replace ("{cache}", cacheDirectory.getFullPathName().quoted());

    auto arguments = juce::StringArray::fromTokens (expanded, " \n\r\t", "\"'");
    for (auto& arg : arguments)
    {
        arg = arg.trim().unquoted();
        if (arg.startsWithChar ('\'') && arg.endsWithChar ('\'') && arg.length() >= 2)
            arg = arg.substring (1, arg.length() - 1);
    }

    if (! arguments.contains (sourceFile.getFullPathName()))
    {
        arguments.add ("--input");
        arguments.add (sourceFile.getFullPathName());
    }

    if (! arguments.contains (tempDirectory.getFullPathName()))
    {
        arguments.add ("--output-dir");
        arguments.add (tempDirectory.getFullPathName());
    }

    juce::ChildProcess process;
    if (arguments.isEmpty() || ! process.start (arguments))
    {
        message = "Could not start offline separator command.";
        tempDirectory.deleteRecursively();
        return false;
    }

    juce::String output;
    const uint32 startMs = juce::Time::getMillisecondCounter();
    constexpr uint32 timeoutMs = 45u * 60u * 1000u;
    while (process.isRunning())
    {
        output += process.readAllProcessOutput();
        // Shutdown must not wait on a separation. The only other exit from this
        // loop is the 45-minute timeout, so without this a plugin being removed
        // from a host would block its message thread for as long as demucs runs.
        if (shouldCancel && shouldCancel())
        {
            process.kill();
            message = "Offline separator cancelled.";
            tempDirectory.deleteRecursively();
            return false;
        }
        if (juce::Time::getMillisecondCounter() - startMs > timeoutMs)
        {
            process.kill();
            message = "Offline separator timed out.";
            tempDirectory.deleteRecursively();
            return false;
        }
        juce::Thread::sleep (100);
    }
    output += process.readAllProcessOutput();

    const auto exitCode = process.getExitCode();
    if (exitCode != 0)
    {
        message = "Offline separator failed with exit " + juce::String ((int64) exitCode) + ": " + output.trim().substring (0, 240);
        tempDirectory.deleteRecursively();
        return false;
    }

    const std::array<juce::String, 4> expected { "drums.wav", "bass.wav", "music.wav", "vocals.wav" };

    // Demucs writes to a subdirectory: {output}/{model}/{trackname}/stem.wav
    // Flatten any such subdirectory structure up to tempDirectory root.
    {
        juce::Array<juce::File> topDirs;
        tempDirectory.findChildFiles (topDirs, juce::File::findDirectories, false);
        for (auto& modelDir : topDirs)
        {
            juce::Array<juce::File> trackDirs;
            modelDir.findChildFiles (trackDirs, juce::File::findDirectories, false);
            // Also handle the case where stems are directly inside modelDir (not nested)
            if (trackDirs.isEmpty())
                trackDirs.add (modelDir);
            for (auto& trackDir : trackDirs)
            {
                for (const auto& name : expected)
                {
                    auto src = trackDir.getChildFile (name);
                    if (src.existsAsFile() && ! tempDirectory.getChildFile (name).existsAsFile())
                        src.copyFileTo (tempDirectory.getChildFile (name));
                }
                // demucs 4-stem model uses "other.wav" instead of "music.wav"
                auto other = trackDir.getChildFile ("other.wav");
                if (other.existsAsFile() && ! tempDirectory.getChildFile ("music.wav").existsAsFile())
                    other.copyFileTo (tempDirectory.getChildFile ("music.wav"));
            }
        }
    }

    // Also handle other.wav at root level
    if (! tempDirectory.getChildFile ("music.wav").existsAsFile()
        && tempDirectory.getChildFile ("other.wav").existsAsFile())
        tempDirectory.getChildFile ("other.wav").copyFileTo (tempDirectory.getChildFile ("music.wav"));

    bool allReady = true;
    for (const auto& name : expected)
        allReady = allReady && tempDirectory.getChildFile (name).existsAsFile();

    if (! allReady)
    {
        message = "Offline separator finished but did not produce drums/bass/music/vocals WAVs.";
        tempDirectory.deleteRecursively();
        return false;
    }

    cacheDirectory.createDirectory();
    for (const auto& name : expected)
    {
        auto finalFile = cacheDirectory.getChildFile (name);
        finalFile.deleteFile();
        tempDirectory.getChildFile (name).moveFileTo (finalFile);
    }

    tempDirectory.deleteRecursively();
    message = "Offline separator generated cached stems.";
    return true;
}
