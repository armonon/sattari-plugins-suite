#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <memory>
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

class SattariModule
{
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
    // APVTS exists and before prepare().
    virtual void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) = 0;

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
};

} // namespace sattari::dsp
