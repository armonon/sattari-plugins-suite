#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StateReader.h"
#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/modules/CleanModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/EchoModule.h>
#include <sattari_audio_core/modules/SpaceModule.h>
#include <sattari_audio_core/modules/PulseModule.h>
#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/ImageModule.h>
#include <sattari_audio_core/modules/GranulatorModule.h>
#include <sattari_audio_core/modules/RhythmGenieModule.h>
#include <cmath>

namespace
{
using P = SattariModularProcessor;
using Module = sattari::dsp::SattariModule;
constexpr int maxSampleFrames = 1440000;
constexpr int maxMidiEvents = 4096;
using Values = std::array<float, P::controls>;

// JUCE's bool setter retains fractional automation internally. Report a
// canonical bit so host snapshots agree with APVTS's snapped state value.
class BinaryParameter final : public juce::AudioParameterBool
{
public:
    using juce::AudioParameterBool::AudioParameterBool;
private:
    float getValue() const override { return get() ? 1.0f : 0.0f; }
};

std::unique_ptr<Module> makeModule (int type)
{
    using namespace sattari::dsp;
    switch (type)
    {
        case P::clean: return std::make_unique<CleanModule>();
        case P::eq: return std::make_unique<EqModule>();
        case P::comp: return std::make_unique<CompModule>();
        case P::heat: return std::make_unique<HeatModule>();
        case P::echo: return std::make_unique<EchoModule>();
        case P::space: return std::make_unique<SpaceModule>();
        case P::pulse: return std::make_unique<PulseModule>();
        case P::deess: return std::make_unique<DeEssModule>();
        case P::image: return std::make_unique<ImageModule>();
        case P::granulator: return std::make_unique<GranulatorModule>();
        case P::rhythm: return std::make_unique<RhythmGenieModule>();
        default: return {};
    }
}

std::array<const char*, P::controls> keys (int type)
{
    switch (type)
    {
        case P::clean: return { "amount", "sensitivity", "output" };
        case P::eq: return { "g1", "g2", "g3", "f1", "f2", "f3", "hpf", "lpf" };
        case P::comp: return { "threshold", "ratio", "attack", "release", "knee", "makeup", "mix", "scHpf" };
        case P::heat: return { "drive", "tone", "mix", "output", "model", "bias" };
        case P::echo: return { "timems", "feedback", "mix", "tone", "width", "sync", "div", "output" };
        case P::space: return { "size", "damp", "revmix", "delaymix", "feedback", "div", "width", "output" };
        case P::pulse: return { "depth", "rate", "shape", "smooth", "filter", "mix" };
        case P::deess: return { "freq", "threshold", "range", "mode", "output" };
        case P::image: return { "width", "widthLow", "widthMid", "widthHigh", "haas", "lowMono", "monoAudition", "lowMonoFreq" };
        case P::granulator: return { "A_grainSize", "A_density", "A_posBase", "A_spray", "A_tuneCoarse", "A_level", "A_grainPan", "A_unisonVoices" };
        case P::rhythm: return { "density", "swing", "velocity", "kitLevel", "kickTune", "snareTune", "style", "accent" };
        default: return {};
    }
}

// Private parameter state lets every instance reuse the original DSP, including
// modules that cache raw pointers. Only the audio thread writes these values.
class Unit final : public juce::AudioProcessor
{
public:
    explicit Unit (int type) : module (makeModule (type)), state (*this, nullptr, "UNIT", createLayout())
    {
        module->link (state, {});
        const auto ids = keys (type);
        for (int i = 0; i < P::controls; ++i)
            if (ids[(size_t) i] != nullptr)
            {
                auto* parameter = state.getParameter (ids[(size_t) i]);
                jassert (parameter != nullptr);
                if (parameter == nullptr) continue;
                params[(size_t) i] = parameter;
                raw[(size_t) i] = state.getRawParameterValue (ids[(size_t) i]);
                auto& info = metadata[(size_t) i];
                info.valid = true; info.name = parameter->getName (64);
                info.range = parameter->getNormalisableRange();
                info.initial = parameter->getDefaultValue(); info.unit = parameter->getLabel();
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter)) info.choices = choice->choices;
                info.toggle = dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr;
                const juce::String key (ids[(size_t) i]);
                if (info.choices.isEmpty() && ! info.toggle)
                {
                    if (key == "output" || key == "threshold" || key == "makeup" || key == "knee" || key == "range" || key == "A_level" || (type == P::heat && key == "drive")) info.unit = "dB";
                    else if (key == "timems" || key == "attack" || key == "release" || key == "A_grainSize") info.unit = "ms";
                    else if (key == "freq" || key == "hpf" || key == "lpf" || key == "scHpf" || key == "lowMonoFreq" || (type == P::eq && i >= 3)) info.unit = "Hz";
                    else if (key == "A_density") info.unit = "gr/s";
                    else if (key == "A_tuneCoarse") info.unit = "st";
                    else if ((juce::approximatelyEqual (info.range.start, 0.0f) && juce::approximatelyEqual (info.range.end, 1.0f)) || key.startsWith ("width") || key == "feedback")
                    { info.unit = "%"; info.displayScale = 100.0f; }
                }
                if (type == P::eq && i < 3) { info.name = i == 0 ? "Body" : i == 1 ? "Presence" : "Air"; info.unit = "dB"; }
            }
        if (type == P::granulator) state.getRawParameterValue ("A_srcLoopMode")->store (1.0f);
    }
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    { Module::ParamList values; module->addParameters (values, {}); return { values.begin(), values.end() }; }
    void apply (const Values& values)
    {
        for (int i = 0; i < P::controls; ++i)
            if (raw[(size_t) i] != nullptr)
                raw[(size_t) i]->store (params[(size_t) i]->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, values[(size_t) i])), std::memory_order_relaxed);
        if (auto* rhythm = dynamic_cast<sattari::dsp::RhythmGenieModule*> (module.get()))
        {
            const int density = juce::roundToInt (values[0] * 100), style = juce::roundToInt (raw[6]->load());
            if (density != rhythmDensity || style != rhythmStyle)
            {
                using Style = sattari::dsp::GrooveStyle;
                static constexpr Style styles[] { Style::FourFloor, Style::BoomBap, Style::Trap, Style::House, Style::Breakbeat, Style::HalfTime };
                auto& grid = rhythm->editableGrid(); grid.setLanes (8); grid.setSteps (16);
                sattari::dsp::generate (grid, styles[juce::jlimit (0, 5, style)], (float) density / 100.0f, 0x53415452u);
                rhythmDensity = density; rhythmStyle = style;
            }
        }
    }
    const juce::String getName() const override { return "Module state"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    std::unique_ptr<Module> module;
    juce::AudioProcessorValueTreeState state;
    std::array<juce::RangedAudioParameter*, P::controls> params {};
    std::array<std::atomic<float>*, P::controls> raw {};
    std::array<P::ControlInfo, P::controls> metadata;
    int rhythmDensity = -1, rhythmStyle = -1;
};

struct Delay
{
    juce::AudioBuffer<float> ring;
    int position = 0;
    void prepare (int channels, int maximum) { ring.setSize (channels, juce::jmax (1, maximum + 1)); reset(); }
    void reset() { ring.clear(); position = 0; }
    void process (juce::AudioBuffer<float>& b, int delay)
    {
        const int capacity = ring.getNumSamples();
        delay = juce::jlimit (0, capacity - 1, delay);
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            const int read = (position + capacity - delay) % capacity;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const float input = b.getSample (ch, s);
                ring.setSample (ch, position, input);
                b.setSample (ch, s, ring.getSample (ch, read));
            }
            position = (position + 1) % capacity;
        }
    }
};

float sane (float value, float fallback = 0.0f) { return std::isfinite (value) ? value : fallback; }
}

struct SattariModularProcessor::Impl
{
    struct Node
    {
        std::array<std::unique_ptr<Unit>, typeCount> units;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::array<std::atomic<float>*, controls> values {};
        std::array<std::atomic<float>, controls> effective {};
        std::array<juce::SmoothedValue<float>, controls> smooth;
        juce::SmoothedValue<float> wet;
        int lastType = -1;
        Delay dryDelay, wetDelay;
    };
    struct Map { std::atomic<float>* target = nullptr; std::atomic<float>* low = nullptr; std::atomic<float>* high = nullptr; };
    struct Frame
    {
        std::array<int, nodes> types {};
        std::array<bool, nodes> bypass {};
        std::array<Values, nodes> values {};
        float gain = 0, returnLevel = 0, wet = 1;
        bool play = false;
    } current;
    std::array<Node, nodes> slots;
    std::array<std::atomic<float>*, macros> macroValues {};
    std::array<std::array<Map, mappings>, macros> maps;
    std::atomic<float> *gain = nullptr, *returnLevel = nullptr, *bypass = nullptr, *workspace = nullptr;
    juce::AudioBuffer<float> raw, branch, dry, source;
    juce::MidiBuffer events, layerEvents, noMidi;
    Delay branchDry, bypassDelay;
    juce::SmoothedValue<float> outputGain, sendGain, masterWet;
    int capacity = 0, channels = 2, budget = 0;
    double sampleRate = 48000.0, freePpq = 0.0;
    bool prepared = false;
    struct Note { int note = 60; float velocity = 0; bool on = false; };
    juce::AbstractFifo noteQueue { 128 };
    std::atomic<bool> panic { false };
    std::atomic<bool> resetRequested { false };
    std::array<Note, 128> notes;
    std::array<juce::ValueTree, sourceSlots> sampleStates;
    std::array<std::shared_ptr<const sattari::dsp::SourceSample>, sourceSlots> sources;
    std::array<juce::String, sourceSlots> sampleNames { "Factory harmonic source", "Factory harmonic source" };
    std::shared_ptr<const sattari::dsp::SourceSample> factorySource;

    explicit Impl (SattariModularProcessor& p)
    {
        for (int n = 0; n < nodes; ++n)
        {
            auto& slot = slots[(size_t) n];
            slot.type = p.parameters.getRawParameterValue (nodeId (n, "type"));
            slot.bypass = p.parameters.getRawParameterValue (nodeId (n, "bypass"));
            for (int c = 0; c < controls; ++c) slot.values[(size_t) c] = p.parameters.getRawParameterValue (controlId (n, c));
            for (int type = 1; type < typeCount; ++type)
                if (allowedType (n, type)) slot.units[(size_t) type] = std::make_unique<Unit> (type);
        }
        for (int m = 0; m < macros; ++m)
        {
            macroValues[(size_t) m] = p.parameters.getRawParameterValue (macroId (m));
            for (int i = 0; i < mappings; ++i)
            {
                auto& map = maps[(size_t) m][(size_t) i];
                const auto suffix = juce::String (i + 1);
                map.target = p.parameters.getRawParameterValue (macroId (m, "target" + suffix));
                map.low = p.parameters.getRawParameterValue (macroId (m, "low" + suffix));
                map.high = p.parameters.getRawParameterValue (macroId (m, "high" + suffix));
            }
        }
        gain = p.parameters.getRawParameterValue ("output"); returnLevel = p.parameters.getRawParameterValue ("return");
        bypass = p.parameters.getRawParameterValue ("bypass"); workspace = p.parameters.getRawParameterValue ("workspace");
        auto sample = std::make_shared<sattari::dsp::SourceSample>();
        sample->audio.setSize (1, 48000); sample->lengthSamples = 48000; sample->version = 1;
        const double hz = juce::MidiMessage::getMidiNoteInHertz (60);
        for (int i = 0; i < 48000; ++i)
        {
            double x = 0.0;
            for (int h = 1; h <= 12; ++h) x += std::sin (juce::MathConstants<double>::twoPi * hz * h * i / 48000.0) / (h * h);
            sample->audio.setSample (0, i, (float) (x * 0.25));
        }
        factorySource = sample;
        for (int l = 0; l < sourceSlots; ++l) publishSource (l, factorySource);
    }

    void publishSource (int layer, std::shared_ptr<const sattari::dsp::SourceSample> sourceData)
    {
        sources[(size_t) layer] = sourceData;
        auto& unit = slots[(size_t) (effectSlots + layer)].units[granulator];
        static_cast<sattari::dsp::GranulatorModule*> (unit->module.get())->setSource (
            sattari::dsp::GranulatorModule::Layer::A, std::move (sourceData));
    }

    void copySource (int from, int to)
    {
        publishSource (to, sources[(size_t) from]);
        // Source payloads remain immutable and shared by bounded undo entries.
        sampleStates[(size_t) to] = sampleStates[(size_t) from];
        sampleNames[(size_t) to] = sampleNames[(size_t) from];
    }

    void swapSources()
    {
        std::swap (sources[0], sources[1]); std::swap (sampleStates[0], sampleStates[1]); std::swap (sampleNames[0], sampleNames[1]);
        for (int layer = 0; layer < sourceSlots; ++layer)
        {
            publishSource (layer, sources[(size_t) layer]);
        }
    }

    bool readFrame (SattariModularProcessor& p)
    {
        const unsigned revision = p.editRevision.load (std::memory_order_acquire);
        if ((revision & 1u) != 0) return false;
        Frame next;
        for (int n = 0; n < nodes; ++n)
        {
            auto& slot = slots[(size_t) n];
            const int type = juce::jlimit (0, typeCount - 1, juce::roundToInt (sane (slot.type->load())));
            next.types[(size_t) n] = allowedType (n, type) ? type : empty;
            next.bypass[(size_t) n] = slot.bypass->load() >= 0.5f;
            for (int c = 0; c < controls; ++c) next.values[(size_t) n][(size_t) c] = juce::jlimit (0.0f, 1.0f, sane (slot.values[(size_t) c]->load()));
        }
        for (int m = 0; m < macros; ++m)
            for (auto& map : maps[(size_t) m])
            {
                const int target = juce::jlimit (0, nodes * controls, juce::roundToInt (sane (map.target->load()))) - 1;
                if (target < 0) continue;
                const float low = sane (map.low->load()), high = sane (map.high->load());
                next.values[(size_t) (target / controls)][(size_t) (target % controls)] = juce::jlimit (0.0f, 1.0f,
                    low + sane (macroValues[(size_t) m]->load()) * (high - low));
            }
        next.gain = juce::jlimit (-48.0f, 12.0f, sane (gain->load()));
        next.returnLevel = juce::jlimit (0.0f, 1.0f, sane (returnLevel->load()));
        next.wet = bypass->load() >= 0.5f ? 0.0f : 1.0f;
        next.play = SATTARI_MODULAR_SYNTH != 0 || workspace->load() >= 1.5f;
        if (p.editRevision.load (std::memory_order_acquire) != revision) return false;
        current = next; return true;
    }

    void processNode (int n, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                      const sattari::host::TransportSnapshot& transport)
    {
        auto& slot = slots[(size_t) n];
        const int type = current.types[(size_t) n];
        auto* unit = slot.units[(size_t) type].get();
        if (slot.lastType != type)
        {
            if (slot.lastType > 0) slot.units[(size_t) slot.lastType]->module->reset();
            if (unit != nullptr) unit->module->reset();
            slot.lastType = type;
            for (int c = 0; c < controls; ++c) slot.smooth[(size_t) c].setCurrentAndTargetValue (current.values[(size_t) n][(size_t) c]);
        }
        Values effective;
        for (int c = 0; c < controls; ++c)
        {
            auto& smoother = slot.smooth[(size_t) c];
            const float target = current.values[(size_t) n][(size_t) c];
            if (unit && (unit->metadata[(size_t) c].toggle || ! unit->metadata[(size_t) c].choices.isEmpty()))
                smoother.setCurrentAndTargetValue (target);
            else smoother.setTargetValue (target);
            effective[(size_t) c] = smoother.skip (buffer.getNumSamples());
            slot.effective[(size_t) c].store (effective[(size_t) c], std::memory_order_relaxed);
        }
        juce::AudioBuffer<float> dryView (dry.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) dryView.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
        if (n >= effectSlots) dryView.clear();
        slot.dryDelay.process (dryView, n < effectSlots ? budget : 0);
        if (unit != nullptr)
        {
            unit->apply (effective); unit->module->setTransport (transport); unit->module->setSidechain (nullptr);
            unit->module->process (buffer, midi);
        }
        else if (n >= effectSlots) buffer.clear();
        slot.wetDelay.process (buffer, n < effectSlots ? budget - (unit ? unit->module->latencySamples() : 0) : 0);
        slot.wet.setTargetValue (type != empty && ! current.bypass[(size_t) n] ? 1.0f : 0.0f);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            const float mix = slot.wet.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, s, dryView.getSample (ch, s) + mix * (buffer.getSample (ch, s) - dryView.getSample (ch, s)));
        }
    }
};

struct SattariModularProcessor::History
{
    juce::ValueTree rack;
    std::array<std::shared_ptr<const sattari::dsp::SourceSample>, sourceSlots> sources;
    std::array<juce::ValueTree, sourceSlots> sampleStates;
    std::array<juce::String, sourceSlots> sampleNames;
};

juce::String SattariModularProcessor::nodeId (int node, const juce::String& suffix) { return "node" + juce::String (node + 1) + "." + suffix; }
juce::String SattariModularProcessor::controlId (int node, int control) { return nodeId (node, "control" + juce::String (control + 1)); }
juce::String SattariModularProcessor::macroId (int macro, const juce::String& suffix) { return "macro" + juce::String (macro + 1) + "." + suffix; }
juce::String SattariModularProcessor::typeName (int type)
{
    static const char* names[] = { "Empty", "Clean", "EQ", "Comp", "Heat", "Echo", "Space", "Pulse", "De-Ess", "Image", "Granulator", "Rhythm Genie" };
    return names[juce::jlimit (0, typeCount - 1, type)];
}
bool SattariModularProcessor::allowedType (int node, int type)
{ return type == empty || (node < effectSlots ? type > empty && type <= image : type == granulator || type == rhythm); }
juce::StringArray SattariModularProcessor::availableTypes (int node)
{ juce::StringArray result; for (int t = 0; t < typeCount; ++t) if (allowedType (node, t)) result.add (typeName (t)); return result; }
juce::String SattariModularProcessor::ControlInfo::text (float normalised) const
{
    const float actual = range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised));
    if (! choices.isEmpty()) return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (actual))];
    if (toggle) return actual >= 0.5f ? "On" : "Off";
    if (unit == "%") return juce::String (actual * displayScale, 1) + "%";
    return juce::String (actual, range.interval >= 1.0f ? 0 : range.interval >= 0.1f ? 1 : 2) + (unit.isEmpty() ? "" : " " + unit);
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariModularProcessor::layout()
{
    Module::ParamList p;
    using F = juce::AudioParameterFloat; using I = juce::AudioParameterInt;
    for (int n = 0; n < nodes; ++n)
    {
        const auto label = (n < effectSlots ? "FX " : "Layer ") + juce::String (n < effectSlots ? n + 1 : n - effectSlots + 1);
        p.push_back (std::make_unique<I> (juce::ParameterID { nodeId (n, "type"), 1 }, label + " Module", 0, typeCount - 1, 0));
        p.push_back (std::make_unique<BinaryParameter> (juce::ParameterID { nodeId (n, "bypass"), 1 }, label + " Bypass", false));
        for (int c = 0; c < controls; ++c)
            p.push_back (std::make_unique<F> (juce::ParameterID { controlId (n, c), 1 }, label + " Control " + juce::String (c + 1), juce::NormalisableRange<float> (0, 1), 0));
    }
    for (int m = 0; m < macros; ++m)
    {
        const auto label = "Macro " + juce::String (m + 1);
        p.push_back (std::make_unique<F> (juce::ParameterID { macroId (m), 1 }, label, juce::NormalisableRange<float> (0, 1), 0));
        for (int map = 0; map < mappings; ++map)
        {
            const auto suffix = juce::String (map + 1);
            p.push_back (std::make_unique<I> (juce::ParameterID { macroId (m, "target" + suffix), 1 }, label + " Target " + suffix, 0, nodes * controls, 0));
            for (const auto* key : { "low", "high" })
                p.push_back (std::make_unique<F> (juce::ParameterID { macroId (m, key + suffix), 1 }, label + " " + key + suffix, juce::NormalisableRange<float> (0, 1), juce::String (key) == "low" ? 0.0f : 1.0f));
        }
    }
    p.push_back (std::make_unique<F> (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-48, 12, 0.1f), -6));
    p.push_back (std::make_unique<F> (juce::ParameterID { "return", 1 }, "Ambience Return", juce::NormalisableRange<float> (0, 1, 0.001f), 0.18f));
    p.push_back (std::make_unique<BinaryParameter> (juce::ParameterID { "bypass", 1 }, "Master Bypass", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "workspace", 1 }, "Workspace", juce::StringArray { "Mix", "Design", "Play" }, SATTARI_MODULAR_SYNTH ? 2 : 0));
    return { p.begin(), p.end() };
}

SattariModularProcessor::SattariModularProcessor()
    : AudioProcessor (SATTARI_MODULAR_SYNTH ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MODULAR_VALUES", layout()), impl (std::make_unique<Impl> (*this))
{ loadFactory (SATTARI_MODULAR_SYNTH ? 2 : 0); undoStates.clear(); }
SattariModularProcessor::~SattariModularProcessor() = default;
bool SattariModularProcessor::isBusesLayoutSupported (const BusesLayout& b) const
{
    const auto out = b.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && (SATTARI_MODULAR_SYNTH ? b.getMainInputChannelSet().isDisabled() : b.getMainInputChannelSet() == out);
}
void SattariModularProcessor::prepareToPlay (double sr, int block)
{
    auto& e = *impl; e.prepared = false;
    e.sampleRate = sr > 0 ? sr : 48000.0; e.capacity = juce::jmax (1, block); e.channels = getTotalNumOutputChannels(); e.budget = 0;
    for (int n = 0; n < nodes; ++n)
        for (auto& unit : e.slots[(size_t) n].units)
            if (unit)
            {
                unit->module->prepare (e.sampleRate, e.capacity, e.channels);
                if (n < effectSlots) e.budget = juce::jmax (e.budget, unit->module->latencySamples());
            }
    for (auto* b : { &e.raw, &e.branch, &e.dry, &e.source }) b->setSize (e.channels, e.capacity);
    for (auto& slot : e.slots)
    {
        slot.dryDelay.prepare (e.channels, e.budget); slot.wetDelay.prepare (e.channels, e.budget);
        slot.wet.reset (e.sampleRate, 0.01);
        for (auto& smooth : slot.smooth) smooth.reset (e.sampleRate, 0.02);
    }
    e.branchDry.prepare (e.channels, 2 * e.budget); e.bypassDelay.prepare (e.channels, effectSlots * e.budget);
    e.events.ensureSize (131072); e.layerEvents.ensureSize (131072); e.noMidi.ensureSize (4096);
    e.outputGain.reset (e.sampleRate, 0.02); e.sendGain.reset (e.sampleRate, 0.02); e.masterWet.reset (e.sampleRate, 0.01);
    e.readFrame (*this); e.outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (e.current.gain));
    e.sendGain.setCurrentAndTargetValue (e.current.returnLevel); e.masterWet.setCurrentAndTargetValue (e.current.wet);
    setLatencySamples (effectSlots * e.budget);
    reset(); e.resetRequested.store (false); e.prepared = true;
}
void SattariModularProcessor::releaseResources() { impl->prepared = false; }
void SattariModularProcessor::reset()
{
    resetEngine();
    auto& e = *impl;
    int a, as, c, cs; e.noteQueue.prepareToRead (e.noteQueue.getNumReady(), a, as, c, cs);
    e.noteQueue.finishedRead (as + cs); e.panic.store (false);
}
void SattariModularProcessor::resetEngine()
{
    auto& e = *impl;
    for (auto& slot : e.slots)
    {
        if (slot.lastType > 0) slot.units[(size_t) slot.lastType]->module->reset();
        slot.lastType = -1; slot.dryDelay.reset(); slot.wetDelay.reset(); slot.wet.setCurrentAndTargetValue (0.0f);
    }
    e.branchDry.reset(); e.bypassDelay.reset(); e.freePpq = 0;
    clipped.store (false); cpu.store (0);
    for (auto& v : rms) v.store (0); for (auto& v : peak) v.store (0);
}
void SattariModularProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) { process (b, midi, false); }
void SattariModularProcessor::processBlockBypassed (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) { process (b, midi, true); }
void SattariModularProcessor::process (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;
    auto& e = *impl;
    const auto startTicks = juce::Time::getHighResolutionTicks();
    if (! e.prepared || b.getNumChannels() != e.channels) { b.clear(); midi.clear(); return; }
    if (e.readFrame (*this) && e.resetRequested.exchange (false)) resetEngine();
    const int total = b.getNumSamples();
    auto transport = sattari::host::read (*this);
    if (! std::isfinite (transport.bpm)) { transport.bpm = 120; transport.hasHostBpm = false; }
    if (! std::isfinite (transport.ppqPosition)) transport.hasPpq = false;
    if (! transport.hasPpq) transport.ppqPosition = e.freePpq;
    e.outputGain.setTargetValue (juce::Decibels::decibelsToGain (e.current.gain));
    const bool branchActive = (e.current.types[6] != empty && ! e.current.bypass[6]) || (e.current.types[7] != empty && ! e.current.bypass[7]);
    e.sendGain.setTargetValue (branchActive ? e.current.returnLevel : 0.0f);
    e.masterWet.setTargetValue (hostBypass ? 0.0f : e.current.wet);
    double sum[2] {}; float maximum[2] {};
    for (int offset = 0; offset < total; offset += e.capacity)
    {
        const int count = juce::jmin (e.capacity, total - offset);
        juce::AudioBuffer<float> main (b.getArrayOfWritePointers(), e.channels, offset, count);
        juce::AudioBuffer<float> dryInput (e.raw.getArrayOfWritePointers(), e.channels, count);
        juce::AudioBuffer<float> branch (e.branch.getArrayOfWritePointers(), e.channels, count);
        for (int ch = 0; ch < e.channels; ++ch)
        {
            if (SATTARI_MODULAR_SYNTH) main.clear (ch, 0, count);
            for (int s = 0; s < count; ++s) main.setSample (ch, s, sane (main.getSample (ch, s)));
            dryInput.copyFrom (ch, 0, main, ch, 0, count);
        }
        e.bypassDelay.process (dryInput, getLatencySamples());
        e.events.clear(); int admitted = 0; bool midiOverflow = false;
        for (const auto meta : midi)
            if (meta.samplePosition >= offset && meta.samplePosition < offset + count && meta.numBytes <= 3)
            {
                if (admitted++ < maxMidiEvents) e.events.addEvent (meta.data, meta.numBytes, meta.samplePosition - offset);
                else midiOverflow = true;
            }
        if (offset == 0)
        {
            int a, as, c, cs; e.noteQueue.prepareToRead (e.noteQueue.getNumReady(), a, as, c, cs);
            for (int i = 0; i < as + cs; ++i)
            {
                const auto& note = e.notes[(size_t) (i < as ? a + i : c + i - as)];
                e.events.addEvent (note.on ? juce::MidiMessage::noteOn (1, note.note, note.velocity) : juce::MidiMessage::noteOff (1, note.note), 0);
            }
            e.noteQueue.finishedRead (as + cs);
            if (e.panic.exchange (false))
            {
                e.events.clear();
                for (int channel = 1; channel <= 16; ++channel) e.events.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
            }
        }
        if (midiOverflow)
        {
            e.events.clear();
            for (int channel = 1; channel <= 16; ++channel) e.events.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
        }
        if (e.current.play)
        {
            main.clear();
            for (int layer = 0; layer < sourceSlots; ++layer)
            {
                juce::AudioBuffer<float> source (e.source.getArrayOfWritePointers(), e.channels, count); source.clear();
                e.layerEvents.clear(); e.layerEvents.addEvents (e.events, 0, count, 0);
                e.processNode (effectSlots + layer, source, e.layerEvents, transport);
                for (int ch = 0; ch < e.channels; ++ch) main.addFrom (ch, 0, source, ch, 0, count, 0.7f);
            }
        }
        else
            for (int n = effectSlots; n < nodes; ++n)
            {
                auto& slot = e.slots[(size_t) n];
                if (slot.lastType > 0) { slot.units[(size_t) slot.lastType]->module->reset(); slot.lastType = -1; }
            }
        e.noMidi.clear();
        for (int n = 0; n < mainSlots; ++n) e.processNode (n, main, e.noMidi, transport);
        for (int ch = 0; ch < e.channels; ++ch) branch.copyFrom (ch, 0, main, ch, 0, count);
        for (int n = mainSlots; n < effectSlots; ++n) e.processNode (n, branch, e.noMidi, transport);
        e.branchDry.process (main, 2 * e.budget);
        for (int s = 0; s < count; ++s)
        {
            const float gainValue = e.outputGain.getNextValue(), send = e.sendGain.getNextValue(), wet = e.masterWet.getNextValue();
            for (int ch = 0; ch < e.channels; ++ch)
            {
                const float processed = (main.getSample (ch, s) + send * branch.getSample (ch, s)) * gainValue;
                const float input = dryInput.getSample (ch, s);
                const float out = sane (input + wet * (processed - input));
                main.setSample (ch, s, out); sum[ch] += (double) out * out; maximum[ch] = juce::jmax (maximum[ch], std::abs (out));
            }
        }
        if (transport.isPlaying || ! transport.hasPpq) transport.ppqPosition += count / e.sampleRate * transport.bpm / 60.0;
    }
    e.freePpq += total / e.sampleRate * transport.bpm / 60.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const int sourceCh = juce::jmin (ch, e.channels - 1);
        rms[(size_t) ch].store (total > 0 ? (float) std::sqrt (sum[sourceCh] / total) : 0.0f);
        peak[(size_t) ch].store (maximum[sourceCh]); if (maximum[sourceCh] >= 1.0f) clipped.store (true);
    }
    if (total > 0) cpu.store ((float) (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks) / (total / e.sampleRate)));
    midi.clear();
}

int SattariModularProcessor::nodeType (int node) const
{
    if (node < 0 || node >= nodes) return empty;
    const int type = juce::jlimit (0, typeCount - 1, juce::roundToInt (value (nodeId (node, "type"))));
    return allowedType (node, type) ? type : empty;
}
SattariModularProcessor::ControlInfo SattariModularProcessor::info (int type, int control) const
{
    if (type <= empty || type >= typeCount || control < 0 || control >= controls) return {};
    const auto& unit = impl->slots[(size_t) (type <= image ? 0 : effectSlots)].units[(size_t) type];
    return unit ? unit->metadata[(size_t) control] : ControlInfo {};
}
float SattariModularProcessor::value (const juce::String& id) const
{ if (auto* p = parameters.getRawParameterValue (id)) return sane (p->load()); return 0; }
void SattariModularProcessor::set (const juce::String& id, float actual, bool gesture)
{
    if (auto* p = parameters.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (sane (actual)));
        if (gesture) p->endChangeGesture();
    }
}
void SattariModularProcessor::selectModuleInternal (int node, int type)
{
    if (! allowedType (node, type)) return;
    set (nodeId (node, "type"), (float) type); set (nodeId (node, "bypass"), 0);
    for (int c = 0; c < controls; ++c) set (controlId (node, c), info (type, c).initial);
    for (int m = 0; m < macros; ++m)
        for (int i = 0; i < mappings; ++i)
        {
            const auto id = macroId (m, "target" + juce::String (i + 1)); const int target = (int) value (id) - 1;
            if (target >= 0 && target / controls == node) set (id, 0);
        }
}
void SattariModularProcessor::selectModule (int node, int type)
{
    if (node < 0 || node >= nodes || type < 0 || type >= typeCount || ! allowedType (node, type)) return;
    const juce::ScopedLock lock (storageLock); captureUndo(); editRevision.fetch_add (1);
    selectModuleInternal (node, type); editRevision.fetch_add (1); ++uiRevision;
}
bool SattariModularProcessor::duplicateModule (int node)
{
    if (node < 0 || node >= nodes || nodeType (node) == empty) return false;
    const int first = node < mainSlots ? 0 : node < effectSlots ? mainSlots : effectSlots;
    const int end = node < mainSlots ? mainSlots : node < effectSlots ? effectSlots : nodes;
    for (int dest = first; dest < end; ++dest)
        if (nodeType (dest) == empty)
        {
            const juce::ScopedLock lock (storageLock); captureUndo(); editRevision.fetch_add (1);
            set (nodeId (dest, "type"), (float) nodeType (node)); set (nodeId (dest, "bypass"), value (nodeId (node, "bypass")));
            for (int c = 0; c < controls; ++c)
            {
                float copied = value (controlId (node, c));
                for (int m = 0; m < macros; ++m) for (int i = 0; i < mappings; ++i)
                {
                    const auto suffix = juce::String (i + 1);
                    if ((int) value (macroId (m, "target" + suffix)) != node * controls + c + 1) continue;
                    const float low = value (macroId (m, "low" + suffix));
                    copied = low + value (macroId (m)) * (value (macroId (m, "high" + suffix)) - low);
                }
                set (controlId (dest, c), copied);
            }
            if (node >= effectSlots) impl->copySource (node - effectSlots, dest - effectSlots);
            editRevision.fetch_add (1); ++uiRevision; return true;
        }
    return false;
}
void SattariModularProcessor::moveModule (int node, int direction)
{
    if (node < 0 || node >= nodes || (direction != -1 && direction != 1)) return;
    const int other = node + direction;
    if (other < 0 || other >= nodes || (node < mainSlots) != (other < mainSlots) || (node < effectSlots) != (other < effectSlots)) return;
    const juce::ScopedLock lock (storageLock); captureUndo(); editRevision.fetch_add (1);
    for (const auto& suffix : juce::StringArray { "type", "bypass", "control1", "control2", "control3", "control4", "control5", "control6", "control7", "control8" })
    {
        const auto a = nodeId (node, suffix), b = nodeId (other, suffix); const float old = value (a);
        set (a, value (b)); set (b, old);
    }
    for (int m = 0; m < macros; ++m)
        for (int i = 0; i < mappings; ++i)
        {
            const auto id = macroId (m, "target" + juce::String (i + 1)); const int target = (int) value (id) - 1;
            if (target >= 0 && (target / controls == node || target / controls == other))
                set (id, (float) ((target / controls == node ? other : node) * controls + target % controls + 1));
        }
    if (node >= effectSlots) impl->swapSources();
    editRevision.fetch_add (1); ++uiRevision;
}
void SattariModularProcessor::clearRack()
{
    const juce::ScopedLock lock (storageLock); captureUndo(); editRevision.fetch_add (1);
    for (int n = 0; n < nodes; ++n) selectModuleInternal (n, empty);
    set ("return", 0); set ("output", 0); set ("bypass", 0); name = "Empty Rack";
    editRevision.fetch_add (1); ++uiRevision;
}
const juce::String SattariModularProcessor::getProgramName (int p)
{ return juce::StringArray { "Studio Vocal", "Motion & Air", "Glass Keys", "Empty Rack" }[juce::jlimit (0, 3, p)]; }
void SattariModularProcessor::setCurrentProgram (int p) { loadFactory (p); }
void SattariModularProcessor::loadFactory (int program)
{
    program = juce::jlimit (0, 3, program);
    const juce::ScopedLock lock (storageLock); captureUndo(); editRevision.fetch_add (1);
    for (int n = 0; n < nodes; ++n) selectModuleInternal (n, empty);
    for (int m = 0; m < macros; ++m)
    {
        set (macroId (m), 0);
        for (int i = 0; i < mappings; ++i)
        {
            const auto suffix = juce::String (i + 1);
            set (macroId (m, "low" + suffix), 0); set (macroId (m, "high" + suffix), 1);
        }
    }
    for (int layer = 0; layer < sourceSlots; ++layer)
    {
        impl->publishSource (layer, impl->factorySource); impl->sampleStates[(size_t) layer] = {};
        impl->sampleNames[(size_t) layer] = "Factory harmonic source";
    }
    set ("workspace", (float) (program < 3 ? program : SATTARI_MODULAR_SYNTH ? 2 : 0));
    set ("bypass", 0); set ("output", -6); set ("return", program == 3 ? 0 : 0.16f);
    if (program != 3)
    {
        const std::array<int, 4> types = program == 0 ? std::array<int, 4> { clean, eq, comp, heat }
            : program == 1 ? std::array<int, 4> { heat, pulse, eq, empty } : std::array<int, 4> { eq, comp, heat, empty };
        for (int n = 0; n < 4; ++n) selectModuleInternal (n, types[(size_t) n]);
        selectModuleInternal (6, echo); selectModuleInternal (7, space);
        set (controlId (6, 2), 1); set (controlId (7, 2), 1); set (controlId (7, 3), 0);
        if (program == 2 || SATTARI_MODULAR_SYNTH) selectModuleInternal (8, granulator);
        int heatSlot = program == 0 ? 3 : program == 1 ? 0 : 2;
        set (macroId (0, "target1"), (float) (heatSlot * controls + 1));
        set (macroId (0, "low1"), 0); set (macroId (0, "high1"), 0.5f); set (macroId (0), 0.25f);
        set (macroId (1, "target1"), (float) (7 * controls + 1));
        set (macroId (1, "low1"), 0.1f); set (macroId (1, "high1"), 0.95f); set (macroId (1), 0.4f);
        set (macroId (2, "target1"), (float) (6 * controls + 2));
        set (macroId (2, "low1"), 0.05f); set (macroId (2, "high1"), 0.7f); set (macroId (2), 0.2f);
    }
    currentProgram.store (program); name = getProgramName (program); sceneIndex = abIndex = 0;
    impl->resetRequested.store (true);
    editRevision.fetch_add (1);
    for (auto& scene : scenes) scene = snapshot(); for (auto& comparison : comparisons) comparison = snapshot();
    ++uiRevision;
}
juce::ValueTree SattariModularProcessor::snapshot()
{ juce::ValueTree tree ("RACK"); tree.setProperty ("name", name, nullptr); tree.addChild (parameters.copyState(), -1, nullptr); return tree; }
void SattariModularProcessor::restore (const juce::ValueTree& tree)
{
    const auto values = tree.getChildWithName ("MODULAR_VALUES"); if (! values.isValid()) return;
    editRevision.fetch_add (1); parameters.replaceState (values.createCopy()); name = tree.getProperty ("name", "User Rack").toString();
    impl->resetRequested.store (true); editRevision.fetch_add (1); ++uiRevision;
}
SattariModularProcessor::History SattariModularProcessor::historySnapshot()
{
    juce::ValueTree result ("HISTORY"); result.addChild (snapshot(), -1, nullptr);
    result.setProperty ("scene", sceneIndex, nullptr); result.setProperty ("ab", abIndex, nullptr);
    result.setProperty ("program", currentProgram.load(), nullptr);
    juce::ValueTree sceneStates ("SCENES"), abStates ("COMPARISONS");
    for (const auto& s : scenes) if (s.isValid()) sceneStates.addChild (s.createCopy(), -1, nullptr);
    for (const auto& s : comparisons) if (s.isValid()) abStates.addChild (s.createCopy(), -1, nullptr);
    result.addChild (sceneStates, -1, nullptr); result.addChild (abStates, -1, nullptr);
    return { result, impl->sources, impl->sampleStates, impl->sampleNames };
}
void SattariModularProcessor::restoreHistory (const History& history)
{
    const auto& tree = history.rack;
    for (int layer = 0; layer < sourceSlots; ++layer) impl->publishSource (layer, history.sources[(size_t) layer]);
    impl->sampleStates = history.sampleStates; impl->sampleNames = history.sampleNames;
    restore (tree.getChildWithName ("RACK")); sceneIndex = (int) tree["scene"]; abIndex = (int) tree["ab"];
    currentProgram.store ((int) tree["program"]);
    for (int i = 0; i < 3; ++i) { auto s = tree.getChildWithName ("SCENES").getChild (i); scenes[(size_t) i] = s.isValid() ? s.createCopy() : snapshot(); }
    for (int i = 0; i < 2; ++i) { auto s = tree.getChildWithName ("COMPARISONS").getChild (i); comparisons[(size_t) i] = s.isValid() ? s.createCopy() : snapshot(); }
}
void SattariModularProcessor::captureUndo()
{ const juce::ScopedLock lock (storageLock); undoStates.push_back (historySnapshot()); if (undoStates.size() > 40) undoStates.erase (undoStates.begin()); redoStates.clear(); }
bool SattariModularProcessor::canUndo() const { const juce::ScopedLock lock (storageLock); return ! undoStates.empty(); }
bool SattariModularProcessor::canRedo() const { const juce::ScopedLock lock (storageLock); return ! redoStates.empty(); }
void SattariModularProcessor::undo()
{ const juce::ScopedLock lock (storageLock); if (undoStates.empty()) return; redoStates.push_back (historySnapshot()); const auto s = undoStates.back(); undoStates.pop_back(); restoreHistory (s); }
void SattariModularProcessor::redo()
{ const juce::ScopedLock lock (storageLock); if (redoStates.empty()) return; undoStates.push_back (historySnapshot()); const auto s = redoStates.back(); redoStates.pop_back(); restoreHistory (s); }
void SattariModularProcessor::selectScene (int index)
{ const juce::ScopedLock lock (storageLock); if (index < 0 || index > 2 || index == sceneIndex) return; captureUndo(); scenes[(size_t) sceneIndex] = snapshot(); sceneIndex = index; restore (scenes[(size_t) index]); }
void SattariModularProcessor::selectAB (int index)
{ const juce::ScopedLock lock (storageLock); if (index < 0 || index > 1 || index == abIndex) return; captureUndo(); comparisons[(size_t) abIndex] = snapshot(); abIndex = index; restore (comparisons[(size_t) index]); }
void SattariModularProcessor::copyAB() { const juce::ScopedLock lock (storageLock); captureUndo(); comparisons[(size_t) (1 - abIndex)] = snapshot(); }
bool SattariModularProcessor::isMapped (int n, int c) const
{
    for (int m = 0; m < macros; ++m) for (int i = 0; i < mappings; ++i)
        if ((int) value (macroId (m, "target" + juce::String (i + 1))) == n * controls + c + 1) return true;
    return false;
}
float SattariModularProcessor::effectiveValue (int n, int c) const { return impl->slots[(size_t) n].effective[(size_t) c].load(); }
juce::String SattariModularProcessor::presetName() const { const juce::ScopedLock lock (storageLock); return name; }
juce::String SattariModularProcessor::lastError() const { const juce::ScopedLock lock (storageLock); return error; }
juce::String SattariModularProcessor::sampleName (int layer) const { const juce::ScopedLock lock (storageLock); return impl->sampleNames[(size_t) juce::jlimit (0, sourceSlots - 1, layer)]; }

juce::Result SattariModularProcessor::loadSample (int layer, const juce::File& file)
{
    if (layer < 0 || layer >= sourceSlots) return juce::Result::fail ("Invalid instrument layer");
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (! reader || reader->lengthInSamples < 32 || reader->lengthInSamples > maxSampleFrames
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate < 8000 || reader->sampleRate > 192000
        || reader->numChannels < 1 || reader->numChannels > 2) return juce::Result::fail ("Choose a mono/stereo audio file up to 1,440,000 samples, 8-192 kHz.");
    auto source = std::make_shared<sattari::dsp::SourceSample>(); const int length = (int) reader->lengthInSamples;
    source->audio.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), length);
    if (! reader->read (&source->audio, 0, length, 0, true, source->audio.getNumChannels() > 1)) return juce::Result::fail ("Could not decode the audio file.");
    source->sourceSampleRate = reader->sampleRate; source->lengthSamples = length; source->version = 1;
    juce::MemoryOutputStream pcm;
    for (int ch = 0; ch < source->audio.getNumChannels(); ++ch)
        for (int s = 0; s < length; ++s)
        {
            const float sample = juce::jlimit (-4.0f, 4.0f, sane (source->audio.getSample (ch, s)));
            source->audio.setSample (ch, s, sample); pcm.writeFloat (sample);
        }
    juce::ValueTree sampleState ("SAMPLE"); sampleState.setProperty ("layer", layer, nullptr); sampleState.setProperty ("name", file.getFileName(), nullptr);
    sampleState.setProperty ("rate", reader->sampleRate, nullptr); sampleState.setProperty ("channels", source->audio.getNumChannels(), nullptr);
    sampleState.setProperty ("length", length, nullptr); sampleState.setProperty ("pcm", juce::var (pcm.getMemoryBlock()), nullptr);
    const juce::ScopedLock lock (storageLock); impl->publishSource (layer, source);
    impl->sampleStates[(size_t) layer] = sampleState; impl->sampleNames[(size_t) layer] = file.getFileName(); ++uiRevision;
    undoStates.clear(); redoStates.clear();
    return juce::Result::ok();
}
void SattariModularProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const juce::ScopedLock lock (storageLock);
    juce::ValueTree state ("SATTARI_MODULAR"); state.setProperty ("version", 1, nullptr);
    state.setProperty ("scene", sceneIndex, nullptr); state.setProperty ("ab", abIndex, nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.addChild (snapshot(), -1, nullptr);
    juce::ValueTree sceneStates ("SCENES"), abStates ("COMPARISONS");
    for (int i = 0; i < 3; ++i) sceneStates.addChild ((i == sceneIndex ? snapshot() : scenes[(size_t) i].createCopy()), -1, nullptr);
    for (int i = 0; i < 2; ++i) abStates.addChild ((i == abIndex ? snapshot() : comparisons[(size_t) i].createCopy()), -1, nullptr);
    state.addChild (sceneStates, -1, nullptr); state.addChild (abStates, -1, nullptr);
    for (int layer = 0; layer < sourceSlots; ++layer)
        if (const auto& sample = impl->sampleStates[(size_t) layer]; sample.isValid())
        {
            auto serialized = sample.createCopy(); serialized.setProperty ("layer", layer, nullptr);
            state.addChild (serialized, -1, nullptr);
        }
    juce::MemoryOutputStream out (dest, false); state.writeToStream (out);
}
void SattariModularProcessor::setStateInformation (const void* data, int size)
{
    const auto result = restoreState (data, size);
    const juce::ScopedLock lock (storageLock); error = result.getErrorMessage();
}
juce::Result SattariModularProcessor::restoreState (const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 32 * 1024 * 1024) return juce::Result::fail ("Invalid preset size.");
    const auto state = sattari::modular::StateReader::read (data, (size_t) size);
    if (! state.hasType ("SATTARI_MODULAR") || ! state["version"].isInt() || (int) state["version"] != 1) return juce::Result::fail ("Not a compatible Modular preset.");
    const auto integerInRange = [] (const juce::var& v, int low, int high)
    { return v.isInt() && (int) v >= low && (int) v <= high; };
    if (! integerInRange (state["scene"], 0, 2) || ! integerInRange (state["ab"], 0, 1)
        || ! integerInRange (state["program"], 0, 3)) return juce::Result::fail ("Invalid preset selection.");
    int racks = 0, sceneGroups = 0, comparisonGroups = 0;
    for (const auto child : state)
    {
        if (child.hasType ("RACK")) ++racks;
        else if (child.hasType ("SCENES")) ++sceneGroups;
        else if (child.hasType ("COMPARISONS")) ++comparisonGroups;
        else if (! child.hasType ("SAMPLE")) return juce::Result::fail ("Unknown preset section.");
    }
    if (racks != 1 || sceneGroups != 1 || comparisonGroups != 1) return juce::Result::fail ("Duplicate or missing preset section.");
    const auto validRack = [this] (const juce::ValueTree& rack)
    {
        const auto values = rack.getChildWithName ("MODULAR_VALUES");
        if (! rack.hasType ("RACK") || rack.getNumChildren() != 1 || ! rack["name"].isString()
            || ! values.isValid() || values.getNumChildren() != getParameters().size()) return false;
        juce::StringArray seen;
        for (auto child : values)
        {
            const auto id = child["id"].toString(); auto* parameter = parameters.getParameter (id);
            const auto rawValue = child["value"];
            const float v = (float) rawValue;
            if (! child.hasType ("PARAM") || child.getNumChildren() != 0 || ! (rawValue.isDouble() || rawValue.isInt() || rawValue.isInt64() || rawValue.isBool())
                || parameter == nullptr || seen.contains (id) || ! std::isfinite (v)) return false;
            const auto& range = parameter->getNormalisableRange();
            if (v < range.start || v > range.end || std::abs (v - range.snapToLegalValue (v)) > 0.0001f) return false;
            seen.add (id);
        }
        return true;
    };
    if (! validRack (state.getChildWithName ("RACK"))) return juce::Result::fail ("Invalid rack parameters.");
    for (const auto* group : { "SCENES", "COMPARISONS" })
    {
        const auto children = state.getChildWithName (group);
        if (children.getNumChildren() != (juce::String (group) == "SCENES" ? 3 : 2)) return juce::Result::fail ("Incomplete scene or A/B state.");
        for (const auto& rack : children) if (! validRack (rack)) return juce::Result::fail ("Invalid scene or A/B parameters.");
    }
    std::array<std::shared_ptr<const sattari::dsp::SourceSample>, sourceSlots> sources { impl->factorySource, impl->factorySource };
    std::array<juce::ValueTree, sourceSlots> sampleStates;
    for (auto child : state)
        if (child.hasType ("SAMPLE"))
        {
            const int layer = child["layer"], length = child["length"], channels = child["channels"]; const double sr = child["rate"];
            const auto bytes = child.getProperty ("pcm"); const auto* pcm = bytes.getBinaryData();
            if (! integerInRange (child["layer"], 0, sourceSlots - 1) || ! integerInRange (child["length"], 32, maxSampleFrames)
                || ! integerInRange (child["channels"], 1, 2) || ! child["name"].isString() || child.getNumChildren() != 0
                || ! std::isfinite (sr) || sr < 8000 || sr > 192000 || pcm == nullptr || pcm->getSize() != (size_t) length * (size_t) channels * sizeof (float)
                || sampleStates[(size_t) layer].isValid()) return juce::Result::fail ("Invalid embedded source sample.");
            auto sample = std::make_shared<sattari::dsp::SourceSample>(); sample->audio.setSize (channels, length);
            sample->sourceSampleRate = sr; sample->lengthSamples = length; sample->version = 1;
            juce::MemoryInputStream input (*pcm, false);
            for (int ch = 0; ch < channels; ++ch) for (int s = 0; s < length; ++s)
            {
                const float value = input.readFloat();
                if (! std::isfinite (value) || std::abs (value) > 4.0f) return juce::Result::fail ("Invalid embedded audio data.");
                sample->audio.setSample (ch, s, value);
            }
            sources[(size_t) layer] = sample; sampleStates[(size_t) layer] = child.createCopy();
        }
    const juce::ScopedLock lock (storageLock);
    for (int l = 0; l < sourceSlots; ++l)
    {
        impl->publishSource (l, sources[(size_t) l]); impl->sampleStates[(size_t) l] = sampleStates[(size_t) l];
        impl->sampleNames[(size_t) l] = sampleStates[(size_t) l].isValid() ? sampleStates[(size_t) l]["name"].toString() : "Factory harmonic source";
    }
    restore (state.getChildWithName ("RACK")); sceneIndex = juce::jlimit (0, 2, (int) state["scene"]); abIndex = juce::jlimit (0, 1, (int) state["ab"]);
    currentProgram.store (juce::jlimit (0, 3, (int) state["program"]));
    undoStates.clear(); redoStates.clear();
    for (int i = 0; i < 3; ++i) { const auto s = state.getChildWithName ("SCENES").getChild (i); scenes[(size_t) i] = s.isValid() ? s.createCopy() : snapshot(); }
    for (int i = 0; i < 2; ++i) { const auto s = state.getChildWithName ("COMPARISONS").getChild (i); comparisons[(size_t) i] = s.isValid() ? s.createCopy() : snapshot(); }
    ++uiRevision; return juce::Result::ok();
}
juce::Result SattariModularProcessor::savePreset (const juce::File& file)
{
    juce::MemoryBlock bytes; getStateInformation (bytes);
    juce::TemporaryFile temporary (file, juce::TemporaryFile::useHiddenFile);
    {
        juce::FileOutputStream output (temporary.getFile());
        if (! output.openedOk() || ! output.write (bytes.getData(), bytes.getSize())) return juce::Result::fail ("Could not write preset. The previous file was left intact.");
        output.flush();
        if (output.getStatus().failed()) return juce::Result::fail ("Could not flush preset. The previous file was left intact.");
    }
    if (! temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail ("Could not replace preset. Check folder permissions.");
    return juce::Result::ok();
}
juce::Result SattariModularProcessor::loadPreset (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (file.getSize() > 32 * 1024 * 1024 || ! file.loadFileAsData (bytes)) return juce::Result::fail ("Could not read preset.");
    // Disk loads are explicit session changes; invalid files leave the rack intact.
    return restoreState (bytes.getData(), (int) bytes.getSize());
}
void SattariModularProcessor::allNotesOff() { impl->panic.store (true); }
void SattariModularProcessor::queueNote (int note, float velocity, bool on)
{
    int a, as, b, bs; impl->noteQueue.prepareToWrite (1, a, as, b, bs);
    if (as > 0) { impl->notes[(size_t) a] = { juce::jlimit (0, 127, note), juce::jlimit (0.0f, 1.0f, velocity), on }; impl->noteQueue.finishedWrite (1); }
    else impl->panic.store (true);
}
juce::AudioProcessorEditor* SattariModularProcessor::createEditor() { return new SattariModularEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariModularProcessor(); }
