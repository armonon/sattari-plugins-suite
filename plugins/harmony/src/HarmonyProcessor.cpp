#include "HarmonyProcessor.h"
#include "HarmonyEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
constexpr const char* kVoiceOnIds[SattariHarmonyAudioProcessor::kNumVoices]      = { "v1on", "v2on", "v3on", "v4on", "v5on", "v6on", "v7on", "v8on" };
constexpr const char* kVoiceIntervalIds[SattariHarmonyAudioProcessor::kNumVoices]= { "v1int", "v2int", "v3int", "v4int", "v5int", "v6int", "v7int", "v8int" };
constexpr const char* kVoiceLevelIds[SattariHarmonyAudioProcessor::kNumVoices]   = { "v1level", "v2level", "v3level", "v4level", "v5level", "v6level", "v7level", "v8level" };
constexpr const char* kVoicePanIds[SattariHarmonyAudioProcessor::kNumVoices]     = { "v1pan", "v2pan", "v3pan", "v4pan", "v5pan", "v6pan", "v7pan", "v8pan" };
constexpr const char* kVoiceFormantIds[SattariHarmonyAudioProcessor::kNumVoices] = { "v1formant", "v2formant", "v3formant", "v4formant", "v5formant", "v6formant", "v7formant", "v8formant" };
constexpr const char* kVoiceOctaveIds[SattariHarmonyAudioProcessor::kNumVoices]  = { "v1oct", "v2oct", "v3oct", "v4oct", "v5oct", "v6oct", "v7oct", "v8oct" };
}

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
juce::StringArray SattariHarmonyAudioProcessor::sourceChoices()
{
    return { "Scale", "Chord (MIDI)" };
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
    // Choir: a chorus-modulated double of each voice, panned opposite, for a
    // wider/lusher ensemble out of the same voices.
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "choir",  1 }, "Choir",    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "humanize",1 }, "Humanize", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));

    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "source", 1 }, "Harmonise To", sourceChoices(), 0));
    // Default OFF: the manual root/scale menu is authoritative out of the box.
    // Follow Song Key is an explicit opt-in for the whole-suite workflow (Sattari
    // Key on the master drives every plugin); when it's on, the editor disables +
    // annotates the menu so the bus override is never silent.
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "followKey", 1 }, "Follow Song Key", false));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "root",  1 }, "Root",  rootChoices(),  0));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "scale", 1 }, "Scale", scaleChoices(), 0));
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "lowLatency", 1 }, "Low Latency", false));

    // Per-voice. Defaults keep the out-of-box behaviour a 3-part choir (v1+2+3 on);
    // voices 5-8 are the Pro extension — present but off until dialled in.
    const int   defInterval[kNumVoices] = { 6, 8, 0, 8, 9, 4, 6, 8 };   // 3rd↑ 5th↑ Oct↓ 5th↑ | 6th↑ 2nd↑ 3rd↑ 5th↑
    const bool  defOn      [kNumVoices] = { true, true, true, false, false, false, false, false };
    const float defLevel   [kNumVoices] = { -3.0f, -5.0f, -6.0f, -6.0f, -7.0f, -7.0f, -8.0f, -8.0f };
    const float defPan     [kNumVoices] = { -0.4f,  0.4f,  0.0f,  0.6f, -0.7f,  0.7f, -0.55f, 0.55f };
    const float defFormant [kNumVoices] = {  0.0f,  0.0f, -1.0f,  1.0f, -2.0f,  2.0f, -1.5f, 1.5f };
    const int   defOctave  [kNumVoices] = {  0, 0, 0, 0, 0, 1, -1, 0 };

    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto s  = juce::String (v + 1);
        const auto pfx = "Voice " + s + " ";
        p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "v" + s + "on",    1 }, pfx + "On", defOn[v]));
        p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "v" + s + "int",   1 }, pfx + "Interval", intervalChoices(), defInterval[v]));
        p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "v" + s + "oct", 1 }, pfx + "Octave", -2, 2, defOctave[v]));
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
    registerFactoryPresets();
}

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Presets shape the voicing (which voices are on and
// their intervals) plus the ensemble macros; the key/scale menu is left untouched
// so harmonies still follow whatever key the user (or the song bus) has set.
void SattariHarmonyAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // Voice interval choice indices: 0=Oct↓ 1=6↓ 2=5↓ 3=4↓ 4=3↓ 5=Uni 6=3↑ 7=4↑ 8=5↑ 9=6↑ 10=Oct↑
    presetBank.addFactoryPreset ("Third Above",
        PV{ {"mix", 0.65f}, {"tune", 0.85f}, {"spread", 0.4f}, {"choir", 0.0f}, {"humanize", 0.25f},
            {"v1on", 1}, {"v1int", 6}, {"v2on", 0}, {"v3on", 0} });

    presetBank.addFactoryPreset ("Third & Fifth",
        PV{ {"mix", 0.7f}, {"tune", 0.85f}, {"spread", 0.55f}, {"choir", 0.1f}, {"humanize", 0.3f},
            {"v1on", 1}, {"v1int", 6}, {"v2on", 1}, {"v2int", 8}, {"v3on", 0} });

    presetBank.addFactoryPreset ("Octave Doubler",
        PV{ {"mix", 0.6f}, {"tune", 0.9f}, {"spread", 0.5f}, {"choir", 0.0f}, {"humanize", 0.15f},
            {"v1on", 1}, {"v1int", 10}, {"v2on", 1}, {"v2int", 0}, {"v3on", 0} });

    presetBank.addFactoryPreset ("Low Harmony",
        PV{ {"mix", 0.7f}, {"tune", 0.8f}, {"spread", 0.45f}, {"choir", 0.1f}, {"humanize", 0.3f},
            {"v1on", 1}, {"v1int", 4}, {"v2on", 1}, {"v2int", 2}, {"v3on", 0} });

    presetBank.addFactoryPreset ("Wide Choir",
        PV{ {"mix", 0.8f}, {"tune", 0.75f}, {"spread", 0.85f}, {"choir", 0.6f}, {"humanize", 0.45f},
            {"v1on", 1}, {"v1int", 6}, {"v2on", 1}, {"v2int", 8}, {"v3on", 1}, {"v3int", 0} });

    presetBank.addFactoryPreset ("Full Ensemble",
        PV{ {"mix", 0.85f}, {"tune", 0.7f}, {"spread", 0.9f}, {"choir", 0.5f}, {"humanize", 0.5f},
            {"v1on", 1}, {"v1int", 6}, {"v2on", 1}, {"v2int", 8}, {"v3on", 1}, {"v3int", 10},
            {"v4on", 1}, {"v4int", 4}, {"v5on", 1}, {"v5int", 9} });

    presetBank.addFactoryPreset ("Hard Tune Stack",
        PV{ {"mix", 0.75f}, {"tune", 1.0f}, {"spread", 0.6f}, {"choir", 0.0f}, {"humanize", 0.0f},
            {"v1on", 1}, {"v1int", 6}, {"v2on", 1}, {"v2int", 8}, {"v3on", 1}, {"v3int", 10} });
}

// ---- prepare ----------------------------------------------------------------
void SattariHarmonyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (kSafetyBlockSize, juce::jmax (32, samplesPerBlock));

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

    const int choirLen = (int) (currentSampleRate * 0.05) + 4;   // up to 50 ms of doubler delay
    for (auto& ring : choirRing) ring.assign ((size_t) choirLen, 0.0f);
    choirWrite.fill (0);
    choirPhase.fill (0.0);

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

    // Track the held chord (Chord mode follows the notes on the MIDI/chord track).
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            bool alreadyHeld = false;
            for (int i = 0; i < heldNoteCount; ++i)
                alreadyHeld = alreadyHeld || heldNotes[(size_t) i] == n;
            if (! alreadyHeld && heldNoteCount < kMaxHeldChordNotes)
                heldNotes[(size_t) heldNoteCount++] = n;
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            for (int i = 0; i < heldNoteCount;)
            {
                if (heldNotes[(size_t) i] == note)
                {
                    for (int j = i; j + 1 < heldNoteCount; ++j)
                        heldNotes[(size_t) j] = heldNotes[(size_t) (j + 1)];
                    --heldNoteCount;
                }
                else
                {
                    ++i;
                }
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            heldNoteCount = 0;
    }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0) return;

    const float inGain = juce::Decibels::decibelsToGain (param ("input"));
    const float outGain = juce::Decibels::decibelsToGain (param ("output"));
    if (numSamples > preparedBlockSize)
    {
        buffer.applyGain (inGain * outGain);
        const float mag = buffer.getMagnitude (0, numSamples);
        inputMeter.store (mag);
        outputMeter.store (mag);
        return;
    }

    buffer.applyGain (inGain);

    inputMeter.store (buffer.getMagnitude (0, numSamples));

    // Mono sum of the (input-gained) signal for detection + shifting.
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

    // Chord mode: voice each harmony to the nearest tone of the held chord.
    const bool chordMode = ((int) std::round (param ("source")) == 1) && heldNoteCount > 0;
    chordPcCount = 0;
    if (chordMode)
    {
        for (int i = 0; i < heldNoteCount; ++i)
            chordPcs[(size_t) chordPcCount++] = ((heldNotes[(size_t) i] % 12) + 12) % 12;
    }

    // Voiced gate: fast in, slow out, so harmonies bloom on the note and tail off.
    const double gateTarget = haveLead ? 1.0 : 0.0;
    voicedGain += (gateTarget - voicedGain) * (gateTarget > voicedGain ? 0.4 : 0.05);

    harmonyAccum.clear (0, 0, numSamples);
    harmonyAccum.clear (1, 0, numSamples);

    for (int v = 0; v < kNumVoices; ++v)
    {
        const bool on = param (kVoiceOnIds[(size_t) v]) > 0.5f;
        snapVoiceOn[v].store (on);

        // Target pitch ratio for this voice (only re-aimed when we have a lead).
        if (haveLead)
        {
            const int deg = intervalDegrees ((int) std::round (param (kVoiceIntervalIds[(size_t) v])));
            int harmonyMidi = sattari::harmony::diatonicShift (leadMidiLive, root, scale, deg);
            harmonyMidi += 12 * (int) std::round (param (kVoiceOctaveIds[(size_t) v]));   // Pro: per-voice octave
            harmonyMidi = juce::jlimit (0, 127, harmonyMidi);
            if (chordMode)
                harmonyMidi = sattari::harmony::nearestChordTone (harmonyMidi, chordPcs.data(), chordPcCount);
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

        const double formantSemis = (double) param (kVoiceFormantIds[(size_t) v]);
        const double formantScale = std::abs (formantSemis) < 1.0e-3 ? 0.0 : std::pow (2.0, formantSemis / 12.0);
        st->setFormantOption (RubberBand::RubberBandStretcher::OptionFormantPreserved);
        st->setFormantScale (formantScale);
        st->setPitchScale (juce::jlimit (0.25, 4.0, smoothedRatio[(size_t) v]));

        voiceBuffer.clear (0, 0, numSamples);
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
        const float basePan = juce::jlimit (-1.0f, 1.0f, param (kVoicePanIds[(size_t) v]) * (0.4f + 0.6f * spread));
        float gL, gR; panGains (basePan, gL, gR);
        const float lvl = juce::Decibels::decibelsToGain (param (kVoiceLevelIds[(size_t) v])) * (float) voicedGain;
        const float* vb = voiceBuffer.getReadPointer (0);
        harmonyAccum.addFrom (0, 0, vb, numSamples, gL * lvl);
        harmonyAccum.addFrom (1, 0, vb, numSamples, gR * lvl);

        // Choir doubler: a slow chorus-modulated Haas copy panned opposite the
        // voice, so the few voices read as a wide ensemble (no extra shifter).
        const float choir = param ("choir");
        auto& ring = choirRing[(size_t) v];
        const int rsize = (int) ring.size();
        if (choir > 0.001f && rsize > 4)
        {
            const int    haas   = juce::jlimit (1, rsize - 2, (int) ((0.013 + 0.006 * v) * currentSampleRate));
            const float  depth  = (float) (0.002 * currentSampleRate);
            const double twoPi  = juce::MathConstants<double>::twoPi;
            const double lfoInc = (0.6 + 0.25 * v) / currentSampleRate * twoPi;
            float oGL, oGR; panGains (-basePan, oGL, oGR);
            const float cg = lvl * choir * 0.8f;
            int    w  = choirWrite[(size_t) v];
            double ph = choirPhase[(size_t) v];
            for (int i = 0; i < numSamples; ++i)
            {
                ring[(size_t) w] = vb[i];
                double rpos = (double) w - ((double) haas + (double) depth * std::sin (ph));
                while (rpos < 0.0) rpos += rsize;
                const int   r0 = (int) rpos;
                const int   r1 = (r0 + 1) % rsize;
                const float fr = (float) (rpos - r0);
                const float d  = ring[(size_t) r0] * (1.0f - fr) + ring[(size_t) r1] * fr;
                harmonyAccum.addSample (0, i, d * cg * oGL);
                harmonyAccum.addSample (1, i, d * cg * oGR);
                ph += lfoInc; if (ph > twoPi) ph -= twoPi;
                if (++w >= rsize) w = 0;
            }
            choirWrite[(size_t) v] = w;
            choirPhase[(size_t) v] = ph;
        }
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

    buffer.applyGain (outGain);
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
