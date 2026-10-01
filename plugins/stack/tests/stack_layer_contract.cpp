#include <sattari_audio_core/SattariInstrumentRack.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>
#include <cmath>
#include <thread>

namespace
{
class CountingModule final : public sattari::dsp::SattariModule
{
public:
    const char* moduleId() const noexcept override { return "counting"; }
    const char* displayName() const noexcept override { return "Counting"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        out.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "gain", 1 }, "Gain",
            juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState&, const juce::String&) override {}

    void prepare (double, int blockSize, int) override { calls = 0; prepared = blockSize; }
    void reset() override { calls = 0; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        ++calls;
        widest = juce::jmax (widest, buffer.getNumSamples());
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill (
                buffer.getWritePointer (channel), cachedParameterValue ("gain"),
                buffer.getNumSamples());
    }

    int processCalls() const noexcept { return calls; }
    // The rack's per-layer scratch is sized to the PREPARED block, so handing a
    // module more samples than that is the overflow itself, not a symptom of it.
    int widestBlock() const noexcept { return widest; }
    int preparedBlock() const noexcept { return prepared; }

private:
    int calls = 0, widest = 0, prepared = 0;
};

std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> result;
    result.push_back (std::make_unique<CountingModule>());
    return result;
}

class TestRack final : public sattari::rack::SattariInstrumentRack
{
public:
    TestRack()
        : SattariInstrumentRack (
              "Stack Contract", "STACK_CONTRACT",
              BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true),
              &makeModules)
    {
    }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    void selectModule (int layer)
    {
        if (auto* parameter = parameters().getParameter (layerParamId (layer)))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (1.0f));
    }
};

class SourceRack final : public sattari::rack::SattariInstrumentRack
{
public:
    SourceRack() : SattariInstrumentRack ("Source Contract", "SOURCE_CONTRACT",
        BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true), [] {
            std::vector<std::unique_ptr<sattari::dsp::SattariModule>> modules;
            modules.push_back (std::make_unique<sattari::dsp::ChopModule>());
            modules.push_back (std::make_unique<sattari::dsp::GranulatorModule>());
            return modules;
        }) {}
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    void select (int layer = 0, int module = 0) {
        auto* p = parameters().getParameter (layerParamId (layer));
        p->setValueNotifyingHost (p->convertTo0to1 ((float) module + 1.0f));
    }
    sattari::dsp::ChopModule* chop() {
        return dynamic_cast<sattari::dsp::ChopModule*> (moduleAtForLayer (0, 0));
    }
};

bool sourceRecallContract()
{
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("sattari-stack-recall-" + juce::Uuid().toString());
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup { directory };
    if (directory.createDirectory().failed()) return false;
    const auto file = directory.getChildFile ("source.wav");
    juce::AudioBuffer<float> fixture (1, 4096);
    for (int i = 0; i < fixture.getNumSamples(); ++i)
        fixture.setSample (0, i, 0.2f * std::sin (0.03f * (float) i));
    {
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
            .withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (fixture, 0, fixture.getNumSamples())) return false;
    }
    SourceRack original;
    original.prepareToPlay (48000, 128);
    original.select();
    original.select (1, 1);
    if (! original.loadSourceForLayer (0, 0, file)) return false;
    if (! original.loadSourceForLayer (1, 0, file) || ! original.loadSourceForLayer (1, 1, file)) return false;
    juce::MemoryBlock saved;
    original.getStateInformation (saved);
    SourceRack reopened;
    reopened.prepareToPlay (48000, 128);
    auto restore = [&] (const juce::MemoryBlock& state) {
        reopened.setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    };
    restore (saved);
    if (reopened.chop()->snapshot() == nullptr || reopened.sourceNameForLayer (0, 0) != "source.wav"
        || reopened.sourceUnavailableForLayer (0, 0)) return false;
    if (reopened.sourceNameForLayer (1, 0) != "source.wav" || reopened.sourceNameForLayer (1, 1) != "source.wav"
        || reopened.sourceUnavailableForLayer (1, 0) || reopened.sourceUnavailableForLayer (1, 1)) return false;
    if (std::abs (reopened.chop()->snapshot()->audio.getSample (0, 100) - fixture.getSample (0, 100)) > 0.0001f)
        return false;
    juce::AudioBuffer<float> audible (2, 128);
    juce::MidiBuffer trigger;
    trigger.addEvent (juce::MidiMessage::noteOn (1, sattari::dsp::ChopModule::kBaseNote, 1.0f), 0);
    reopened.processBlock (audible, trigger);
    if (audible.getMagnitude (0, 128) < 0.0001f) return false;
    juce::XmlElement foreign ("WRONG_PRODUCT");
    juce::MemoryBlock foreignState;
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignState);
    restore (foreignState);
    if (reopened.chop()->snapshot() == nullptr
        || reopened.parameters().getRawParameterValue (SourceRack::layerParamId (0))->load() != 1.0f) return false;
    // A missing restored file must not leave this previously loaded source active.
    if (! file.moveFileTo (directory.getChildFile ("moved.wav"))) return false;
    restore (saved);
    if (reopened.chop()->snapshot() != nullptr || ! reopened.sourceUnavailableForLayer (0, 0)) return false;
    if (! reopened.sourceUnavailableForLayer (1, 0) || ! reopened.sourceUnavailableForLayer (1, 1)) return false;
    juce::AudioBuffer<float> output (2, 128);
    juce::MidiBuffer notes;
    notes.addEvent (juce::MidiMessage::noteOn (1, sattari::dsp::ChopModule::kBaseNote, 1.0f), 0);
    reopened.processBlock (output, notes);
    if (output.getMagnitude (0, 128) != 0.0f) return false;
    // Missing references remain in project state so the user can recover/relink.
    juce::MemoryBlock missingSaved;
    reopened.getStateInformation (missingSaved);
    const auto missingXml = juce::AudioProcessor::getXmlFromBinary (missingSaved.getData(), (int) missingSaved.getSize());
    if (missingXml == nullptr || missingXml->getStringAttribute ("sourceLayer1APath") != file.getFullPathName()) return false;
    if (! directory.getChildFile ("moved.wav").moveFileTo (file)) return false;
    restore (saved);
    if (reopened.chop()->snapshot() == nullptr || reopened.sourceUnavailableForLayer (0, 0)) return false;
    // A source-free project also clears old samples, without a false missing warning.
    SourceRack empty;
    juce::MemoryBlock emptyState;
    empty.getStateInformation (emptyState);
    restore (emptyState);
    if (reopened.chop()->snapshot() != nullptr || reopened.sourceUnavailableForLayer (0, 0)
        || reopened.sourceNameForLayer (1, 0).isNotEmpty() || reopened.sourceNameForLayer (1, 1).isNotEmpty()) return false;
    std::printf ("Stack source recall ok: Chop + Granulator A/B, state reopen, missing/moved media, silence, relink, source clearing\n");
    return true;
}

bool sourceLifetimeContract()
{
    int destroyed = 0;
    bool destroyedOnWorker = false;
    const auto owner = std::this_thread::get_id();
    struct TrackedSource final : sattari::dsp::ChopModule::Source {
        TrackedSource (int& count, bool& wrongThread, std::thread::id ownerThread)
            : counter (count), offThread (wrongThread), owner (ownerThread) {}
        ~TrackedSource() override { ++counter; offThread = offThread || std::this_thread::get_id() != owner; }
        int& counter;
        bool& offThread;
        std::thread::id owner;
    };
    sattari::dsp::ChopModule module;
    sattari::dsp::ChopModule::Source::Ptr held;
    for (int i = 0; i < 100; ++i) {
        module.setSource (new TrackedSource (destroyed, destroyedOnWorker, owner));
        if (i == 0) held = module.snapshot(); // models a source still held by a voice
    }
    if (destroyed != 98 || held == nullptr || module.snapshot() == nullptr) {
        std::fprintf (stderr, "Chop source retention: destroyed %d/98 unused sources\n", destroyed);
        return false;
    }
    for (int i = 0; i < 10; ++i) module.setSource (module.snapshot());
    std::thread releaseVoice ([voice = std::move (held)]() mutable { voice = nullptr; });
    releaseVoice.join();
    if (destroyed != 98 || destroyedOnWorker) return false;
    module.setSource ({});
    if (destroyed != 100 || destroyedOnWorker) return false;
    std::printf ("Chop source lifetime ok: 100 replacements, duplicate publish, held sources protected, no worker-thread deallocation\n");
    return true;
}

bool granularSourceLifetimeContract()
{
    int destroyed = 0;
    bool destroyedOnWorker = false;
    const auto owner = std::this_thread::get_id();
    SourceRack rack;
    rack.prepareToPlay (48000.0, 128);
    rack.select (1, 1);
    auto* module = dynamic_cast<sattari::dsp::GranulatorModule*> (rack.moduleAtForLayer (1, 1));
    if (module == nullptr) return false;
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    auto render = [&] {
        std::thread audio ([&] { rack.processBlock (buffer, midi); });
        audio.join();
    };
    for (int i = 0; i < 100; ++i)
    {
        std::shared_ptr<const sattari::dsp::SourceSample> source (
            new sattari::dsp::SourceSample(), [&] (const auto* sample) {
                ++destroyed;
                destroyedOnWorker = destroyedOnWorker || std::this_thread::get_id() != owner;
                delete sample;
            });
        // Duplicate publication must not create extra retained owners.
        module->setSource (sattari::dsp::GranulatorModule::Layer::A, source);
        module->setSource (sattari::dsp::GranulatorModule::Layer::A, std::move (source));
        render();
    }
    if (destroyed != 98 || destroyedOnWorker)
    {
        std::fprintf (stderr, "Granulator source lifetime FAILED: %d/98 reclaimed, callback frees=%d\n",
                      destroyed, (int) destroyedOnWorker);
        return false;
    }
    module->setSource (sattari::dsp::GranulatorModule::Layer::A, {});
    render(); // Drops the engine's old reference, but cannot delete its buffer.
    module->setSource (sattari::dsp::GranulatorModule::Layer::A, {});
    if (destroyed != 100 || destroyedOnWorker) return false;
    std::printf ("Granulator source lifetime ok: 100 replacements, duplicate publish, no audio-callback deallocation\n");
    return true;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    if (! sourceRecallContract()) {
        std::fprintf (stderr, "Stack source recall contract FAILED\n");
        return 1;
    }
    if (! sourceLifetimeContract()) return 1;
    if (! granularSourceLifetimeContract()) return 1;
    TestRack rack;
    rack.prepareToPlay (48000.0, 128);
    rack.selectModule (0);
    rack.selectModule (1);

    auto* first = dynamic_cast<CountingModule*> (rack.moduleAtForLayer (0, 0));
    auto* second = dynamic_cast<CountingModule*> (rack.moduleAtForLayer (1, 0));
    if (first == nullptr || second == nullptr || first == second)
    {
        std::fprintf (stderr, "Stack layer contract FAILED: layers share a DSP instance\n");
        return 1;
    }

    juce::AudioBuffer<float> audio (2, 128);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    rack.processBlock (audio, midi);

    if (first->processCalls() != 1 || second->processCalls() != 1)
    {
        std::fprintf (stderr, "Stack layer contract FAILED: first=%d second=%d\n",
                      first->processCalls(), second->processCalls());
        return 1;
    }

    // JUCE documents prepareToPlay's block size as a HINT; hosts may exceed it.
    // The rack's per-layer scratch is sized to that hint, and the view handed to a
    // module was built with the INCOMING sample count, so an oversized block wrote
    // past the allocation. Every sample must still be rendered, MIDI must fire
    // exactly once, and nothing may be written outside the caller's buffer.
    {
        TestRack big;
        big.prepareToPlay (48000.0, 64);          // small hint...
        big.selectModule (0);
        auto* mod = dynamic_cast<CountingModule*> (big.moduleAtForLayer (0, 0));
        if (mod == nullptr)
        {
            std::fprintf (stderr, "Stack layer contract FAILED: no module for oversized test\n");
            return 1;
        }

        // Guard band: if the rack writes past the block it was handed, it lands here.
        constexpr int kBlock = 1024, kGuard = 256;
        juce::AudioBuffer<float> padded (2, kBlock + kGuard);
        padded.clear();
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::fill (
                padded.getWritePointer (c) + kBlock, 12345.0f, kGuard);

        juce::AudioBuffer<float> view (padded.getArrayOfWritePointers(), 2, 0, kBlock);
        juce::MidiBuffer midiBig;
        midiBig.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        midiBig.addEvent (juce::MidiMessage::noteOff (1, 60), kBlock - 1);
        big.processBlock (view, midiBig);         // ...much larger block

        for (int c = 0; c < 2; ++c)
            for (int i = kBlock; i < kBlock + kGuard; ++i)
                if (padded.getSample (c, i) != 12345.0f)
                {
                    std::fprintf (stderr,
                        "Stack layer contract FAILED: wrote past the block (ch %d, +%d)\n",
                        c, i - kBlock);
                    return 1;
                }

        if (mod->widestBlock() > mod->preparedBlock())
        {
            std::fprintf (stderr,
                "Stack layer contract FAILED: module handed %d samples on a %d-sample "
                "scratch - the rack overflowed its own buffer\n",
                mod->widestBlock(), mod->preparedBlock());
            return 1;
        }

        const int expected = (kBlock + 63) / 64;   // one process() per chunk
        if (mod->processCalls() != expected)
        {
            std::fprintf (stderr,
                "Stack layer contract FAILED: oversized block rendered %d chunks, expected %d\n",
                mod->processCalls(), expected);
            return 1;
        }
    }

    std::printf ("Stack independent-layer contract ok\n");
    return 0;
}
