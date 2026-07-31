#include <sattari_audio_core/SattariInstrumentRack.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

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
}

int main()
{
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
