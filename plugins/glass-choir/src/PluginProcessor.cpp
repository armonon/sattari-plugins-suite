#include "PluginProcessor.h"
#include "PluginEditor.h"

// =============================================================================
//  Sattari Glass Choir — processor implementation (assembly of suite DSP)
// =============================================================================

SattariGlassChoirAudioProcessor::SattariGlassChoirAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",      juce::AudioChannelSet::stereo(), true)
          .withOutput ("Lead",       juce::AudioChannelSet::stereo(), true)
          .withOutput ("Choir",      juce::AudioChannelSet::stereo(), false)
          .withOutput ("Atmosphere", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();   // per-voice RNG seeding lives in the module
}

// Factory presets: real parameter values (0..1 macros, choice index, dB, cents/ms). Only
// listed params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariGlassChoirAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Natural Double",     PV{ {"character", 0.15f}, {"mode", 0}, {"voiceCount", 0}, {"detuneSpread", 8}, {"timing", 12}, {"humanize", 0.25f}, {"reverbMix", 0.20f}, {"width", 1.1f}, {"harmonyOn", 0} });
    presetBank.addFactoryPreset ("Tight Ensemble",     PV{ {"character", 0.35f}, {"mode", 1}, {"voiceCount", 1}, {"detuneSpread", 12}, {"timing", 16}, {"humanize", 0.35f}, {"reverbMix", 0.30f}, {"width", 1.3f}, {"harmonyOn", 0} });
    presetBank.addFactoryPreset ("Massive Choir",      PV{ {"character", 0.60f}, {"mode", 1}, {"voiceCount", 2}, {"detuneSpread", 20}, {"timing", 24}, {"humanize", 0.50f}, {"reverbMix", 0.40f}, {"width", 1.5f}, {"harmonyOn", 1}, {"harmonyMix", 0.50f} });
    presetBank.addFactoryPreset ("Glass Cathedral",    PV{ {"character", 0.70f}, {"mode", 2}, {"voiceCount", 2}, {"detuneSpread", 28}, {"timing", 30}, {"humanize", 0.60f}, {"reverbMix", 0.55f}, {"reverbSize", 0.85f}, {"width", 1.6f}, {"trailsOn", 1}, {"trailsMix", 0.35f} });
    presetBank.addFactoryPreset ("Destroyed Angel",    PV{ {"character", 0.85f}, {"mode", 2}, {"voiceCount", 2}, {"destroyOn", 1}, {"destroyMix", 0.50f}, {"drive", 0.60f}, {"crush", 0.40f}, {"reverbMix", 0.50f}, {"width", 1.6f} });
    presetBank.addFactoryPreset ("Key-Locked Harmony", PV{ {"character", 0.40f}, {"mode", 1}, {"voiceCount", 1}, {"harmonyOn", 1}, {"harmonyMix", 0.60f}, {"followKey", 1}, {"reverbMix", 0.35f}, {"width", 1.3f} });
    presetBank.addFactoryPreset ("Airy Whisper Pad",   PV{ {"character", 0.30f}, {"mode", 0}, {"voiceCount", 1}, {"detuneSpread", 10}, {"timing", 14}, {"reverbMix", 0.45f}, {"reverbSize", 0.75f}, {"duckAmount", 0.70f}, {"width", 1.4f} });
}

// -- Parameter layout (full v1 list per spec) ---------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout
SattariGlassChoirAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::GlassChoirModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

void SattariGlassChoirAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    const int block = juce::jmax (1, samplesPerBlock);
    module.link (apvts, "");
    module.prepare (sr, block, getTotalNumInputChannels());

    // Stem scratch for the Choir / Atmosphere buses. Allocated here so the audio
    // thread never does; the module writes into these and we copy them out.
    choirOut.setSize (2, block);
    atmosOut.setSize (2, block);
    module.setAuxOutputs (&choirOut, &atmosOut);

    setLatencySamples (module.latencySamples());
}

void SattariGlassChoirAudioProcessor::releaseResources() {}

bool SattariGlassChoirAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Input: mono or stereo (or disabled — some hosts probe with no input).
    const auto& in = layouts.getMainInputChannelSet();
    if (! in.isDisabled()
        && in != juce::AudioChannelSet::mono()
        && in != juce::AudioChannelSet::stereo())
        return false;

    // Aux output buses (Choir, Atmosphere): each disabled OR stereo.
    for (int b = 1; b < layouts.outputBuses.size(); ++b)
    {
        const auto& aux = layouts.getChannelSet (false, b);
        if (! aux.isDisabled() && aux != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SattariGlassChoirAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // Hand the module only the LEAD bus; it writes the other two stems into our
    // scratch buffers, which we then copy out to the Choir / Atmosphere buses.
    // A rack slot skips all of this and gets the folded mix instead.
    auto leadBus = getBusBuffer (buffer, false, 0);

    const bool choirOn = getBusCount (false) > 1 && getBus (false, 1) != nullptr && getBus (false, 1)->isEnabled();
    const bool atmosOn = getBusCount (false) > 2 && getBus (false, 2) != nullptr && getBus (false, 2)->isEnabled();
    module.setAuxOutputs (choirOn ? &choirOut : nullptr,
                          atmosOn ? &atmosOut : nullptr);

    module.process (leadBus, midi);

    if (choirOn)
    {
        auto cb = getBusBuffer (buffer, false, 1);
        for (int ch = 0; ch < juce::jmin (cb.getNumChannels(), choirOut.getNumChannels()); ++ch)
            cb.copyFrom (ch, 0, choirOut, ch, 0, juce::jmin (n, choirOut.getNumSamples()));
    }
    if (atmosOn)
    {
        auto ab = getBusBuffer (buffer, false, 2);
        for (int ch = 0; ch < juce::jmin (ab.getNumChannels(), atmosOut.getNumChannels()); ++ch)
            ab.copyFrom (ch, 0, atmosOut, ch, 0, juce::jmin (n, atmosOut.getNumSamples()));
    }

    // Lookahead-free, but the engine latency can change with engine quality.
    const int lat = module.latencySamples();
    if (lat != reportedLatencyPublished)
    {
        reportedLatencyPublished = lat;
        setLatencySamples (lat);
    }
}

void SattariGlassChoirAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariGlassChoirAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariGlassChoirAudioProcessor::createEditor()
{
    return new SattariGlassChoirAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariGlassChoirAudioProcessor();
}
