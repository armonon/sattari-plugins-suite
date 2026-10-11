#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>

namespace sattari::modular
{
// Instances are created/restored on the message thread. Audio never waits for
// loading or state capture; the processor takes a try-lock for the whole block.
class HostedPlugins final : private juce::AsyncUpdater
{
public:
    static constexpr int slotCount = 12, firstInstrument = 8;
    static constexpr int maximumLatency = 16384;
    static constexpr int maximumStateBytes = 256 * 1024 * 1024;
    static constexpr int maximumPluginBytes = 32 * 1024 * 1024;
    HostedPlugins();
    ~HostedPlugins() override;
    juce::Array<juce::PluginDescription> scan (const juce::File&, bool instrument, juce::String& error);
    juce::Result load (int, const juce::PluginDescription&);
    void clear (int);
    void copy (int from, int to);
    void swap (int, int);
    juce::ValueTree capture();
    static bool validState (const juce::ValueTree&);
    void restore (const juce::ValueTree&);
    void flush();
    void prepare (double, int, int);
    void release();
    void setNonRealtime (bool);
    double tail() const;
    void reset (int);
    int latency (int) const;
    bool ready (int) const;
    void process (int, juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void setPosition (juce::AudioPlayHead*, int offset, double bpm, double ppq, bool playing);
    juce::String status (int) const;
    std::shared_ptr<juce::AudioPluginInstance> instance (int) const;
    mutable juce::CriticalSection mutex;

private:
    struct Slot
    {
        juce::ValueTree saved;
        std::shared_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioBuffer<float> audio;
        juce::String error;
        bool pending = false, prepared = false;
        unsigned revision = 0;
    };
    struct PlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return position; }
        PositionInfo position;
    } playHead;
    std::array<Slot, slotCount> slots;
    juce::AudioPluginFormatManager formats;
    double sampleRate = 48000;
    int blockSize = 512, channels = 2;
    bool prepared = false;
    static bool permitted (const juce::PluginDescription&, bool instrument);
    juce::ValueTree captureSlot (int);
    juce::Result configure (juce::AudioPluginInstance&);
    void handleAsyncUpdate() override;
};
}
