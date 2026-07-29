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

    void prepare (double, int, int) override { calls = 0; }
    void reset() override { calls = 0; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        ++calls;
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            juce::FloatVectorOperations::fill (
                buffer.getWritePointer (channel), cachedParameterValue ("gain"),
                buffer.getNumSamples());
    }

    int processCalls() const noexcept { return calls; }

private:
    int calls = 0;
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

    std::printf ("Stack independent-layer contract ok\n");
    return 0;
}
