#pragma once

#include <JuceHeader.h>
#include <array>
#include <map>
#include <memory>
#include "DeckPlayer.h"
#include "RoadmapAdapters.h"
#include "NeuralStemEngine.h"
#include "ComponentStoragePolicy.h"

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

    StemProvider();
    explicit StemProvider (ComponentStoragePolicy);
    ~StemProvider() override;

    juce::String getName() const override { return "StemDeck offline cached StemProvider"; }
    bool isAvailable() const override
    {
        return ! storagePolicy.isRestricted()
            || (cacheRoot != juce::File() && storagePolicy.permitsPath (cacheRoot) && cacheRoot.isDirectory());
    }

    void setCacheRoot (const juce::File& root);
    juce::File getCacheRoot() const;
    void setSeparatorCommand (juce::String command);
    juce::String getSeparatorCommand() const;
    juce::File getCacheDirectoryForSource (const juce::File& sourceFile) const;

    void requestStemsAsync (const juce::File& sourceFile, Callback callback);
    Result inspectCache (const juce::File& sourceFile) const;
    Status getStatusForSource (const juce::File& sourceFile) const;
    bool runSeparatorCommand (const juce::String& command, const juce::File& sourceFile, const juce::File& cacheDirectory, juce::String& message);

    // On-device neural separation (ONNX Runtime). Writes the 4 core-stem WAVs into
    // cacheDirectory. Preferred over the CLI when a model is loaded. Returns false
    // (with message) so callers fall back to the demucs CLI.
    bool runNeuralSeparation (const juce::File& sourceFile, const juce::File& cacheDirectory, juce::String& message);

    // "Usable", not "resident": true when the build has ONNX and a model file is on
    // disk. The model itself is loaded lazily (see ensureNeuralModelLoaded) because
    // an ONNX session for the ~350 MB model costs ~2 GB of RSS, which we refuse to
    // pay at launch for users who never press AI Split.
    bool neuralAvailable() const;

private:
    class StemJob;

    // Loads the ONNX session on first use. Runs on the separation pool (2 threads),
    // so the load is serialised by neuralLoadLock. Returns false with a message when
    // the model can't be loaded, letting the caller fall back to the demucs CLI.
    bool ensureNeuralModelLoaded (juce::String& message);

    const ComponentStoragePolicy storagePolicy;
    juce::File cacheRoot;
    juce::String separatorCommand;
    // Restricted instances must not construct an ONNX-enabled engine/OrtEnv.
    std::unique_ptr<NeuralStemEngine> neuralEngine;
    juce::CriticalSection neuralLoadLock;
    bool neuralLoadAttempted = false;   // guarded by neuralLoadLock
    juce::ThreadPool pool { 2 };
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
