#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::StringArray kNoteNames { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };
}

SattariTunerAudioProcessor::SattariTunerAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values (reference Hz, 0..1 smoothing, transpose semis).
// Only listed params are set; anything omitted keeps its default.
void SattariTunerAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Standard (A=440)", PV{ {"referenceA", 440}, {"smoothing", 0.50f}, {"transpose",  0} });
    presetBank.addFactoryPreset ("432 Hz",           PV{ {"referenceA", 432}, {"smoothing", 0.50f}, {"transpose",  0} });
    presetBank.addFactoryPreset ("Baroque (A=415)",  PV{ {"referenceA", 415}, {"smoothing", 0.60f}, {"transpose",  0} });
    presetBank.addFactoryPreset ("Bb Instrument",    PV{ {"referenceA", 440}, {"smoothing", 0.50f}, {"transpose",  2} });
    presetBank.addFactoryPreset ("Eb Instrument",    PV{ {"referenceA", 440}, {"smoothing", 0.50f}, {"transpose", -3} });
    presetBank.addFactoryPreset ("Steady Needle",    PV{ {"referenceA", 440}, {"smoothing", 0.90f}, {"transpose",  0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariTunerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Reference pitch for A4. 440 standard; 432/415 for alternative/period tuning.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "referenceA", "Reference A",
        juce::NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f));

    // Readout smoothing: 0 = snappy, 1 = very steady (slower).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "smoothing", "Smoothing",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

    // Transpose the reported note (transposing instruments / capo), in semitones.
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        "transpose", "Transpose", -12, 12, 0));

    return { params.begin(), params.end() };
}

void SattariTunerAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    // Cover down to ~40 Hz (low bass) up to ~1.2 kHz; ~50 ms window is plenty for
    // a stable low-note period while staying responsive.
    detector.prepare (sr, 40.0f, 1200.0f, 0.050f);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    smoothedMidi = -1.0;
    liveVoiced.store (false);
    liveMidi.store (-1);
}

bool SattariTunerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariTunerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    // Hosts may exceed the block-size estimate passed to prepareToPlay.
    // Feed all input through bounded scratch chunks without growing on the
    // audio thread. Analysis still happens once, after the whole callback.
    float fallback = 0.0f;
    float* const scratch = monoScratch.empty() ? &fallback : monoScratch.data();
    const int capacity = monoScratch.empty() ? 1 : (int) monoScratch.size();
    for (int offset = 0; offset < n;)
    {
        const int chunk = juce::jmin (n - offset, capacity);
        for (int i = 0; i < chunk; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numCh; ++ch) s += buffer.getReadPointer (ch)[offset + i];
            scratch[i] = (numCh > 0) ? s / (float) numCh : 0.0f;
        }
        detector.pushBlock (scratch, chunk);
        offset += chunk;
    }
    const auto r = detector.analyse (0.3f);

    const double refA      = (double) param ("referenceA");
    const double transpose = (double) param ("transpose");
    const double smoothing = (double) param ("smoothing");

    if (r.voiced && r.frequencyHz > 0.0f)
    {
        // MIDI (fractional) relative to the chosen reference A, then transposed.
        double midiF = 69.0 + 12.0 * std::log2 ((double) r.frequencyHz / refA) + transpose;

        // One-pole smoothing toward the fresh reading for a steady needle.
        const double alpha = juce::jmap (smoothing, 0.0, 1.0, 0.6, 0.06);
        if (smoothedMidi < 0.0) smoothedMidi = midiF;   // arm on first voiced frame
        else                    smoothedMidi += alpha * (midiF - smoothedMidi);

        const int nearest = (int) std::llround (smoothedMidi);
        const float cents = (float) ((smoothedMidi - (double) nearest) * 100.0);

        liveMidi.store (juce::jlimit (0, 127, nearest), std::memory_order_relaxed);
        liveCents.store (juce::jlimit (-50.0f, 50.0f, cents), std::memory_order_relaxed);
        liveHz.store (r.frequencyHz, std::memory_order_relaxed);
        liveConf.store (r.confidence, std::memory_order_relaxed);
        liveVoiced.store (true, std::memory_order_relaxed);
    }
    else
    {
        smoothedMidi = -1.0;                              // disarm on silence
        liveVoiced.store (false, std::memory_order_relaxed);
        liveConf.store (r.confidence, std::memory_order_relaxed);
    }

    // Pass-through: a tuner never alters the audio.
    juce::ignoreUnused (buffer);
}

juce::String SattariTunerAudioProcessor::noteName (int midiNote)
{
    if (midiNote < 0) return "--";
    const int pc = ((midiNote % 12) + 12) % 12;
    const int octave = midiNote / 12 - 1;
    return kNoteNames[pc] + juce::String (octave);
}

void SattariTunerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariTunerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariTunerAudioProcessor::createEditor()
{
    return new SattariTunerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariTunerAudioProcessor();
}
