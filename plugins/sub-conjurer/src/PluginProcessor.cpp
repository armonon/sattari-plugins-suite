#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

float clampHz (float hz)
{
    return juce::jlimit (25.0f, 160.0f, hz);
}

float softClip (float x)
{
    return std::tanh (x);
}
}

SubConjurerAudioProcessor::SubConjurerAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Presets shape the sub tone/character; Tuning, Lock
// Note and Follow Song Key are left untouched so the sub keeps the user's key.
void SubConjurerAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // mode: 0=Clean 1=Power 2=Deep 3=808 4=Perc-to-Sub   (octave int 0..3)
    presetBank.addFactoryPreset ("Clean Sub",
        PV{ {"mode", 0}, {"octave", 1}, {"amount", 0.8f}, {"dry", 0.5f}, {"richness", 0.1f},
            {"glide", 0.15f}, {"saturation", 0.1f}, {"attack", 0.008f}, {"release", 0.18f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("Power Bass",
        PV{ {"mode", 1}, {"octave", 1}, {"amount", 1.0f}, {"dry", 0.35f}, {"richness", 0.4f},
            {"glide", 0.2f}, {"saturation", 0.35f}, {"attack", 0.006f}, {"release", 0.2f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("Deep Rumble",
        PV{ {"mode", 2}, {"octave", 0}, {"amount", 1.0f}, {"dry", 0.2f}, {"richness", 0.2f},
            {"glide", 0.3f}, {"saturation", 0.2f}, {"attack", 0.012f}, {"release", 0.35f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("808 Boom",
        PV{ {"mode", 3}, {"octave", 1}, {"amount", 1.1f}, {"dry", 0.25f}, {"richness", 0.35f},
            {"glide", 0.3f}, {"saturation", 0.45f}, {"attack", 0.005f}, {"release", 0.5f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("Perc Sub",
        PV{ {"mode", 4}, {"octave", 1}, {"amount", 0.9f}, {"dry", 0.3f}, {"richness", 0.3f},
            {"glide", 0.05f}, {"saturation", 0.3f}, {"attack", 0.003f}, {"release", 0.12f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("Warm Saturated",
        PV{ {"mode", 1}, {"octave", 1}, {"amount", 0.95f}, {"dry", 0.3f}, {"richness", 0.5f},
            {"glide", 0.2f}, {"saturation", 0.6f}, {"attack", 0.008f}, {"release", 0.25f}, {"subonly", 0} });
    presetBank.addFactoryPreset ("Sub Only",
        PV{ {"mode", 2}, {"octave", 0}, {"amount", 1.0f}, {"dry", 0.0f}, {"richness", 0.25f},
            {"glide", 0.25f}, {"saturation", 0.2f}, {"attack", 0.008f}, {"release", 0.22f}, {"subonly", 1} });
}

juce::AudioProcessorValueTreeState::ParameterLayout SubConjurerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("mode", "Mode",
        juce::StringArray { "Clean", "Power", "Deep", "808", "Perc-to-Sub" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("tuning", "Tuning",
        juce::StringArray { "Follow Pitch", "Lock Note", "MIDI Note" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("note", "Lock Note",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9));
    // Cross-plugin session bus: when on, the locked sub note follows the song key
    // published by Sattari Key (or any Sattari publisher), so the sub always sits
    // in the current key without manual retuning. Default OFF so the manual Lock
    // Note is authoritative out of the box; the editor greys it while Follow is on.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followKey", "Follow Song Key", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("octave", "Octave", 0, 3, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("amount", "Sub Amount",
        juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 0.9f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("dry", "Dry Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("richness", "Harmonics",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.28f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("glide", "Glide",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.20f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("saturation", "Saturation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("attack", "Attack",
        juce::NormalisableRange<float> (0.001f, 0.100f, 0.001f), 0.008f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("release", "Release",
        juce::NormalisableRange<float> (0.030f, 0.800f, 0.001f), 0.180f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("limiter", "Limiter Ceiling",
        juce::NormalisableRange<float> (0.25f, 0.98f, 0.001f), 0.82f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("subonly", "Sub Only", false));

    return { params.begin(), params.end() };
}

void SubConjurerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    oscillatorPhase = 0.0f;
    envelopeFollower.prepare (currentSampleRate);
    smoothedHz = 55.0f;
    limiterState = 1.0f;
    monoScratch.resize ((size_t) juce::jmax (samplesPerBlock, 32));
    sessionReader.start();   // poll the cross-plugin session bus off the audio thread

    // Shared detector with a long low-frequency window (>= one period down to 25 Hz);
    // ~90 ms so the 25 Hz floor still gets a couple of periods inside the window.
    pitchDetector.prepare (sampleRate, 25.0f, 160.0f, 0.090f);
    analysisAccumulator = 0;
    analysisHop = juce::jmax (512, samplesPerBlock);

    // Sample-rate-correct limiter recovery (~20 ms) instead of a fixed per-sample step.
    limiterRecoveryCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.02));
}

bool SubConjurerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

float SubConjurerAudioProcessor::midiNoteToHz (int note) const
{
    return 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
}

int SubConjurerAudioProcessor::noteNameToMidi (int index) const
{
    const int octave = (int) *apvts.getRawParameterValue ("octave");
    return 12 * (octave + 1) + index;
}

void SubConjurerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if ((int) monoScratch.size() < numSamples)
        monoScratch.resize ((size_t) numSamples);

    std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* input = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            monoScratch[(size_t) i] += input[i] / (float) numChannels;
    }

    const int mode = (int) *apvts.getRawParameterValue ("mode");
    const int tuning = (int) *apvts.getRawParameterValue ("tuning");
    const float amount = *apvts.getRawParameterValue ("amount");
    const float dryMix = *apvts.getRawParameterValue ("dry");
    const float richness = *apvts.getRawParameterValue ("richness");
    const float attack = *apvts.getRawParameterValue ("attack");
    const float release = *apvts.getRawParameterValue ("release");
    const float ceiling = *apvts.getRawParameterValue ("limiter");
    const float glide = *apvts.getRawParameterValue ("glide");
    const float saturation = *apvts.getRawParameterValue ("saturation");
    const bool subOnly = *apvts.getRawParameterValue ("subonly") > 0.5f;

    // Feed this block's mono sum into the shared low-frequency pitch detector.
    pitchDetector.pushBlock (monoScratch.data(), numSamples);

    // Re-estimate the fundamental on a decimated schedule (analysis is O(window*lags)).
    analysisAccumulator += numSamples;
    float wantedHz = smoothedHz;
    if (analysisAccumulator >= analysisHop)
    {
        analysisAccumulator = 0;
        const auto detected = pitchDetector.analyse (0.22f);
        if (detected.voiced)
        {
            wantedHz = detected.frequencyHz;
            // Octave-continuity guard (Follow Pitch only): fold a fresh estimate
            // that leaps more than ~a fifth back toward the running pitch, so the
            // sub stays locked instead of jumping an octave on a mis-detection.
            if (tuning == 0 && smoothedHz > 0.0f)
                wantedHz = sattari::dsp::foldToOctaveNear (wantedHz, smoothedHz);
        }
    }

    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
            wantedHz = midiNoteToHz (metadata.getMessage().getNoteNumber());

    if (tuning == 1)
    {
        int lockNote = (int) *apvts.getRawParameterValue ("note");
        // Follow Song Key: override the locked note with the published session
        // root when enabled and a fresh key is on the bus.
        if (apvts.getRawParameterValue ("followKey")->load() > 0.5f)
            if (auto s = sessionReader.current(); s.valid)
                lockNote = s.root;
        wantedHz = midiNoteToHz (noteNameToMidi (lockNote));
    }
    else if (tuning == 2 && wantedHz <= 0.0f)
        wantedHz = smoothedHz;

    if (mode == 2) wantedHz *= 0.5f;       // Deep
    if (wantedHz < 25.0f) wantedHz *= 2.0f; // infrasonic guardrail
    targetHz = clampHz (wantedHz);

    envelopeFollower.setTimes (attack * 1000.0f, release * 1000.0f);
    // Time-based portamento toward the target pitch (glide=0 ~ instant, 1 ~ 300 ms),
    // glided per sample so pitch changes sweep smoothly instead of stepping per block.
    const float glideTime = 0.001f + glide * 0.30f;
    const float glideCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * glideTime));
    const float modeDrive = mode == 0 ? 0.9f : mode == 1 ? 1.25f : mode == 3 ? 1.45f : 1.1f;
    const float harmonic2 = richness * (mode == 0 ? 0.10f : 0.22f);
    const float harmonic3 = richness * (mode == 3 ? 0.16f : 0.08f);
    // Extra saturation stage (normalised so it adds harmonics without a level jump);
    // 808 mode leans harder for the classic distorted-sub growl.
    const float satK = 1.0f + saturation * (mode == 3 ? 5.0f : 3.0f);
    const float satNorm = std::tanh (satK);

    for (int i = 0; i < numSamples; ++i)
    {
        const float envelope = envelopeFollower.processRectified (monoScratch[(size_t) i]);

        smoothedHz += glideCoeff * (targetHz - smoothedHz);
        const float phaseInc = twoPi * smoothedHz / (float) currentSampleRate;
        oscillatorPhase += phaseInc;
        if (oscillatorPhase >= twoPi)
            oscillatorPhase -= twoPi;

        float sub = std::sin (oscillatorPhase);
        sub += harmonic2 * std::sin (2.0f * oscillatorPhase);
        sub += harmonic3 * std::sin (3.0f * oscillatorPhase);
        sub = softClip (sub * modeDrive) * envelope * amount * 2.2f;
        if (saturation > 0.0f)
            sub = std::tanh (sub * satK) / satNorm;   // dynamic transient-following drive

        const float peak = std::abs (sub);
        if (peak > ceiling)
            limiterState = juce::jmin (limiterState, ceiling / (peak + 1.0e-9f));
        else
            limiterState += limiterRecoveryCoeff * (1.0f - limiterState);

        sub *= limiterState;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            const float dry = subOnly ? 0.0f : out[i] * dryMix;
            out[i] = juce::jlimit (-0.98f, 0.98f, dry + sub);
        }
    }
}

juce::AudioProcessorEditor* SubConjurerAudioProcessor::createEditor()
{
    return new SubConjurerAudioProcessorEditor (*this);
}

void SubConjurerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SubConjurerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SubConjurerAudioProcessor();
}
