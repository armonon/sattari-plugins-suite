#include "RoyalChainProcessor.h"
#include "RoyalChainEditor.h"
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>

RoyalChainAudioProcessor::RoyalChainAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Each preset picks a vocal Style and dials the macro
// chain; Tune Key / Sync-from-Auto-Pitch are left untouched so the key is the
// user's (Tune stays subtle so no key bias is imposed).
void RoyalChainAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // style: 0=Modern Rap 1=R&B 2=Pop 3=Persian/World Pop 4=Indie
    //        5=Podcast/Spoken 6=Background 7=Hook 8=Demo Rescue 9=Live
    presetBank.addFactoryPreset ("Modern Rap",
        PV{ {"style", 0}, {"clean", 0.6f}, {"tune", 0.0f}, {"control", 0.65f}, {"tone", 0.5f},
            {"air", 0.5f}, {"smooth", 0.4f}, {"glow", 0.3f}, {"width", 0.3f}, {"space", 0.2f}, {"royal", 0.55f} });
    presetBank.addFactoryPreset ("R&B Silk",
        PV{ {"style", 1}, {"clean", 0.55f}, {"tune", 0.0f}, {"control", 0.55f}, {"tone", 0.55f},
            {"air", 0.55f}, {"smooth", 0.6f}, {"glow", 0.45f}, {"width", 0.4f}, {"space", 0.4f}, {"royal", 0.55f} });
    presetBank.addFactoryPreset ("Pop Sheen",
        PV{ {"style", 2}, {"clean", 0.6f}, {"tune", 0.0f}, {"control", 0.6f}, {"tone", 0.6f},
            {"air", 0.65f}, {"smooth", 0.45f}, {"glow", 0.4f}, {"width", 0.45f}, {"space", 0.35f}, {"royal", 0.6f} });
    presetBank.addFactoryPreset ("Persian / World Pop",
        PV{ {"style", 3}, {"clean", 0.55f}, {"tune", 0.0f}, {"control", 0.55f}, {"tone", 0.55f},
            {"air", 0.5f}, {"smooth", 0.5f}, {"glow", 0.4f}, {"width", 0.35f}, {"space", 0.45f}, {"royal", 0.55f} });
    presetBank.addFactoryPreset ("Podcast Clean",
        PV{ {"style", 5}, {"clean", 0.5f}, {"tune", 0.0f}, {"control", 0.5f}, {"tone", 0.45f},
            {"air", 0.35f}, {"smooth", 0.35f}, {"glow", 0.15f}, {"width", 0.0f}, {"space", 0.0f}, {"royal", 0.3f} });
    presetBank.addFactoryPreset ("Background Vox",
        PV{ {"style", 6}, {"clean", 0.5f}, {"tune", 0.0f}, {"control", 0.5f}, {"tone", 0.45f},
            {"air", 0.4f}, {"smooth", 0.5f}, {"glow", 0.3f}, {"width", 0.6f}, {"space", 0.5f}, {"royal", 0.4f} });
    presetBank.addFactoryPreset ("Hook Lift",
        PV{ {"style", 7}, {"clean", 0.6f}, {"tune", 0.0f}, {"control", 0.65f}, {"tone", 0.6f},
            {"air", 0.7f}, {"smooth", 0.5f}, {"glow", 0.55f}, {"width", 0.55f}, {"space", 0.5f}, {"royal", 0.7f} });
    presetBank.addFactoryPreset ("Demo Rescue",
        PV{ {"style", 8}, {"clean", 0.7f}, {"tune", 0.0f}, {"control", 0.7f}, {"tone", 0.6f},
            {"air", 0.5f}, {"smooth", 0.6f}, {"glow", 0.35f}, {"width", 0.3f}, {"space", 0.3f}, {"royal", 0.6f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout RoyalChainAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    auto macroRange = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("style", "Vocal Style",
        juce::StringArray { "Modern Rap", "R&B", "Pop", "Persian/World Pop", "Indie", "Podcast/Spoken", "Background", "Hook", "Demo Rescue", "Live" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("input", "Input", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("output", "Output", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> ("clean", "Clean", macroRange, 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tune", "Tune", macroRange, 0.00f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("tuneKey", "Tune Key",
        juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("syncFromAutoPitch", "Sync Key from Auto Pitch", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("tuneScale", "Tune Scale",
        juce::StringArray { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("control", "Control", macroRange, 0.58f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tone", "Tone", macroRange, 0.52f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("air", "Air", macroRange, 0.42f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("smooth", "Smooth", macroRange, 0.44f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("glow", "Glow", macroRange, 0.32f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("width", "Width", macroRange, 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("space", "Space", macroRange, 0.22f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("royal", "Royal", macroRange, 0.50f));

    return { params.begin(), params.end() };
}

void RoyalChainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    compassTap.setSampleRate (sampleRate);
    compassInTap.setSampleRate (sampleRate);
    cleanModule.prepare (sampleRate);
    tuneModule.prepare (sampleRate, samplesPerBlock);
    // The Tune stage (Auto Pitch's Extreme Live engine) is the chain's only
    // latency: a constant 2 ms, independent of the Tune amount, so host delay
    // compensation never moves when Tune is automated.
    setLatencySamples (tuneModule.getLatencySamples());
    controlModule.prepare (sampleRate);
    toneModule.prepare (sampleRate);
    deEsserModule.prepare (sampleRate);
    colorModule.prepare (sampleRate);
    widthModule.prepare (sampleRate, samplesPerBlock);
    spaceModule.prepare (sampleRate, samplesPerBlock);
    finishModule.prepare (sampleRate);
    sessionReader.start();   // poll the cross-plugin session bus off the audio thread
}

bool RoyalChainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

float RoyalChainAudioProcessor::measurePeak (const juce::AudioBuffer<float>& buffer) const
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (data[i]));
    }
    return juce::jlimit (0.0f, 1.0f, peak);
}

void RoyalChainAudioProcessor::updateMeter (std::atomic<float>& meter, float target)
{
    const float current = meter.load();
    const float coeff = target > current ? 0.35f : 0.08f;
    meter.store (current + coeff * (target - current));
}

void RoyalChainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int totalInputs = getTotalNumInputChannels();
    const int totalOutputs = getTotalNumOutputChannels();
    for (int ch = totalInputs; ch < totalOutputs; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateMeter (inputMeter, measurePeak (buffer));
    // Compass: the chain's INPUT, so the dish's rim can show what the effects changed.
    compassInTap.push (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());

    const auto macros = MacroController::readMacros (apvts);
    const float inputDb = apvts.getRawParameterValue ("input")->load();
    const float outputDb = apvts.getRawParameterValue ("output")->load();

    cleanModule.process (buffer, inputDb, macros.clean, macros.royal);
    int tuneKey = (int) apvts.getRawParameterValue ("tuneKey")->load();
    int tuneScaleIdx = (int) apvts.getRawParameterValue ("tuneScale")->load();

    // Sync Key from Auto Pitch (cross-plugin bus). The SessionReader's background
    // thread polls the bus; here we just read its lock-free snapshot and, when a
    // fresh key has been published, override tuneKey + tuneScaleIdx for this block.
    if (apvts.getRawParameterValue ("syncFromAutoPitch")->load() > 0.5f)
    {
        // Read the bus via the lock-free SessionReader snapshot (the background
        // poll thread does the file I/O, never the audio thread).
        if (auto s = sessionReader.current(); s.valid)
        {
            tuneKey = s.root;
            // Auto Pitch scaleIdx: 0=major, 1=minor, 2=chromatic.
            // Royal Chain tuneScale: 0=Chromatic, 1=Major, 2=Minor, etc.
            switch (s.scale)
            {
                case 0: tuneScaleIdx = 1; break;   // major
                case 1: tuneScaleIdx = 2; break;   // minor
                default: tuneScaleIdx = 0; break;  // chromatic / unknown
            }
        }
    }

    const int vocalStyle = (int) apvts.getRawParameterValue ("style")->load();
    tuneModule.process (buffer, macros.tune, tuneKey, (sattari::dsp::ScaleId) tuneScaleIdx, vocalStyle);
    pitchHz.store (tuneModule.getLastHz(), std::memory_order_relaxed);
    effectiveTuneKey.store (tuneKey, std::memory_order_relaxed);         // after the Auto Pitch sync override
    effectiveTuneScale.store (tuneScaleIdx, std::memory_order_relaxed);
    controlModule.process (buffer, macros.control, macros.royal);
    toneModule.process (buffer, macros.tone, macros.air, macros.royal);
    deEsserModule.process (buffer, macros.smooth, macros.royal);
    colorModule.process (buffer, macros.glow, macros.royal);
    widthModule.process (buffer, macros.width, macros.royal);
    spaceModule.process (buffer, macros.space, macros.royal);
    finishModule.process (buffer, outputDb, macros.royal);

    updateMeter (outputMeter, measurePeak (buffer));
    // Compass water dish: publish the processed output (lock-free, no allocation).
    compassTap.push (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
    tuneConfidence.store (tuneModule.getConfidence());
    controlMeter.store (juce::jlimit (0.0f, 1.0f, controlModule.getGainReductionDb() / 12.0f));
    sibilanceMeter.store (deEsserModule.getSibilance());
    widthMeter.store (widthModule.getWidthMeter());
    limiterMeter.store (finishModule.getLimiterMeter());
}

juce::AudioProcessorEditor* RoyalChainAudioProcessor::createEditor()
{
    return new RoyalChainAudioProcessorEditor (*this);
}

void RoyalChainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RoyalChainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new RoyalChainAudioProcessor();
}
