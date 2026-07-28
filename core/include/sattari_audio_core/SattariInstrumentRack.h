#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPresetManager.h>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// =============================================================================
//  SattariInstrumentRack — the shared INSTRUMENT host.
// -----------------------------------------------------------------------------
//  This is deliberately NOT SattariRackProcessor with a different roster, and the
//  difference is the whole reason it exists:
//
//      an effect rack CHAINS  — slot N processes what slot N-1 produced;
//      an instrument rack LAYERS — every slot renders the SAME MIDI, in parallel,
//                                  into its own buffer, and the results are summed.
//
//  Chaining instruments is meaningless: each one writes its output over the top of
//  the previous, so all but the last would be inaudible. Layering is what a player
//  actually wants — a sub under a lead, a kit plus a bass, three synths detuned
//  against each other.
//
//  Three things follow from layering, and none of them are optional:
//
//  1. PER-LAYER LEVEL AND PAN. Four instruments summed at unity clips instantly.
//     Level defaults to -6 dB per layer so a fresh stack has headroom.
//
//  2. PER-LAYER KEY RANGE AND TRANSPOSE. Without these, "stacking" only ever means
//     unison. With them the same rack does keyboard SPLITS (bass below C3, lead
//     above) and OCTAVE LAYERS — which is most of what stacking is for. MIDI is
//     filtered per layer before the instrument ever sees it.
//
//  3. LATENCY IS THE MAX, NOT THE SUM. Layers run in parallel, so the host must be
//     told the worst one. But a layer that reports LESS latency than the worst
//     arrives EARLY relative to it, which is an audible flam on transients — so
//     each layer is delayed by (max - its own) to bring the stack back into phase.
//     A chain host never has this problem; a layering host always does.
// =============================================================================

namespace sattari::rack
{

class SattariInstrumentRack : public juce::AudioProcessor,
                              private juce::AsyncUpdater
{
public:
    static constexpr int kNumLayers = 4;

    using ModuleFactory = std::function<std::vector<std::unique_ptr<dsp::SattariModule>>()>;

    SattariInstrumentRack (juce::String displayName,
                           juce::String stateTreeId,
                           const BusesProperties& buses,
                           ModuleFactory factory,
                           juce::String presetProductId = {})
        : juce::AudioProcessor (buses),
          name_ (std::move (displayName)),
          factory_ (std::move (factory)),
          apvts (*this, nullptr, juce::Identifier (stateTreeId), createLayout()),
          presetBank (apvts, presetProductId.isNotEmpty() ? presetProductId : stateTreeId.toLowerCase())
    {
        for (auto& m : modules)
            m->link (apvts, juce::String (m->moduleId()) + ".");
    }

    ~SattariInstrumentRack() override = default;

    // --- AudioProcessor ---------------------------------------------------
    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return true; }    // it is an instrument
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets()) { currentProgram_ = i; presetBank.loadFactoryPreset (i); }
    }
    const juce::String getProgramName (int i) override
    {
        const auto names = presetBank.factoryPresetNames();
        return juce::isPositiveAndBelow (i, names.size()) ? names[i] : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto o = layouts.getMainOutputChannelSet();
        return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        sr_ = sampleRate;
        const int n = juce::jmax (1, samplesPerBlock);

        // Every module prepared, not just the ones in layers — rewiring must never
        // allocate on the audio thread.
        for (auto& m : modules)
            m->prepare (sampleRate, n, 2);

        layerBuf_.setSize (2, n);

        // Alignment delay lines, sized for the worst latency any module reports.
        int worst = 0;
        for (auto& m : modules) worst = juce::jmax (worst, m->latencySamples());
        alignCap_ = worst + 1;
        for (auto& d : align_)
        {
            d.setSize (2, juce::jmax (1, alignCap_));
            d.clear();
        }
        alignPos_.fill (0);

        reportedLatency_ = computeLatency();
        pendingLatency_.store (reportedLatency_, std::memory_order_relaxed);
        setLatencySamples (reportedLatency_);
    }

    void reset() override
    {
        for (auto& m : modules) m->reset();
        for (auto& d : align_) d.clear();
        alignPos_.fill (0);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n  = buffer.getNumSamples();
        const int ch = juce::jmin (2, buffer.getNumChannels());
        if (n <= 0 || ch <= 0) return;

        const auto transport = sattari::host::read (*this);
        const int  maxLat    = computeLatency();

        // An instrument host OWNS its output: nothing upstream feeds it, so the
        // buffer starts silent and every layer ADDS into it.
        buffer.clear();

        for (int L = 0; L < kNumLayers; ++L)
        {
            const int sel = (int) apvts.getRawParameterValue (layerParamId (L))->load();
            if (sel <= 0) continue;
            if (apvts.getRawParameterValue (layerMuteParamId (L))->load() > 0.5f) continue;

            auto* m = moduleAt (sel - 1);
            if (m == nullptr) continue;

            // Filter MIDI for this layer: key range + transpose. This is what makes
            // a stack a split rather than a unison.
            const int lo    = (int) apvts.getRawParameterValue (layerLoParamId (L))->load();
            const int hi    = (int) apvts.getRawParameterValue (layerHiParamId (L))->load();
            const int trans = (int) apvts.getRawParameterValue (layerTransParamId (L))->load();
            filterMidiForLayer (midi, layerMidi_, lo, hi, trans);

            layerBuf_.clear();
            juce::AudioBuffer<float> view (layerBuf_.getArrayOfWritePointers(), ch, n);

            m->setTransport (transport);
            m->process (view, layerMidi_);

            // Delay-align this layer to the worst-case latency so a stack of
            // instruments with different latencies does not flam.
            const int lag = juce::jlimit (0, juce::jmax (0, alignCap_ - 1), maxLat - m->latencySamples());
            if (lag > 0)
                applyAlignmentDelay (view, align_[(size_t) L], alignPos_[(size_t) L], lag, ch, n);

            const float gain = juce::Decibels::decibelsToGain (
                                   apvts.getRawParameterValue (layerLevelParamId (L))->load());
            const float pan  = apvts.getRawParameterValue (layerPanParamId (L))->load();

            // Equal-power pan, so moving a layer across the image holds its loudness.
            const float t  = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
            const float gl = std::cos (t * juce::MathConstants<float>::halfPi) * gain;
            const float gr = std::sin (t * juce::MathConstants<float>::halfPi) * gain;

            if (ch >= 2)
            {
                buffer.addFrom (0, 0, view, 0, 0, n, gl * juce::MathConstants<float>::sqrt2);
                buffer.addFrom (1, 0, view, 1, 0, n, gr * juce::MathConstants<float>::sqrt2);
            }
            else
            {
                buffer.addFrom (0, 0, view, 0, 0, n, gain);
            }
        }

        // The rack consumed MIDI to drive its instruments; nothing goes downstream.
        midi.clear();

        if (maxLat != pendingLatency_.load (std::memory_order_relaxed))
        {
            pendingLatency_.store (maxLat, std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    // --- Introspection for the editor --------------------------------------
    juce::AudioProcessorValueTreeState& parameters() { return apvts; }
    int numModules() const noexcept { return (int) modules.size(); }

    juce::StringArray moduleMenuNames() const
    {
        juce::StringArray menu { "Empty" };
        for (const auto& m : modules) menu.add (m->displayName());
        return menu;
    }

    dsp::SattariModule* moduleAt (int index) noexcept
    {
        return juce::isPositiveAndBelow (index, (int) modules.size())
                 ? modules[(size_t) index].get() : nullptr;
    }

    static juce::String layerParamId      (int L) { return "layer" + juce::String (L + 1); }
    static juce::String layerMuteParamId  (int L) { return "layer" + juce::String (L + 1) + "mute"; }
    static juce::String layerLevelParamId (int L) { return "layer" + juce::String (L + 1) + "level"; }
    static juce::String layerPanParamId   (int L) { return "layer" + juce::String (L + 1) + "pan"; }
    static juce::String layerLoParamId    (int L) { return "layer" + juce::String (L + 1) + "lo"; }
    static juce::String layerHiParamId    (int L) { return "layer" + juce::String (L + 1) + "hi"; }
    static juce::String layerTransParamId (int L) { return "layer" + juce::String (L + 1) + "transpose"; }

protected:
    // A stack preset is a set of layers, each an instrument plus its placement.
    struct LayerSpec
    {
        juce::String moduleId;
        float levelDb = -6.0f;
        float pan     = 0.0f;
        int   lo      = 0;
        int   hi      = 127;
        int   transpose = 0;
    };

    void addStackPreset (const juce::String& name,
                         const std::vector<LayerSpec>& layers,
                         sattari::presets::SattariPresetManager::PresetValues params = {})
    {
        sattari::presets::SattariPresetManager::PresetValues values;

        // Every layer is written, so a preset fully defines the stack and nothing
        // survives from whatever was loaded before.
        for (int L = 0; L < kNumLayers; ++L)
        {
            float sel = 0.0f;
            LayerSpec spec;
            if (L < (int) layers.size())
            {
                spec = layers[(size_t) L];
                const int idx = indexOfModule (spec.moduleId);
                jassert (idx >= 0);   // unknown module id in a factory preset
                if (idx >= 0) sel = (float) (idx + 1);
            }
            values.push_back ({ layerParamId (L),      sel });
            values.push_back ({ layerMuteParamId (L),  0.0f });
            values.push_back ({ layerLevelParamId (L), spec.levelDb });
            values.push_back ({ layerPanParamId (L),   spec.pan });
            values.push_back ({ layerLoParamId (L),    (float) spec.lo });
            values.push_back ({ layerHiParamId (L),    (float) spec.hi });
            values.push_back ({ layerTransParamId (L), (float) spec.transpose });
        }

        for (auto& kv : params) values.push_back (kv);
        presetBank.addFactoryPreset (name, std::move (values));
    }

    int indexOfModule (const juce::String& id) const noexcept
    {
        for (int i = 0; i < (int) modules.size(); ++i)
            if (id == modules[(size_t) i]->moduleId())
                return i;
        return -1;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        modules = factory_();

        dsp::SattariModule::ParamList p;

        juce::StringArray menu { "Empty" };
        for (auto& m : modules) menu.add (m->displayName());

        // A one-entry AudioParameterChoice is a DEGENERATE range and the plugin
        // fails to instantiate outright — auval cannot even open it. That is
        // reachable whenever a roster is empty, so the menu always carries a second
        // entry. Cheap guard; the alternative is a host that silently will not load.
        if (menu.size() < 2) menu.add ("(none installed)");

        for (int L = 0; L < kNumLayers; ++L)
        {
            const juce::String n = juce::String (L + 1);
            p.push_back (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { layerParamId (L), 1 }, "Layer " + n, menu, 0));
            p.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { layerMuteParamId (L), 1 }, "Layer " + n + " Mute", false));
            // -6 dB default: four layers at unity would clip on the first chord.
            p.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { layerLevelParamId (L), 1 }, "Layer " + n + " Level",
                juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), -6.0f));
            p.push_back (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { layerPanParamId (L), 1 }, "Layer " + n + " Pan",
                juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
            p.push_back (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { layerLoParamId (L), 1 }, "Layer " + n + " Low Key", 0, 127, 0));
            p.push_back (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { layerHiParamId (L), 1 }, "Layer " + n + " High Key", 0, 127, 127));
            p.push_back (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { layerTransParamId (L), 1 }, "Layer " + n + " Transpose", -24, 24, 0));
        }

        for (auto& m : modules)
            m->addParameters (p, juce::String (m->moduleId()) + ".");

        return { p.begin(), p.end() };
    }

    // Key range + transpose, applied before the instrument sees anything. Notes
    // outside the range are dropped entirely (not just silenced) so a split layer
    // never burns a voice on a note it will not play.
    static void filterMidiForLayer (const juce::MidiBuffer& in, juce::MidiBuffer& out,
                                    int lo, int hi, int transpose)
    {
        out.clear();
        if (lo > hi) std::swap (lo, hi);

        for (const auto meta : in)
        {
            auto m = meta.getMessage();
            if (m.isNoteOnOrOff())
            {
                const int note = m.getNoteNumber();
                if (note < lo || note > hi) continue;
                const int shifted = note + transpose;
                if (shifted < 0 || shifted > 127) continue;
                m.setNoteNumber (shifted);
            }
            out.addEvent (m, meta.samplePosition);
        }
    }

    // The write cursor MUST persist across blocks — a per-block cursor would reset
    // the ring every buffer and the "delay" would degenerate into noise at the
    // block rate.
    static void applyAlignmentDelay (juce::AudioBuffer<float>& buf,
                                     juce::AudioBuffer<float>& ring,
                                     int& writePos, int lag, int ch, int n)
    {
        const int cap = ring.getNumSamples();
        if (cap <= 0 || lag <= 0 || lag >= cap) return;

        const int chans = juce::jmin (ch, ring.getNumChannels());
        int w = writePos;
        for (int i = 0; i < n; ++i)
        {
            const int r = (w + cap - lag) % cap;
            for (int c = 0; c < chans; ++c)
            {
                float* x  = buf.getWritePointer (c);
                float* rr = ring.getWritePointer (c);
                const float delayed = rr[r];
                rr[w] = x[i];
                x[i]  = delayed;
            }
            if (++w >= cap) w = 0;
        }
        writePos = w;
    }

    int computeLatency() const
    {
        int worst = 0;
        for (int L = 0; L < kNumLayers; ++L)
        {
            const int sel = (int) apvts.getRawParameterValue (layerParamId (L))->load();
            if (sel <= 0) continue;
            if (apvts.getRawParameterValue (layerMuteParamId (L))->load() > 0.5f) continue;
            if (auto* m = const_cast<SattariInstrumentRack*> (this)->moduleAt (sel - 1))
                worst = juce::jmax (worst, m->latencySamples());   // MAX, not sum
        }
        return worst;
    }

    void handleAsyncUpdate() override
    {
        const int want = pendingLatency_.load (std::memory_order_relaxed);
        if (want != reportedLatency_)
        {
            reportedLatency_ = want;
            setLatencySamples (want);
        }
    }

    juce::String name_;
    ModuleFactory factory_;

    // ALL THREE ABOVE MUST PRECEDE apvts. Members initialise in DECLARATION order,
    // and apvts's constructor runs createLayout(), which calls factory_() and fills
    // `modules`. Declaring apvts first means createLayout() reads a factory that has
    // not been constructed yet — the plugin then fails to instantiate at all
    // (auval: "OpenAComponent: result: -1"), with nothing pointing at the cause.
    std::vector<std::unique_ptr<dsp::SattariModule>> modules;

protected:
    juce::AudioProcessorValueTreeState apvts;
    sattari::presets::SattariPresetManager presetBank;   // declared AFTER apvts

private:
    int currentProgram_ = 0;
    double sr_ = 44100.0;

    juce::AudioBuffer<float> layerBuf_;                       // one layer's render
    juce::MidiBuffer         layerMidi_;                      // that layer's filtered MIDI
    std::array<juce::AudioBuffer<float>, kNumLayers> align_;  // per-layer alignment rings
    int alignCap_ = 1;
    std::array<int, kNumLayers> alignPos_ {};   // persistent per-layer ring cursors

    std::atomic<int> pendingLatency_ { 0 };
    int reportedLatency_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariInstrumentRack)
};

} // namespace sattari::rack
