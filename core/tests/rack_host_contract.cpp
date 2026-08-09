#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariInstrumentRack.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include <sattari_audio_core/SattariRackProcessor.h>
#include <sattari_audio_core/SattariStackEditor.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/CleanModule.h>
#include <sattari_audio_core/modules/GateModule.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/ShimmerModule.h>
#include <sattari_audio_core/modules/SpaceModule.h>
#include <sattari_audio_core/modules/WarpModule.h>

#include <cmath>
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
        // Recorded through the normal read path, so this is exactly what any
        // module would see: the user's setting plus whatever the rack modulated.
        seenGain = cachedParameterValue ("gain");
        userGain = rawParameterValue ("gain");
    }
    int latencySamples() const noexcept override { return 17; }

    int calls = 0;
    float seenGain = 0.0f;   // modulated
    float userGain = 0.0f;   // unmodulated

private:
    std::string displayName_;
};

// A playhead the test drives directly. Modulation is a function of song
// POSITION, so position has to be something the test can set — otherwise the
// phase-lock assertion cannot be written at all.
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setPpqPosition (ppq);
        info.setIsPlaying (true);
        info.setTimeInSamples (0);
        return info;
    }
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
                if (renderModuleName == "Vocal Studio")
                {
                    // Keep this roster in the same frozen order as Vocal's real
                    // processor. The marketing render can then show the current
                    // eight-stage factory chain instead of an empty mock-up.
                    modules.push_back (std::make_unique<sattari::dsp::DeEssModule>());
                    modules.push_back (std::make_unique<sattari::dsp::DoubleModule>());
                    modules.push_back (std::make_unique<CountingModule> ("Face"));
                    modules.push_back (std::make_unique<CountingModule> ("Glass Choir"));
                    modules.push_back (std::make_unique<sattari::dsp::CleanModule>());
                    modules.push_back (std::make_unique<sattari::dsp::GateModule>());
                    modules.push_back (std::make_unique<sattari::dsp::EqModule>());
                    modules.push_back (std::make_unique<sattari::dsp::CompModule>());
                    modules.push_back (std::make_unique<sattari::dsp::HeatModule>());
                    modules.push_back (std::make_unique<sattari::dsp::SpaceModule>());
                }
                else if (renderModuleName == "EQ")
                    modules.push_back (
                        std::make_unique<sattari::dsp::EqModule>());
                else if (renderModuleName == "Shimmer")
                    modules.push_back (
                        std::make_unique<sattari::dsp::ShimmerModule>());
                else if (renderModuleName == "Double")
                    modules.push_back (
                        std::make_unique<sattari::dsp::DoubleModule>());
                else if (renderModuleName == "Heat")
                    modules.push_back (
                        std::make_unique<sattari::dsp::HeatModule>());
                else if (renderModuleName == "Space")
                    modules.push_back (
                        std::make_unique<sattari::dsp::SpaceModule>());
                else if (renderModuleName == "Warp")
                    modules.push_back (
                        std::make_unique<sattari::dsp::WarpModule>());
                else
                    modules.push_back (
                        std::make_unique<CountingModule> (renderModuleName));
                return modules;
            })
    {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    TestPlayHead head;
    bool hasEditor() const override { return false; }

    void select (int slot, bool bypass)
    {
        selectModule (slot, 1, bypass);
    }

    void selectModule (int slot, int oneBasedModuleIndex, bool bypass = false)
    {
        auto& state = parameters();
        state.getParameter (slotParamId (slot))->setValueNotifyingHost (
            state.getParameter (slotParamId (slot))->convertTo0to1 (
                (float) oneBasedModuleIndex));
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
        moduleName = "Vocal Studio";
    }
    else
    {
        return false;
    }

    TestRack rack (std::move (moduleName));
    if (slug == "vocal")
    {
        // Vocal Studio: Clean, Gate, De-Ess, EQ, Comp, Heat, Double, Space.
        const int chain[] { 5, 6, 1, 7, 8, 9, 2, 10 };
        for (int slot = 0; slot < 8; ++slot)
            rack.selectModule (slot, chain[slot]);
    }
    else
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

    // JUCE documents prepareToPlay's block size as a hint: hosts may exceed it.
    // Every module that owns block-sized scratch must process the complete block
    // in bounded chunks, without bypassing or truncating the tail.
    for (const auto* moduleName : { "Double", "Heat", "Shimmer", "Space", "Warp" })
    {
        TestRack oversized (moduleName);
        oversized.select (0, false);
        oversized.prepareToPlay (48000.0, 64);

        constexpr int oversizedSamples = 4097;
        juce::AudioBuffer<float> block (2, oversizedSamples);
        juce::AudioBuffer<float> input (2, oversizedSamples);
        for (int i = 0; i < oversizedSamples; ++i)
        {
            const float sample = 0.3f * std::sin (
                juce::MathConstants<float>::twoPi * 220.0f * (float) i / 48000.0f);
            block.setSample (0, i, sample);
            block.setSample (1, i, sample);
            input.setSample (0, i, sample);
            input.setSample (1, i, sample);
        }

        oversized.processBlock (block, midi);

        bool finite = true;
        float tailDifference = 0.0f;
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            for (int i = 3072; i < oversizedSamples; ++i)
            {
                const float sample = block.getSample (ch, i);
                finite = finite && std::isfinite (sample);
                tailDifference = juce::jmax (
                    tailDifference,
                    std::abs (sample - input.getSample (ch, i)));
            }

        const auto finiteMessage = std::string (moduleName) + " oversized block produced non-finite audio";
        const auto tailMessage = std::string (moduleName) + " oversized block tail was bypassed";
        check (finite, finiteMessage.c_str());
        check (tailDifference > 1.0e-6f, tailMessage.c_str());
    }

    // --- MODULATION ---------------------------------------------------------
    //  A rack of eight processors with no cross-module modulation is eight
    //  plugins in one window. These pin the three things that make it more: the
    //  value actually moves, it releases cleanly, and it is phase-locked to song
    //  POSITION rather than free-running — which is what makes a looped section
    //  repeat identically instead of drifting somewhere new on every pass.
    {
        TestRack rack;                                   // CountingModule in the roster
        rack.setPlayHead (&rack.head);
        rack.select (0, false);                          // slot 1 = the module
        rack.prepareToPlay (48000.0, 512);

        auto* mod = dynamic_cast<CountingModule*> (rack.moduleAt (0));
        check (mod != nullptr, "modulation: no module in slot 1");

        if (mod != nullptr)
        {
            check (mod->modulationTargetCount() > 0, "modulation: module exposes no targets");
            check (mod->modulationTargetId (0) != nullptr, "modulation: target 0 has no id");

            auto setP = [&rack] (const char* id, float v)
            {
                if (auto* prm = rack.parameters().getParameter (id))
                    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            };
            auto runAt = [&] (double ppq)
            {
                rack.head.ppq = ppq;
                juce::AudioBuffer<float> buf (2, 512);
                buf.clear();
                juce::MidiBuffer midi;
                rack.processBlock (buf, midi);
            };

            setP ("counting.gain", 0.5f);                // mid-range, room to move both ways
            setP ("mod1.target", 1.0f);                  // slot 1
            setP ("mod1.param",  0.0f);                  // its first target
            setP ("mod1.shape",  0.0f);                  // sine
            setP ("mod1.rate",   4.0f);                  // one bar = 4 beats
            setP ("mod1.phase",  0.0f);
            setP ("mod1.depth",  0.0f);

            runAt (0.0);
            const float user = mod->userGain;
            check (std::abs (mod->seenGain - user) < 1.0e-5f,
                   "modulation: depth 0 changed the value");

            // sin(0) = 0 at ppq 0; the peak of a 4-beat sine is a quarter cycle in.
            setP ("mod1.depth", 1.0f);
            runAt (0.0);
            const float atZero = mod->seenGain;
            runAt (1.0);
            const float atPeak = mod->seenGain;

            check (std::abs (atZero - user) < 1.0e-5f,
                   "modulation: sine at phase 0 should leave the value alone");
            check (std::abs (atPeak - user) > 1.0e-4f,
                   "modulation: full depth did not move the value");
            check (atPeak > user, "modulation: sine peak should move the value upward");

            // Phase-lock: 40 blocks at scattered positions, then return. Same
            // position must give the same value.
            for (int i = 0; i < 40; ++i) runAt (0.37 * (double) i);
            runAt (1.0);
            check (std::abs (mod->seenGain - atPeak) < 1.0e-5f,
                   "modulation: value at a given song position drifted over time");

            // Releasing must be complete — a stale offset left applied reads as a
            // broken parameter, not as leftover modulation.
            setP ("mod1.depth", 0.0f);
            runAt (1.0);
            check (std::abs (mod->seenGain - user) < 1.0e-5f,
                   "modulation: value stayed modulated after depth returned to zero");

            // Off must also mean off, independently of depth.
            setP ("mod1.depth", 1.0f);
            setP ("mod1.target", 0.0f);
            runAt (1.0);
            check (std::abs (mod->seenGain - user) < 1.0e-5f,
                   "modulation: target Off still modulated the value");
        }
    }

    if (failures != 0)
        return 1;
    std::printf ("Rack host contract ok\n");
    return 0;
}
