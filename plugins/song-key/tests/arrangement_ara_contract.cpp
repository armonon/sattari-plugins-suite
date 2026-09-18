// In-process ARA host proof. Exercises the production factory and JUCE region
// reader, not just the generic reader seam used by the analysis smoke tests.
#include <JuceHeader.h>
#include <sattari_audio_core/SattariArrangementClip.h>
#include <sattari_audio_core/SattariEditorRender.h>
#include "../../../core/tests/ArrangementClipFixture.h"
#include "../../../core/tests/ClipPlaybackContract.h"
#if SATTARI_TEST_AUTOPITCH
 #include "../../auto-pitch/src/PluginProcessor.h"
 using TestedProcessor = SattariAutoPitchAudioProcessor;
#else
 #include "../src/SongKeyProcessor.h"
 using TestedProcessor = SattariSongKeyAudioProcessor;
#endif
// This executable is a host as well as a plugin. The plugin shared-code
// library does not provide the SDK's host-side dispatch implementation.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wgnu-zero-variadic-macro-arguments", "-Wmissing-prototypes")
#include <ARA_Library/Dispatch/ARAHostDispatch.cpp>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <iostream>

extern const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory();

namespace
{
#if SATTARI_TEST_AUTOPITCH
class ClipPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo position;
        position.setTimeInSamples (46 * 48000);
        position.setTimeInSeconds (46.0);
        position.setIsPlaying (true);
        return position;
    }
};
#endif
bool isAnalysing (TestedProcessor& p)
{
#if SATTARI_TEST_AUTOPITCH
    return p.fileProgress.load() >= 0 && p.fileProgress.load() < 1;
#else
    return p.detected().analysing;
#endif
}
bool analysisMatchesClip (TestedProcessor& p)
{
#if SATTARI_TEST_AUTOPITCH
    return p.fileKeyValid.load() && p.fileProgress.load() >= 1
        && p.fileKeyRoot.load() == 0 && p.fileKeyMinor.load() == 0
        && p.getSongSourceName() == "DAW chorus" && std::abs (p.fileBpm.load() - 120) < 2;
#else
    const auto r = p.detected();
    return r.valid && ! r.analysing && r.root == 0 && ! r.minor
        && r.source == TestedProcessor::AnalysisSource::Arrangement
        && r.sourceName == "DAW chorus" && std::abs (r.bpm - 120.0) < 2.0;
#endif
}
bool analysisRevoked (TestedProcessor& p)
{
#if SATTARI_TEST_AUTOPITCH
    return ! p.fileKeyValid.load() && p.fileProgress.load() < 0 && ! p.songAnalysisCopy().valid;
#else
    return ! p.detected().valid && ! p.detected().sessionLocked && p.detected().error.isNotEmpty();
#endif
}
class AudioAccess final : public ARA::Host::AudioAccessControllerInterface
{
public:
    std::shared_ptr<sattari::test::ClipReaderProbe> probe = std::make_shared<sattari::test::ClipReaderProbe>();
    ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef, bool use64) noexcept override
    {
        if (use64) return nullptr;
        return reinterpret_cast<ARA::ARAAudioReaderHostRef> (new sattari::test::ArrangementFixtureReader (probe));
    }
    bool readAudioSamples (ARA::ARAAudioReaderHostRef ref, ARA::ARASamplePosition start,
                           ARA::ARASampleCount count, void* const buffers[]) noexcept override
    {
        auto* reader = reinterpret_cast<juce::AudioFormatReader*> (ref);
        return reader->read (reinterpret_cast<float* const*> (buffers), (int) reader->numChannels, start, (int) count);
    }
    void destroyAudioReader (ARA::ARAAudioReaderHostRef ref) noexcept override
    {
        delete reinterpret_cast<juce::AudioFormatReader*> (ref);
    }
};

class Archiving final : public ARA::Host::ArchivingControllerInterface
{
public:
    ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef) noexcept override { return 0; }
    bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef, ARA::ARASize, ARA::ARASize, ARA::ARAByte[]) noexcept override { return false; }
    bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef, ARA::ARASize, ARA::ARASize, const ARA::ARAByte[]) noexcept override { return false; }
    void notifyDocumentArchivingProgress (float) noexcept override {}
    void notifyDocumentUnarchivingProgress (float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef) noexcept override { return "arrangement-contract"; }
};
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto session = "ara-contract-" + juce::String (SATTARI_TEST_AUTOPITCH) + "-"
        + juce::String (sattari::xplugin::currentTimeMs());
#if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", session.toRawUTF8());
#else
    setenv ("SATTARI_SESSION_ID", session.toRawUTF8(), 1);
#endif
    bool passed = true;
    passed &= sattari::test::clipPlaybackContract();
    auto check = [&] (bool condition, const char* message)
    {
        if (! condition) { std::cerr << "FAIL: " << message << '\n'; passed = false; }
    };
    const auto* factory = createARAFactory();
    const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAInterfaceConfiguration, assertFunctionAddress)> config
        { ARA::kARAAPIGeneration_2_0_Final, nullptr };
    factory->initializeARAWithConfiguration (&config);
    {
        AudioAccess audio;
        Archiving archive;
        ARA::Host::DocumentControllerHostInstance host { &audio, &archive };
        const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARADocumentProperties, name)> document { "Arrangement contract" };
        const auto* instance = factory->createDocumentControllerWithDocument (&host, &document);
        ARA::Host::DocumentController controller (instance);
        controller.beginEditing();
        const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAMusicalContextProperties, color)> contextProperties { "Song", 0, nullptr };
        auto context = controller.createMusicalContext (reinterpret_cast<ARA::ARAMusicalContextHostRef> (&host), &contextProperties);
        const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARARegionSequenceProperties, color)> sequenceProperties { "Track", 0, context, nullptr };
        auto sequence = controller.createRegionSequence (reinterpret_cast<ARA::ARARegionSequenceHostRef> (&host), &sequenceProperties);
        const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAAudioSourceProperties, channelArrangement)> sourceProperties
            { "Whole take", "source-1", 36 * 48000, 48000.0, 2, false, ARA::kARAChannelArrangementUndefined, nullptr };
        auto source = controller.createAudioSource (reinterpret_cast<ARA::ARAAudioSourceHostRef> (&audio), &sourceProperties);
        const ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAAudioModificationProperties, persistentID)> modificationProperties { "Original", "mod-1" };
        auto modification = controller.createAudioModification (source, reinterpret_cast<ARA::ARAAudioModificationHostRef> (&audio), &modificationProperties);
        ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAPlaybackRegionProperties, color)> regionProperties
            { 0, 12.0, 12.0, 45.0, 12.0, context, sequence, "DAW chorus", nullptr };
        auto region = controller.createPlaybackRegion (modification, reinterpret_cast<ARA::ARAPlaybackRegionHostRef> (&audio), &regionProperties);
        auto secondRegion = controller.createPlaybackRegion (modification, reinterpret_cast<ARA::ARAPlaybackRegionHostRef> (&archive), &regionProperties);
        controller.endEditing();
        controller.enableAudioSourceSamplesAccess (source, true);
        {
            TestedProcessor processor;
#if ! SATTARI_TEST_AUTOPITCH
            processor.parameters().getParameter ("publish")->setValueNotifyingHost (0.0f);
#endif
            juce::String unavailable;
            check (! processor.analyseSelectedArrangementClip (unavailable) && unavailable.isNotEmpty(),
                "an ordinary insert explains that arrangement access is unavailable");
            processor.bindToARA (instance->documentControllerRef,
                ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole,
                ARA::kARAEditorViewRole);
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            auto& view = *processor.getEditorView();
            ARA::SizedStruct<ARA_STRUCT_MEMBER (ARAViewSelection, timeRange)> selection { ARA::ARASize { 0 }, nullptr, ARA::ARASize { 0 }, nullptr, nullptr };
            view.notifySelection (&selection);
            check (! sattari::arrangement::selectedClip (&view).error.isEmpty(), "empty selection must not scan a different clip");
            const ARA::ARAPlaybackRegionRef regions[] { region, secondRegion };
            selection.playbackRegionRefs = regions;
            selection.playbackRegionRefsCount = 2;
            view.notifySelection (&selection);
            check (! sattari::arrangement::selectedClip (&view).error.isEmpty(), "multiple selection must be explicit");
            selection.playbackRegionRefsCount = 1;
            view.notifySelection (&selection);
            auto request = sattari::arrangement::selectedClip (&view);
            check (request.reader != nullptr && request.error.isEmpty(), "selected ARA region must expose a reader");
            if (request.reader)
            {
                check (request.name == "DAW chorus" && request.timelineStartSeconds == 45.0
                       && request.reader->lengthInSamples == 12 * 48000, "clip name, trim and timeline origin");
                bool readOK = true;
                std::thread worker ([&]
                {
                    juce::AudioBuffer<float> block (2, 4096);
                    for (juce::int64 position = 0; position < request.reader->lengthInSamples; position += block.getNumSamples())
                    {
                        const int count = (int) juce::jmin<juce::int64> (block.getNumSamples(), request.reader->lengthInSamples - position);
                        readOK &= request.reader->read (&block, 0, count, position, true, true);
                    }
                });
                worker.join();
                check (readOK && sattari::test::readOnlyTrimmedClip (*audio.probe), "ARA worker reads only the trimmed source range");
                juce::String analysisError;
                check (processor.analyseSelectedArrangementClip (analysisError), "production editor/processor can scan host selection");
                for (int i = 0; isAnalysing (processor) && i < 1000; ++i)
                    std::this_thread::sleep_for (std::chrono::milliseconds (5));
                check (analysisMatchesClip (processor),
                    "host clip reaches the real analysis pipeline without import or playback");
#if SATTARI_TEST_AUTOPITCH
                processor.prepareToPlay (48000, 512);
                processor.parameters().getParameter ("followSongMap")->setValueNotifyingHost (1.0f);
                ClipPlayHead playHead;
                processor.setPlayHead (&playHead);
                juce::AudioBuffer<float> input (2, 512);
                input.clear();
                juce::MidiBuffer midi;
                processor.processBlock (input, midi);
                processor.setPlayHead (nullptr);
                check (processor.currentKeyOrigin().source == TestedProcessor::KeyOrigin::Source::KeyMap,
                    "arrangement key map drives correction at the clip's timeline position");
                processor.publishCurrentKeyToBus();
                const auto published = sattari::xplugin::readSession();
                check (published && published->root == 0,
                    "Auto Pitch publishes the reviewed arrangement map key");
#else
                processor.parameters().getParameter ("publish")->setValueNotifyingHost (1.0f);
                processor.setSessionLocked (true);
                check (processor.detected().sessionLocked, "reviewed clip key can be locked");
#endif
                if (argc == 3 && juce::String (argv[1]) == "--render-ui")
                {
                    // Reopening refreshes the editor's snapshot synchronously;
                    // no synthetic UI labels or mocks are used for this image.
                    editor.reset();
                    editor.reset (processor.createEditor());
                    view.notifySelection (&selection);
                    check (sattari::test::renderComponentToPng (*editor, juce::File (argv[2])),
                        "render the production editor with its ARA clip result");
                }
                controller.beginEditing();
                regionProperties.startInPlaybackTime = 50.0;
                controller.updatePlaybackRegionProperties (region, &regionProperties);
                controller.endEditing();
                check (analysisRevoked (processor), "moving a scanned clip revokes the published analysis");
                const auto bus = sattari::xplugin::readSession();
                check (! bus, "a changed clip cannot leave its old key on the suite bus");
                juce::AudioBuffer<float> block (2, 512);
                check (! request.reader->read (&block, 0, 512, 0, true, true), "moving a clip invalidates its in-flight reader");
                request.reader.reset();
                check (audio.probe->destroyedOnOwnerThread.load(), "host reader is released on its creating thread");
            }
            // Exercise the actual playback role, including its read-ahead cache,
            // not only the offline region-reader used by analysis.
            {
                juce::AudioProcessorARAExtension extension;
                extension.bindToARA (instance->documentControllerRef,
                    ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole,
                    ARA::kARAPlaybackRendererRole);
                auto* renderer = extension.getPlaybackRenderer();
                renderer->addPlaybackRegion (region);
                for (double rate : { 44100.0, 48000.0, 96000.0 })
                for (int channels : { 1, 2 })
                {
                    renderer->prepareToPlay (rate, 512, channels, juce::AudioProcessor::singlePrecision,
                        juce::ARARenderer::AlwaysNonRealtime::no);
                    juce::AudioPlayHead::PositionInfo position;
                    position.setIsPlaying (true);
                    position.setTimeInSamples ((juce::int64) std::llround (50.25 * rate));
                    juce::AudioBuffer<float> before (channels, 2048), after (channels, 2048);
                    check (renderer->processBlock (before, juce::AudioProcessor::Realtime::no, position)
                        && before.getRMSLevel (0, 0, before.getNumSamples()) > 0.01f,
                        "mixed-format ARA playback and oversized host block are audible");
                    const double oldGain = audio.probe->gain;
                    audio.probe->gain *= 0.5;
                    controller.beginEditing();
                    controller.updateAudioSourceContent (source, nullptr, ARA::kARAContentUpdateEverythingChanged);
                    controller.endEditing();
                    check (renderer->processBlock (after, juce::AudioProcessor::Realtime::no, position),
                        "changed source is readable without re-preparing the renderer");
                    double maxDifference = 0;
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < before.getNumSamples(); ++i)
                            maxDifference = juce::jmax (maxDifference,
                                (double) std::abs (after.getSample (ch, i) - before.getSample (ch, i) * 0.5f));
                    check (maxDifference < 0.000002, "content edits flush old playback caches");
                    controller.enableAudioSourceSamplesAccess (source, false);
                    after.clear();
                    check (! renderer->processBlock (after, juce::AudioProcessor::Realtime::yes, position)
                        && juce::exactlyEqual (after.getMagnitude (0, after.getNumSamples()), 0.0f),
                        "unavailable host samples do not leak stale cached playback");
                    controller.enableAudioSourceSamplesAccess (source, true);
                    check (renderer->processBlock (after, juce::AudioProcessor::Realtime::no, position)
                        && after.getRMSLevel (0, 0, after.getNumSamples()) > 0.005f,
                        "host sample access resumes without re-preparing playback");
                    position.setIsPlaying (false);
                    check (renderer->processBlock (after, juce::AudioProcessor::Realtime::yes, position)
                        && juce::exactlyEqual (after.getMagnitude (0, after.getNumSamples()), 0.0f), "stopped transport is silent");
                    renderer->releaseResources();
                    audio.probe->gain = oldGain;
                }
                renderer->removePlaybackRegion (region);
            }
            // A different instance may have published since our last scan.
            // Replacing/cancelling this scan must never revoke that newer key.
            const juce::String publisher = SATTARI_TEST_AUTOPITCH ? "auto-pitch" : "song-key";
            check (sattari::xplugin::publishSession (publisher, 7, 0, 120.0, 0, {}, "other-instance"),
                "competing instance fixture publishes");
            audio.probe->slow = true;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                juce::String error;
                check (processor.analyseSelectedArrangementClip (error), "repeated scan starts");
                controller.beginEditing();
                regionProperties.startInPlaybackTime += 1.0;
                controller.updatePlaybackRegionProperties (region, &regionProperties);
                controller.endEditing();
                check (analysisRevoked (processor), "editing during a scan cancels and revokes it");
                const auto other = sattari::xplugin::readSession();
                check (other && other->root == 7, "scan cancellation preserves another instance's key");
            }
            audio.probe->slow = false;
            sattari::xplugin::withdrawSessionState (publisher, {}, "other-instance");
            controller.beginEditing();
            regionProperties.durationInPlaybackTime = 6.0;
            controller.updatePlaybackRegionProperties (region, &regionProperties);
            controller.endEditing();
            check (sattari::arrangement::selectedClip (&view).reader == nullptr, "warped region must not return unwarped analysis");

            controller.beginEditing();
            regionProperties.durationInPlaybackTime = 12.0;
            controller.updatePlaybackRegionProperties (region, &regionProperties);
            controller.endEditing();
            juce::String error;
            check (processor.analyseSelectedArrangementClip (error), "scan before region deletion starts");
            for (int i = 0; isAnalysing (processor) && i < 1000; ++i)
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            check (analysisMatchesClip (processor), "scan before region deletion completes");
            controller.beginEditing();
            controller.destroyPlaybackRegion (region);
            region = nullptr;
            controller.endEditing();
            check (analysisRevoked (processor), "deleting the selected clip revokes its completed analysis");
        }
        controller.enableAudioSourceSamplesAccess (source, false);
        controller.beginEditing();
        controller.destroyPlaybackRegion (secondRegion);
        if (region != nullptr) controller.destroyPlaybackRegion (region);
        controller.destroyAudioModification (modification);
        controller.destroyAudioSource (source);
        controller.destroyRegionSequence (sequence);
        controller.destroyMusicalContext (context);
        controller.endEditing();
        controller.destroyDocumentController();
    }
    factory->uninitializeARA();
    std::cout << (SATTARI_TEST_AUTOPITCH ? "Auto Pitch" : "Key")
              << " ARA selected-clip host contract: " << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
