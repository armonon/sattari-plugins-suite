#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// =============================================================================
//  SattariModule — one unit of DSP that can live in two shells.
// -----------------------------------------------------------------------------
//  A module is the DSP of a plugin with the plugin-ness removed: it declares its
//  parameters, prepares, resets, and processes. Nothing else.
//
//  That lets the SAME implementation back both shells we ship:
//    * the standalone plugin (thin wrapper, prefix "")  — so saved sessions keep
//      working, because the parameter IDs are unchanged;
//    * a rack host slot (prefix "<id>."), where several modules share one plugin.
//
//  There is exactly ONE copy of each DSP. That is the whole point: a fix or a
//  tuning change lands in both shells at once and they can never drift apart.
//
//  reset() is PURE VIRTUAL on purpose. A suite-wide scan found six shipping
//  effects with delay/reverb tails and no reset override, so a transport jump
//  replayed the previous position's tail. Making it un-skippable here means no
//  module can ever reintroduce that bug.
//
//  Realtime contract: prepare() may allocate; process() and reset() must not.
//  A host prepares EVERY module it owns up front (not just the ones in slots),
//  because the user can rewire the rack at any moment and the audio thread can
//  never be the thing that allocates.
// =============================================================================

namespace sattari::dsp
{

// =============================================================================
//  Modulation
// -----------------------------------------------------------------------------
//  A rack slot with eight processors and no cross-module modulation answers the
//  question "why is this better than eight separate plugins?" with "presets and
//  one window", which is not much of an answer. Modulation is the thing separate
//  plugins structurally cannot do: one sequencer, clocked from the session,
//  moving parameters across every slot in time with the song.
//
//  It is applied HERE, at cachedParameterValue(), because that is the single
//  point every module already reads its parameters through — so all 28 modules
//  gain modulation without one line changing in any of them.
//
//  Offsets are NORMALISED (-1..+1) and applied in 0..1 space through the
//  parameter's own range before being converted back. That matters: a raw offset
//  is meaningless across parameters measured in Hz, dB, seconds and ratios, and
//  a "50% up" sweep should mean the same gesture on a filter as on a mix knob.
//
//  The values are owned by the rack and rewritten once per block on the audio
//  thread before any module runs. Nothing here allocates or locks.
struct ModulationBus
{
    const float* offsets = nullptr;   // normalised, -1..+1, indexed by parameter
    int          count   = 0;

    float at (int index) const noexcept
    {
        return (offsets != nullptr && index >= 0 && index < count) ? offsets[index] : 0.0f;
    }
};


class SattariModule
{
    // Declared before anything that returns it: a member struct defined further
    // down is not visible to an earlier member function's return type, and the
    // compiler's "did you mean juce::URL::ParameterHandling?" is not a hint that
    // points anywhere near the real problem.
    struct ParameterBinding
    {
        std::string id;
        std::atomic<float>* value = nullptr;
        // The parameter's own range, so a normalised offset maps to the right
        // physical amount, and a stable index so a rack can address it.
        const juce::NormalisableRange<float>* range = nullptr;
        int modIndex = -1;
    };

public:
    using ParamList = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

    virtual ~SattariModule() = default;

    // Stable slug, e.g. "transient" — also the parameter prefix inside a host.
    virtual const char* moduleId() const noexcept = 0;
    // Human label for rack slots / tabs, e.g. "Transient".
    virtual const char* displayName() const noexcept = 0;

    // Append this module's parameters. `prefix` is "" in the standalone shell
    // (preserving the historical IDs) and "<id>." inside a host.
    virtual void addParameters (ParamList& out, const juce::String& prefix) = 0;

    // Resolve raw parameter pointers once, off the audio thread. Called after the
    // APVTS exists and before prepare(). The non-virtual entry point always builds
    // the shared allocation-free lookup first, so a module cannot accidentally
    // perform APVTS/String lookup from process().
    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix)
    {
        parameterCache.clear();

        for (auto* parameter : apvts.processor.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            if (withId == nullptr || ! withId->paramID.startsWith (prefix))
                continue;

            if (auto* value = apvts.getRawParameterValue (withId->paramID))
            {
                const juce::NormalisableRange<float>* range = nullptr;
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                    range = &ranged->getNormalisableRange();
                parameterCache.push_back (
                    { withId->paramID.substring (prefix.length()).toStdString(),
                      value, range, -1 });
            }
        }

        std::sort (parameterCache.begin(), parameterCache.end(),
                   [] (const ParameterBinding& a, const ParameterBinding& b)
                   {
                       return a.id < b.id;
                   });

        // Modulation index is assigned AFTER the sort, so it is stable for a given
        // module build regardless of the order the host reported parameters in.
        for (std::size_t i = 0; i < parameterCache.size(); ++i)
            parameterCache[i].modIndex = (int) i;

        linkModule (apvts, prefix);
    }

    // May allocate.
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;

    // Flush all streaming state. Must be allocation-free — it is called on
    // transport relocate, live.
    virtual void reset() = 0;

    // Must be allocation-free and lock-free.
    virtual void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) = 0;

    // Reported so a host can sum its active slots into setLatencySamples().
    virtual int latencySamples() const noexcept { return 0; }

    // Host tempo/transport for the coming block. A module cannot reach the playhead
    // itself — getPlayHead() belongs to AudioProcessor, and a module deliberately has
    // no AudioProcessor — so whichever shell owns it (standalone plugin or rack host)
    // reads the transport once per block and pushes it down here BEFORE process().
    //
    // Fifteen plugins are tempo-synced (Comp's tempo-release, Echo, Pulse, Space,
    // Riser, Warble, Arp, Rhythm Genie ...), so without this none of them could be
    // extracted. Declared as a defaulted no-op rather than a change to process()'s
    // signature so modules that don't care — and the five already extracted — need no
    // edit at all.
    //
    // Called on the audio thread: implementations must be trivial (store the value).
    virtual void setTransport (const sattari::host::TransportSnapshot&) noexcept {}

    // External sidechain for the coming block, or nullptr when the host/rack has none
    // routed. Same reasoning as setTransport(): a module cannot reach getBusBuffer()
    // because it has no AudioProcessor, so the shell resolves the bus and hands down a
    // read-only view before process().
    //
    // This is what lets a channel strip do the things a strip is expected to do —
    // compress a bass to a kick, gate a snare to a trigger, duck a pad under a vocal —
    // and it is the only route by which inherently two-signal tools (Align's Guide
    // track, Side Chain Master's external key) can work as rack modules at all.
    //
    // Defaulted to a no-op so modules that don't key off anything, and every module
    // already extracted, need no edit. The pointer is valid ONLY for the duration of
    // the following process() call — never retain it.
    virtual void setSidechain (const juce::AudioBuffer<float>*) noexcept {}

    // --- Modulation -------------------------------------------------------
    //  Set by the host rack, once, after link(). The rack rewrites the values
    //  behind this pointer each block; the module never owns them.
    void setModulationBus (const ModulationBus& bus) noexcept { modulation = bus; }
    void clearModulation() noexcept { modulation = {}; }

    // How many modulatable parameters this module exposes, and their ids in
    // modulation-index order — what a rack needs to build a target menu.
    int  modulationTargetCount() const noexcept { return (int) parameterCache.size(); }
    const char* modulationTargetId (int index) const noexcept
    {
        return juce::isPositiveAndBelow (index, (int) parameterCache.size())
             ? parameterCache[(std::size_t) index].id.c_str() : nullptr;
    }

protected:
    // Module-specific link hook. Implementations can cache frequently-read raw
    // pointers directly; cachedParameterValue() covers variable/generated ids
    // without constructing a juce::String on the audio thread.
    virtual void linkModule (juce::AudioProcessorValueTreeState& apvts,
                             const juce::String& prefix) = 0;

    const ParameterBinding* cachedBinding (const char* id) const noexcept
    {
        const auto it = std::lower_bound (
            parameterCache.begin(), parameterCache.end(), id,
            [] (const ParameterBinding& binding, const char* wanted)
            {
                return binding.id.compare (wanted) < 0;
            });
        if (it != parameterCache.end() && std::strcmp (it->id.c_str(), id) == 0)
            return &(*it);
        jassertfalse;
        return nullptr;
    }

    std::atomic<float>* cachedParameterPointer (const char* id) const noexcept
    {
        const auto it = std::lower_bound (
            parameterCache.begin(), parameterCache.end(), id,
            [] (const ParameterBinding& binding, const char* wanted)
            {
                return binding.id.compare (wanted) < 0;
            });

        if (it != parameterCache.end() && std::strcmp (it->id.c_str(), id) == 0)
            return it->value;

        jassertfalse;
        return nullptr;
    }

    float cachedParameterValue (const char* id, float fallback = 0.0f) const noexcept
    {
        const auto* binding = cachedBinding (id);
        if (binding == nullptr || binding->value == nullptr)
            return fallback;

        const float raw = binding->value->load();
        const float offset = modulation.at (binding->modIndex);
        if (offset == 0.0f || binding->range == nullptr)
            return raw;

        // Apply in normalised space through the parameter's own range: the same
        // gesture then means the same proportion on a Hz control and a ratio.
        const float moved = binding->range->convertTo0to1 (raw) + offset;
        return binding->range->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, moved));
    }

    // The unmodulated value, for anything that must read the user's setting
    // itself — a display, or a parameter whose modulation would be nonsense.
    float rawParameterValue (const char* id, float fallback = 0.0f) const noexcept
    {
        if (auto* value = cachedParameterPointer (id))
            return value->load();
        return fallback;
    }

private:

    std::vector<ParameterBinding> parameterCache;
    ModulationBus modulation;
};

} // namespace sattari::dsp
