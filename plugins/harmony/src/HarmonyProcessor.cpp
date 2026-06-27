#include "HarmonyProcessor.h"
#include "HarmonyEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ---- shared choice lists ----------------------------------------------------
juce::StringArray SattariHarmonyAudioProcessor::rootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}
juce::StringArray SattariHarmonyAudioProcessor::scaleChoices()
{
    return { "Major", "Minor", "Dorian", "Mixolydian", "Harmonic Minor",
             "Major Pentatonic", "Minor Pentatonic" };
}
juce::StringArray SattariHarmonyAudioProcessor::intervalChoices()
{
    return { "Octave Below", "6th Below", "5th Below", "4th Below", "3rd Below",
             "Unison", "3rd Above", "4th Above", "5th Above", "6th Above", "Octave Above" };
}
int SattariHarmonyAudioProcessor::intervalDegrees (int choiceIndex) noexcept
{
    static constexpr int degrees[] = { -7, -5, -4, -3, -2, 0, 2, 3, 4, 5, 7 };
    return juce::isPositiveAndBelow (choiceIndex, (int) std::size (degrees)) ? degrees[choiceIndex] : 0;
}

// ---- parameter layout -------------------------------------------------------
APVTS::ParameterLayout SattariHarmonyAudioProcessor::createParameterLayout()
{
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "input",  1 }, "Input",  juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "mix",    1 }, "Harmonies", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f));
    // Auto-Tune amount: blends each voice between "transpose the lead's actual
    // pitch by the interval" (0, keeps the singer's expression/detune) and "snap
    // the voice onto the exact scale note" (1, the Auto Pitch hard-tune feel).
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "tune",   1 }, "Auto-Tune", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.85f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "spread", 1 }, "Spread",    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.6f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "humanize",1 }, "Humanize", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));

    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "followKey", 1 }, "Follow Song Key", true));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "root",  1 }, "Root",  rootChoices(),  0));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "scale", 1 }, "Scale", scaleChoices(), 0));
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "lowLatency", 1 }, "Low Latency", false));

    // Per-voice. Sensible defaults give an instant 3-part choir (1+2+3 on).
    const int   defInterval[kNumVoices] = { 6, 8, 0, 8 };          // 3rd↑, 5th↑, Oct↓, 5th↑
    const bool  defOn      [kNumVoices] = { true, true, true, false };
    const float defLevel   [kNumVoices] = { -3.0f, -5.0f, -6.0f, -6.0f };
    const float defPan     [kNumVoices] = { -0.4f,  0.4f,  0.0f,  0.6f };
    const float defFormant [kNumVoices] = {  0.0f,  0.0f, -1.0f,  1.0f };

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto s  = juce::String (v + 1);
        const auto pfx = "Voice " + s + " ";
        p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "v" + s + "on",    1 }, pfx + "On", defOn[v]));
        p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "v" + s + "int",   1 }, pfx + "Interval", intervalChoices(), defInterval[v]));
        p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "v" + s + "level", 1 }, pfx + "Level", juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), defLevel[v]));
        p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "v" + s + "pan",   1 }, pfx + "Pan",   juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), defPan[v]));
        p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "v" + s + "formant",1 }, pfx + "Formant", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), defFormant[v]));
    }

    return { p.begin(), p.end() };
}

// ---- ctor -------------------------------------------------------------------
SattariHarmonyAudioProcessor::SattariHarmonyAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        snapVoiceMidi[v].store (-1);
        snapVoiceOn[v].store (false);
    }
}

// ---- prepare ----------------------------------------------------------------
void SattariHarmonyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (32, samplesPerBlock);

    pitchDetector.prepare (currentSampleRate, 70.0f, 1200.0f);
    analysisHop = juce::jmax (256, (int) (currentSampleRate * 0.011));  // ~11 ms hops
    analysisCounter = 0;
    leadMidiLive = -1;
    voicedLive = false;
    voicedGain = 0.0;
    leadHzSmoothed = 0.0f;
    smoothedRatio.fill (1.0);
    humanizeCents.fill (0.0);
    humanizeTarget.fill (0.0);

    monoScratch.assign ((size_t) preparedBlockSize, 0.0f);
    voiceBuffer.setSize (1, preparedBlockSize);
    harmonyAccum.setSize (2, preparedBlockSize);

    juce::dsp::ProcessSpec spec { currentSampleRate, (juce::uint32) preparedBlockSize, 2 };
    dryDelay.prepare (spec);

    int startDelay = 0;
#if SATTARI_HARMONY_HAS_RUBBERBAND
    const bool lowLat = param ("lowLatency") > 0.5f;
    const auto opts = RubberBand::RubberBandStretcher::OptionProcessRealTime
                    | RubberBand::RubberBandStretcher::OptionTransientsMixed
                    | RubberBand::RubberBandStretcher::OptionDetectorSoft
                    | (lowLat ? RubberBand::RubberBandStretcher::OptionWindowShort
                              : RubberBand::RubberBandStretcher::OptionPitchHighQuality);
    for (int v = 0; v < kNumVoices; ++v)
    {
        stretchers[v] = std::make_unique<RubberBand::RubberBandStretcher> (
            currentSampleRate, 1, opts, 1.0, 1.0);
        stretchers[v]->setMaxProcessSize ((size_t) preparedBlockSize);
        stretchers[v]->setPitchScale (1.0);
    }
    startDelay = (int) stretchers[0]->getStartDelay();
#endif

    reportedLatency = startDelay;
    dryDelay.setMaximumDelayInSamples (juce::jmax (1, startDelay + 1));
    dryDelay.setDelay ((float) startDelay);
    setLatencySamples (reportedLatency);

    sessionReader.start();
}

void SattariHarmonyAudioProcessor::releaseResources()
{
    sessionReader.stop();
#if SATTARI_HARMONY_HAS_RUBBERBAND
    for (auto& s : stretchers) s.reset();
#endif
    dryDelay.reset();
}

bool SattariHarmonyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// ---- key resolution ---------------------------------------------------------
void SattariHarmonyAudioProcessor::resolveKey (int& root, sattari::dsp::ScaleId& scale,
                                               int& scaleChoiceOut, bool& fromBus) const
{
    fromBus = false;
    if (param ("followKey") > 0.5f)
    {
        const auto snap = sessionReader.current();
        if (snap.valid)
        {
            root  = juce::jlimit (0, 11, snap.root);
            scale = sattari::harmony::scaleIdFromBus (snap.scale);
            scaleChoiceOut = (snap.scale == 1) ? 1 : 0;   // minor : major
            fromBus = true;
            return;
        }
    }
    root  = (int) std::round (param ("root"));
    scaleChoiceOut = (int) std::round (param ("scale"));
    scale = sattari::harmony::scaleIdFromChoice (scaleChoiceOut);
}

// ---- equal-power pan --------------------------------------------------------
static inline void panGains (float pan, float& l, float& r) noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
    const float a = t * juce::MathConstants<float>::halfPi;
    l = std::cos (a);
    r = std::sin (a);
}

// ---- process ----------------------------------------------------------------
void SattariHarmonyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0) return;

    const float inGain = juce::Decibels::decibelsToGain (param ("input"));
    buffer.applyGain (inGain);

    inputMeter.store (buffer.getMagnitude (0, numSamples));

    // Mono sum of the (input-gained) signal for detection + shifting.
    if ((int) monoScratch.size() < numSamples) monoScratch.resize ((size_t) numSamples);
    std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i) monoScratch[(size_t) i] += in[i] / (float) numChannels;
    }

    // Pitch tracking on a hop schedule.
    pitchDetector.pushBlock (monoScratch.data(), numSamples);
    analysisCounter += numSamples;
    if (analysisCounter >= analysisHop)
    {
        analysisCounter = 0;
        const auto r = pitchDetector.analyse (0.3f);
        voicedLive = r.voiced;
        if (r.voiced)
        {
            // Octave-continuity guard (shared with Auto Pitch / Sub Conjurer):
            // fold a fresh estimate that leaps more than ~a fifth back toward the
            // running pitch, so the harmonies never octave-jump on a misdetection.
            float hz = r.frequencyHz;
            if (leadHzSmoothed > 0.0f)
                hz = sattari::dsp::foldToOctaveNear (hz, leadHzSmoothed);
            leadHzSmoothed = leadHzSmoothed > 0.0f ? leadHzSmoothed + 0.5f * (hz - leadHzSmoothed) : hz;
            leadMidiLive = sattari::dsp::nearestMidiNote (leadHzSmoothed);
        }
        // Re-roll humanise targets each hop.
        const float hum = param ("humanize");
        for (int v = 0; v < kNumVoices; ++v)
            humanizeTarget[(size_t) v] = (humanizeRng.nextFloat() * 2.0f - 1.0f) * 22.0f * hum;
    }

    int root = 0, scaleChoice = 0; bool fromBus = false;
    sattari::dsp::ScaleId scale = sattari::dsp::ScaleId::Major;
    resolveKey (root, scale, scaleChoice, fromBus);

    const float spread    = param ("spread");
    const float tuneAmt   = param ("tune");
    const bool  haveLead  = voicedLive && leadMidiLive >= 0 && leadHzSmoothed > 0.0f;

    // Voiced gate: fast in, slow out, so harmonies bloom on the note and tail off.
    const double gateTarget = haveLead ? 1.0 : 0.0;
    voicedGain += (gateTarget - voicedGain) * (gateTarget > voicedGain ? 0.4 : 0.05);

    harmonyAccum.setSize (2, numSamples, false, false, true);
    harmonyAccum.clear();

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto s = juce::String (v + 1);
        const bool on = param (("v" + s + "on").toRawUTF8()) > 0.5f;
        snapVoiceOn[v].store (on);

        // Target pitch ratio for this voice (only re-aimed when we have a lead).
        if (haveLead)
        {
            const int deg = intervalDegrees ((int) std::round (param (("v" + s + "int").toRawUTF8())));
            const int harmonyMidi = sattari::harmony::diatonicShift (leadMidiLive, root, scale, deg);
            snapVoiceMidi[v].store (harmonyMidi);

            // Two ways to place the voice, blended by Auto-Tune amount:
            //  * transpose : keep the singer's real pitch (incl. detune/vibrato),
            //                moved by the diatonic interval.
            //  * hard-tune : land exactly on the scale note's frequency — the
            //                Auto Pitch correction feel applied to the harmony.
            const double targetHz       = (double) sattari::dsp::midiToHz ((float) harmonyMidi);
            const double ratioTranspose = std::pow (2.0, (double) (harmonyMidi - leadMidiLive) / 12.0);
            const double ratioHardTune  = targetHz / (double) leadHzSmoothed;
            const double t = (double) tuneAmt;
            double ratio = std::pow (ratioTranspose, 1.0 - t) * std::pow (ratioHardTune, t);

            // Humanise: a slow per-voice cents walk so the choir isn't robotically
            // unison-locked (the same idea as Auto Pitch's humanise).
            humanizeCents[(size_t) v] += (humanizeTarget[(size_t) v] - humanizeCents[(size_t) v]) * 0.05;
            ratio *= std::pow (2.0, humanizeCents[(size_t) v] / 1200.0);

            smoothedRatio[(size_t) v] += (ratio - smoothedRatio[(size_t) v]) * 0.2;
        }

        if (! on)
            continue;

#if SATTARI_HARMONY_HAS_RUBBERBAND
        auto* st = stretchers[(size_t) v].get();
        if (st == nullptr) continue;

        const double formantSemis = (double) param (("v" + s + "formant").toRawUTF8());
        const double formantScale = std::abs (formantSemis) < 1.0e-3 ? 0.0 : std::pow (2.0, formantSemis / 12.0);
        st->setFormantOption (RubberBand::RubberBandStretcher::OptionFormantPreserved);
        st->setFormantScale (formantScale);
        st->setPitchScale (juce::jlimit (0.25, 4.0, smoothedRatio[(size_t) v]));

        voiceBuffer.setSize (1, numSamples, false, false, true);
        voiceBuffer.clear();
        const float* in = monoScratch.data();
        st->process (&in, (size_t) numSamples, false);
        int written = 0;
        while (written < numSamples && st->available() > 0)
        {
            const int count = juce::jmin (numSamples - written, (int) st->available());
            float* out = voiceBuffer.getWritePointer (0, written);
            st->retrieve (&out, (size_t) count);
            written += count;
        }

        // Place the voice in the stereo field (pan widened by Spread).
        const float basePan = juce::jlimit (-1.0f, 1.0f, param (("v" + s + "pan").toRawUTF8()) * (0.4f + 0.6f * spread));
        float gL, gR; panGains (basePan, gL, gR);
        const float lvl = juce::Decibels::decibelsToGain (param (("v" + s + "level").toRawUTF8())) * (float) voicedGain;
        const float* vb = voiceBuffer.getReadPointer (0);
        harmonyAccum.addFrom (0, 0, vb, numSamples, gL * lvl);
        harmonyAccum.addFrom (1, 0, vb, numSamples, gR * lvl);
#endif
    }

    // Delay the dry lead to line up with the shifters' latency, then blend.
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    dryDelay.process (ctx);

    const float harmGain = param ("mix");
    if (numChannels >= 2)
    {
        buffer.addFrom (0, 0, harmonyAccum, 0, 0, numSamples, harmGain);
        buffer.addFrom (1, 0, harmonyAccum, 1, 0, numSamples, harmGain);
    }
    else
    {
        // Mono out: fold the stereo harmony sum down.
        buffer.addFrom (0, 0, harmonyAccum, 0, 0, numSamples, harmGain * 0.5f);
        buffer.addFrom (0, 0, harmonyAccum, 1, 0, numSamples, harmGain * 0.5f);
    }

    buffer.applyGain (juce::Decibels::decibelsToGain (param ("output")));
    outputMeter.store (buffer.getMagnitude (0, numSamples));

    // Publish live readout for the editor.
    snapLeadMidi.store (haveLead ? leadMidiLive : -1);
    snapVoiced.store (haveLead);
    snapRoot.store (root);
    snapScale.store (scaleChoice);
    snapBus.store (fromBus);
}

SattariHarmonyAudioProcessor::Snapshot SattariHarmonyAudioProcessor::getSnapshot() const noexcept
{
    Snapshot s;
    s.voiced       = snapVoiced.load();
    s.leadMidi     = snapLeadMidi.load();
    s.root         = snapRoot.load();
    s.scaleChoice  = snapScale.load();
    s.followingBus = snapBus.load();
    for (int v = 0; v < kNumVoices; ++v)
    {
        s.voiceMidi[(size_t) v] = snapVoiceMidi[v].load();
        s.voiceOn[(size_t) v]   = snapVoiceOn[v].load();
    }
    return s;
}

void SattariHarmonyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariHarmonyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariHarmonyAudioProcessor::createEditor()
{
    return new SattariHarmonyAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariHarmonyAudioProcessor();
}
