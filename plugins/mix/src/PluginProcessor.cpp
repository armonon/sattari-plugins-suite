#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <sattari_audio_core/modules/EqModule.h>

SattariMixAudioProcessor::SattariMixAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MIX", createLayout())
{
    // Link every module to its namespaced parameters once, off the audio thread.
    for (auto& m : modules)
        m->link (apvts, juce::String (m->moduleId()) + ".");
}

// The rack's module roster. Adding a module here is the ONLY step needed to make it
// available in every slot — its parameters, preparation and reset all follow.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::EqModule>());
    v.push_back (std::make_unique<sattari::dsp::TransientModule>());
    return v;
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariMixAudioProcessor::createLayout()
{
    // Build the roster first: the layout is derived from it, and the same roster is
    // then kept as the live modules.
    modules = makeModules();

    sattari::dsp::SattariModule::ParamList p;

    // Slot assignment + bypass. Slot value 0 = Empty, N = modules[N - 1].
    juce::StringArray menu { "Empty" };
    for (auto& m : modules) menu.add (m->displayName());
    for (int s = 0; s < kNumSlots; ++s)
    {
        p.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { slotParamId (s), 1 }, "Slot " + juce::String (s + 1), menu, 0));
        p.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { slotBypassParamId (s), 1 }, "Slot " + juce::String (s + 1) + " Bypass", false));
    }

    // Every module's parameters, always present and namespaced. A plugin's parameter
    // set has to be fixed at construction, so the rack changes routing, never the set.
    for (auto& m : modules)
        m->addParameters (p, juce::String (m->moduleId()) + ".");

    return { p.begin(), p.end() };
}

juce::StringArray SattariMixAudioProcessor::moduleMenuNames() const
{
    juce::StringArray menu { "Empty" };
    for (const auto& m : modules) menu.add (m->displayName());
    return menu;
}

sattari::dsp::SattariModule* SattariMixAudioProcessor::moduleAt (int index) noexcept
{
    return juce::isPositiveAndBelow (index, (int) modules.size()) ? modules[(size_t) index].get() : nullptr;
}

bool SattariMixAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto o = layouts.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void SattariMixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    const int n  = juce::jmax (1, samplesPerBlock);
    const int ch = juce::jmax (1, getTotalNumInputChannels());

    // Prepare EVERY module, not just the ones in slots — the rack can be rewired at
    // any moment and the audio thread must never allocate.
    for (auto& m : modules)
        m->prepare (sampleRate, n, ch);

    reportedLatency_ = computeLatency();
    pendingLatency_.store (reportedLatency_, std::memory_order_relaxed);
    setLatencySamples (reportedLatency_);
}

void SattariMixAudioProcessor::reset()
{
    for (auto& m : modules)
        m->reset();
}

int SattariMixAudioProcessor::computeLatency() const
{
    int total = 0;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const int sel = (int) apvts.getRawParameterValue (slotParamId (s))->load();
        const bool byp = apvts.getRawParameterValue (slotBypassParamId (s))->load() > 0.5f;
        if (sel <= 0 || byp) continue;
        if (auto* m = const_cast<SattariMixAudioProcessor*> (this)->moduleAt (sel - 1))
            total += m->latencySamples();
    }
    return total;
}

void SattariMixAudioProcessor::handleAsyncUpdate()
{
    const int want = pendingLatency_.load (std::memory_order_relaxed);
    if (want != reportedLatency_)
    {
        reportedLatency_ = want;
        setLatencySamples (want);
    }
}

void SattariMixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int s = 0; s < kNumSlots; ++s)
    {
        const int sel = (int) apvts.getRawParameterValue (slotParamId (s))->load();
        if (sel <= 0) continue;
        if (apvts.getRawParameterValue (slotBypassParamId (s))->load() > 0.5f) continue;
        if (auto* m = moduleAt (sel - 1))
            m->process (buffer, midi);
    }

    // Rack rewired? Flag it and let the message thread publish — telling the host about
    // a latency change is not realtime-safe, so it never happens here.
    const int lat = computeLatency();
    if (lat != pendingLatency_.load (std::memory_order_relaxed))
    {
        pendingLatency_.store (lat, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void SattariMixAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SattariMixAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMixAudioProcessor::createEditor()
{
    return new SattariMixAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMixAudioProcessor(); }
