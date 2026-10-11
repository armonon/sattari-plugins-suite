#include <JuceHeader.h>

class HostingFixture final : public juce::AudioProcessor
{
public:
    HostingFixture() : AudioProcessor (FIXTURE_SYNTH ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
        : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addParameter (gain = new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 1.0f, .5f));
        setLatencySamples (FIXTURE_SYNTH ? 128 : 64);
    }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool isBusesLayoutSupported (const BusesLayout& b) const override
    { return b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && (FIXTURE_SYNTH || b.getMainInputChannelSet() == b.getMainOutputChannelSet()); }
    void prepareToPlay (double, int) override { reset(); }
    void releaseResources() override {}
    void reset() override { for (auto& channel : ring) channel.fill (0); cursor = 0; sounding = false; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        for (int s = 0; s < b.getNumSamples(); ++s)
        {
            for (const auto event : midi) if (event.samplePosition == s)
            {
                const auto message = event.getMessage();
                if (message.isNoteOn()) sounding = true;
                if (message.isNoteOff() || message.isAllNotesOff() || message.isAllSoundOff()) sounding = false;
            }
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
            {
                const auto input = FIXTURE_SYNTH ? (sounding ? .2f : 0.0f) : b.getSample (ch, s);
                const auto delayed = ring[(size_t) ch][(size_t) cursor];
                ring[(size_t) ch][(size_t) cursor] = input * gain->get(); b.setSample (ch, s, delayed);
            }
            cursor = (cursor + 1) % getLatencySamples();
        }
        midi.clear();
    }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Test"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& bytes) override
    { juce::MemoryOutputStream out (bytes, false); out.writeFloat (gain->get()); }
    void setStateInformation (const void* data, int size) override
    { if (size == 4) { juce::MemoryInputStream input (data, (size_t) size, false); gain->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, input.readFloat())); } }
private:
    juce::AudioParameterFloat* gain = nullptr;
    std::array<std::array<float, 128>, 2> ring {};
    int cursor = 0;
    bool sounding = false;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HostingFixture(); }
