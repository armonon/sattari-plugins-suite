#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariGranularEngine.h>
#include <sattari_audio_core/SattariGranSourceSample.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariRealtime.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

// =============================================================================
//  GranulatorModule — Sattari Granulator's two-layer granular engine, extracted
//  so the standalone plugin and a Sattari Stack layer run the SAME synthesis.
//
//  Split along the realtime boundary, as with Chop and Rhythm Genie — but this
//  one does NOT move a whole subsystem across, and that is what makes it the
//  hardest of the three:
//
//  `LayerState` SPLITS. The module keeps `engine` and `sourceDsp` (audio thread);
//  the shell keeps `sourceMsg`, the undo stack and the file bookkeeping (message
//  thread). They are two views of one load, and only the dsp view crosses.
//
//  THE CAPTURE RING GOES BOTH WAYS. Live Capture is written by the AUDIO thread
//  (ring writes only — never a decode or an allocation) and drained by the
//  MESSAGE thread, which builds a SourceSample and swaps it back in. Chop and
//  Rhythm Genie each had a one-directional hand-off; this does not. The ring
//  therefore lives HERE, because the module is what writes it, and the shell
//  drains it through the capture* accessors. Putting it in the shell would mean
//  the audio thread writing through a borrowed pointer for no gain.
//
//  Capture's INPUT arrives via setSidechain() rather than a bespoke hook: "a
//  second input the module reads but never outputs" is exactly what that
//  interface point already means. A Stack layer supplies none, so capture is
//  simply unavailable there — which is honest, since a layer has no input.
// =============================================================================

namespace sattari::dsp
{

// The .cpp reached these through file-local `using` declarations.
using Engine       = SattariGranularEngine;
using EngineParams = SattariGranularEngine::EngineParams;
using DspSource    = SourceSample;

class GranulatorModule final : public SattariModule
{
public:
    enum class Layer { A = 0, B = 1 };
    static constexpr int kNumLayers = 2;

private:
    // =========================================================================
    //  Per-layer state (source slots, engines, undo, viz) — indexed by Layer.
    // =========================================================================
    struct LayerState
    {
        sattari::dsp::SattariGranularEngine engine;

        // The dsp-facing source the engine granulates. The SHELL owns the
        // editor-facing copy, the undo stack and the file bookkeeping — none of
        // that is audio-thread state and none of it belongs here.
        std::shared_ptr<const sattari::dsp::SourceSample> sourceDsp;
        juce::SpinLock sourceLock;
        // Message-thread ownership keeps engine replacement from deleting a
        // sample buffer in the audio callback. Unused sources are collected on
        // the next publication, not retained for the lifetime of the plug-in.
        std::vector<std::shared_ptr<const sattari::dsp::SourceSample>> retainedSources;
    };


    // =========================================================================
    //  Live-capture ring (audio-thread writer / message-thread drainer).
    // =========================================================================
    struct CaptureRing
    {
        // Preallocated in prepareToPlay to the max capture length (8 s * sr).
        std::vector<float> data;                 // mono ring storage
        int   capacity = 0;                      // == data.size()
        std::atomic<int>  writePos { 0 };        // audio-thread write cursor
        std::atomic<int>  filled   { 0 };        // samples written since arm (<= target)
        std::atomic<int>  target   { 0 };        // samples to capture before "done"
        std::atomic<bool> armed    { false };    // audio thread writes while true
        std::atomic<bool> done     { false };    // set by audio thread at target
        std::atomic<int>  targetLayer { 0 };     // Layer to publish into (latched at arm)
        double captureSr = 48000.0;              // sr the ring is recorded at
    } capture;

    // =========================================================================
    //  State
    // =========================================================================
    double sampleRate = 44100.0;

    std::array<LayerState, kNumLayers> layers;

    // Preallocated per-layer render scratch (sized in prepareToPlay so the audio
    // thread never allocates). Each layer renders here, then blends into `buffer`.
    juce::AudioBuffer<float> layerScratch;   // 2 x maxBlockSize

    // --- Built-in reverb on the BLENDED two-layer output ---------------------
    // A global Freeverb-based reverb applied AFTER the morph blend and BEFORE the
    // master volume. juce::Reverb scales dry *2 and wet *3 internally, so its
    // dry/wet is not a clean crossfade — we take a PURE wet from it (dryLevel 0)
    // and do our own dry/wet against a saved dry copy, mirroring plugins/space.
    // reverbMix == 0 is then an EXACT pass-through. `reverbDry` holds the pre-verb
    // signal; both are sized in prepareToPlay so the audio thread never allocates.
    juce::Reverb             reverb;
    juce::AudioBuffer<float> reverbDry;      // 2 x maxBlockSize (pre-reverb copy)


    // Cross-plugin buses + host transport.
    sattari::xplugin::SessionReader sessionReader;   // KEY bus (Follow Song Key)
    sattari::xplugin::TempoReader   tempoReader;     // TEMPO bus (bpm fallback)

    // Lock-free audio -> UI publish of each layer's granulation viz frame.
    std::array<sattari::rt::AtomicSnapshot<sattari::dsp::SattariGranularEngine::GrainVizFrame>,
               kNumLayers> viz;

    // KEY status mirrored for the editor (updated each block).
    std::atomic<bool> followingBus { false };
    std::atomic<int>  effKeyRoot { 0 };
    std::atomic<int>  effKeyScale { 0 };

    // captureArm edge detection (message thread reads the param; audio thread
    // never allocates). The editor pulses captureArm; serviceCapture() reacts.
    bool lastArmParam = false;

    // --- Per-note expression (MPE) drive state (audio thread only) -----------
    // Latest pitch-bend wheel value per MIDI channel, stored as the WHEEL fraction
    // (-1..+1) so the semitone amount is (fraction * bendRange) resolved per block
    // from the `bendRange` param. MPE spreads notes across channels 2..16, so a
    // per-channel bend maps to per-note bend; classic MIDI uses channel 1 only.
    // `mpeMode` (param) selects the default bend range convention: classic 2 st
    // vs. the MPE ±48 st spec — but the actual range comes from the `bendRange`
    // param (default 2 st) so a plain host with the wheel behaves musically.
    // Channel pressure / poly aftertouch map to per-note pressure via
    // setVoicePressure / setPressureAllHeld.
    static constexpr float kMpeBendRangeSemis = 48.0f;   // MPE full-spec range
    std::array<float, 16> channelBendFrac {};            // per-channel wheel -1..+1

    // --- Publish-to-key-bus (broadcastKey) — audio -> message hand-off -------
    // When broadcastKey is on (and NOT following), playing the granulator DRIVES
    // the suite's key: the AUDIO thread resolves the current (key-snapped) root +
    // scale that a note-on would produce and stashes it in these lock-free atomics
    // (NO file I/O on the audio thread). The MESSAGE thread (editor timer, via
    // serviceKeyBroadcast()) reads them, throttles, and calls publishSession so
    // other Sattari plugins follow. broadcastWant* carry the intended pair;
    // broadcastActive gates publishing on "broadcastKey on AND a note is sounding
    // AND not following" (feedback guard). lastBroadcast* / broadcastId live on the
    // message thread and only re-publish when the pair actually changes.
    std::atomic<int>  broadcastWantRoot  { -1 };
    std::atomic<int>  broadcastWantScale { -1 };   // 0=major,1=minor bus code
    std::atomic<bool> broadcastActive    { false };
    // lastBroadcast* were message-thread-only bookkeeping and now live in the
    // shell, which is the thread that actually publishes.

    // --- Transient bus edge detection (audio thread) -------------------------
    // We poll tempoReader.current().transientId each block; when it advances AND
    // the `transientTrigger` param is on, an external onset fired (Rhythm Genie /
    // Side Chain published) — re-fire the grain cloud on both engines via
    // triggerAccent (strength from `transientSens`) so grains lock to the pulse.
    juce::int64 lastTransientId = 0;

    // Version tag written into saved state so future format changes stay safe.

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    sattari::host::TransportSnapshot transport;
    const juce::AudioBuffer<float>* captureIn = nullptr;

    LayerState& L (Layer layer) noexcept { return layers[(size_t) layer]; }
    const LayerState& L (Layer layer) const noexcept { return layers[(size_t) layer]; }

    float layerParam (Layer layer, const char* baseId) const noexcept
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
        return cachedParameterValue (buf);
    }

    EngineParams gatherParams (Layer layer) const noexcept
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

    float softCeiling (float x) noexcept
    {
        constexpr float knee = 0.75f, ceiling = 0.988f, range = ceiling - knee;
        const float a = std::fabs (x);
        if (a <= knee) return x;
        return std::copysign (knee + range * std::tanh ((a - knee) / range), x);
    }

    float param (const char* id) const noexcept
    {
        if (state == nullptr) return 0.0f;
        return cachedParameterValue (id);
    }

public:
    ~GranulatorModule() override { sessionReader.stop(); tempoReader.stop(); }

    const char* moduleId()    const noexcept override { return "granulator"; }
    const char* displayName() const noexcept override { return "Granulator"; }

    static const char* layerPrefix (Layer layer) noexcept { return layer == Layer::B ? "B_" : "A_"; }
    static juce::String layerParamId (Layer layer, const char* baseId)
    {
        return juce::String (layerPrefix (layer)) + baseId;
    }

    static juce::StringArray keyRootChoices()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }

    static juce::StringArray keyScaleChoices()
    {
        // Order matches scaleIdFromChoice below and the Arp/Harmony layout.
        return { "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" };
    }

    static juce::StringArray divisionChoices()
    {
        // Matches SattariGranularEngine::divisionBeats() (quarter = 1.0), Space-style.
        return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/4 dot", "1/8", "1/8 dot", "1/8 trip", "1/16" };
    }

    static ScaleId scaleIdFromChoice (int choice) noexcept
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

    static ScaleId scaleIdFromBus (int busScale) noexcept
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

    static const std::vector<juce::String>& perLayerParamIds()
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

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using IP = juce::AudioParameterInt;
        using BP = juce::AudioParameterBool;
        using CP = juce::AudioParameterChoice;
        using R  = juce::NormalisableRange<float>;
    
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
            auto id   = [&] (const char* base) { return prefix + pre + base; };
            auto name = [&] (const juce::String& base) { return nm + base; };

            // --- Source region / edit-op state ------------------------------------
            out.push_back (std::make_unique<FP> (id ("srcStart"), name ("Start"), R (0.0f, 1.0f, 1.0e-5f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("srcEnd"),   name ("End"),   R (0.0f, 1.0f, 1.0e-5f), 1.0f));
            out.push_back (std::make_unique<BP> (id ("srcReverse"), name ("Reverse Grains"), false));
            out.push_back (std::make_unique<CP> (id ("srcLoopMode"), name ("Loop"), juce::StringArray { "Off", "Loop" }, 0));
            out.push_back (std::make_unique<FP> (id ("srcGain"), name ("Source Gain"), R (-24.0f, 24.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<CP> (id ("srcMono"), name ("Channels"),
                juce::StringArray { "Stereo", "Mono", "Left", "Right" }, 0));

            // --- File Position ----------------------------------------------------
            out.push_back (std::make_unique<FP> (id ("posBase"), name ("File Position"), R (0.0f, 1.0f, 1.0e-5f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("posScanRate"), name ("Scan Rate"), R (-2.0f, 2.0f, 0.001f), 0.0f));
            out.push_back (std::make_unique<BP> (id ("posScanSync"), name ("Scan Sync"), false));
            out.push_back (std::make_unique<CP> (id ("posScanDiv"), name ("Scan Division"), divisionChoices(), 2));   // 1 bar
            out.push_back (std::make_unique<FP> (id ("posEnvAmt"), name ("Position Env"), R (-1.0f, 1.0f, 0.001f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("posLfoAmt"), name ("Position LFO"), R (0.0f, 1.0f, 0.001f), 0.0f));

            // --- Grain cloud ------------------------------------------------------
            out.push_back (std::make_unique<FP> (id ("grainSize"), name ("Grain Size"), skewRange (1.0f, 500.0f, 0.1f, 80.0f), 60.0f));
            out.push_back (std::make_unique<FP> (id ("spray"), name ("Spray"), R (0.0f, 1.0f, 0.001f), 0.1f));
            out.push_back (std::make_unique<FP> (id ("density"), name ("Density"), skewRange (0.1f, 100.0f, 0.01f, 20.0f), 20.0f));
            out.push_back (std::make_unique<CP> (id ("densityMode"), name ("Density Mode"),
                juce::StringArray { "Rate", "Overlap" }, 1));   // Overlap
            out.push_back (std::make_unique<CP> (id ("window"), name ("Window"),
                juce::StringArray { "Hann", "Gauss", "Tukey" }, 0));
            out.push_back (std::make_unique<FP> (id ("grainPan"), name ("Grain Pan"), R (0.0f, 1.0f, 0.001f), 0.2f));

            // --- Smooth / Texture mode --------------------------------------------
            // Interp: source-read interpolation. Default Cubic (cleaner high reads).
            out.push_back (std::make_unique<CP> (id ("interp"), name ("Interp"),
                juce::StringArray { "Linear", "Cubic" }, 1));   // Cubic
            // Smoothness: musical macro that fuses the cloud into a glassy pad without
            // touching Density. 0 (default) is bit-identical to the classic cloud.
            out.push_back (std::make_unique<FP> (id ("smoothness"), name ("Smoothness"), R (0.0f, 1.0f, 0.001f), 0.0f));

            // --- Rhythmic (tempo-synced) grains -----------------------------------
            out.push_back (std::make_unique<BP> (id ("densitySync"), name ("Density Sync"), false));
            out.push_back (std::make_unique<CP> (id ("densityDiv"), name ("Density Division"), divisionChoices(), 6));   // 1/8
            out.push_back (std::make_unique<BP> (id ("grainSizeSync"), name ("Grain Size Sync"), false));
            out.push_back (std::make_unique<CP> (id ("grainSizeDiv"), name ("Grain Size Division"), divisionChoices(), 6)); // 1/8

            // --- Unison (per-grain stacked detune) --------------------------------
            out.push_back (std::make_unique<IP> (id ("unisonVoices"), name ("Unison Voices"), 1, 4, 1));
            out.push_back (std::make_unique<FP> (id ("unisonDetune"), name ("Unison Detune"), R (0.0f, 50.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("unisonWidth"), name ("Unison Width"), R (0.0f, 1.0f, 0.001f), 0.5f));

            // --- Pitch (per-layer offsets) ----------------------------------------
            out.push_back (std::make_unique<IP> (id ("tuneCoarse"), name ("Coarse"), -24, 24, 0));
            out.push_back (std::make_unique<FP> (id ("tuneFine"), name ("Fine"), R (-100.0f, 100.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("pitchSpread"), name ("Pitch Spread"), R (0.0f, 100.0f, 0.1f), 0.0f));

            // --- Formant (spectral shaping; +/-12 st, Lock = anti-chipmunk hold) ---
            out.push_back (std::make_unique<FP> (id ("formantShift"), name ("Formant"), R (-12.0f, 12.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<BP> (id ("formantLock"), name ("Formant Lock"), false));

            // --- Per-layer mix ----------------------------------------------------
            out.push_back (std::make_unique<FP> (id ("level"), name ("Level"), R (-60.0f, 6.0f, 0.1f), 0.0f));
            out.push_back (std::make_unique<FP> (id ("pan"), name ("Pan"), R (-1.0f, 1.0f, 0.001f), 0.0f));
        };

        addLayer ("A_", "A ");
        addLayer ("B_", "B ");

        // -------------------------------------------------------------------------
        //  TWO-LAYER blend + capture (top-level, non-per-layer).
        // -------------------------------------------------------------------------
        // Morph 0 = A only, 1 = B only, 0.5 = equal blend. Default 0 (A only) so a
        // freshly-inserted instance behaves like the classic single-layer patch.
        out.push_back (std::make_unique<FP> (prefix + "layerMorph", "Layer Morph", R (0.0f, 1.0f, 0.001f), 0.0f));
        // layerSel is UI-ONLY (which layer the editor edits / capture targets by
        // default). It is intentionally NON-automatable so hosts don't sweep it.
        out.push_back (std::make_unique<CP> (prefix + "layerSel", "Layer Select",
            juce::StringArray { "A", "B" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable (false)));

        // Live Capture.
        out.push_back (std::make_unique<BP> (prefix + "captureArm", "Capture Arm", false,
            juce::AudioParameterBoolAttributes().withAutomatable (false)));
        out.push_back (std::make_unique<FP> (prefix + "captureSecs", "Capture Length", R (0.5f, 8.0f, 0.1f), 2.0f));
        out.push_back (std::make_unique<CP> (prefix + "captureTarget", "Capture Target",
            juce::StringArray { "A", "B" }, 0,
            juce::AudioParameterChoiceAttributes().withAutomatable (false)));

        // -------------------------------------------------------------------------
        //  GLOBAL (shared) parameters — single id, pushed to BOTH engines.
        // -------------------------------------------------------------------------
        // --- Pitch env / LFO / glide (shared) ------------------------------------
        out.push_back (std::make_unique<FP> (prefix + "glide", "Glide", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<CP> (prefix + "glideMode", "Glide Mode",
            juce::StringArray { "Always", "Legato" }, 1));   // Legato
        out.push_back (std::make_unique<FP> (prefix + "pitchEnvAmt", "Pitch Env", R (-24.0f, 24.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "pitchEnvAttack", "Pitch Env Attack", skewRange (0.0f, 2000.0f, 0.1f, 100.0f), 5.0f));
        out.push_back (std::make_unique<FP> (prefix + "pitchEnvDecay", "Pitch Env Decay", skewRange (1.0f, 4000.0f, 0.1f, 300.0f), 200.0f));
        out.push_back (std::make_unique<FP> (prefix + "pitchLfoAmt", "Vibrato Depth", R (0.0f, 1200.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "pitchLfoRate", "Vibrato Rate", skewRange (0.05f, 20.0f, 0.01f, 5.0f), 5.0f));

        // --- Key snap (shared) ----------------------------------------------------
        out.push_back (std::make_unique<BP> (prefix + "keySnap", "Key Snap", false));
        out.push_back (std::make_unique<CP> (prefix + "keyRoot", "Key Root", keyRootChoices(), 0));
        out.push_back (std::make_unique<CP> (prefix + "keyScale", "Key Scale", keyScaleChoices(), 0));
        // Follow Song Key defaults OFF per suite MEMORY policy; header KeyButton menu
        // is authoritative.
        out.push_back (std::make_unique<BP> (prefix + "followKey", "Follow Song Key", false));

        // --- Sub oscillator (shared) ---------------------------------------------
        out.push_back (std::make_unique<BP> (prefix + "subOsc", "Sub Osc", false));
        out.push_back (std::make_unique<FP> (prefix + "subLevel", "Sub Level", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<CP> (prefix + "subWave", "Sub Wave",
            juce::StringArray { "Sine", "Triangle" }, 0));
        out.push_back (std::make_unique<IP> (prefix + "subOct", "Sub Octave", -2, 0, -1));

        // --- Filter (shared) ------------------------------------------------------
        out.push_back (std::make_unique<CP> (prefix + "filterType", "Filter Type",
            juce::StringArray { "LP24", "HP12", "BP" }, 0));
        out.push_back (std::make_unique<FP> (prefix + "cutoff", "Cutoff", skewRange (20.0f, 20000.0f, 0.1f, 1000.0f), 20000.0f));
        out.push_back (std::make_unique<FP> (prefix + "reso", "Resonance", R (0.0f, 1.0f, 0.001f), 0.1f));
        out.push_back (std::make_unique<FP> (prefix + "filterKeyTrack", "Key Track", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "filterEnvAmt", "Filter Env", R (-100.0f, 100.0f, 0.1f), 0.0f));

        // --- Velocity routing (shared; per-voice at note level / per spawn) -------
        // Each -1..+1; the note velocity offsets File Position, grain size and filter
        // cutoff by these amounts so harder hits scrub / brighten. 0 = no-op.
        out.push_back (std::make_unique<FP> (prefix + "velToPos", "Vel -> Position", R (-1.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "velToSize", "Vel -> Grain Size", R (-1.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "velToCutoff", "Vel -> Cutoff", R (-1.0f, 1.0f, 0.001f), 0.0f));

        // --- Envelopes (shared) ---------------------------------------------------
        out.push_back (std::make_unique<FP> (prefix + "ampAttack", "Amp Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
        out.push_back (std::make_unique<FP> (prefix + "ampDecay", "Amp Decay", skewRange (1.0f, 8000.0f, 0.1f, 400.0f), 400.0f));
        out.push_back (std::make_unique<FP> (prefix + "ampSustain", "Amp Sustain", R (0.0f, 1.0f, 0.001f), 1.0f));
        out.push_back (std::make_unique<FP> (prefix + "ampRelease", "Amp Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
        out.push_back (std::make_unique<FP> (prefix + "fltAttack", "Filter Attack", skewRange (0.0f, 5000.0f, 0.1f, 100.0f), 5.0f));
        out.push_back (std::make_unique<FP> (prefix + "fltDecay", "Filter Decay", skewRange (1.0f, 8000.0f, 0.1f, 300.0f), 300.0f));
        out.push_back (std::make_unique<FP> (prefix + "fltSustain", "Filter Sustain", R (0.0f, 1.0f, 0.001f), 0.4f));
        out.push_back (std::make_unique<FP> (prefix + "fltRelease", "Filter Release", skewRange (1.0f, 10000.0f, 0.1f, 300.0f), 300.0f));
        out.push_back (std::make_unique<FP> (prefix + "envVelSens", "Velocity Sens", R (0.0f, 1.0f, 0.001f), 0.5f));

        // --- LFOs (shared) --------------------------------------------------------
        out.push_back (std::make_unique<FP> (prefix + "lfo1Rate", "LFO1 Rate", skewRange (0.01f, 30.0f, 0.01f, 2.0f), 1.0f));
        out.push_back (std::make_unique<CP> (prefix + "lfo1Shape", "LFO1 Shape",
            juce::StringArray { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, 0));
        out.push_back (std::make_unique<BP> (prefix + "lfo1Sync", "LFO1 Sync", false));
        out.push_back (std::make_unique<CP> (prefix + "lfo1Div", "LFO1 Division", divisionChoices(), 4));   // 1/4
        out.push_back (std::make_unique<CP> (prefix + "lfo1Dest", "LFO1 Destination",
            juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, 0));
        out.push_back (std::make_unique<FP> (prefix + "lfo1Amt", "LFO1 Amount", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "lfo2Rate", "LFO2 Rate", skewRange (0.01f, 30.0f, 0.01f, 2.0f), 1.0f));
        out.push_back (std::make_unique<CP> (prefix + "lfo2Shape", "LFO2 Shape",
            juce::StringArray { "Sine", "Tri", "Saw", "Square", "S&H", "Random" }, 0));
        out.push_back (std::make_unique<BP> (prefix + "lfo2Sync", "LFO2 Sync", false));
        out.push_back (std::make_unique<CP> (prefix + "lfo2Div", "LFO2 Division", divisionChoices(), 4));   // 1/4
        out.push_back (std::make_unique<CP> (prefix + "lfo2Dest", "LFO2 Destination",
            juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size", "Density", "Pan", "Amp" }, 0));
        out.push_back (std::make_unique<FP> (prefix + "lfo2Amt", "LFO2 Amount", R (0.0f, 1.0f, 0.001f), 0.0f));

        // --- Shared mod AD --------------------------------------------------------
        out.push_back (std::make_unique<FP> (prefix + "modEnvAttack", "Mod Env Attack", skewRange (0.0f, 2000.0f, 0.1f, 100.0f), 5.0f));
        out.push_back (std::make_unique<FP> (prefix + "modEnvDecay", "Mod Env Decay", skewRange (1.0f, 4000.0f, 0.1f, 300.0f), 300.0f));

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
            out.push_back (std::make_unique<CP> (sid, nm + "Source",
                juce::StringArray { "Off", "LFO1", "LFO2", "Mod Env", "Velocity",
                                    "Key Track", "Pressure", "Random" }, 0));
            out.push_back (std::make_unique<CP> (did, nm + "Dest",
                juce::StringArray { "Off", "File Position", "Cutoff", "Pitch", "Grain Size",
                                    "Density", "Pan", "Amp", "Morph", "Formant" }, 0));
            out.push_back (std::make_unique<FP> (aid, nm + "Amount", R (-1.0f, 1.0f, 0.001f), 0.0f));
        }

        // --- Per-note pressure routing (shared) ----------------------------------
        // How far a voice's MPE pressure (0..1) pushes its fixed destinations. These
        // only take effect once the host actually sends pressure, so leaving them at
        // their defaults keeps no-expression playback bit-identical.
        out.push_back (std::make_unique<FP> (prefix + "pressToCutoff", "Pressure -> Cutoff", R (-8.0f, 8.0f, 0.1f), 2.0f));
        out.push_back (std::make_unique<FP> (prefix + "pressToAmp", "Pressure -> Amp", R (0.0f, 4.0f, 0.01f), 0.25f));

        // --- MPE / pitch-bend (shared) -------------------------------------------
        // mpeMode: Classic (global wheel bends every held note; bendRange defaults to
        // the classic 2 st) vs. MPE (per-channel wheel bends the one note on that
        // channel; the spec default is ±48 st but bendRange is still authoritative).
        // bendRange: semitones at full wheel throw, default 2 st so a plain host with
        // the wheel behaves musically. No pitch-bend received == no expression == the
        // engine stays bit-identical to today.
        out.push_back (std::make_unique<CP> (prefix + "mpeMode", "Bend Mode",
            juce::StringArray { "Classic", "MPE" }, 0));
        out.push_back (std::make_unique<FP> (prefix + "bendRange", "Bend Range", R (1.0f, 48.0f, 1.0f), 2.0f));

        // --- Built-in reverb (global, on the blended output) ---------------------
        // reverbMix 0 (default) is an EXACT pass-through — the reverb is skipped
        // entirely so a fresh instance sounds identical to today. Size / damp shape
        // the Freeverb tail; the wet is taken pure and crossfaded by reverbMix.
        out.push_back (std::make_unique<FP> (prefix + "reverbMix", "Reverb Mix", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "reverbSize", "Reverb Size", R (0.0f, 1.0f, 0.001f), 0.5f));
        out.push_back (std::make_unique<FP> (prefix + "reverbDamp", "Reverb Damp", R (0.0f, 1.0f, 0.001f), 0.5f));

        // --- Cross-plugin bus drive (shared) -------------------------------------
        // broadcastKey (default OFF): when on, playing the granulator PUBLISHES the
        // currently-played (key-snapped) root + scale to the cross-plugin key bus so
        // the suite follows the granulator's key. Guarded against feedback — never
        // broadcasts while followKey is on. transientTrigger (default OFF): when on,
        // an external transient pulse (Rhythm Genie / Side Chain) re-fires the grain
        // cloud in time; transientSens scales the accent strength.
        out.push_back (std::make_unique<BP> (prefix + "broadcastKey", "Broadcast Key", false,
            juce::AudioParameterBoolAttributes().withAutomatable (false)));
        out.push_back (std::make_unique<BP> (prefix + "transientTrigger", "Transient Trigger", false));
        out.push_back (std::make_unique<FP> (prefix + "transientSens", "Transient Sens", R (0.0f, 1.0f, 0.001f), 1.0f));

        // --- Global / output ------------------------------------------------------
        out.push_back (std::make_unique<IP> (prefix + "voices", "Voices", 1, 16, 8));
        out.push_back (std::make_unique<CP> (prefix + "voiceMode", "Voice Mode",
            juce::StringArray { "Poly", "Mono", "Legato" }, 0));
        out.push_back (std::make_unique<FP> (prefix + "width", "Width", R (0.0f, 2.0f, 0.001f), 1.0f));
        out.push_back (std::make_unique<FP> (prefix + "tone", "Tone", R (-1.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (prefix + "volume", "Volume", R (-60.0f, 6.0f, 0.1f), -6.0f));
    }

    // =============================================================================
    //  Lifecycle
    // =============================================================================

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sr, int samplesPerBlock, int /*numChannels*/) override
    {
        sampleRate = sr;

        for (int i = 0; i < kNumLayers; ++i)
        {
            auto& ls = layers[(size_t) i];
            ls.engine.prepare (sr, samplesPerBlock, 2);
            // Re-hand the current source to the engine (prepare() reset its pools).
            const juce::SpinLock::ScopedLockType lock (ls.sourceLock);
            ls.engine.setSource (ls.sourceDsp);
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

    // Grain clouds, the reverb tail and every held voice carry across a transport
    // jump, so a relocate without this replays the previous position's cloud.
    void reset() override
    {
        for (auto& l : layers) l.engine.allNotesOff();
        reverb.reset();
        layerScratch.clear();
        reverbDry.clear();
        channelBendFrac.fill (0.0f);
        capture.armed.store (false);
        capture.done.store (false);
        capture.filled.store (0);
        capture.writePos.store (0);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }
    void setSidechain (const juce::AudioBuffer<float>* in) noexcept override { captureIn = in; }

    // MESSAGE THREAD. The shell decodes/edits and hands over the dsp view only.
    void setSource (Layer layer, std::shared_ptr<const SourceSample> src)
    {
        auto& ls = layers[(size_t) layer];
        if (src && std::find (ls.retainedSources.begin(), ls.retainedSources.end(), src)
                       == ls.retainedSources.end())
            ls.retainedSources.push_back (src);
        {
            const juce::SpinLock::ScopedLockType lock (ls.sourceLock);
            ls.sourceDsp = std::move (src);
        }
        // Only the message thread accesses this list. A count of one means the
        // source is neither published nor held by the rendering engine, so no
        // audio callback can acquire it now.
        ls.retainedSources.erase (std::remove_if (ls.retainedSources.begin(), ls.retainedSources.end(),
            [] (const auto& source) { return source.use_count() == 1; }), ls.retainedSources.end());
    }

    void process (juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();

        // --- Live Capture: record the INPUT bus BEFORE we overwrite the buffer ----
        // Ring write only (lock-free); message-thread serviceCapture() decodes it.
        if (capture.armed.load (std::memory_order_acquire)
            && ! capture.done.load (std::memory_order_acquire)
            && capture.capacity > 0)
        {
            const bool haveInput = captureIn != nullptr && captureIn->getNumChannels() > 0;
            if (haveInput)
            {
                const auto& inBuffer = *captureIn;
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
        auto& outBus = buffer;   // the module is handed the output buffer directly
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
        // The shell reads the playhead once per block and pushes it down.
        if (transport.hasHostBpm && transport.bpm > 1.0) { bpm = transport.bpm; haveHostBpm = true; }
        if (transport.hasPpq) ppq = transport.ppqPosition;
        playing = transport.isPlaying;
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

            {
                // C++17 atomic shared_ptr loads may block internally. If the
                // publisher is busy, keep this engine's previous source for one
                // block. Its retained owner also prevents callback-side frees.
                const juce::SpinLock::ScopedTryLockType lock (ls.sourceLock);
                if (lock.isLocked()) ls.engine.setSource (ls.sourceDsp);
            }
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

    // --- Live Capture: the audio thread writes, the shell drains -------------
    void armCaptureRing (int targetLayer, int targetSamples, double sr) noexcept
    {
        capture.targetLayer.store (targetLayer);
        capture.target.store (juce::jlimit (0, capture.capacity, targetSamples));
        capture.captureSr = sr;
        capture.filled.store (0);
        capture.writePos.store (0);
        capture.done.store (false);
        capture.armed.store (true);
    }

    void disarmCaptureRing() noexcept { capture.armed.store (false); }
    bool captureArmed()  const noexcept { return capture.armed.load(); }
    bool captureDone()   const noexcept { return capture.done.load(); }
    int  captureFilled() const noexcept { return capture.filled.load(); }
    int  captureTargetLayer() const noexcept { return capture.targetLayer.load(); }
    double captureSampleRate() const noexcept { return capture.captureSr; }
    const std::vector<float>& captureData() const noexcept { return capture.data; }
    int captureCapacity() const noexcept { return capture.capacity; }

    const sattari::rt::AtomicSnapshot<Engine::GrainVizFrame>& vizFor (Layer l) const noexcept
    {
        return viz[(size_t) l];
    }

    // Key-broadcast intent, published by the audio thread and read by the shell's
    // message-thread timer (the shell owns the throttle and the bus write).
    bool broadcastWanted()   const noexcept { return broadcastActive.load (std::memory_order_relaxed); }
    int  broadcastRoot()     const noexcept { return broadcastWantRoot.load (std::memory_order_relaxed); }
    int  broadcastScale()    const noexcept { return broadcastWantScale.load (std::memory_order_relaxed); }

    bool isFollowingBusKey() const noexcept { return followingBus.load (std::memory_order_relaxed); }
    int  effectiveRoot()     const noexcept { return effKeyRoot.load (std::memory_order_relaxed); }
    int  effectiveScale()    const noexcept { return effKeyScale.load (std::memory_order_relaxed); }
};

} // namespace sattari::dsp
