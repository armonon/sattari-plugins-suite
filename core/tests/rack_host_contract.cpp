#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariInstrumentRack.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include <sattari_audio_core/SattariRackProcessor.h>
#include <sattari_audio_core/SattariStackEditor.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/ShimmerModule.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
class CountingModule final : public sattari::dsp::SattariModule
{
public:
    explicit CountingModule (std::string displayName = "Counting")
        : displayName_ (std::move (displayName))
    {}

    const char* moduleId() const noexcept override { return "counting"; }
    const char* displayName() const noexcept override
    {
        return displayName_.c_str();
    }

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

private:
    std::string displayName_;
};

class TestRack final : public sattari::rack::SattariRackProcessor
{
public:
    explicit TestRack (std::string requestedModuleName = "Counting")
        : SattariRackProcessor (
            "Rack Contract", "RACK_CONTRACT",
            BusesProperties()
                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
            [renderModuleName = std::move (requestedModuleName)]
            {
                std::vector<std::unique_ptr<sattari::dsp::SattariModule>> modules;
                if (renderModuleName == "EQ")
                    modules.push_back (
                        std::make_unique<sattari::dsp::EqModule>());
                else if (renderModuleName == "Shimmer")
                    modules.push_back (
                        std::make_unique<sattari::dsp::ShimmerModule>());
                else if (renderModuleName == "Double")
                    modules.push_back (
                        std::make_unique<sattari::dsp::DoubleModule>());
                else
                    modules.push_back (
                        std::make_unique<CountingModule> (renderModuleName));
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

class TestInstrumentRack final : public sattari::rack::SattariInstrumentRack
{
public:
    TestInstrumentRack()
        : SattariInstrumentRack (
            "Stack Contract", "STACK_CONTRACT",
            BusesProperties().withOutput (
                "Output", juce::AudioChannelSet::stereo(), true),
            []
            {
                std::vector<std::unique_ptr<sattari::dsp::SattariModule>> modules;
                modules.push_back (std::make_unique<CountingModule>());
                return modules;
            })
    {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    void select (int layer)
    {
        parameters().getParameter (layerParamId (layer))
            ->setValueNotifyingHost (1.0f);
    }
};

bool renderRackWorkspace (const char* product,
                          const juce::File& destination,
                          bool minimumSize)
{
    const juce::String slug (product);
    if (slug == "stack")
    {
        TestInstrumentRack rack;
        rack.select (0);
        sattari::ui::SattariStackEditor editor (
            rack, "Sattari Stack", "INSTRUMENT LAYER WORKSPACE", "stack");
        if (minimumSize) editor.setSize (1160, 620);
        return sattari::test::renderComponentToPng (editor, destination);
    }

    juce::String name;
    juce::String category;
    std::string moduleName;
    if (slug == "mix")
    {
        name = "Sattari Mix";
        category = "MIXING WORKFLOW";
        moduleName = "EQ";
    }
    else if (slug == "create")
    {
        name = "Sattari Create";
        category = "CREATIVE EFFECTS WORKFLOW";
        moduleName = "Shimmer";
    }
    else if (slug == "vocal")
    {
        name = "Sattari Vocal";
        category = "VOCAL FINISHING WORKFLOW";
        moduleName = "Double";
    }
    else
    {
        return false;
    }

    TestRack rack (std::move (moduleName));
    rack.select (0, false);
    sattari::ui::SattariRackEditor editor (
        rack, name, category, product);
    if (minimumSize) editor.setSize (760, 560);
    return sattari::test::renderComponentToPng (editor, destination);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (argc == 4
        && (std::strcmp (argv[1], "--render-ui") == 0
            || std::strcmp (argv[1], "--render-ui-min") == 0))
    {
        const juce::File destination (argv[3]);
        const bool minimumSize =
            std::strcmp (argv[1], "--render-ui-min") == 0;
        if (! renderRackWorkspace (argv[2], destination, minimumSize))
        {
            std::fprintf (stderr, "FAIL: editor render failed\n");
            return 1;
        }
        std::printf ("Rendered %s\n",
                     destination.getFullPathName().toRawUTF8());
        return 0;
    }

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
