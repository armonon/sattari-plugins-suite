#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                       const juce::String& name,
                                                       float min,
                                                       float max,
                                                       float def,
                                                       const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, 0.001f);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

float dbToGain (float db)
{
    return juce::Decibels::decibelsToGain (db, -120.0f);
}
}

SideChainMasterAudioProcessor::SideChainMasterAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SideChainMasterAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("mode", 1), "Mode",
        juce::StringArray { "Clean", "Pump", "Tight", "Smooth", "Creative" }, 1));
    params.push_back (floatParam ("threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB"));
    params.push_back (floatParam ("ratio", "Ratio", 1.0f, 20.0f, 6.0f, ":1"));
    params.push_back (floatParam ("attack", "Attack", 0.1f, 80.0f, 3.0f, "ms"));
    params.push_back (floatParam ("release", "Release", 20.0f, 1000.0f, 180.0f, "ms"));
    params.push_back (floatParam ("depth", "Duck Depth", 0.0f, 36.0f, 12.0f, "dB"));
    params.push_back (floatParam ("makeup", "Makeup", -12.0f, 12.0f, 0.0f, "dB"));
    params.push_back (floatParam ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%"));
    params.push_back (floatParam ("sidechainHpf", "Sidechain HPF", 20.0f, 250.0f, 65.0f, "Hz"));
    params.push_back (floatParam ("lookahead", "Lookahead", 0.0f, 10.0f, 0.0f, "ms"));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("external", 1), "Use External Sidechain", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("listen", 1), "Solo Detector Listen", false));

    // Tempo-synced "volume shaper" ducking: a host-locked curve ducks the signal
    // on the beat with no sidechain needed — the classic EDM pump.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("syncDuck", 1), "Tempo Sync Duck", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("syncRate", 1), "Sync Rate",
        juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }, 2));
    params.push_back (floatParam ("syncCurve", "Sync Curve", 0.2f, 4.0f, 1.0f));

    return { params.begin(), params.end() };
}

void SideChainMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    smoothedGainReductionDb = 0.0f;
    // The detector's own follower runs at fixed-ish times (~1.5 ms attack, 45 ms
    // release) shared by every mode, so prepare picks the times once. The GR
    // smoother uses the user attack/release, set per-block below.
    detectorFollower.prepare (currentSampleRate, 1.5f, 45.0f);
    grSmoother.prepare (currentSampleRate);
    hpX1 = 0.0f;
    hpY1 = 0.0f;
    syncPhase = 0.0;
    lastGainReductionDb.store (0.0f);
    lastExternalSidechainAvailable.store (false);

    // Lookahead delay line sized for the 10 ms maximum (stereo, allocation-free after this).
    maxLookaheadSamples = (int) std::ceil (0.010 * currentSampleRate);
    lookaheadRingLen = maxLookaheadSamples + 1;
    lookaheadRing.setSize (2, lookaheadRingLen);
    lookaheadRing.clear();
    lookaheadRingPos = 0;
    reportedLatency = -1;   // force a setLatencySamples() on the first block
}

bool SideChainMasterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);

    if (mainIn != mainOut)
        return false;

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

float SideChainMasterAudioProcessor::getFloatParam (const char* id) const
{
    if (auto* value = apvts.getRawParameterValue (id))
        return value->load();
    return 0.0f;
}

int SideChainMasterAudioProcessor::getChoiceParam (const char* id) const
{
    return (int) std::round (getFloatParam (id));
}

bool SideChainMasterAudioProcessor::getBoolParam (const char* id) const
{
    return getFloatParam (id) > 0.5f;
}

float SideChainMasterAudioProcessor::applyDetectorHighPass (float input, float cutoffHz)
{
    cutoffHz = juce::jlimit (20.0f, 250.0f, cutoffHz);
    const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoffHz);
    const float dt = 1.0f / (float) currentSampleRate;
    const float alpha = rc / (rc + dt);
    const float y = alpha * (hpY1 + input - hpX1);
    hpX1 = input;
    hpY1 = y;
    return y;
}

float SideChainMasterAudioProcessor::nextDetectorSample (const juce::AudioBuffer<float>& detectorBuffer,
                                                         const juce::AudioBuffer<float>& fallbackBuffer,
                                                         int sampleIndex,
                                                         bool useExternal)
{
    const auto& source = useExternal ? detectorBuffer : fallbackBuffer;
    if (source.getNumChannels() == 0 || source.getNumSamples() <= sampleIndex)
        return 0.0f;

    float mono = 0.0f;
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
        mono += source.getSample (ch, sampleIndex);

    return mono / (float) source.getNumChannels();
}

void SideChainMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    auto mainInput = getBusBuffer (buffer, true, 0);
    auto mainOutput = getBusBuffer (buffer, false, 0);

    const bool sidechainBusUsable = getBusCount (true) > 1
        && getBus (true, 1) != nullptr
        && getBus (true, 1)->isEnabled();
    auto sidechainInput = sidechainBusUsable ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const bool useExternal = getBoolParam ("external") && sidechainBusUsable && sidechainInput.getNumChannels() > 0;
    lastExternalSidechainAvailable.store (useExternal);

    const int numSamples = mainOutput.getNumSamples();
    const int outputChannels = mainOutput.getNumChannels();
    const int inputChannels = mainInput.getNumChannels();

    for (int ch = 0; ch < outputChannels; ++ch)
    {
        if (ch < inputChannels)
            mainOutput.copyFrom (ch, 0, mainInput, ch, 0, numSamples);
        else
            mainOutput.clear (ch, 0, numSamples);
    }

    int mode = getChoiceParam ("mode");
    float thresholdDb = getFloatParam ("threshold");
    float ratio = juce::jmax (1.0f, getFloatParam ("ratio"));
    float attackMs = getFloatParam ("attack");
    float releaseMs = getFloatParam ("release");
    float depthDb = getFloatParam ("depth");
    float makeupDb = getFloatParam ("makeup");
    float mix = getFloatParam ("mix") * 0.01f;
    float hpfHz = getFloatParam ("sidechainHpf");
    const bool listen = getBoolParam ("listen");
    const float lookaheadMs = getFloatParam ("lookahead");
    const bool syncDuck = getBoolParam ("syncDuck");
    const int syncRate = getChoiceParam ("syncRate");
    const float syncCurve = juce::jmax (0.05f, getFloatParam ("syncCurve"));

    // Lookahead: delay the dry/wet path so the detector (reading the undelayed
    // sidechain) can react just before the transient. Report the latency, and
    // only re-notify the host when the integer sample count actually changes.
    const int lookaheadSamples = juce::jlimit (0, maxLookaheadSamples,
                                               (int) std::lround (lookaheadMs * 0.001 * currentSampleRate));
    if (lookaheadSamples != reportedLatency)
    {
        reportedLatency = lookaheadSamples;
        setLatencySamples (lookaheadSamples);
    }

    // Tempo-synced duck phase: lock to host PPQ while playing, else free-run on BPM.
    const auto t = sattari::host::read (*this);
    static const double rateQuarters[] = { 4.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 0.25 };
    const double cycleQuarters = rateQuarters[juce::jlimit (0, 5, syncRate)];
    const double cyclesPerSample = (t.bpm / 60.0) / currentSampleRate / cycleQuarters;
    if (syncDuck && t.isPlaying && t.hasPpq)
        syncPhase = std::fmod (t.ppqPosition / cycleQuarters, 1.0);
    const float syncDepthGain = dbToGain (-depthDb);

    if (mode == 1) { thresholdDb -= 3.0f; depthDb += 6.0f; releaseMs *= 0.75f; }      // Pump
    if (mode == 2) { attackMs *= 0.45f; releaseMs *= 0.55f; depthDb += 2.0f; }         // Tight
    if (mode == 3) { attackMs *= 1.8f; releaseMs *= 1.7f; depthDb *= 0.75f; }          // Smooth
    if (mode == 4) { ratio = juce::jmax (ratio, 10.0f); depthDb += 10.0f; mix *= 0.9f; } // Creative

    grSmoother.setTimes (attackMs, releaseMs);
    const float makeupGain = dbToGain (makeupDb);

    float grForMeter = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float rawDetector = nextDetectorSample (sidechainInput, mainInput, i, useExternal);
        const float filteredDetector = applyDetectorHighPass (rawDetector, hpfHz);
        const float detectorEnvelope = detectorFollower.processRectified (filteredDetector);

        float wetGain;
        if (syncDuck)
        {
            // Tempo-locked curve: full duck on the beat (phase 0), recovering to
            // unity by the next cycle; syncCurve shapes the recovery slope.
            const float shaped = std::pow ((float) syncPhase, syncCurve);
            const float reductionGain = syncDepthGain + (1.0f - syncDepthGain) * shaped;
            wetGain = juce::jlimit (0.0f, 4.0f, reductionGain * makeupGain);
            grForMeter = -juce::Decibels::gainToDecibels (reductionGain + 1.0e-9f, -120.0f);
            syncPhase += cyclesPerSample;
            if (syncPhase >= 1.0) syncPhase -= 1.0;
        }
        else
        {
            const float detectorDb = juce::Decibels::gainToDecibels (detectorEnvelope + 1.0e-9f, -120.0f);
            const float overDb = juce::jmax (0.0f, detectorDb - thresholdDb);
            const float compressedDb = overDb * (1.0f - (1.0f / ratio));
            const float targetReductionDb = -juce::jmin (depthDb, compressedDb);
            smoothedGainReductionDb = grSmoother.processFalling (targetReductionDb);
            wetGain = juce::jlimit (0.0f, 4.0f, dbToGain (smoothedGainReductionDb) * makeupGain);
            grForMeter = -smoothedGainReductionDb;
        }

        // Lookahead: write current sample, read the delayed one (delay 0 -> reads
        // what we just wrote, i.e. unity); both dry and wet use the delayed signal.
        const int readPos = (lookaheadRingPos - lookaheadSamples + lookaheadRingLen) % lookaheadRingLen;
        for (int ch = 0; ch < outputChannels; ++ch)
        {
            const int ringCh = juce::jmin (ch, lookaheadRing.getNumChannels() - 1);
            const float cur = mainOutput.getSample (ch, i);
            lookaheadRing.setSample (ringCh, lookaheadRingPos, cur);
            const float delayed = lookaheadRing.getSample (ringCh, readPos);
            const float wet = listen ? filteredDetector : delayed * wetGain;
            mainOutput.setSample (ch, i, juce::jlimit (-0.995f, 0.995f, delayed * (1.0f - mix) + wet * mix));
        }
        if (++lookaheadRingPos >= lookaheadRingLen) lookaheadRingPos = 0;
    }

    lastGainReductionDb.store (grForMeter);
}

juce::AudioProcessorEditor* SideChainMasterAudioProcessor::createEditor()
{
    return new SideChainMasterAudioProcessorEditor (*this);
}

void SideChainMasterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SideChainMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
    return new SideChainMasterAudioProcessor();
}
