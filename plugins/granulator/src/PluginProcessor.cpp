#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <memory>

using Engine = sattari::dsp::SattariGranularEngine;
using EngineParams = Engine::EngineParams;
using GranSource = sattari::granulator::SourceSample;
using DspSource = sattari::dsp::SourceSample;

// =============================================================================
//  Construction / parameter layout
// =============================================================================
SattariGranulatorAudioProcessor::SattariGranulatorAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    formats.registerBasicFormats();   // wav / aiff / flac / ogg / mp3 (platform dependent)
}

SattariGranulatorAudioProcessor::~SattariGranulatorAudioProcessor()
{
    sessionReader.stop();
    tempoReader.stop();
}

juce::StringArray SattariGranulatorAudioProcessor::keyRootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray SattariGranulatorAudioProcessor::keyScaleChoices()
{
    // Order matches scaleIdFromChoice below and the Arp/Harmony layout.
    return { "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" };
}

juce::StringArray SattariGranulatorAudioProcessor::divisionChoices()
{
    // Matches SattariGranularEngine::divisionBeats() (quarter = 1.0), Space-style.
    return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4 dot", "1/8", "1/8 dot", "1/8 trip", "1/16" };
}

sattari::dsp::ScaleId SattariGranulatorAudioProcessor::scaleIdFromChoice (int choice) noexcept
{
    using S = sattari::dsp::ScaleId;
    switch (choice)
    {
        case 0: return S::Major;
        case 1: return S::Minor;
        case 2: return S::PentaMaj;
        case 3: return S::PentaMin;
        case 4: return S::Dorian;
        case 5: return S::Mixolydian;
        case 6: return S::HarmMinor;
        default: return S::Major;
    }
}

sattari::dsp::ScaleId SattariGranulatorAudioProcessor::scaleIdFromBus (int busScale) noexcept
{
    // Bus scale codes: 0=major, 1=minor, 2=chromatic. Chromatic falls back to
    // Major so a key-snap still lands somewhere musical (matches Harmony).
    return busScale == 1 ? sattari::dsp::ScaleId::Minor : sattari::dsp::ScaleId::Major;
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariGranulatorAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    using IP = juce::AudioParameterInt;
    using BP = juce::AudioParameterBool;
    using CP = juce::AudioParameterChoice;
    using R  = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto skewRange = [] (float lo, float hi, float step, float centre)
    {
        R r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    };

    // --- Source region / edit-op state ---------------------------------------
    p.push_back (std::make_unique<FP> ("srcStart", "Start", R (0.0f, 1.0f, 1.0e-5f), 0.0f));
    p.push_back (std::make_unique<FP> ("srcEnd",   "End",   R (0.0f, 1.0f, 1.0e-5f), 1.0f));
    p.push_back (std::make_unique<BP> ("srcReverse", "Reverse Grains", false));
    p.push_back (std::make_unique<CP> ("srcLoopMode", "Loop", juce::StringArray { "Off", "Loop" }, 0));
    p.push_back (std::make_unique<FP> ("srcGain", "Source Gain", R (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<CP> ("srcMono", "Channels",
        juce::StringArray { "Stereo", "Mono", "Left", "Right" }, 0));

    // --- File Position --------------------------------------------------------
    p.push_back (std::make_unique<FP> ("posBase", "File Position", R (0.0f, 1.0f, 1.0e-5f), 0.0f));
    p.push_back (std::make_unique<FP> ("posScanRate", "Scan Rate", R (-2.0f, 2.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<BP> ("posScanSync", "Scan Sync", false));
    p.push_back (std::make_unique<CP> ("posScanDiv", "Scan Division", divisionChoices(), 2));   // 1 bar
    p.push_back (std::make_unique<FP> ("posEnvAmt", "Position Env", R (-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("posLfoAmt", "Position LFO", R (0.0f, 1.0f, 0.001f), 0.0f));

    // --- Grain cloud ----------------------------------------------------------
    p.push_back (std::make_unique<FP> ("grainSize", "Grain Size", skewRange (1.0f, 500.0f, 0.1f, 80.0f), 60.0f));
    p.push_back (std::make_unique<FP> ("spray", "Spray", R (0.0f, 1.0f, 0.001f), 0.1f));
    p.push_back (std::make_unique<FP> ("density", "Density", skewRange (0.1f, 100.0f, 0.01f, 20.0f), 20.0f));
    p.push_back (std::make_unique<CP> ("densityMode", "Density Mode",
        juce::StringArray { "Rate", "Overlap" }, 1));   // Overlap
    p.push_back (std::make_unique<CP> ("window", "Window",
        juce::StringArray { "Hann", "Gauss", "Tukey" }, 0));
    p.push_back (std::make_unique<FP> ("grainPan", "Grain Pan", R (0.0f, 1.0f, 0.001f), 0.2f));

    // --- Pitch ----------------------------------------------------------------
    p.push_back (std::make_unique<IP> ("tuneCoarse", "Coarse", -24, 24, 0));
    p.push_back (std::make_unique<FP> ("tuneFine", "Fine", R (-100.0f, 100.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("pitchSpread", "Pitch Spread", R (0.0f, 100.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("glide", "Glide", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<CP> ("glideMode", "Glide Mode",
        juce::StringArray { "Always", "Legato" }, 1));   // Legato
    p.push_back (std::make_unique<FP> ("pitchEnvAmt", "Pitch Env", R (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("pitchEnvAttack", "Pitch Env Attack", skewRange (0.0f, 2000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("pitchEnvDecay", "Pitch Env Decay", skewRange (1.0f, 4000.0f, 0.1f, 300.0f), 200.0f));
    p.push_back (std::make_unique<FP> ("pitchLfoAmt", "Vibrato Depth", R (0.0f, 1200.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("pitchLfoRate", "Vibrato Rate", skewRange (0.05f, 20.0f, 0.01f, 5.0f), 5.0f));

    // --- Key snap -------------------------------------------------------------
    p.push_back (std::make_unique<BP> ("keySnap", "Key Snap", false));
    p.push_back (std::make_unique<CP> ("keyRoot", "Key Root", keyRootChoices(), 0));
    p.push_back (std::make_unique<CP> ("keyScale", "Key Scale", keyScaleChoices(), 0));
    // Follow Song Key defaults OFF per suite MEMORY policy; the header KeyButton
    // menu is authoritative.
    p.push_back (std::make_unique<BP> ("followKey", "Follow Song Key", false));

    // --- Sub oscillator -------------------------------------------------------
    p.push_back (std::make_unique<BP> ("subOsc", "Sub Osc", false));
    p.push_back (std::make_unique<FP> ("subLevel", "Sub Level", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<CP> ("subWave", "Sub Wave",
        juce::StringArray { "Sine", "Triangle" }, 0));
    p.push_back (std::make_unique<IP> ("subOct", "Sub Octave", -2, 0, -1));

    // --- Filter ---------------------------------------------------------------
    p.push_back (std::make_unique<CP> ("filterType", "Filter Type",
        juce::StringArray { "LP24", "HP12", "BP" }, 0));
    p.push_back (std::make_unique<FP> ("cutoff", "Cutoff", skewRange (20.0f, 20000.0f, 0.1f, 1000.0f), 20000.0f));
    p.push_back (std::make_unique<FP> ("reso", "Resonance", R (0.0f, 1.0f, 0.001f), 0.1f));
    p.push_back (std::make_unique<FP> ("filterKeyTrack", "Key Track", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("filterEnvAmt", "Filter Env", R (-100.0f, 100.0f, 0.1f), 0.0f));

    // --- Envelopes ------------------------------------------------------------
    p.push_back (std::make_unique<FP> ("ampAttack", "Amp Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("ampDecay", "Amp Decay", skewRange (1.0f, 8000.0f, 0.1f, 400.0f), 400.0f));
    p.push_back (std::make_unique<FP> ("ampSustain", "Amp Sustain", R (0.0f, 1.0f, 0.001f), 1.0f));
    p.push_back (std::make_unique<FP> ("ampRelease", "Amp Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("fltAttack", "Filter Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("fltDecay", "Filter Decay", skewRange (1.0f, 8000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("fltSustain", "Filter Sustain", R (0.0f, 1.0f, 0.001f), 0.4f));
    p.push_back (std::make_unique<FP> ("fltRelease", "Filter Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("envVelSens", "Velocity Sens", R (0.0f, 1.0f, 0.001f), 0.5f));

    // --- LFOs -----------------------------------------------------------------
    p.push_back (std::make_unique<FP> ("lfo1Rate", "LFO1 Rate", skewRange (0.01f, 30.0f, 0.01f, 2.0f), 1.0f));
    p.push_back (std::make_unique<CP> ("lfo1Shape", "LFO1 Shape",
        juce::StringArray { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, 0));
    p.push_back (std::make_unique<BP> ("lfo1Sync", "LFO1 Sync", false));
    p.push_back (std::make_unique<CP> ("lfo1Div", "LFO1 Division", divisionChoices(), 4));   // 1/4
    p.push_back (std::make_unique<CP> ("lfo1Dest", "LFO1 Destination",
        juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, 0));
    p.push_back (std::make_unique<FP> ("lfo1Amt", "LFO1 Amount", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("lfo2Rate", "LFO2 Rate", skewRange (0.01f, 30.0f, 0.01f, 2.0f), 1.0f));
    p.push_back (std::make_unique<CP> ("lfo2Shape", "LFO2 Shape",
        juce::StringArray { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, 0));
    p.push_back (std::make_unique<BP> ("lfo2Sync", "LFO2 Sync", false));
    p.push_back (std::make_unique<CP> ("lfo2Div", "LFO2 Division", divisionChoices(), 4));   // 1/4
    p.push_back (std::make_unique<CP> ("lfo2Dest", "LFO2 Destination",
        juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, 0));
    p.push_back (std::make_unique<FP> ("lfo2Amt", "LFO2 Amount", R (0.0f, 1.0f, 0.001f), 0.0f));

    // --- Shared mod AD --------------------------------------------------------
    p.push_back (std::make_unique<FP> ("modEnvAttack", "Mod Env Attack", skewRange (0.0f, 2000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("modEnvDecay", "Mod Env Decay", skewRange (1.0f, 4000.0f, 0.1f, 300.0f), 300.0f));

    // --- Global / output ------------------------------------------------------
    p.push_back (std::make_unique<IP> ("voices", "Voices", 1, 16, 8));
    p.push_back (std::make_unique<CP> ("voiceMode", "Voice Mode",
        juce::StringArray { "Poly", "Mono", "Legato" }, 0));
    p.push_back (std::make_unique<FP> ("width", "Width", R (0.0f, 2.0f, 0.001f), 1.0f));
    p.push_back (std::make_unique<FP> ("tone", "Tone", R (-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("volume", "Volume", R (-60.0f, 6.0f, 0.1f), -6.0f));

    return { p.begin(), p.end() };
}

// =============================================================================
//  Lifecycle
// =============================================================================
void SattariGranulatorAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    engine.prepare (sr, samplesPerBlock, 2);

    // Re-hand the current source to the engine (prepare() reset its pools).
    if (auto s = std::atomic_load (&sourceDsp))
        engine.setSource (s);

    sessionReader.start();
    tempoReader.start();
}

void SattariGranulatorAudioProcessor::releaseResources()
{
    sessionReader.stop();
    tempoReader.stop();
}

bool SattariGranulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// =============================================================================
//  EngineParams gathering (audio thread; no allocation)
// =============================================================================
EngineParams SattariGranulatorAudioProcessor::gatherParams() const noexcept
{
    EngineParams e;

    e.srcStart    = param ("srcStart");
    e.srcEnd      = param ("srcEnd");
    e.srcReverse  = param ("srcReverse") > 0.5f;
    e.srcLoopMode = (int) std::lround (param ("srcLoopMode"));
    e.srcGainDb   = param ("srcGain");

    e.posBase     = param ("posBase");
    e.posScanRate = param ("posScanRate");
    e.posScanSync = param ("posScanSync") > 0.5f;
    e.posScanDiv  = (int) std::lround (param ("posScanDiv"));
    e.posEnvAmt   = param ("posEnvAmt");
    e.posLfoAmt   = param ("posLfoAmt");

    e.grainSizeMs = param ("grainSize");
    e.spray       = param ("spray");
    e.density     = param ("density");
    e.densityMode = (int) std::lround (param ("densityMode"));
    e.window      = (int) std::lround (param ("window"));
    e.grainPan    = param ("grainPan");

    e.tuneCoarse      = (int) std::lround (param ("tuneCoarse"));
    e.tuneFine        = param ("tuneFine");
    e.pitchSpreadCents = param ("pitchSpread");
    e.glide           = param ("glide");
    e.glideMode       = (int) std::lround (param ("glideMode"));
    e.pitchEnvSemis   = param ("pitchEnvAmt");
    e.pitchEnvAttackMs = param ("pitchEnvAttack");
    e.pitchEnvDecayMs  = param ("pitchEnvDecay");
    e.pitchLfoCents   = param ("pitchLfoAmt");
    e.pitchLfoRateHz  = param ("pitchLfoRate");

    e.keySnap    = param ("keySnap") > 0.5f;
    e.keyRoot    = (int) std::lround (param ("keyRoot"));
    e.keyScaleId = (int) scaleIdFromChoice ((int) std::lround (param ("keyScale")));
    e.followKey  = param ("followKey") > 0.5f;

    e.subOsc   = param ("subOsc") > 0.5f;
    e.subLevel = param ("subLevel");
    e.subWave  = (int) std::lround (param ("subWave"));
    e.subOct   = (int) std::lround (param ("subOct"));

    e.filterType     = (int) std::lround (param ("filterType"));
    e.cutoff         = param ("cutoff");
    e.reso           = param ("reso");
    e.filterKeyTrack = param ("filterKeyTrack");
    e.filterEnvAmt   = param ("filterEnvAmt");

    e.ampA = param ("ampAttack"); e.ampD = param ("ampDecay");
    e.ampS = param ("ampSustain"); e.ampR = param ("ampRelease");
    e.fltA = param ("fltAttack"); e.fltD = param ("fltDecay");
    e.fltS = param ("fltSustain"); e.fltR = param ("fltRelease");
    e.envVelSens = param ("envVelSens");

    e.lfo1RateHz = param ("lfo1Rate"); e.lfo1Shape = (int) std::lround (param ("lfo1Shape"));
    e.lfo1Sync = param ("lfo1Sync") > 0.5f; e.lfo1Div = (int) std::lround (param ("lfo1Div"));
    e.lfo1Dest = (int) std::lround (param ("lfo1Dest")); e.lfo1Amt = param ("lfo1Amt");
    e.lfo2RateHz = param ("lfo2Rate"); e.lfo2Shape = (int) std::lround (param ("lfo2Shape"));
    e.lfo2Sync = param ("lfo2Sync") > 0.5f; e.lfo2Div = (int) std::lround (param ("lfo2Div"));
    e.lfo2Dest = (int) std::lround (param ("lfo2Dest")); e.lfo2Amt = param ("lfo2Amt");

    e.modEnvA = param ("modEnvAttack"); e.modEnvD = param ("modEnvDecay");

    e.voices    = (int) std::lround (param ("voices"));
    e.voiceMode = (int) std::lround (param ("voiceMode"));
    e.width     = param ("width");
    e.tone      = param ("tone");
    e.volumeDb  = param ("volume");

    return e;
}

// =============================================================================
//  processBlock — sample-accurate MIDI walk + grain render
// =============================================================================
void SattariGranulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();   // instrument: start from silence, render on top

    // --- Push parameters + source into the engine (once per block) -----------
    EngineParams eParams = gatherParams();
    engine.setParams (eParams);

    // Block-local ref-hold of the source so a message-thread swap can't free the
    // buffer under in-flight grains this block.
    std::shared_ptr<const DspSource> src = std::atomic_load (&sourceDsp);
    engine.setSource (src);

    // --- Resolve tempo + key bus, feed the engine ----------------------------
    double bpm = 120.0;
    double ppq = 0.0;
    bool   playing = false;
    bool   haveHostBpm = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b && *b > 1.0) { bpm = *b; haveHostBpm = true; }
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    }
    // Tempo-bus fallback (Space-style) when the host reports no tempo.
    if (! haveHostBpm)
        if (auto t = tempoReader.current(); t.valid && t.bpm > 1.0)
            bpm = t.bpm;

    // Resolve the KEY bus into a ScaleId ordinal (processor maps bus 0/1/2).
    int busRoot = eParams.keyRoot;
    int busScaleOrdinal = eParams.keyScaleId;
    bool following = false;
    {
        const auto s = sessionReader.current();
        if (eParams.followKey && s.valid)
        {
            busRoot = s.root;
            busScaleOrdinal = (int) scaleIdFromBus (s.scale);
            following = true;
        }
    }
    engine.setBusState (busRoot, busScaleOrdinal, bpm, ppq, playing);

    // Mirror the effective key + follow state for the editor.
    followingBus.store (following, std::memory_order_relaxed);
    effKeyRoot.store (following ? busRoot : eParams.keyRoot, std::memory_order_relaxed);
    if (following)
        effKeyScale.store (busScaleOrdinal == (int) sattari::dsp::ScaleId::Minor ? 1 : 0,
                           std::memory_order_relaxed);
    else
        effKeyScale.store ((int) std::lround (param ("keyScale")), std::memory_order_relaxed);

    // --- Walk the MidiBuffer, rendering each sub-slice between events ---------
    int cursor = 0;
    for (const auto meta : midi)
    {
        const int pos = juce::jlimit (0, n, meta.samplePosition);
        if (pos > cursor)
        {
            engine.render (buffer, cursor, pos - cursor);
            cursor = pos;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.allNotesOff();
    }
    if (n > cursor)
        engine.render (buffer, cursor, n - cursor);

    // --- Publish the viz frame for the editor --------------------------------
    viz.store (engine.getDebugFrame());
}

// =============================================================================
//  Source load / swap (message thread)
// =============================================================================
std::shared_ptr<const DspSource>
SattariGranulatorAudioProcessor::toDspSource (const std::shared_ptr<const GranSource>& s)
{
    if (! s) return nullptr;
    auto d = std::make_shared<DspSource>();
    d->audio            = s->audio;              // shares the underlying buffer copy
    d->sourceSampleRate = s->sourceSampleRate;
    d->lengthSamples    = s->lengthSamples;
    d->rootNote         = s->rootNote;
    d->version          = s->version;
    return d;
}

void SattariGranulatorAudioProcessor::publishSource (std::shared_ptr<const GranSource> next,
                                                     bool pushUndo)
{
    // Bounded undo of the outgoing source (message thread only).
    if (pushUndo)
    {
        if (auto prev = std::atomic_load (&sourceMsg))
        {
            undoStack.push_back (prev);
            while ((int) undoStack.size() > kMaxUndo)
                undoStack.erase (undoStack.begin());
        }
    }

    auto dsp = toDspSource (next);
    std::atomic_store (&sourceMsg, next);
    std::atomic_store (&sourceDsp, dsp);
    engine.setSource (dsp);   // engine keeps the shared_ptr alive
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::applyChannelMode (const std::shared_ptr<const GranSource>& src, int mode)
{
    if (! src || src->lengthSamples <= 0 || mode == 0)   // 0 = Stereo (as-is)
        return src;

    const int len = src->lengthSamples;
    const int inCh = src->audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src->sourceSampleRate;
    out->lengthSamples    = len;
    out->rootNote         = src->rootNote;
    out->version          = src->version + 1;

    if (mode == 1)   // Mono: average all channels
    {
        out->audio.setSize (1, len);
        out->audio.clear();
        auto* d = out->audio.getWritePointer (0);
        for (int ch = 0; ch < inCh; ++ch)
        {
            const float* s = src->audio.getReadPointer (ch);
            for (int i = 0; i < len; ++i) d[i] += s[i] / (float) inCh;
        }
    }
    else   // 2 = Left, 3 = Right (pick one channel, keep it mono)
    {
        const int pick = juce::jlimit (0, inCh - 1, mode == 2 ? 0 : 1);
        out->audio.setSize (1, len);
        out->audio.copyFrom (0, 0, src->audio, pick, 0, len);
    }
    return out;
}

bool SattariGranulatorAudioProcessor::loadFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int len = (int) juce::jmin ((juce::int64) (48 * 60 * 192000),   // hard sanity cap
                                      reader->lengthInSamples);
    const int ch  = (int) juce::jmax (1u, juce::jmin (2u, reader->numChannels));

    auto built = std::make_shared<GranSource>();
    built->audio.setSize (ch, len);
    built->audio.clear();
    reader->read (&built->audio, 0, len, 0, true, ch > 1);
    built->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : sampleRate;
    built->lengthSamples    = len;
    built->rootNote         = 60;   // C4 default; retune via Coarse/Fine
    built->version          = 1;

    // Apply the current channel-mode downmix at build time.
    const int monoMode = (int) std::lround (param ("srcMono"));
    auto shaped = applyChannelMode (built, monoMode);

    publishSource (shaped, /*pushUndo=*/true);

    sourcePath = file.getFullPathName();
    sourceHash = juce::String (file.hashCode64());
    return true;
}

std::shared_ptr<const GranSource> SattariGranulatorAudioProcessor::currentSource() const
{
    return std::atomic_load (&sourceMsg);
}

juce::String SattariGranulatorAudioProcessor::currentSourceName() const
{
    if (sourcePath.isNotEmpty())
        return juce::File (sourcePath).getFileNameWithoutExtension();
    return {};
}

// =============================================================================
//  Destructive edit primitives (pure SourceSample -> SourceSample)
// =============================================================================
std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeReversed (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = src.audio.getReadPointer (c);
        float* d = out->audio.getWritePointer (c);
        for (int i = 0; i < len; ++i) d[i] = s[len - 1 - i];
    }
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeNormalized (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    float peak = 0.0f;
    for (int c = 0; c < ch; ++c)
        peak = juce::jmax (peak, src.audio.getMagnitude (c, 0, len));
    const float target = juce::Decibels::decibelsToGain (-0.5f);
    const float gain = peak > 1.0e-6f ? target / peak : 1.0f;

    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
    {
        out->audio.copyFrom (c, 0, src.audio, c, 0, len);
        out->audio.applyGain (c, 0, len, gain);
    }
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeTrimmed (const GranSource& src, float start01, float end01)
{
    const int srcLen = src.lengthSamples, ch = src.audio.getNumChannels();
    const int a = juce::jlimit (0, juce::jmax (0, srcLen - 1),
                                (int) std::floor (juce::jlimit (0.0f, 1.0f, start01) * (float) srcLen));
    int b = juce::jlimit (0, srcLen,
                          (int) std::ceil (juce::jlimit (0.0f, 1.0f, end01) * (float) srcLen));
    if (b <= a + 1) b = juce::jmin (srcLen, a + 1);
    const int len = b - a;

    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
        out->audio.copyFrom (c, 0, src.audio, c, a, len);
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeMonoFold (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (1, len);
    out->audio.clear();
    auto* d = out->audio.getWritePointer (0);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = src.audio.getReadPointer (c);
        for (int i = 0; i < len; ++i) d[i] += s[i] / (float) juce::jmax (1, ch);
    }
    return out;
}

// =============================================================================
//  Destructive edit ops (message thread; each rebuild + swap)
// =============================================================================
void SattariGranulatorAudioProcessor::editReverse()
{
    if (auto s = std::atomic_load (&sourceMsg)) publishSource (makeReversed (*s), true);
}

void SattariGranulatorAudioProcessor::editNormalize()
{
    if (auto s = std::atomic_load (&sourceMsg)) publishSource (makeNormalized (*s), true);
}

void SattariGranulatorAudioProcessor::editTrimToSelection()
{
    if (auto s = std::atomic_load (&sourceMsg))
    {
        publishSource (makeTrimmed (*s, param ("srcStart"), param ("srcEnd")), true);
        // Trim resets the region back to the whole (new) buffer.
        if (auto* ps = apvts.getParameter ("srcStart")) ps->setValueNotifyingHost (0.0f);
        if (auto* pe = apvts.getParameter ("srcEnd"))   pe->setValueNotifyingHost (1.0f);
    }
}

void SattariGranulatorAudioProcessor::editMonoFold()
{
    if (auto s = std::atomic_load (&sourceMsg)) publishSource (makeMonoFold (*s), true);
}

void SattariGranulatorAudioProcessor::editUndo()
{
    if (undoStack.empty()) return;
    auto prev = undoStack.back();
    undoStack.pop_back();
    publishSource (prev, /*pushUndo=*/false);
}

bool SattariGranulatorAudioProcessor::canUndo() const noexcept
{
    return ! undoStack.empty();
}

// =============================================================================
//  State (path + hash; graceful "sample missing" fallback)
// =============================================================================
void SattariGranulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    // Stash the source file path + content hash so presets recall the file.
    state.setProperty ("srcFilePath", sourcePath, nullptr);
    state.setProperty ("srcFileHash", sourceHash, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariGranulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    const juce::String savedPath = tree.getProperty ("srcFilePath", juce::String()).toString();
    const juce::String savedHash = tree.getProperty ("srcFileHash", juce::String()).toString();

    apvts.replaceState (tree);

    // Try to re-load the sample referenced by the preset; a missing file simply
    // leaves the engine source-less (the editor shows an empty "drop audio" state).
    if (savedPath.isNotEmpty())
    {
        const juce::File f (savedPath);
        if (f.existsAsFile() && loadFile (f))
        {
            // loadFile stamps a fresh path/hash; keep the recalled hash so a
            // moved-but-identical file still reports "matched".
            sourceHash = savedHash.isNotEmpty() ? savedHash : sourceHash;
        }
        else
        {
            sourcePath = savedPath;   // remember the intended file for the UI label
            sourceHash = savedHash;
        }
    }
}

// =============================================================================
//  Editor + factory
// =============================================================================
juce::AudioProcessorEditor* SattariGranulatorAudioProcessor::createEditor()
{
    return new SattariGranulatorAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariGranulatorAudioProcessor();
}
