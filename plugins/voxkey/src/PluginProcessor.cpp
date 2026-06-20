#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariVoxKeyAudioProcessor::SattariVoxKeyAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Voice In", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Dry Voice Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    apvts.state.setProperty ("schema", "SattariVoxKey.v0.1.0", nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariVoxKeyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("mode", "Mode",
        juce::StringArray { "Melody", "Bass Follow", "Harmony", "Drum Mouth", "Pad/Soul Layer" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("key", "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale",
        juce::StringArray { "Chromatic", "Major", "Minor", "Pentatonic" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("sensitivity", "Sensitivity", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.62f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("smoothness", "Smoothness", juce::NormalisableRange<float> (0.0f, 0.98f, 0.01f), 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("confidence", "Confidence", juce::NormalisableRange<float> (0.20f, 0.95f, 0.01f), 0.58f));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("octave", "Octave", -3, 3, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("hold", "Hold", juce::NormalisableRange<float> (10.0f, 300.0f, 1.0f), 60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("release", "Release", juce::NormalisableRange<float> (20.0f, 1000.0f, 1.0f), 140.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("velocity", "Velocity Response", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("pitchBend", "Pitch Bend", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("pitchBendRange", "Bend Range", 1, 12, 2));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("glide", "Glide", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));

    return { params.begin(), params.end() };
}

void SattariVoxKeyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.prepare (sampleRate, samplesPerBlock);
    midiMapper.prepare (sampleRate);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock), false, false, true);
    publishDisplayState ({});
}

bool SattariVoxKeyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == out) && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

VoxKeyVoiceMidiMapper::Settings SattariVoxKeyAudioProcessor::readSettings() const
{
    VoxKeyVoiceMidiMapper::Settings s;
    s.mode = (VoxKeyVoiceMidiMapper::Mode) (int) *apvts.getRawParameterValue ("mode");
    s.keyRoot = (int) *apvts.getRawParameterValue ("key");
    s.scale = (VoxKeyVoiceMidiMapper::Scale) (int) *apvts.getRawParameterValue ("scale");
    s.octaveShift = (int) *apvts.getRawParameterValue ("octave");
    s.holdMs = *apvts.getRawParameterValue ("hold");
    s.releaseMs = *apvts.getRawParameterValue ("release");
    s.velocityResponse = *apvts.getRawParameterValue ("velocity");
    s.noteStabilityCents = 45.0f;
    s.pitchBendRangeSemis = *apvts.getRawParameterValue ("pitchBendRange");
    // Onset gate sits a margin above the detector's own confidence floor so
    // breaths/consonants don't spawn notes, while sustained pitch changes pass.
    s.startConfidence = juce::jlimit (0.0f, 1.0f, *apvts.getRawParameterValue ("confidence") + 0.12f);
    s.pitchBend = *apvts.getRawParameterValue ("pitchBend") > 0.5f;
    s.glide = *apvts.getRawParameterValue ("glide") > 0.5f;
    s.bypass = *apvts.getRawParameterValue ("bypass") > 0.5f;
    return s;
}

void SattariVoxKeyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    for (int ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    midi.clear();

    if (panicRequested.exchange (false, std::memory_order_relaxed))
    {
        midiMapper.reset (midi, 0);
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    if (monoScratch.getNumSamples() < numSamples)
        monoScratch.setSize (1, numSamples, false, false, true);

    auto* mono = monoScratch.getWritePointer (0);
    std::fill (mono, mono + numSamples, 0.0f);
    const int channelsToRead = juce::jmax (1, numInputChannels);
    for (int ch = 0; ch < channelsToRead; ++ch)
    {
        const float* src = buffer.getReadPointer (juce::jmin (ch, buffer.getNumChannels() - 1));
        for (int i = 0; i < numSamples; ++i)
            mono[i] += src[i] / (float) channelsToRead;
    }

    const auto frame = pitchDetector.analyseBlock (mono,
                                                   numSamples,
                                                   *apvts.getRawParameterValue ("sensitivity"),
                                                   *apvts.getRawParameterValue ("confidence"),
                                                   *apvts.getRawParameterValue ("smoothness"));

    midiMapper.processFrame (frame, readSettings(), midi, 0);
    publishDisplayState (midiMapper.getState());
}

void SattariVoxKeyAudioProcessor::publishDisplayState (const VoxKeyVoiceMidiMapper::State& state)
{
    displayRms.store (state.inputRms, std::memory_order_relaxed);
    displayConfidence.store (state.confidence, std::memory_order_relaxed);
    displayDetectedNote.store (state.detectedNote, std::memory_order_relaxed);
    displayOutputNote.store (state.outputRoot, std::memory_order_relaxed);
    displayVoiced.store (state.voiced, std::memory_order_relaxed);
    displayMidiActive.store (state.midiActive, std::memory_order_relaxed);
}

VoxKeyVoiceMidiMapper::State SattariVoxKeyAudioProcessor::getDisplayState() const
{
    VoxKeyVoiceMidiMapper::State state;
    state.inputRms = displayRms.load (std::memory_order_relaxed);
    state.confidence = displayConfidence.load (std::memory_order_relaxed);
    state.detectedNote = displayDetectedNote.load (std::memory_order_relaxed);
    state.outputRoot = displayOutputNote.load (std::memory_order_relaxed);
    state.detectedName = VoxKeyVoiceMidiMapper::midiNoteName (state.detectedNote);
    state.outputName = VoxKeyVoiceMidiMapper::midiNoteName (state.outputRoot);
    state.voiced = displayVoiced.load (std::memory_order_relaxed);
    state.midiActive = displayMidiActive.load (std::memory_order_relaxed);
    return state;
}

void SattariVoxKeyAudioProcessor::panic()
{
    panicRequested.store (true, std::memory_order_relaxed);
}

void SattariVoxKeyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, true);
    apvts.state.writeToStream (stream);
}

void SattariVoxKeyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
        apvts.replaceState (tree);
}

juce::AudioProcessorEditor* SattariVoxKeyAudioProcessor::createEditor()
{
    return new SattariVoxKeyAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariVoxKeyAudioProcessor();
}
