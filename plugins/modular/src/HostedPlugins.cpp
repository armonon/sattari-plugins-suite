#include "HostedPlugins.h"
#include <cmath>

namespace sattari::modular
{
HostedPlugins::HostedPlugins() { formats.addFormat (std::make_unique<juce::VST3PluginFormat>()); }
HostedPlugins::~HostedPlugins() { cancelPendingUpdate(); release(); }

bool HostedPlugins::permitted (const juce::PluginDescription& d, bool instrument)
{
    return d.pluginFormatName == "VST3" && d.isInstrument == instrument
        && ! d.name.containsIgnoreCase ("Sattari Modular")
        && ! juce::File (d.fileOrIdentifier).getFileName().containsIgnoreCase ("Sattari Modular");
}

juce::Array<juce::PluginDescription> HostedPlugins::scan (const juce::File& file, bool instrument, juce::String& error)
{
    juce::Array<juce::PluginDescription> result;
    if (! file.exists() || ! file.hasFileExtension ("vst3")) { error = "Choose an installed VST3 plugin."; return result; }
    if (file.getFileName().containsIgnoreCase ("Sattari Modular")) { error = "Modular cannot host another Modular instance."; return result; }
    juce::OwnedArray<juce::PluginDescription> found;
    formats.getFormat (0)->findAllTypesForFile (found, file.getFullPathName());
    for (const auto* d : found) if (permitted (*d, instrument)) result.add (*d);
    if (result.isEmpty()) error = instrument ? "No compatible VST3 instrument found in this file." : "No compatible VST3 effect found in this file.";
    return result;
}

juce::Result HostedPlugins::load (int node, const juce::PluginDescription& d)
{
    if (node < 0 || node >= slotCount || ! permitted (d, node >= firstInstrument))
        return juce::Result::fail ("Plugin type does not match this slot, or recursive hosting was rejected.");
    juce::ValueTree state ("PLUGIN"); state.setProperty ("node", node, nullptr);
    state.setProperty ("description", d.createXml()->toString(), nullptr);
    const juce::ScopedLock lock (mutex);
    auto& s = slots[(size_t) node];
    if (s.saved.isValid())
    {
        juce::PluginDescription previous;
        if (const auto xml = juce::parseXML (s.saved["description"].toString()); xml && previous.loadFromXml (*xml)
            && previous.uniqueId == d.uniqueId && previous.manufacturerName == d.manufacturerName && previous.name == d.name)
        {
            auto captured = captureSlot (node);
            if (captured.hasProperty ("data")) state.setProperty ("data", captured["data"], nullptr);
        }
    }
    s.saved = state; s.pending = true; s.error.clear(); ++s.revision;
    triggerAsyncUpdate(); return juce::Result::ok();
}

void HostedPlugins::clear (int node)
{
    const juce::ScopedLock lock (mutex); auto& s = slots[(size_t) node];
    if (! s.saved.isValid() && ! s.plugin) return;
    s.saved = {}; s.pending = true; s.error.clear(); ++s.revision; triggerAsyncUpdate();
}

juce::ValueTree HostedPlugins::captureSlot (int node)
{
    auto& s = slots[(size_t) node];
    if (! s.saved.isValid()) return {};
    auto result = s.saved.createCopy(); result.setProperty ("node", node, nullptr);
    if (s.plugin && ! s.pending)
    {
        juce::MemoryBlock bytes; s.plugin->getStateInformation (bytes);
        result.setProperty ("data", juce::var (bytes), nullptr);
    }
    return result;
}

void HostedPlugins::copy (int from, int to)
{
    const juce::ScopedLock lock (mutex);
    auto saved = captureSlot (from); if (saved.isValid()) saved.setProperty ("node", to, nullptr);
    auto& s = slots[(size_t) to]; s.saved = saved; s.pending = true; s.error.clear(); ++s.revision; triggerAsyncUpdate();
}
void HostedPlugins::swap (int a, int b)
{
    const juce::ScopedLock lock (mutex); std::swap (slots[(size_t) a], slots[(size_t) b]);
    for (int n : { a, b }) { auto& s = slots[(size_t) n]; if (s.saved.isValid()) s.saved.setProperty ("node", n, nullptr); ++s.revision; }
}
juce::ValueTree HostedPlugins::capture()
{
    const juce::ScopedLock lock (mutex); juce::ValueTree tree ("HOSTED");
    for (int n = 0; n < slotCount; ++n) if (auto s = captureSlot (n); s.isValid()) tree.addChild (s, -1, nullptr);
    return tree;
}

bool HostedPlugins::validState (const juce::ValueTree& tree)
{
    if (! tree.isValid()) return true;
    if (! tree.hasType ("HOSTED") || tree.getNumChildren() > slotCount || tree.getNumProperties() != 0) return false;
    std::array<bool, slotCount> seen {};
    for (auto child : tree)
    {
        const int n = child["node"];
        if (! child.hasType ("PLUGIN") || ! child["node"].isInt() || n < 0 || n >= slotCount
            || seen[(size_t) n] || child.getNumChildren() != 0 || ! child["description"].isString()
            || child["description"].toString().length() > 4096) return false;
        seen[(size_t) n] = true;
        juce::PluginDescription d; const auto xml = juce::parseXML (child["description"].toString());
        if (! xml || ! d.loadFromXml (*xml) || ! permitted (d, n >= firstInstrument)) return false;
        if (child.hasProperty ("data"))
        {
            const auto* bytes = child["data"].getBinaryData();
            if (! bytes || bytes->getSize() > maximumPluginBytes) return false;
        }
    }
    return true;
}

void HostedPlugins::restore (const juce::ValueTree& tree)
{
    const juce::ScopedLock lock (mutex);
    for (int n = 0; n < slotCount; ++n)
    {
        auto& s = slots[(size_t) n]; auto saved = tree.getChildWithProperty ("node", n);
        if (! saved.isValid() && ! s.saved.isValid() && ! s.plugin) continue;
        s.saved = saved.createCopy(); s.pending = true; s.error.clear(); ++s.revision;
    }
    triggerAsyncUpdate();
}
void HostedPlugins::flush() { handleUpdateNowIfNeeded(); }

juce::Result HostedPlugins::configure (juce::AudioPluginInstance& plugin)
{
    bool supported = false;
    const int other = channels == 1 ? 2 : 1;
    for (const auto counts : { std::pair<int, int> { channels, channels }, { other, other }, { 1, 2 }, { 2, 1 } })
    {
        auto layout = plugin.getBusesLayout();
        for (auto& bus : layout.inputBuses) bus = juce::AudioChannelSet::disabled();
        for (auto& bus : layout.outputBuses) bus = juce::AudioChannelSet::disabled();
        if (! layout.inputBuses.isEmpty()) layout.inputBuses.getReference (0) = juce::AudioChannelSet::canonicalChannelSet (counts.first);
        if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference (0) = juce::AudioChannelSet::canonicalChannelSet (counts.second);
        if (plugin.setBusesLayout (layout)) { supported = true; break; }
    }
    if (! supported || plugin.getTotalNumOutputChannels() < 1 || plugin.getTotalNumOutputChannels() > 2
        || plugin.getTotalNumInputChannels() > 2) return juce::Result::fail ("Only mono/stereo main-bus plugins are supported; auxiliary buses must be disabled.");
    plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin.setProcessingPrecision (juce::AudioProcessor::singlePrecision);
    plugin.prepareToPlay (sampleRate, blockSize);
    if (plugin.getLatencySamples() < 0 || plugin.getLatencySamples() > maximumLatency)
    { plugin.releaseResources(); return juce::Result::fail ("Plugin latency exceeds the 16,384-sample hosting limit."); }
    return juce::Result::ok();
}

void HostedPlugins::handleAsyncUpdate()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    for (int n = 0; n < slotCount; ++n)
    {
        juce::ValueTree saved; unsigned revision;
        double sr; int block;
        {
            const juce::ScopedLock lock (mutex); const auto& s = slots[(size_t) n];
            if (! s.pending) continue;
            saved = s.saved.createCopy(); revision = s.revision; sr = sampleRate; block = blockSize;
        }
        std::shared_ptr<juce::AudioPluginInstance> plugin;
        juce::String failure;
        if (saved.isValid())
        {
            juce::PluginDescription d; const auto xml = juce::parseXML (saved["description"].toString());
            if (xml && d.loadFromXml (*xml) && permitted (d, n >= firstInstrument))
            {
                plugin = formats.createPluginInstance (d, sr, block, failure);
                if (plugin) if (const auto* bytes = saved["data"].getBinaryData())
                    plugin->setStateInformation (bytes->getData(), (int) bytes->getSize());
            }
            else failure = "Invalid hosted plugin description.";
        }
        std::shared_ptr<juce::AudioPluginInstance> retired;
        {
            const juce::ScopedLock lock (mutex); auto& s = slots[(size_t) n];
            if (s.revision != revision) { triggerAsyncUpdate(); continue; }
            if (plugin && prepared)
            {
                const auto result = configure (*plugin); failure = result.getErrorMessage();
                if (result.failed()) plugin.reset();
            }
            retired = std::move (s.plugin); s.plugin = std::move (plugin);
            s.audio.setSize (2, blockSize); s.pending = false; s.prepared = prepared && s.plugin != nullptr;
            s.error = failure; if (saved.isValid() && ! s.plugin && failure.isEmpty()) s.error = "Plugin is unavailable.";
        }
        if (retired) retired->releaseResources();
    }
}

void HostedPlugins::prepare (double sr, int block, int count)
{
    const juce::ScopedLock lock (mutex); sampleRate = sr; blockSize = block; channels = count; prepared = true;
    for (auto& s : slots)
    {
        s.audio.setSize (2, blockSize);
        if (! s.plugin || s.pending) continue;
        if (s.prepared) s.plugin->releaseResources();
        auto result = configure (*s.plugin); s.prepared = result.wasOk(); s.error = result.getErrorMessage();
    }
}
void HostedPlugins::release()
{
    const juce::ScopedLock lock (mutex); prepared = false;
    for (auto& s : slots) if (s.plugin && s.prepared) { s.plugin->releaseResources(); s.prepared = false; }
}
void HostedPlugins::setNonRealtime (bool offline)
{
    for (auto& s : slots) if (s.plugin && s.plugin->isNonRealtime() != offline) s.plugin->setNonRealtime (offline);
}
double HostedPlugins::tail() const
{
    const juce::ScopedLock lock (mutex); double total = 0;
    for (const auto& s : slots) if (s.plugin && ! s.pending)
    { const double value = s.plugin->getTailLengthSeconds(); if (std::isfinite (value) && value > 0) total += value; }
    return total;
}
bool HostedPlugins::ready (int n) const
{
    const auto& s = slots[(size_t) n];
    return s.plugin && ! s.pending && s.prepared && s.plugin->getLatencySamples() >= 0
        && s.plugin->getLatencySamples() <= maximumLatency
        && s.plugin->getTotalNumInputChannels() <= 2 && s.plugin->getTotalNumOutputChannels() >= 1
        && s.plugin->getTotalNumOutputChannels() <= 2;
}
int HostedPlugins::latency (int n) const { return ready (n) ? slots[(size_t) n].plugin->getLatencySamples() : 0; }
void HostedPlugins::reset (int n) { auto& s = slots[(size_t) n]; if (ready (n)) s.plugin->reset(); }
void HostedPlugins::setPosition (juce::AudioPlayHead* parent, int offset, double bpm, double ppq, bool playing)
{
    auto position = parent ? parent->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
    playHead.position = position ? *position : juce::AudioPlayHead::PositionInfo();
    playHead.position.setBpm (bpm); playHead.position.setPpqPosition (ppq); playHead.position.setIsPlaying (playing);
    if (const auto t = playHead.position.getTimeInSamples()) playHead.position.setTimeInSamples (*t + offset);
    if (const auto t = playHead.position.getTimeInSeconds()) playHead.position.setTimeInSeconds (*t + offset / sampleRate);
}
void HostedPlugins::process (int n, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    if (! ready (n)) { if (n >= firstInstrument) audio.clear(); return; }
    auto& s = slots[(size_t) n]; auto& plugin = *s.plugin;
    const int count = audio.getNumSamples(), ins = plugin.getTotalNumInputChannels(), outs = plugin.getTotalNumOutputChannels();
    juce::AudioBuffer<float> work (s.audio.getArrayOfWritePointers(), juce::jmax (ins, outs), count); work.clear();
    if (n < firstInstrument) for (int ch = 0; ch < ins; ++ch)
    {
        work.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), 0, count);
        if (ins == 1 && audio.getNumChannels() == 2)
        { work.applyGain (ch, 0, count, .5f); work.addFrom (ch, 0, audio, 1, 0, count, .5f); }
    }
    plugin.setPlayHead (&playHead); plugin.processBlock (work, midi);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        audio.copyFrom (ch, 0, work, juce::jmin (ch, outs - 1), 0, count);
        if (audio.getNumChannels() == 1 && outs == 2)
        { audio.applyGain (.5f); audio.addFrom (0, 0, work, 1, 0, count, .5f); }
        for (int sample = 0; sample < count; ++sample)
            if (! std::isfinite (audio.getSample (ch, sample))) audio.setSample (ch, sample, 0);
    }
}
juce::String HostedPlugins::status (int n) const
{
    const juce::ScopedLock lock (mutex); const auto& s = slots[(size_t) n];
    if (! s.saved.isValid()) return "No plugin loaded";
    juce::PluginDescription d; if (auto xml = juce::parseXML (s.saved["description"].toString())) d.loadFromXml (*xml);
    return d.name + (s.pending ? " / Loading" : s.error.isNotEmpty() ? " / " + s.error
        : s.plugin && s.plugin->getLatencySamples() > maximumLatency ? " / Unsupported latency" : " / VST3");
}
std::shared_ptr<juce::AudioPluginInstance> HostedPlugins::instance (int n) const
{
    const juce::ScopedLock lock (mutex);
    return slots[(size_t) n].pending ? nullptr : slots[(size_t) n].plugin;
}
}
