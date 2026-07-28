#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <map>
#include "DeckPlayer.h"
#include "RoadmapAdapters.h"
#include "NeuralStemEngine.h"

// Offline-first AI stem provider. It never separates in processBlock: requests run
// on a ThreadPool, results are cached on disk, and decks atomically swap buffers
// only after the generated WAV files are ready and decoded by the normal loader.
class StemProvider final : public StemRemovalAdapter
{
public:
    struct StemFile
    {
        int lane = DeckPlayer::fullMixStem;
        juce::File file;
        bool ready = false;
        juce::String status;
    };

    struct Result
    {
        juce::File sourceFile;
        juce::File cacheDirectory;
        std::array<StemFile, DeckPlayer::numStemLanes> stems;
        bool allCoreStemsReady = false;
        juce::String message;
    };

    struct Status
    {
        juce::File sourceFile;
        juce::File cacheDirectory;
        int readyCoreStems = 0;
        int totalCoreStems = 4;
        bool allCoreStemsReady = false;
        bool jobQueued = false;
        bool jobRunning = false;
        bool backendConfigured = false;
        juce::String headline;
        juce::String detail;
    };

    using Callback = std::function<void(Result)>;

    // Progressive (playhead-first) publish. Invoked from a background thread as each
    // segment near the playhead finishes, with an immutable snapshot of one lane's
    // stem separated SO FAR (silent outside the completed region). Lets a deck play
    // separated audio near the playhead within a segment or two instead of waiting
    // for the whole track. laneIndex is a DeckPlayer stem lane.
    using ProgressiveStemCallback =
        std::function<void (int laneIndex, std::shared_ptr<const DeckPlayer::LoadedAudio> partial)>;

    StemProvider();
    ~StemProvider() override;

    // Stop accepting work and join every worker. MainPluginProcessor calls this
    // before draining its audio-analysis pool because a stem completion can enqueue
    // decoded-stem work there.
    void shutdown();

    juce::String getName() const override { return "StemDeck offline cached StemProvider"; }
    bool isAvailable() const override { return true; }

    void setCacheRoot (const juce::File& root);
    juce::File getCacheRoot() const;
    void setSeparatorCommand (juce::String command);
    juce::String getSeparatorCommand() const;
    juce::File getCacheDirectoryForSource (const juce::File& sourceFile) const;

    void requestStemsAsync (const juce::File& sourceFile, Callback callback);

    // As requestStemsAsync, but separates playhead-first and publishes partial
    // in-memory stems via onProgressiveStem as they complete (on-device neural only;
    // silently behaves like the offline path when neural streaming is unavailable).
    void requestStemsStreamingAsync (const juce::File& sourceFile,
                                     juce::int64 playheadSample,
                                     ProgressiveStemCallback onProgressiveStem,
                                     Callback onComplete);

    Result inspectCache (const juce::File& sourceFile) const;
    Status getStatusForSource (const juce::File& sourceFile) const;
    // `shouldCancel` is polled while waiting on the external separator. When it goes
    // true the child process is KILLED and the call returns false. Without this the
    // wait is bounded only by a 45-minute timeout, so a shutdown that joins the pool
    // would block for as long as the separation takes.
    static bool runSeparatorCommand (const juce::String& command, const juce::File& sourceFile,
                                     const juce::File& cacheDirectory, juce::String& message,
                                     const std::function<bool()>& shouldCancel = {});

    // On-device neural separation (ONNX Runtime). Writes the 4 core-stem WAVs into
    // cacheDirectory. Preferred over the CLI when a model is loaded. Returns false
    // (with message) so callers fall back to the demucs CLI.
    bool runNeuralSeparation (const juce::File& sourceFile, const juce::File& cacheDirectory, juce::String& message);

    // Streaming variant: separates playhead-first and calls onProgressiveStem with an
    // immutable per-lane snapshot as each segment lands, then writes the WAV cache so
    // the next load is instant. Returns false (with message) to fall back to the CLI.
    bool runNeuralSeparationStreaming (const juce::File& sourceFile, const juce::File& cacheDirectory,
                                       juce::int64 playheadSample,
                                       const ProgressiveStemCallback& onProgressiveStem, juce::String& message);
    bool neuralAvailable() const { return neuralEngine.isAvailable(); }

private:
    class StemJob;

    juce::File cacheRoot;
    juce::String separatorCommand;
    NeuralStemEngine neuralEngine;
    juce::ThreadPool pool { 2 };
    juce::CriticalSection lifecycleLock;
    bool shuttingDown = false;
    mutable juce::CriticalSection statusLock;
    std::map<juce::String, Status> statusByHash;

    static juce::String hashForFile (const juce::File& sourceFile);
    static juce::String laneCacheName (int lane);
    static bool isCoreStemLane (int lane) noexcept;
    static int readyCoreStemCount (const Result& result) noexcept;
    static Status statusFromResult (const Result& result, bool queued, bool running, bool backendConfigured, const juce::String& detailOverride = {});
    static juce::var resultToManifest (const Result& result);
    static juce::var statusToManifest (const Status& status);
    void updateStatus (const juce::File& sourceFile, const Status& status);
    void writeManifest (const Result& result) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemProvider)
};
