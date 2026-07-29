#include <sattari_audio_core/SattariRackProcessor.h>

#include <cstdio>

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

    void linkModule (juce::AudioProcessorValueTreeState&,
                     const juce::String&) override {}
    void prepare (double, int, int) override {}
    void reset() override { calls = 0; }
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&) override
    {
        ++calls;
    }
    int latencySamples() const noexcept override { return 17; }

    int calls = 0;
};

class TestRack final : public sattari::rack::SattariRackProcessor
{
public:
    TestRack()
        : SattariRackProcessor (
            "Rack Contract", "RACK_CONTRACT",
            BusesProperties()
                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
            []
            {
                std::vector<std::unique_ptr<sattari::dsp::SattariModule>> modules;
                modules.push_back (std::make_unique<CountingModule>());
                return modules;
            })
    {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    void select (int slot, bool bypass)
    {
        auto& state = parameters();
        state.getParameter (slotParamId (slot))->setValueNotifyingHost (1.0f);
        state.getParameter (slotBypassParamId (slot))->setValueNotifyingHost (
            bypass ? 1.0f : 0.0f);
    }
};
} // namespace

int main()
{
    int failures = 0;
    auto check = [&failures] (bool condition, const char* message)
    {
        if (! condition)
        {
            std::fprintf (stderr, "FAIL: %s\n", message);
            ++failures;
        }
    };

    TestRack rack;
    rack.select (0, false);
    rack.select (1, false); // duplicate stateful module
    rack.prepareToPlay (48000.0, 64);

    auto* module = dynamic_cast<CountingModule*> (rack.moduleAt (0));
    check (module != nullptr, "counting module missing");
    check (rack.getLatencySamples() == 17,
           "duplicate module latency was counted twice");

    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer midi;
    rack.processBlock (audio, midi);
    check (module != nullptr && module->calls == 1,
           "duplicate module processed more than once");

    if (module != nullptr) module->calls = 0;
    rack.select (0, true);
    rack.processBlock (audio, midi);
    check (module != nullptr && module->calls == 1,
           "bypassed first occurrence prevented later active slot");

    if (failures != 0)
        return 1;
    std::printf ("Rack host contract ok\n");
    return 0;
}
