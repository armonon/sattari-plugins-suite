#include "MaqamProcessor.h"
#include "MaqamEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::StringArray SattariMaqamAudioProcessor::maqamChoices()
{
    int n = 0; const auto* m = sattari::maqam::maqamat (n);
    juce::StringArray out;
    for (int i = 0; i < n; ++i) out.add (m[i].name);
    return out;
}
juce::StringArray SattariMaqamAudioProcessor::rootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

APVTS::ParameterLayout SattariMaqamAudioProcessor::createParameterLayout()
{
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "maqam", 1 }, "Maqam", maqamChoices(), 0));
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "followKey", 1 }, "Follow Song Key", true));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "root",  1 }, "Root", rootChoices(), 0));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "strength", 1 }, "Strength", unit(), 0.9f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "mix",      1 }, "Mix",      unit(), 1.0f));
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "formant",  1 }, "Preserve Formants", true));
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "lowLatency", 1 }, "Low Latency", false));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "input",  1 }, "Input",  juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    return { p.begin(), p.end() };
}

SattariMaqamAudioProcessor::SattariMaqamAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void SattariMaqamAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (32, samplesPerBlock);

    pitchDetector.prepare (currentSampleRate, 70.0f, 1200.0f);
    analysisHop = juce::jmax (256, (int) (currentSampleRate * 0.011));
    analysisCounter = 0;
    leadHzSmoothed = 0.0f;
    voicedLive = false;
    smoothedRatio = 1.0;

    const int numCh = juce::jmax (1, getTotalNumInputChannels());
    monoScratch.assign ((size_t) preparedBlockSize, 0.0f);
    wetBuffer.setSize (numCh, preparedBlockSize);

    juce::dsp::ProcessSpec spec { currentSampleRate, (juce::uint32) preparedBlockSize, (juce::uint32) numCh };
    dryDelay.prepare (spec);

    int startDelay = 0;
#if SATTARI_MAQAM_HAS_RUBBERBAND
    const bool lowLat = param ("lowLatency") > 0.5f;
    const auto opts = RubberBand::RubberBandStretcher::OptionProcessRealTime
                    | RubberBand::RubberBandStretcher::OptionTransientsMixed
                    | RubberBand::RubberBandStretcher::OptionDetectorSoft
                    | (lowLat ? RubberBand::RubberBandStretcher::OptionWindowShort
                              : RubberBand::RubberBandStretcher::OptionPitchHighQuality);
    stretchers.clear();
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto st = std::make_unique<RubberBand::RubberBandStretcher> (currentSampleRate, 1, opts, 1.0, 1.0);
        st->setMaxProcessSize ((size_t) preparedBlockSize);
        st->setPitchScale (1.0);
        stretchers.push_back (std::move (st));
    }
    startDelay = stretchers.empty() ? 0 : (int) stretchers.front()->getStartDelay();
#endif

    reportedLatency = startDelay;
    dryDelay.setMaximumDelayInSamples (juce::jmax (1, startDelay + 1));
    dryDelay.setDelay ((float) startDelay);
    setLatencySamples (reportedLatency);

    sessionReader.start();
}

void SattariMaqamAudioProcessor::releaseResources()
{
    sessionReader.stop();
#if SATTARI_MAQAM_HAS_RUBBERBAND
    stretchers.clear();
#endif
    dryDelay.reset();
}

bool SattariMaqamAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

int SattariMaqamAudioProcessor::resolveRoot (bool& fromBus) const
{
    fromBus = false;
    if (param ("followKey") > 0.5f)
    {
        const auto snap = sessionReader.current();
        if (snap.valid) { fromBus = true; return juce::jlimit (0, 11, snap.root); }
    }
    return (int) std::round (param ("root"));
}

void SattariMaqamAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0) return;

    buffer.applyGain (juce::Decibels::decibelsToGain (param ("input")));
    inputMeter.store (buffer.getMagnitude (0, numSamples));

    // Mono sum for detection.
    if ((int) monoScratch.size() < numSamples) monoScratch.resize ((size_t) numSamples);
    std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i) monoScratch[(size_t) i] += in[i] / (float) numChannels;
    }

    pitchDetector.pushBlock (monoScratch.data(), numSamples);
    analysisCounter += numSamples;
    if (analysisCounter >= analysisHop)
    {
        analysisCounter = 0;
        const auto r = pitchDetector.analyse (0.3f);
        voicedLive = r.voiced;
        if (r.voiced)
        {
            float hz = r.frequencyHz;
            if (leadHzSmoothed > 0.0f) hz = sattari::dsp::foldToOctaveNear (hz, leadHzSmoothed);
            leadHzSmoothed = leadHzSmoothed > 0.0f ? leadHzSmoothed + 0.5f * (hz - leadHzSmoothed) : hz;
        }
    }

    bool fromBus = false;
    const int  rootPc = resolveRoot (fromBus);
    const int  maqamIdx = (int) std::round (param ("maqam"));
    const auto& mq = sattari::maqam::maqam (maqamIdx);
    const float strength = param ("strength");

    double detectedMidi = 0.0, targetMidi = 0.0;
    const bool haveLead = voicedLive && leadHzSmoothed > 0.0f;
    if (haveLead)
    {
        detectedMidi = sattari::maqam::hzToMidi ((double) leadHzSmoothed);
        targetMidi   = sattari::maqam::snapMaqamMidi (detectedMidi, rootPc, mq);
        const double targetHz   = sattari::maqam::midiToHz (targetMidi);
        const double ratioFull  = targetHz / (double) leadHzSmoothed;
        const double ratio      = std::pow (ratioFull, (double) strength);   // strength scales the correction
        smoothedRatio += (ratio - smoothedRatio) * 0.25;
    }
    else
    {
        smoothedRatio += (1.0 - smoothedRatio) * 0.1;
    }

    // Render the corrected (wet) signal per channel.
    wetBuffer.setSize (numChannels, numSamples, false, false, true);
#if SATTARI_MAQAM_HAS_RUBBERBAND
    const bool preserve = param ("formant") > 0.5f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (ch >= (int) stretchers.size() || stretchers[(size_t) ch] == nullptr)
        {
            wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
            continue;
        }
        auto& st = *stretchers[(size_t) ch];
        st.setFormantOption (preserve ? RubberBand::RubberBandStretcher::OptionFormantPreserved
                                      : RubberBand::RubberBandStretcher::OptionFormantShifted);
        st.setPitchScale (juce::jlimit (0.5, 2.0, smoothedRatio));
        const float* in = buffer.getReadPointer (ch);
        st.process (&in, (size_t) numSamples, false);
        int written = 0;
        while (written < numSamples && st.available() > 0)
        {
            const int count = juce::jmin (numSamples - written, (int) st.available());
            float* out = wetBuffer.getWritePointer (ch, written);
            st.retrieve (&out, (size_t) count);
            written += count;
        }
        if (written < numSamples)
            wetBuffer.copyFrom (ch, written, buffer, ch, written, numSamples - written);
    }
#else
    for (int ch = 0; ch < numChannels; ++ch)
        wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
#endif

    // Delay the dry to the shifter latency, then blend dry/wet.
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    dryDelay.process (ctx);

    const float mix = param ("mix");
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, 1.0f - mix);
        buffer.addFrom (ch, 0, wetBuffer, ch, 0, numSamples, mix);
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (param ("output")));
    outputMeter.store (buffer.getMagnitude (0, numSamples));

    // Snapshot for the maqam ladder.
    auto wrap1200 = [] (double c) { c = std::fmod (c, 1200.0); return c < 0.0 ? c + 1200.0 : c; };
    snapVoiced.store (haveLead);
    snapBus.store (fromBus);
    snapRoot.store (rootPc);
    snapDetected.store  (haveLead ? (int) std::round (wrap1200 ((detectedMidi - rootPc) * 100.0)) : 0);
    snapCorrected.store (haveLead ? (int) std::round (wrap1200 ((targetMidi  - rootPc) * 100.0)) : 0);
}

SattariMaqamAudioProcessor::Snapshot SattariMaqamAudioProcessor::getSnapshot() const noexcept
{
    Snapshot s;
    s.voiced         = snapVoiced.load();
    s.detectedCents  = (double) snapDetected.load();
    s.correctedCents = (double) snapCorrected.load();
    s.maqamIndex     = (int) std::round (apvts.getRawParameterValue ("maqam")->load());
    s.rootPc         = snapRoot.load();
    s.followingBus   = snapBus.load();
    return s;
}

void SattariMaqamAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariMaqamAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariMaqamAudioProcessor::createEditor()
{
    return new SattariMaqamAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariMaqamAudioProcessor();
}
