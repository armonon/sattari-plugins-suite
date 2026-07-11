#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <memory>

using Engine = sattari::dsp::SattariGranularEngine;
using EngineParams = Engine::EngineParams;
using GranSource = sattari::granulator::SourceSample;
using DspSource = sattari::dsp::SourceSample;
using Layer = SattariGranulatorAudioProcessor::Layer;

// =============================================================================
//  Construction / parameter layout
// =============================================================================
SattariGranulatorAudioProcessor::SattariGranulatorAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          // Optional discrete audio INPUT bus so a DAW can feed Live Capture.
          // Declared with isActiveByDefault = false so the plugin still loads as
          // a pure synth in hosts that never route audio in.
          .withInput  ("Capture In", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output",     juce::AudioChannelSet::stereo(), true)),
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
    // Bus scale codes: 0=major, 1=minor, 2=chromatic. Chromatic disables snap.
    if (busScale == 1) return sattari::dsp::ScaleId::Minor;
    if (busScale == 2) return sattari::dsp::ScaleId::ChromaticOff;
    return sattari::dsp::ScaleId::Major;
}

// -----------------------------------------------------------------------------
//  Per-layer parameter id table (base ids, editor order). Each of these is
//  duplicated with an "A_" / "B_" prefix in the layout below.
// -----------------------------------------------------------------------------
const std::vector<juce::String>& SattariGranulatorAudioProcessor::perLayerParamIds()
{
    static const std::vector<juce::String> ids {
        // source region / edit-op state
        "srcStart", "srcEnd", "srcReverse", "srcLoopMode", "srcGain", "srcMono",
        // File Position
        "posBase", "posScanRate", "posScanSync", "posScanDiv", "posEnvAmt", "posLfoAmt",
        // grain cloud
        "grainSize", "spray", "density", "densityMode", "window", "grainPan",
        // smooth / texture + rhythmic (tempo-synced) grains
        "interp", "smoothness",
        "densitySync", "densityDiv", "grainSizeSync", "grainSizeDiv",
        // unison
        "unisonVoices", "unisonDetune", "unisonWidth",
        // pitch (per-layer offsets)
        "tuneCoarse", "tuneFine", "pitchSpread",
        // formant (spectral shaping / anti-chipmunk)
        "formantShift", "formantLock",
        // per-layer mix
        "level", "pan"
    };
    return ids;
}

float SattariGranulatorAudioProcessor::layerParam (Layer layer, const char* baseId) const noexcept
{
    // Build the prefixed id on the stack (no heap alloc): "A_"/"B_" + baseId.
    char buf[64];
    const char* pre = layerPrefix (layer);   // 2-char prefix ("A_" / "B_")
    buf[0] = pre[0];
    buf[1] = pre[1];
    int n = 2;
    for (int k = 0; baseId[k] != '\0' && n < 63; ++k, ++n)
        buf[n] = baseId[k];
    buf[n] = '\0';
    return apvts.getRawParameterValue (buf)->load();
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

    // -------------------------------------------------------------------------
    //  PER-LAYER parameters — emitted twice, once per layer with an A_/B_ prefix.
    //  A helper lambda takes the prefix + a friendly-name prefix ("A " / "B ").
    // -------------------------------------------------------------------------
    auto addLayer = [&] (const juce::String& pre, const juce::String& nm)
    {
        auto id   = [&] (const char* base) { return pre + base; };
        auto name = [&] (const juce::String& base) { return nm + base; };

        // --- Source region / edit-op state ------------------------------------
        p.push_back (std::make_unique<FP> (id ("srcStart"), name ("Start"), R (0.0f, 1.0f, 1.0e-5f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("srcEnd"),   name ("End"),   R (0.0f, 1.0f, 1.0e-5f), 1.0f));
        p.push_back (std::make_unique<BP> (id ("srcReverse"), name ("Reverse Grains"), false));
        p.push_back (std::make_unique<CP> (id ("srcLoopMode"), name ("Loop"), juce::StringArray { "Off", "Loop" }, 0));
        p.push_back (std::make_unique<FP> (id ("srcGain"), name ("Source Gain"), R (-24.0f, 24.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<CP> (id ("srcMono"), name ("Channels"),
            juce::StringArray { "Stereo", "Mono", "Left", "Right" }, 0));

        // --- File Position ----------------------------------------------------
        p.push_back (std::make_unique<FP> (id ("posBase"), name ("File Position"), R (0.0f, 1.0f, 1.0e-5f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("posScanRate"), name ("Scan Rate"), R (-2.0f, 2.0f, 0.001f), 0.0f));
        p.push_back (std::make_unique<BP> (id ("posScanSync"), name ("Scan Sync"), false));
        p.push_back (std::make_unique<CP> (id ("posScanDiv"), name ("Scan Division"), divisionChoices(), 2));   // 1 bar
        p.push_back (std::make_unique<FP> (id ("posEnvAmt"), name ("Position Env"), R (-1.0f, 1.0f, 0.001f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("posLfoAmt"), name ("Position LFO"), R (0.0f, 1.0f, 0.001f), 0.0f));

        // --- Grain cloud ------------------------------------------------------
        p.push_back (std::make_unique<FP> (id ("grainSize"), name ("Grain Size"), skewRange (1.0f, 500.0f, 0.1f, 80.0f), 60.0f));
        p.push_back (std::make_unique<FP> (id ("spray"), name ("Spray"), R (0.0f, 1.0f, 0.001f), 0.1f));
        p.push_back (std::make_unique<FP> (id ("density"), name ("Density"), skewRange (0.1f, 100.0f, 0.01f, 20.0f), 20.0f));
        p.push_back (std::make_unique<CP> (id ("densityMode"), name ("Density Mode"),
            juce::StringArray { "Rate", "Overlap" }, 1));   // Overlap
        p.push_back (std::make_unique<CP> (id ("window"), name ("Window"),
            juce::StringArray { "Hann", "Gauss", "Tukey" }, 0));
        p.push_back (std::make_unique<FP> (id ("grainPan"), name ("Grain Pan"), R (0.0f, 1.0f, 0.001f), 0.2f));

        // --- Smooth / Texture mode --------------------------------------------
        // Interp: source-read interpolation. Default Cubic (cleaner high reads).
        p.push_back (std::make_unique<CP> (id ("interp"), name ("Interp"),
            juce::StringArray { "Linear", "Cubic" }, 1));   // Cubic
        // Smoothness: musical macro that fuses the cloud into a glassy pad without
        // touching Density. 0 (default) is bit-identical to the classic cloud.
        p.push_back (std::make_unique<FP> (id ("smoothness"), name ("Smoothness"), R (0.0f, 1.0f, 0.001f), 0.0f));

        // --- Rhythmic (tempo-synced) grains -----------------------------------
        p.push_back (std::make_unique<BP> (id ("densitySync"), name ("Density Sync"), false));
        p.push_back (std::make_unique<CP> (id ("densityDiv"), name ("Density Division"), divisionChoices(), 6));   // 1/8
        p.push_back (std::make_unique<BP> (id ("grainSizeSync"), name ("Grain Size Sync"), false));
        p.push_back (std::make_unique<CP> (id ("grainSizeDiv"), name ("Grain Size Division"), divisionChoices(), 6)); // 1/8

        // --- Unison (per-grain stacked detune) --------------------------------
        p.push_back (std::make_unique<IP> (id ("unisonVoices"), name ("Unison Voices"), 1, 4, 1));
        p.push_back (std::make_unique<FP> (id ("unisonDetune"), name ("Unison Detune"), R (0.0f, 50.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("unisonWidth"), name ("Unison Width"), R (0.0f, 1.0f, 0.001f), 0.5f));

        // --- Pitch (per-layer offsets) ----------------------------------------
        p.push_back (std::make_unique<IP> (id ("tuneCoarse"), name ("Coarse"), -24, 24, 0));
        p.push_back (std::make_unique<FP> (id ("tuneFine"), name ("Fine"), R (-100.0f, 100.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("pitchSpread"), name ("Pitch Spread"), R (0.0f, 100.0f, 0.1f), 0.0f));

        // --- Formant (spectral shaping; +/-12 st, Lock = anti-chipmunk hold) ---
        p.push_back (std::make_unique<FP> (id ("formantShift"), name ("Formant"), R (-12.0f, 12.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<BP> (id ("formantLock"), name ("Formant Lock"), false));

        // --- Per-layer mix ----------------------------------------------------
        p.push_back (std::make_unique<FP> (id ("level"), name ("Level"), R (-60.0f, 6.0f, 0.1f), 0.0f));
        p.push_back (std::make_unique<FP> (id ("pan"), name ("Pan"), R (-1.0f, 1.0f, 0.001f), 0.0f));
    };

    addLayer ("A_", "A ");
    addLayer ("B_", "B ");

    // -------------------------------------------------------------------------
    //  TWO-LAYER blend + capture (top-level, non-per-layer).
    // -------------------------------------------------------------------------
    // Morph 0 = A only, 1 = B only, 0.5 = equal blend. Default 0 (A only) so a
    // freshly-inserted instance behaves like the classic single-layer patch.
    p.push_back (std::make_unique<FP> ("layerMorph", "Layer Morph", R (0.0f, 1.0f, 0.001f), 0.0f));
    // layerSel is UI-ONLY (which layer the editor edits / capture targets by
    // default). It is intentionally NON-automatable so hosts don't sweep it.
    p.push_back (std::make_unique<CP> ("layerSel", "Layer Select",
        juce::StringArray { "A", "B" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Live Capture.
    p.push_back (std::make_unique<BP> ("captureArm", "Capture Arm", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<FP> ("captureSecs", "Capture Length", R (0.5f, 8.0f, 0.1f), 2.0f));
    p.push_back (std::make_unique<CP> ("captureTarget", "Capture Target",
        juce::StringArray { "A", "B" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // -------------------------------------------------------------------------
    //  GLOBAL (shared) parameters — single id, pushed to BOTH engines.
    // -------------------------------------------------------------------------
    // --- Pitch env / LFO / glide (shared) ------------------------------------
    p.push_back (std::make_unique<FP> ("glide", "Glide", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<CP> ("glideMode", "Glide Mode",
        juce::StringArray { "Always", "Legato" }, 1));   // Legato
    p.push_back (std::make_unique<FP> ("pitchEnvAmt", "Pitch Env", R (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("pitchEnvAttack", "Pitch Env Attack", skewRange (0.0f, 2000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("pitchEnvDecay", "Pitch Env Decay", skewRange (1.0f, 4000.0f, 0.1f, 300.0f), 200.0f));
    p.push_back (std::make_unique<FP> ("pitchLfoAmt", "Vibrato Depth", R (0.0f, 1200.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FP> ("pitchLfoRate", "Vibrato Rate", skewRange (0.05f, 20.0f, 0.01f, 5.0f), 5.0f));

    // --- Key snap (shared) ----------------------------------------------------
    p.push_back (std::make_unique<BP> ("keySnap", "Key Snap", false));
    p.push_back (std::make_unique<CP> ("keyRoot", "Key Root", keyRootChoices(), 0));
    p.push_back (std::make_unique<CP> ("keyScale", "Key Scale", keyScaleChoices(), 0));
    // Follow Song Key defaults OFF per suite MEMORY policy; header KeyButton menu
    // is authoritative.
    p.push_back (std::make_unique<BP> ("followKey", "Follow Song Key", false));

    // --- Sub oscillator (shared) ---------------------------------------------
    p.push_back (std::make_unique<BP> ("subOsc", "Sub Osc", false));
    p.push_back (std::make_unique<FP> ("subLevel", "Sub Level", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<CP> ("subWave", "Sub Wave",
        juce::StringArray { "Sine", "Triangle" }, 0));
    p.push_back (std::make_unique<IP> ("subOct", "Sub Octave", -2, 0, -1));

    // --- Filter (shared) ------------------------------------------------------
    p.push_back (std::make_unique<CP> ("filterType", "Filter Type",
        juce::StringArray { "LP24", "HP12", "BP" }, 0));
    p.push_back (std::make_unique<FP> ("cutoff", "Cutoff", skewRange (20.0f, 20000.0f, 0.1f, 1000.0f), 20000.0f));
    p.push_back (std::make_unique<FP> ("reso", "Resonance", R (0.0f, 1.0f, 0.001f), 0.1f));
    p.push_back (std::make_unique<FP> ("filterKeyTrack", "Key Track", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("filterEnvAmt", "Filter Env", R (-100.0f, 100.0f, 0.1f), 0.0f));

    // --- Velocity routing (shared; per-voice at note level / per spawn) -------
    // Each -1..+1; the note velocity offsets File Position, grain size and filter
    // cutoff by these amounts so harder hits scrub / brighten. 0 = no-op.
    p.push_back (std::make_unique<FP> ("velToPos", "Vel -> Position", R (-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("velToSize", "Vel -> Grain Size", R (-1.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("velToCutoff", "Vel -> Cutoff", R (-1.0f, 1.0f, 0.001f), 0.0f));

    // --- Envelopes (shared) ---------------------------------------------------
    p.push_back (std::make_unique<FP> ("ampAttack", "Amp Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("ampDecay", "Amp Decay", skewRange (1.0f, 8000.0f, 0.1f, 400.0f), 400.0f));
    p.push_back (std::make_unique<FP> ("ampSustain", "Amp Sustain", R (0.0f, 1.0f, 0.001f), 1.0f));
    p.push_back (std::make_unique<FP> ("ampRelease", "Amp Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("fltAttack", "Filter Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
    p.push_back (std::make_unique<FP> ("fltDecay", "Filter Decay", skewRange (1.0f, 8000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("fltSustain", "Filter Sustain", R (0.0f, 1.0f, 0.001f), 0.4f));
    p.push_back (std::make_unique<FP> ("fltRelease", "Filter Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
    p.push_back (std::make_unique<FP> ("envVelSens", "Velocity Sens", R (0.0f, 1.0f, 0.001f), 0.5f));

    // --- LFOs (shared) --------------------------------------------------------
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

    // --- MOD MATRIX (shared; fixed 4 slots) ----------------------------------
    // Each slot: Source -> Dest scaled by a signed Amount. Choice lists MUST match
    // SattariGranularEngine::ModSource / ModDest ordinals. Default slot = Off/Off/
    // amount 0 -> a strict no-op (the engine is bit-identical when every slot is
    // Off). The matrix ADDS onto the fixed LFO destinations inside the engine.
    for (int i = 1; i <= 4; ++i)
    {
        const juce::String sid = "mod" + juce::String (i) + "Src";
        const juce::String did = "mod" + juce::String (i) + "Dst";
        const juce::String aid = "mod" + juce::String (i) + "Amt";
        const juce::String nm  = "Mod " + juce::String (i) + " ";
        p.push_back (std::make_unique<CP> (sid, nm + "Source",
            juce::StringArray { "Off", "LFO1", "LFO2", "Mod Env", "Velocity",
                                "Key Track", "Pressure", "Random" }, 0));
        p.push_back (std::make_unique<CP> (did, nm + "Dest",
            juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size",
                                "Density", "Pan", "Amp", "Morph", "Formant" }, 0));
        p.push_back (std::make_unique<FP> (aid, nm + "Amount", R (-1.0f, 1.0f, 0.001f), 0.0f));
    }

    // --- Per-note pressure routing (shared) ----------------------------------
    // How far a voice's MPE pressure (0..1) pushes its fixed destinations. These
    // only take effect once the host actually sends pressure, so leaving them at
    // their defaults keeps no-expression playback bit-identical.
    p.push_back (std::make_unique<FP> ("pressToCutoff", "Pressure -> Cutoff", R (-8.0f, 8.0f, 0.1f), 2.0f));
    p.push_back (std::make_unique<FP> ("pressToAmp", "Pressure -> Amp", R (0.0f, 4.0f, 0.01f), 0.25f));

    // --- MPE / pitch-bend (shared) -------------------------------------------
    // mpeMode: Classic (global wheel bends every held note; bendRange defaults to
    // the classic 2 st) vs. MPE (per-channel wheel bends the one note on that
    // channel; the spec default is ±48 st but bendRange is still authoritative).
    // bendRange: semitones at full wheel throw, default 2 st so a plain host with
    // the wheel behaves musically. No pitch-bend received == no expression == the
    // engine stays bit-identical to today.
    p.push_back (std::make_unique<CP> ("mpeMode", "Bend Mode",
        juce::StringArray { "Classic", "MPE" }, 0));
    p.push_back (std::make_unique<FP> ("bendRange", "Bend Range", R (1.0f, 48.0f, 1.0f), 2.0f));

    // --- Built-in reverb (global, on the blended output) ---------------------
    // reverbMix 0 (default) is an EXACT pass-through — the reverb is skipped
    // entirely so a fresh instance sounds identical to today. Size / damp shape
    // the Freeverb tail; the wet is taken pure and crossfaded by reverbMix.
    p.push_back (std::make_unique<FP> ("reverbMix", "Reverb Mix", R (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<FP> ("reverbSize", "Reverb Size", R (0.0f, 1.0f, 0.001f), 0.5f));
    p.push_back (std::make_unique<FP> ("reverbDamp", "Reverb Damp", R (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Cross-plugin bus drive (shared) -------------------------------------
    // broadcastKey (default OFF): when on, playing the granulator PUBLISHES the
    // currently-played (key-snapped) root + scale to the cross-plugin key bus so
    // the suite follows the granulator's key. Guarded against feedback — never
    // broadcasts while followKey is on. transientTrigger (default OFF): when on,
    // an external transient pulse (Rhythm Genie / Side Chain) re-fires the grain
    // cloud in time; transientSens scales the accent strength.
    p.push_back (std::make_unique<BP> ("broadcastKey", "Broadcast Key", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    p.push_back (std::make_unique<BP> ("transientTrigger", "Transient Trigger", false));
    p.push_back (std::make_unique<FP> ("transientSens", "Transient Sens", R (0.0f, 1.0f, 0.001f), 1.0f));

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

    for (int i = 0; i < kNumLayers; ++i)
    {
        auto& ls = layers[(size_t) i];
        ls.engine.prepare (sr, samplesPerBlock, 2);
        // Re-hand the current source to the engine (prepare() reset its pools).
        if (auto s = std::atomic_load (&ls.sourceDsp))
            ls.engine.setSource (s);
    }

    // Preallocate the per-layer render scratch (2 ch, max block) so processBlock
    // never allocates when rendering + blending the two layers.
    layerScratch.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);
    layerScratch.clear();

    // Built-in reverb: set its sample rate and preallocate the dry-hold scratch so
    // the audio thread never allocates. reset() clears the comb/allpass tails.
    reverb.setSampleRate (sr);
    reverb.reset();
    reverbDry.setSize (2, juce::jmax (1, samplesPerBlock), false, false, true);
    reverbDry.clear();
    // Reset the per-channel wheel + key-broadcast hand-off so a fresh transport
    // start never replays a stale bend or re-publishes a stale key.
    channelBendFrac.fill (0.0f);
    broadcastActive.store (false, std::memory_order_relaxed);
    broadcastWantRoot.store (-1, std::memory_order_relaxed);
    broadcastWantScale.store (-1, std::memory_order_relaxed);
    lastBroadcastRoot = lastBroadcastScale = -1;

    // Preallocate the capture ring for the max capture length (8 s) at this sr.
    capture.captureSr = sr;
    capture.capacity  = (int) std::ceil (8.5 * sr);   // headroom over the 8 s max
    capture.data.assign ((size_t) capture.capacity, 0.0f);
    capture.armed.store (false, std::memory_order_relaxed);
    capture.done.store (false, std::memory_order_relaxed);
    capture.filled.store (0, std::memory_order_relaxed);
    capture.writePos.store (0, std::memory_order_relaxed);
    lastArmParam = false;

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
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Optional input bus: allow it DISABLED (pure synth) OR mono/stereo (capture).
    const auto& in = layouts.getMainInputChannelSet();
    if (in.isDisabled())
        return true;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// =============================================================================
//  EngineParams gathering (audio thread; no allocation)
//  Per-layer fields come from the A_/B_ params; the rest are the shared globals.
// =============================================================================
EngineParams SattariGranulatorAudioProcessor::gatherParams (Layer layer) const noexcept
{
    EngineParams e;

    // ---- PER-LAYER ----------------------------------------------------------
    e.srcStart    = layerParam (layer, "srcStart");
    e.srcEnd      = layerParam (layer, "srcEnd");
    e.srcReverse  = layerParam (layer, "srcReverse") > 0.5f;
    e.srcLoopMode = (int) std::lround (layerParam (layer, "srcLoopMode"));
    e.srcGainDb   = layerParam (layer, "srcGain");

    e.posBase     = layerParam (layer, "posBase");
    e.posScanRate = layerParam (layer, "posScanRate");
    e.posScanSync = layerParam (layer, "posScanSync") > 0.5f;
    e.posScanDiv  = (int) std::lround (layerParam (layer, "posScanDiv"));
    e.posEnvAmt   = layerParam (layer, "posEnvAmt");
    e.posLfoAmt   = layerParam (layer, "posLfoAmt");

    e.grainSizeMs = layerParam (layer, "grainSize");
    e.spray       = layerParam (layer, "spray");
    e.density     = layerParam (layer, "density");
    e.densityMode = (int) std::lround (layerParam (layer, "densityMode"));
    e.window      = (int) std::lround (layerParam (layer, "window"));
    e.grainPan    = layerParam (layer, "grainPan");

    // smooth / texture + rhythmic (tempo-synced) grains
    e.interp        = (int) std::lround (layerParam (layer, "interp"));
    e.smoothness    = layerParam (layer, "smoothness");
    e.densitySync   = layerParam (layer, "densitySync") > 0.5f;
    e.densityDiv    = (int) std::lround (layerParam (layer, "densityDiv"));
    e.grainSizeSync = layerParam (layer, "grainSizeSync") > 0.5f;
    e.grainSizeDiv  = (int) std::lround (layerParam (layer, "grainSizeDiv"));

    e.unisonVoices      = (int) std::lround (layerParam (layer, "unisonVoices"));
    e.unisonDetuneCents = layerParam (layer, "unisonDetune");
    e.unisonWidth       = layerParam (layer, "unisonWidth");

    e.tuneCoarse       = (int) std::lround (layerParam (layer, "tuneCoarse"));
    e.tuneFine         = layerParam (layer, "tuneFine");
    e.pitchSpreadCents = layerParam (layer, "pitchSpread");

    e.formantShiftSemis = layerParam (layer, "formantShift");
    e.formantLock       = layerParam (layer, "formantLock") > 0.5f;

    // ---- GLOBAL (shared) ----------------------------------------------------
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

    // Velocity routing (shared; applied per voice inside each engine).
    e.velToPos    = param ("velToPos");
    e.velToSize   = param ("velToSize");
    e.velToCutoff = param ("velToCutoff");

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

    // MOD MATRIX (shared 4 slots). Choice ordinals map straight onto the engine's
    // ModSource / ModDest enums (the layout choice lists mirror them). All-Off is
    // the default -> a strict no-op inside the engine.
    for (int i = 0; i < 4; ++i)
    {
        char sid[12], did[12], aid[12];
        std::snprintf (sid, sizeof sid, "mod%dSrc", i + 1);
        std::snprintf (did, sizeof did, "mod%dDst", i + 1);
        std::snprintf (aid, sizeof aid, "mod%dAmt", i + 1);
        e.mods[i].source = (int) std::lround (param (sid));
        e.mods[i].dest   = (int) std::lround (param (did));
        e.mods[i].amount = param (aid);
    }

    // Per-note pressure routing (shared).
    e.pressureToCutoff = param ("pressToCutoff");
    e.pressureToAmp    = param ("pressToAmp");

    e.voices    = (int) std::lround (param ("voices"));
    e.voiceMode = (int) std::lround (param ("voiceMode"));
    e.width     = param ("width");
    e.tone      = param ("tone");
    // Master volume is applied ONCE to the blended mix (below), not per engine.
    // Each engine outputs at unity so the morph blend is clean; we push 0 dB.
    e.volumeDb  = 0.0f;

    return e;
}

// =============================================================================
//  processBlock — render BOTH layers, blend by morph + per-layer trims
// =============================================================================
void SattariGranulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // --- Live Capture: record the INPUT bus BEFORE we overwrite the buffer ----
    // Ring write only (lock-free); message-thread serviceCapture() decodes it.
    if (capture.armed.load (std::memory_order_acquire)
        && ! capture.done.load (std::memory_order_acquire)
        && capture.capacity > 0)
    {
        auto inBus = getBus (true, 0);
        const bool haveInput = inBus != nullptr && inBus->isEnabled();
        if (haveInput)
        {
            const auto inBuffer = getBusBuffer (buffer, true, 0);
            const int inCh = inBuffer.getNumChannels();
            int wp     = capture.writePos.load (std::memory_order_relaxed);
            int filled = capture.filled.load (std::memory_order_relaxed);
            const int target = capture.target.load (std::memory_order_relaxed);
            for (int i = 0; i < n && filled < target; ++i)
            {
                float m = 0.0f;
                for (int ch = 0; ch < inCh; ++ch)
                    m += inBuffer.getReadPointer (ch)[i];
                m = (inCh > 0) ? m / (float) inCh : 0.0f;
                capture.data[(size_t) wp] = m;
                wp = (wp + 1) % capture.capacity;
                ++filled;
            }
            capture.writePos.store (wp, std::memory_order_relaxed);
            capture.filled.store (filled, std::memory_order_release);
            if (filled >= target)
                capture.done.store (true, std::memory_order_release);
        }
    }

    // Instrument: start the OUTPUT from silence; both layers render on top. We
    // drive the MAIN OUTPUT bus view (not raw channel 0/1) so a discrete input
    // bus that does NOT alias the output can never be mistaken for the output.
    auto outBus = getBusBuffer (buffer, false, 0);
    outBus.clear();
    const int outCh = juce::jmin (2, outBus.getNumChannels());
    float* outL = (outCh > 0) ? outBus.getWritePointer (0) : nullptr;
    float* outR = (outCh > 1) ? outBus.getWritePointer (1) : nullptr;
    if (outL == nullptr) return;   // no output bus (should never happen)

    // --- Resolve tempo + key bus ONCE (shared by both engines) ---------------
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
    if (! haveHostBpm)
        if (auto t = tempoReader.current(); t.valid && t.bpm > 1.0)
            bpm = t.bpm;

    // --- Transient bus edge -> accent burst ----------------------------------
    // When the tempo/transient bus's transientId advances AND transientTrigger is
    // on, an external onset was published (Rhythm Genie / Side Chain). Re-fire the
    // grain cloud on both engines so grains lock to the pulse; transientSens scales
    // the accent strength. First-ever id just latches (no accent). We ALWAYS track
    // the id so toggling the param on mid-session doesn't fire on a stale advance.
    const bool  transientOn    = param ("transientTrigger") > 0.5f;
    const float transientStren = juce::jlimit (0.0f, 1.0f, param ("transientSens"));
    bool fireAccent = false;
    {
        const auto t = tempoReader.current();
        if (t.transientId != lastTransientId)
        {
            if (lastTransientId != 0 && transientOn)   // skip first latch; gated
                fireAccent = true;
            lastTransientId = t.transientId;
        }
    }

    // Key snap/follow is a SHARED global — resolve it once from layer A's gather
    // (the key fields are identical across layers).
    const EngineParams sharedKey = gatherParams (Layer::A);
    int  busRoot = sharedKey.keyRoot;
    int  busScaleOrdinal = sharedKey.keyScaleId;
    bool following = false;
    {
        const auto s = sessionReader.current();
        if (sharedKey.followKey && s.valid)
        {
            busRoot = s.root;
            busScaleOrdinal = (int) scaleIdFromBus (s.scale);
            following = true;
        }
    }

    // Mirror the effective key + follow state for the editor.
    followingBus.store (following, std::memory_order_relaxed);
    effKeyRoot.store (following ? busRoot : sharedKey.keyRoot, std::memory_order_relaxed);
    const int effScaleCode = following
        ? (busScaleOrdinal == (int) sattari::dsp::ScaleId::Minor ? 1 : 0)
        : (int) std::lround (param ("keyScale"));
    effKeyScale.store (effScaleCode, std::memory_order_relaxed);

    // --- Publish-to-key-bus hand-off (audio -> message) ----------------------
    // When broadcastKey is on and we are NOT following (feedback guard), a note
    // sounding this session means the granulator should DRIVE the suite's key. We
    // stash the intended (root, scale) — the key a note-on snaps to — into lock-
    // free atomics; the message thread (serviceKeyBroadcast) does the file I/O.
    // "Sounding" is true when a note-on arrives this block OR a voice is still
    // active (from the last published viz frame across both layers). The scale is
    // published in the BUS convention (0=major, 1=minor); the granulator's own
    // key root (0..11) is the broadcast root. Off/following -> inactive (no
    // publish), so a passive instance never stomps the bus.
    const bool broadcastOn = param ("broadcastKey") > 0.5f && ! following;
    if (broadcastOn)
    {
        bool noteThisBlock = false;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) { noteThisBlock = true; break; }
        const bool voiceLive = viz[0].load().nActiveVoices > 0
                            || viz[1].load().nActiveVoices > 0;
        const bool sounding = noteThisBlock || voiceLive;
        broadcastActive.store (sounding, std::memory_order_relaxed);
        if (sounding)
        {
            // The root we would key-snap to (bus root when snapping is on, else
            // the plugin's chosen key root) and the scale as a bus code.
            broadcastWantRoot.store (juce::jlimit (0, 11, sharedKey.keyRoot),
                                     std::memory_order_relaxed);
            broadcastWantScale.store (effScaleCode == 1 ? 1 : 0,
                                      std::memory_order_relaxed);
        }
    }
    else
    {
        broadcastActive.store (false, std::memory_order_relaxed);
    }

    // --- Morph + per-layer trims (equal-power morph so 0.5 is a true blend) ---
    const float morph = juce::jlimit (0.0f, 1.0f, param ("layerMorph"));
    const float gMorphA = std::cos (morph * juce::MathConstants<float>::halfPi); // 1 -> 0
    const float gMorphB = std::sin (morph * juce::MathConstants<float>::halfPi); // 0 -> 1

    // --- Pitch-bend range (semitones at full wheel) --------------------------
    // Resolved ONCE per block from the bendRange param. mpeMode is informational
    // for the UI (Classic vs. MPE convention); the wheel is applied identically in
    // both — a Classic host holds one note on channel 1, an MPE host spreads notes
    // across channels, and "bend every held note on this channel" is correct for
    // both. No wheel data -> channelBendFrac stays 0 -> engine bit-identical.
    const float bendRangeSemis = juce::jlimit (1.0f, kMpeBendRangeSemis, param ("bendRange"));

    // Per-layer render scratch. Grow ONCE (never on the audio thread in steady
    // state) if the host block exceeds what prepareToPlay reserved, then keep it.
    if (layerScratch.getNumSamples() < n || layerScratch.getNumChannels() < 2)
        layerScratch.setSize (2, juce::jmax (n, layerScratch.getNumSamples()), false, false, true);
    // A view of exactly n samples over the persistent scratch (no allocation).
    juce::AudioBuffer<float> layerBuf (layerScratch.getArrayOfWritePointers(), 2, n);

    for (int li = 0; li < kNumLayers; ++li)
    {
        const Layer layer = (Layer) li;
        LayerState& ls = L (layer);

        // Per-layer level (dB) folded with the equal-power morph gain.
        const float trimDb   = layerParam (layer, "level");
        const float trimGain = juce::Decibels::decibelsToGain (trimDb);
        const float morphGain = (layer == Layer::A) ? gMorphA : gMorphB;
        const float layerGain = trimGain * morphGain;

        // Per-layer pan (-1..+1) as an equal-power L/R split.
        const float pan01 = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * layerParam (layer, "pan"));
        const float panTheta = pan01 * juce::MathConstants<float>::halfPi;
        const float panL = std::cos (panTheta);
        const float panR = std::sin (panTheta);

        // Push params + source into this engine.
        EngineParams eParams = gatherParams (layer);
        ls.engine.setParams (eParams);

        std::shared_ptr<const DspSource> src = std::atomic_load (&ls.sourceDsp);
        ls.engine.setSource (src);
        ls.engine.setBusState (busRoot, busScaleOrdinal, bpm, ppq, playing);

        // Transient bus accent: re-fire the cloud at block start (gated + scaled).
        if (fireAccent)
            ls.engine.triggerAccent (transientStren);

        // Render this layer into the scratch buffer with a sample-accurate MIDI
        // walk (both engines see the SAME MIDI stream + the SAME MPE expression
        // replay). Pitch-bend is per-MIDI-channel (MPE spreads notes across
        // channels), stored as the wheel FRACTION and mapped to per-note bend in
        // SEMITONES via bendRangeSemis; channel-pressure and poly-aftertouch map to
        // per-note pressure. Both layers must replay the identical bend sequence,
        // so we seed a local per-channel wheel from the persistent member and only
        // write it back after the LAST layer.
        std::array<float, 16> chanFrac = channelBendFrac;
        layerBuf.clear();
        int cursor = 0;
        for (const auto meta : midi)
        {
            const int pos = juce::jlimit (0, n, meta.samplePosition);
            if (pos > cursor)
            {
                ls.engine.render (layerBuf, cursor, pos - cursor);
                cursor = pos;
            }
            const auto msg = meta.getMessage();
            const int  chIdx = juce::jlimit (0, 15, msg.getChannel() - 1);
            if (msg.isNoteOn())
            {
                // Carry the channel's current bend so an MPE note that arrives
                // already bent starts in tune (0 on channel 1 for classic MIDI).
                Engine::NoteExpression ex;
                ex.bendSemis = chanFrac[(size_t) chIdx] * bendRangeSemis;
                ls.engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity(), ex);
            }
            else if (msg.isNoteOff())
                ls.engine.noteOff (msg.getNoteNumber());
            else if (msg.isPitchWheel())
            {
                // 14-bit wheel (0..16383, centre 8192) -> fraction -1..+1, scaled
                // to semitones by the resolved bend range.
                const float frac  = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
                chanFrac[(size_t) chIdx] = frac;
                // Classic (channel-1, all notes) OR MPE (one note per channel):
                // bending every held note is correct for both — an MPE channel
                // holds a single note, so "all held" is exactly that note.
                ls.engine.setBendAllHeld (frac * bendRangeSemis);
            }
            else if (msg.isChannelPressure())
                ls.engine.setPressureAllHeld ((float) msg.getChannelPressureValue() / 127.0f);
            else if (msg.isAftertouch())      // poly aftertouch keys by note number
                ls.engine.setVoicePressure (msg.getNoteNumber(),
                                            (float) msg.getAfterTouchValue() / 127.0f);
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                ls.engine.allNotesOff();
        }
        if (n > cursor)
            ls.engine.render (layerBuf, cursor, n - cursor);

        // Persist the per-channel wheel only after the LAST layer so both layers
        // replayed the identical sequence this block.
        if (li == kNumLayers - 1)
            channelBendFrac = chanFrac;

        // Blend into the output with layer gain + pan (equal-power). The engine
        // wrote L/R; we fold pan across them so a mono/centre layer is unchanged
        // (panL == panR == 0.707 at centre keeps energy constant).
        const float* lyL = layerBuf.getReadPointer (0);
        const float* lyR = (layerBuf.getNumChannels() > 1) ? layerBuf.getReadPointer (1) : lyL;
        const float panScale = juce::MathConstants<float>::sqrt2; // renormalise centre to unity
        for (int i = 0; i < n; ++i)
        {
            const float l = lyL[i] * layerGain;
            const float r = lyR[i] * layerGain;
            // equal-power pan of the stereo layer signal
            outL[i] += (l * panL) * panScale;
            if (outR) outR[i] += (r * panR) * panScale;
        }

        // Publish this layer's viz frame.
        viz[(size_t) li].store (ls.engine.getDebugFrame());
    }

    // --- Built-in reverb on the blended mix (before master volume) -----------
    // juce::Reverb scales dry *2 and wet *3 internally, so its own dry/wet is not
    // a clean crossfade. We take a PURE wet (dryLevel 0, wetLevel 1/3 -> ~unity)
    // and do our own dry/wet against a saved dry copy — mirroring plugins/space so
    // reverbMix == 0 is an EXACT pass-through. mix==0 skips the reverb entirely
    // (no processing, no tail state advance beyond a cheap reset-free no-op), so a
    // fresh instance is bit-identical to today. We still advance the reverb state
    // whenever mix>0 so its comb/allpass tails are continuous.
    const float reverbMix = juce::jlimit (0.0f, 1.0f, param ("reverbMix"));
    if (reverbMix > 0.0f)
    {
        // Hold the dry (pre-reverb) blend so our crossfade is exact.
        reverbDry.copyFrom (0, 0, outL, n);
        if (outR) reverbDry.copyFrom (1, 0, outR, n);

        juce::Reverb::Parameters rp;
        rp.roomSize   = juce::jlimit (0.0f, 1.0f, param ("reverbSize"));
        rp.damping    = juce::jlimit (0.0f, 1.0f, param ("reverbDamp"));
        rp.wetLevel   = 1.0f / 3.0f;   // wet is *3 internally -> ~unity pure wet
        rp.dryLevel   = 0.0f;          // pure wet; we mix the dry ourselves
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        if (outR)
            reverb.processStereo (outL, outR, n);   // outL/outR now hold pure wet
        else
            reverb.processMono (outL, n);

        const float* dryL = reverbDry.getReadPointer (0);
        const float* dryR = (outR ? reverbDry.getReadPointer (1) : nullptr);
        for (int i = 0; i < n; ++i)
        {
            outL[i] = dryL[i] * (1.0f - reverbMix) + outL[i] * reverbMix;
            if (outR) outR[i] = dryR[i] * (1.0f - reverbMix) + outR[i] * reverbMix;
        }
    }

    // --- Master volume + soft ceiling on the blended mix ---------------------
    const float outGain = juce::Decibels::decibelsToGain (param ("volume"));
    for (int i = 0; i < n; ++i)
    {
        float l = outL[i] * outGain;
        l = softCeiling (l);
        if (! std::isfinite (l)) l = 0.0f;
        outL[i] = l;
        if (outR)
        {
            float r = outR[i] * outGain;
            r = softCeiling (r);
            if (! std::isfinite (r)) r = 0.0f;
            outR[i] = r;
        }
    }
    // Zero any output-bus channels beyond the two we drive (outBus was already
    // cleared above, so this is belt-and-braces for >2ch output layouts).
    for (int ch = 2; ch < outBus.getNumChannels(); ++ch)
        outBus.clear (ch, 0, n);
}

// -----------------------------------------------------------------------------
//  tanh soft-ceiling limiter (mirrors the engine's, applied to the blended mix
//  so the two layers summed can never leave the plugin hot).
// -----------------------------------------------------------------------------
float SattariGranulatorAudioProcessor::softCeiling (float x) noexcept
{
    constexpr float knee = 0.75f, ceiling = 0.988f, range = ceiling - knee;
    const float a = std::fabs (x);
    if (a <= knee) return x;
    return std::copysign (knee + range * std::tanh ((a - knee) / range), x);
}

// =============================================================================
//  Source build / swap (message thread) — now per layer.
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

void SattariGranulatorAudioProcessor::publishSource (Layer layer,
                                                     std::shared_ptr<const GranSource> next,
                                                     bool pushUndo)
{
    LayerState& ls = L (layer);

    // Bounded undo of the outgoing source (message thread only).
    if (pushUndo)
    {
        if (auto prev = std::atomic_load (&ls.sourceMsg))
        {
            ls.undoStack.push_back (prev);
            while ((int) ls.undoStack.size() > kMaxUndo)
                ls.undoStack.erase (ls.undoStack.begin());
        }
    }

    auto dsp = toDspSource (next);
    std::atomic_store (&ls.sourceMsg, next);
    std::atomic_store (&ls.sourceDsp, dsp);
    ls.engine.setSource (dsp);   // engine keeps the shared_ptr alive
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

bool SattariGranulatorAudioProcessor::loadFile (Layer layer, const juce::File& file)
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

    // Apply the layer's current channel-mode downmix at build time.
    const int monoMode = (int) std::lround (layerParam (layer, "srcMono"));
    auto shaped = applyChannelMode (built, monoMode);

    publishSource (layer, shaped, /*pushUndo=*/true);

    LayerState& ls = L (layer);
    ls.sourcePath = file.getFullPathName();
    ls.sourceHash = juce::String (file.hashCode64());
    return true;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::currentSource (Layer layer) const
{
    return std::atomic_load (&L (layer).sourceMsg);
}

juce::String SattariGranulatorAudioProcessor::currentSourceName (Layer layer) const
{
    const auto& ls = L (layer);
    if (ls.sourcePath.isNotEmpty())
        return juce::File (ls.sourcePath).getFileNameWithoutExtension();
    // A captured (non-file) buffer has no path but a live source: label it.
    if (std::atomic_load (&ls.sourceMsg) != nullptr)
        return "(captured)";
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
//  Destructive edit ops (message thread; each rebuild + swap) — per layer.
// =============================================================================
void SattariGranulatorAudioProcessor::editReverse (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeReversed (*s), true);
}

void SattariGranulatorAudioProcessor::editNormalize (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeNormalized (*s), true);
}

void SattariGranulatorAudioProcessor::editTrimToSelection (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg))
    {
        const auto startId = layerParamId (layer, "srcStart");
        const auto endId   = layerParamId (layer, "srcEnd");
        publishSource (layer, makeTrimmed (*s, layerParam (layer, "srcStart"),
                                              layerParam (layer, "srcEnd")), true);
        // Trim resets the region back to the whole (new) buffer.
        if (auto* ps = apvts.getParameter (startId)) ps->setValueNotifyingHost (0.0f);
        if (auto* pe = apvts.getParameter (endId))   pe->setValueNotifyingHost (1.0f);
    }
}

void SattariGranulatorAudioProcessor::editMonoFold (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeMonoFold (*s), true);
}

void SattariGranulatorAudioProcessor::editUndo (Layer layer)
{
    LayerState& ls = L (layer);
    if (ls.undoStack.empty()) return;
    auto prev = ls.undoStack.back();
    ls.undoStack.pop_back();
    publishSource (layer, prev, /*pushUndo=*/false);
}

bool SattariGranulatorAudioProcessor::canUndo (Layer layer) const noexcept
{
    return ! L (layer).undoStack.empty();
}

// =============================================================================
//  Live Capture (message thread build; audio thread only ring-writes)
// =============================================================================
void SattariGranulatorAudioProcessor::armCapture (bool arm) noexcept
{
    if (arm)
    {
        // Latch length + target layer, reset the ring, then arm.
        const double secs = (double) juce::jlimit (0.5f, 8.0f, param ("captureSecs"));
        const int target  = juce::jlimit (1, capture.capacity,
                                          (int) std::round (secs * capture.captureSr));
        capture.target.store (target, std::memory_order_relaxed);
        capture.targetLayer.store ((int) std::lround (param ("captureTarget")), std::memory_order_relaxed);
        capture.writePos.store (0, std::memory_order_relaxed);
        capture.filled.store (0, std::memory_order_relaxed);
        capture.done.store (false, std::memory_order_release);
        capture.armed.store (true, std::memory_order_release);
    }
    else
    {
        // Disarm: stop writing. serviceCapture() will drain whatever we have.
        capture.armed.store (false, std::memory_order_release);
    }
}

float SattariGranulatorAudioProcessor::captureProgress() const noexcept
{
    const int target = capture.target.load (std::memory_order_relaxed);
    if (target <= 0 || ! capture.armed.load (std::memory_order_relaxed)) return 0.0f;
    const int filled = capture.filled.load (std::memory_order_relaxed);
    return juce::jlimit (0.0f, 1.0f, (float) filled / (float) target);
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::buildCaptureSource (const std::vector<float>& mono,
                                                     int lengthSamples, double captureSr) const
{
    auto out = std::make_shared<GranSource>();
    const int len = juce::jmax (0, lengthSamples);
    out->audio.setSize (1, juce::jmax (1, len));
    out->audio.clear();
    if (len > 0)
    {
        float* d = out->audio.getWritePointer (0);
        for (int i = 0; i < len; ++i) d[i] = mono[(size_t) i];
    }
    out->sourceSampleRate = captureSr > 0.0 ? captureSr : sampleRate;
    out->lengthSamples    = len;
    out->rootNote         = 60;
    out->version          = 1;
    return out;
}

bool SattariGranulatorAudioProcessor::serviceCapture()
{
    // React to the momentary captureArm param edge (editor pulses it).
    const bool armParam = param ("captureArm") > 0.5f;
    if (armParam != lastArmParam)
    {
        lastArmParam = armParam;
        armCapture (armParam);
        // On a rising edge we just armed; nothing to drain yet.
        if (armParam)
            return false;
    }

    // If the audio thread reports a completed (or the user disarmed a partial)
    // capture, drain it into a fresh SourceSample on THIS (message) thread.
    const bool done       = capture.done.load (std::memory_order_acquire);
    const bool stillArmed = capture.armed.load (std::memory_order_acquire);
    const int  filled     = capture.filled.load (std::memory_order_acquire);

    if (done || (! stillArmed && filled > 0))
    {
        // Copy the ring (it was written from index 0 forward until target/disarm).
        const int len = juce::jmin (filled, capture.capacity);
        std::vector<float> mono ((size_t) juce::jmax (1, len), 0.0f);
        for (int i = 0; i < len; ++i) mono[(size_t) i] = capture.data[(size_t) i];

        const int targetLayerIdx = capture.targetLayer.load (std::memory_order_relaxed);
        const Layer target = (targetLayerIdx == 1) ? Layer::B : Layer::A;

        auto src = buildCaptureSource (mono, len, capture.captureSr);
        publishSource (target, src, /*pushUndo=*/true);

        // Clear the loaded-file path (this layer now holds a captured buffer).
        L (target).sourcePath.clear();
        L (target).sourceHash.clear();

        // Fully disarm + reset so we don't re-drain.
        capture.armed.store (false, std::memory_order_release);
        capture.done.store (false, std::memory_order_release);
        capture.filled.store (0, std::memory_order_release);
        capture.writePos.store (0, std::memory_order_release);

        // Reset the momentary param so the UI toggle returns to off.
        if (auto* pa = apvts.getParameter ("captureArm"))
            if (pa->getValue() > 0.5f) pa->setValueNotifyingHost (0.0f);
        lastArmParam = false;
        return true;
    }
    return false;
}

// =============================================================================
//  Publish-to-key-bus (message thread; call from the editor timer)
// -----------------------------------------------------------------------------
//  The audio thread stashed the intended broadcast (root, scale) + active flag in
//  atomics (broadcastActive / broadcastWant*). Here — off the audio thread — we
//  read them, throttle to ~1 Hz, and only re-publish when the pair CHANGES so the
//  bus file never thrashes. When broadcastKey turns off (or no note is sounding)
//  we simply stop publishing; we never clear the bus (a stale entry ages out via
//  the 30 s window). The feedback guard (never broadcast while following) is
//  enforced on the audio side (broadcastActive is false when following).
// =============================================================================
bool SattariGranulatorAudioProcessor::serviceKeyBroadcast()
{
    if (! broadcastActive.load (std::memory_order_relaxed))
        return false;

    const int root  = juce::jlimit (0, 11, broadcastWantRoot.load  (std::memory_order_relaxed));
    const int scale = juce::jlimit (0, 1,  broadcastWantScale.load (std::memory_order_relaxed));

    const juce::int64 now = juce::Time::currentTimeMillis();
    const bool changed = (root != lastBroadcastRoot) || (scale != lastBroadcastScale);
    // Republish on change immediately; otherwise refresh at most ~1 Hz so a held
    // note keeps the bus entry alive without thrashing the file.
    if (! changed && (now - lastBroadcastMs) < 1000)
        return false;

    lastBroadcastRoot  = root;
    lastBroadcastScale = scale;
    lastBroadcastMs    = now;

    // Publish the full session (key + tempo + transient id). We don't own a tempo
    // source, so publish bpm 0 (readers keep their own tempo); the ++id keeps the
    // session message fresh for followers.
    sattari::xplugin::publishSession ("granulator", root, scale, 0.0, ++broadcastId);
    return true;
}

// =============================================================================
//  State (per-layer path + hash + version tag; graceful "sample missing")
// =============================================================================
void SattariGranulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("granulatorStateVersion", kStateVersion, nullptr);
    // Per-layer source file paths + content hashes.
    state.setProperty ("srcFilePathA", L (Layer::A).sourcePath, nullptr);
    state.setProperty ("srcFileHashA", L (Layer::A).sourceHash, nullptr);
    state.setProperty ("srcFilePathB", L (Layer::B).sourcePath, nullptr);
    state.setProperty ("srcFileHashB", L (Layer::B).sourceHash, nullptr);
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

    const int version = (int) tree.getProperty ("granulatorStateVersion", 1);

    // Legacy (v1) single-layer presets store `srcFilePath` / `srcFileHash` and
    // un-prefixed per-layer params. Migrate every legacy per-layer PARAM value
    // into its A_<base> node so a recalled patch sounds like it did (Layer B is
    // left at defaults). APVTS stores params as "PARAM" nodes with id/value.
    if (version < 2)
    {
        for (const auto& base : perLayerParamIds())
        {
            if (base == "level" || base == "pan")   // new params: no legacy source
                continue;
            auto legacyNode = tree.getChildWithProperty ("id", base);
            auto aNode      = tree.getChildWithProperty ("id", "A_" + base);
            if (legacyNode.isValid() && aNode.isValid())
                aNode.setProperty ("value", legacyNode.getProperty ("value"), nullptr);
        }
    }

    juce::String pathA = tree.getProperty ("srcFilePathA", juce::String()).toString();
    juce::String hashA = tree.getProperty ("srcFileHashA", juce::String()).toString();
    juce::String pathB = tree.getProperty ("srcFilePathB", juce::String()).toString();
    juce::String hashB = tree.getProperty ("srcFileHashB", juce::String()).toString();
    if (version < 2)   // legacy single path lands in A
    {
        pathA = tree.getProperty ("srcFilePath", pathA).toString();
        hashA = tree.getProperty ("srcFileHash", hashA).toString();
    }

    apvts.replaceState (tree);

    auto recall = [this] (Layer layer, const juce::String& path, const juce::String& hash)
    {
        LayerState& ls = L (layer);
        if (path.isEmpty()) return;
        const juce::File f (path);
        if (f.existsAsFile() && loadFile (layer, f))
            ls.sourceHash = hash.isNotEmpty() ? hash : ls.sourceHash;
        else
        {
            ls.sourcePath = path;   // remember the intended file for the UI label
            ls.sourceHash = hash;
        }
    };
    recall (Layer::A, pathA, hashA);
    recall (Layer::B, pathB, hashB);
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
