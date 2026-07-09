#pragma once

// =============================================================================
//  Sattari Audio Suite — polyphonic granular synthesis engine (header-only, RT)
// -----------------------------------------------------------------------------
//  namespace sattari::dsp, class SattariGranularEngine.
//
//  The whole DSP core of Sattari Granulator lives here so the contract test and
//  the plugin processor depend on exactly one file. Everything is fixed-size and
//  nothing allocates or locks after prepare():
//
//    * a single global POD grain pool  (std::array<Grain, kMaxGrains=256>) with a
//      free-list ring — spawn pops a free index, death pushes it back; a full pool
//      DROPS the spawn (it never allocates or steals-with-click);
//    * a fixed 16-voice pool of hand-rolled PODs (ADSR / AD envelopes / LFOs / a
//      per-voice TPT state-variable filter / a sub oscillator) so voice reset is
//      a trivial struct assignment;
//    * a precomputed 2048-point window LUT (Hann / Gauss / Tukey), overlap-
//      normalised grain gain, linear (or cubic) interpolated source read;
//    * a global tanh soft-ceiling limiter + NaN/denormal flush so a runaway grain
//      pile-up can never leave the plugin hot or non-finite.
//
//  MODULATION RATE CONTRACT (see setParams / renderVoice):
//    PER-BLOCK          — recompute filter/amp/env coefficients, advance the File
//                         Position runner (Static/Envelope/LFO/FreeScan), advance
//                         glide toward target, refresh LFO/env increments, poll
//                         the tempo/key bus.
//    PER-GRAIN-SPAWN    — sample every randomised value (spray, size jitter, pitch
//                         spread, pan) and evaluate the FULL multiplicative pitch
//                         chain, snapshot the voice amp into grain.gain.
//    PER-SAMPLE         — only the two accumulator adds per grain + the SVF.
//
//  KEY-SNAP: applied ONCE to the integer note at noteOn via snapMidiToScale, so
//  the note lands in the scale but coarse / fine / env / LFO / glide / spread all
//  multiply the resulting ratio and are NEVER re-quantised — vibrato and detune
//  spread stay smooth around a snapped centre (documented UX decision).
//
//  PITCH CHAIN (per grain, multiplicative in the ratio domain):
//    ratio = basePitchRatio
//          * 2^(coarse/12)
//          * 2^(fineCents/1200)
//          * 2^(pitchEnv * envSemis / 12)
//          * 2^(pitchLfo * lfoCents / 1200)
//          * glideRatio
//          * 2^(rand(-1,1) * spreadCents / 1200)
//    grain.srcInc = ratio * (srcSR / outSR)    (negative when reverse is on)
//
//  SPAWN SCHEDULER: spawnInterval = outSR / grainsPerSecond, where
//  grainsPerSecond = density (Rate mode) OR overlap / grainSizeSeconds (Overlap
//  mode). Each block advances a fractional accumulator by numSamples; while it is
//  >= spawnInterval, pop a free grain, init it from the voice's current modulated
//  state + per-spawn random, subtract spawnInterval. This handles both < 1 and
//  > 1 spawns per block.
// =============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SattariPitchDetector.h"   // sattari::dsp::snapMidiToScale + ScaleId

namespace sattari::dsp
{

// -----------------------------------------------------------------------------
//  The immutable source the processor publishes to the engine via shared_ptr.
//  (Mirrors plugins/granulator/src/SourceSample.h field-for-field so the DSP
//  core stays free of any plugin-local include.)
// -----------------------------------------------------------------------------
struct SourceSample
{
    juce::AudioBuffer<float> audio;
    double   sourceSampleRate = 48000.0;
    int      lengthSamples    = 0;
    int      rootNote         = 60;
    uint32_t version          = 0;
};

class SattariGranularEngine
{
public:
    // -------------------------------------------------------------------------
    //  Fixed capacities. Nothing scales these at runtime.
    // -------------------------------------------------------------------------
    static constexpr int kMaxVoices   = 16;
    static constexpr int kMaxGrains   = 256;
    static constexpr int kWindowSize  = 2048;    // window LUT resolution
    static constexpr int kVizGrains   = 16;      // grain markers surfaced to the UI

    // Choice enumerations (must match the APVTS combo ordinals in the processor).
    enum class Window        { Hann = 0, Gauss, Tukey };
    enum class DensityMode   { Rate = 0, Overlap };
    enum class LoopMode      { Off = 0, Loop };
    enum class FilterType    { LP24 = 0, HP12, BP };
    enum class GlideMode     { Always = 0, Legato };
    enum class VoiceMode     { Poly = 0, Mono, Legato };
    enum class SubWave       { Sine = 0, Triangle };
    enum class ScanMode      { Static = 0, Envelope, Lfo, FreeScan }; // File-Position runner
    enum class LfoShape      { Sine = 0, Tri, Saw, Square, SampleHold, Random };
    enum class LfoDest       { Off = 0, Position, Cutoff, Pitch, GrainSize, Density, Pan, Amp };
    enum class Interp        { Linear = 0, Cubic };

    // =========================================================================
    //  PODs
    // =========================================================================

    // A single live grain. Trivially copyable; lives in a std::array pool.
    struct Grain
    {
        bool  active   = false;
        float srcPos   = 0.0f;   // fractional read index into the source (samples)
        float srcInc   = 0.0f;   // per-out-sample advance = ratio*(srcSR/outSR); <0 = reverse
        float winPhase = 0.0f;   // 0..1 across the whole grain
        float winInc   = 0.0f;   // 1 / grainLenSamples
        float panL     = 0.707f; // equal-power gains precomputed at spawn
        float panR     = 0.707f;
        float gain     = 1.0f;   // overlap-normalise (1/sqrt(overlap)) * voice amp @ spawn
        float loStart  = 0.0f;   // active source window [loStart, hiEnd] in samples
        float hiEnd    = 0.0f;
        int   voiceIndex = -1;   // owner voice
    };

    // Hand-rolled linear ADSR (amp + filter). value/stage reset trivially.
    struct HandRolledADSR
    {
        float a = 0.005f, d = 0.4f, s = 1.0f, r = 0.3f; // seconds / level
        float value = 0.0f;
        int   stage = 0;   // 0 idle, 1 attack, 2 decay, 3 sustain, 4 release

        void noteOn()  noexcept { stage = 1; }
        void noteOff() noexcept { if (stage != 0) stage = 4; }

        float tick (float dt) noexcept
        {
            switch (stage)
            {
                case 1: // attack
                    value += (a > 1.0e-6f) ? dt / a : 1.0f;
                    if (value >= 1.0f) { value = 1.0f; stage = 2; }
                    break;
                case 2: // decay
                    value += (d > 1.0e-6f) ? (s - 1.0f) * dt / d : (s - value);
                    if ((s >= 1.0f) || value <= s) { value = s; stage = 3; }
                    break;
                case 3: // sustain
                    value = s;
                    break;
                case 4: // release — LINEAR to zero over `r` seconds (deterministic,
                        // click-free, and guarantees the voice reaches idle in
                        // bounded time regardless of the starting level).
                    value -= (r > 1.0e-6f) ? dt / r : 1.0f;
                    if (value <= 1.0e-5f) { value = 0.0f; stage = 0; }
                    break;
                default:
                    value = 0.0f;
                    break;
            }
            return value;
        }

        bool idle() const noexcept { return stage == 0; }
    };

    // Dedicated attack/decay envelope (pitch AD + shared "mod" AD). One-shot: it
    // rises to 1 then falls to 0 and stops; no sustain, no release.
    struct EnvRunner
    {
        float a = 0.005f, d = 0.2f;  // seconds
        float value = 0.0f;
        int   stage = 0;             // 0 idle, 1 attack, 2 decay

        void trigger() noexcept { value = 0.0f; stage = 1; }

        float tick (float dt) noexcept
        {
            switch (stage)
            {
                case 1:
                    value += (a > 1.0e-6f) ? dt / a : 1.0f;
                    if (value >= 1.0f) { value = 1.0f; stage = 2; }
                    break;
                case 2:
                    value += (d > 1.0e-6f) ? -dt / d : -1.0f;
                    if (value <= 0.0f) { value = 0.0f; stage = 0; }
                    break;
                default:
                    break;
            }
            return value;
        }

        bool idle() const noexcept { return stage == 0; }
    };

    // Free-running LFO. tick() returns a bipolar -1..+1 value (Amp/unipolar
    // consumers half-rectify at the use site). Sample & Hold / Random latch a new
    // value each cycle wrap.
    struct LfoRunner
    {
        int   shape = 0;      // LfoShape ordinal
        float phase = 0.0f;   // 0..1
        float inc   = 0.0f;   // per-block phase advance
        float held  = 0.0f;   // latched value for S&H / Random
        uint32_t rng = 0x9E3779B9u;

        float nextRand() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng & 0xFFFFFFu) / (float) 0x800000u - 1.0f; // -1..+1
        }

        // Advance by one block-tick and return the current shaped value.
        float tick() noexcept
        {
            const float p = phase;
            float v = 0.0f;
            switch ((LfoShape) shape)
            {
                case LfoShape::Sine:   v = std::sin (p * juce::MathConstants<float>::twoPi); break;
                case LfoShape::Tri:    v = 4.0f * std::fabs (p - 0.5f) - 1.0f; break;
                case LfoShape::Saw:    v = 2.0f * p - 1.0f; break;
                case LfoShape::Square: v = (p < 0.5f) ? 1.0f : -1.0f; break;
                case LfoShape::SampleHold:
                case LfoShape::Random: v = held; break;
                default: break;
            }
            phase += inc;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                if ((LfoShape) shape == LfoShape::SampleHold
                    || (LfoShape) shape == LfoShape::Random)
                    held = nextRand();
            }
            return v;
        }
    };

    // TPT / Zavalishin state-variable filter state, one per channel.
    struct SvfState { float ic1eq = 0.0f, ic2eq = 0.0f; };

    // -------------------------------------------------------------------------
    //  A polyphonic voice: everything a held MIDI note owns.
    // -------------------------------------------------------------------------
    struct Voice
    {
        int   midiNote = -1;
        float velocity = 0.0f;
        bool  held     = false;
        uint64_t age   = 0;   // spawn order, for deterministic oldest-note stealing

        float basePitchRatio = 1.0f;   // 2^((noteForRatio - rootNote)/12), key-snapped note

        HandRolledADSR amp, filterEnv;
        EnvRunner      pitchEnv;    // dedicated AD for the pitch envelope
        EnvRunner      modEnv;      // shared AD mod env (feeds File Position in v1)
        LfoRunner      pitchLfo, posLfo;

        float glideCurrentRatio = 1.0f, glideTargetRatio = 1.0f;

        float posBase       = 0.0f;   // File-Position playhead, normalised 0..1 in [start,end]
        float posScanPhase  = 0.0f;   // FreeScan / tempo-sync accumulator (0..1)

        SvfState fL, fR;

        float subPhase = 0.0f;
        float subRatio = 1.0f;        // sub oscillator freq ratio vs. played fundamental

        float spawnAccumulator = 0.0f;  // fractional grain scheduler
        float spawnInterval    = 1.0e9f;

        uint32_t sourceVersion = 0;

        void reset() noexcept { *this = Voice{}; }

        bool active() const noexcept { return midiNote >= 0 || ! amp.idle(); }
    };

    // -------------------------------------------------------------------------
    //  POD parameter snapshot pushed message/UI-thread -> audio (mirrors APVTS).
    //  keyScaleId is already a ScaleId ordinal resolved by the processor.
    // -------------------------------------------------------------------------
    struct EngineParams
    {
        // source playback offsets (cheap, automatable)
        float srcStart = 0.0f, srcEnd = 1.0f;
        bool  srcReverse = false;
        int   srcLoopMode = 0;      // LoopMode
        float srcGainDb = 0.0f;

        // File Position
        float posBase = 0.0f, posScanRate = 0.0f;
        bool  posScanSync = false;
        int   posScanDiv = 2;       // division ordinal (see divisionBeats)
        float posEnvAmt = 0.0f, posLfoAmt = 0.0f;

        // grain cloud
        float grainSizeMs = 60.0f, spray = 0.1f, density = 20.0f;
        int   densityMode = (int) DensityMode::Overlap;
        int   window = (int) Window::Hann;
        float grainPan = 0.2f;

        // unison / stacked detune (per grain-spawn). unisonVoices == 1 is the
        // default and is BIT-IDENTICAL to no unison (no extra grains, unity gain).
        int   unisonVoices = 1;          // 1..4 stacked copies per spawn
        float unisonDetuneCents = 0.0f;  // 0..50 total pitch spread across the stack
        float unisonWidth = 0.5f;        // 0..1 stereo spread of the stack

        // pitch
        int   tuneCoarse = 0;
        float tuneFine = 0.0f, pitchSpreadCents = 0.0f;
        float glide = 0.0f; int glideMode = (int) GlideMode::Legato;
        float pitchEnvSemis = 0.0f, pitchEnvAttackMs = 5.0f, pitchEnvDecayMs = 200.0f;
        float pitchLfoCents = 0.0f, pitchLfoRateHz = 5.0f;

        // key snap
        bool  keySnap = false;
        int   keyRoot = 0;
        int   keyScaleId = (int) ScaleId::Major;   // ScaleId ordinal
        bool  followKey = false;

        // sub oscillator
        bool  subOsc = false; float subLevel = 0.0f;
        int   subWave = (int) SubWave::Sine; int subOct = -1;

        // filter
        int   filterType = (int) FilterType::LP24;
        float cutoff = 20000.0f, reso = 0.1f, filterKeyTrack = 0.0f, filterEnvAmt = 0.0f;

        // envelopes (ms except sustain 0..1)
        float ampA = 5.0f, ampD = 400.0f, ampS = 1.0f, ampR = 300.0f;
        float fltA = 5.0f, fltD = 300.0f, fltS = 0.4f, fltR = 300.0f;
        float envVelSens = 0.5f;

        // LFOs
        float lfo1RateHz = 1.0f; int lfo1Shape = 0; bool lfo1Sync = false;
        int   lfo1Div = 2; int lfo1Dest = 0; float lfo1Amt = 0.0f;
        float lfo2RateHz = 1.0f; int lfo2Shape = 0; bool lfo2Sync = false;
        int   lfo2Div = 2; int lfo2Dest = 0; float lfo2Amt = 0.0f;

        // shared mod AD
        float modEnvA = 5.0f, modEnvD = 300.0f;

        // global / output
        int   voices = 8; int voiceMode = (int) VoiceMode::Poly;
        float width = 1.0f, tone = 0.0f, volumeDb = -6.0f;
    };

    // -------------------------------------------------------------------------
    //  Small POD published for the editor's live grain visualisation.
    // -------------------------------------------------------------------------
    struct GrainVizFrame
    {
        float    posNorm  = 0.0f;   // representative File Position 0..1
        float    scanNorm = 0.0f;   // scan phase 0..1
        uint32_t nActiveGrains = 0;
        uint32_t nActiveVoices = 0;
        float    grainPos[kVizGrains] = {};   // 0..1 read position of up to 16 grains
        float    grainAge[kVizGrains] = {};   // 0..1 window phase (fade by age)
    };

    // =========================================================================
    //  Lifecycle
    // =========================================================================

    // Sizes the window LUT and resets all pools/voices. Allocates nothing after.
    void prepare (double outSampleRate, int /*maxBlockSize*/, int numOutChannels)
    {
        sr        = outSampleRate > 0.0 ? outSampleRate : 48000.0;
        numOut    = juce::jlimit (1, 2, numOutChannels);
        buildWindowLuts();
        resetAll();
    }

    // Engine keeps the shared_ptr alive; render() borrows a raw view per block.
    void setSource (std::shared_ptr<const SourceSample> src) noexcept
    {
        source = std::move (src);
    }

    // Push the POD; the audio thread reads it once per block.
    void setParams (const EngineParams& p) noexcept { params = p; }

    // Per-block bus feed; the processor has already resolved bus 0/1/2 -> ScaleId.
    void setBusState (int busRoot, int busScale, double bpm,
                      double ppqPosition, bool playing) noexcept
    {
        busRootNote  = busRoot;
        busScaleId   = busScale;
        hostBpm      = bpm > 1.0 ? bpm : hostBpm;
        hostPpq      = ppqPosition;
        hostPlaying  = playing;
    }

    // =========================================================================
    //  MIDI (called on the audio thread while walking the MidiBuffer)
    // =========================================================================

    void noteOn (int midiNote, float velocity) noexcept
    {
        const auto& p = params;

        // Mono / Legato modes reuse a single voice and (optionally) glide.
        if ((VoiceMode) p.voiceMode == VoiceMode::Mono
            || (VoiceMode) p.voiceMode == VoiceMode::Legato)
        {
            int vi = firstActiveVoice();
            if (vi < 0) vi = 0;
            Voice& v = voices[(size_t) vi];
            const bool wasHeld = v.held && ! v.amp.idle();
            startVoice (v, midiNote, velocity, /*legatoGlide=*/wasHeld
                        && (VoiceMode) p.voiceMode == VoiceMode::Legato);
            return;
        }

        // Poly: find a free voice, else steal the oldest (deterministic).
        int slot = -1;
        const int limit = juce::jlimit (1, kMaxVoices, p.voices);
        int activeCount = 0;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[(size_t) i].active()) ++activeCount;

        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices[(size_t) i].active()) { slot = i; break; }

        if (slot < 0 || activeCount >= limit)
            slot = oldestVoiceIndex();

        Voice& v = voices[(size_t) slot];
        startVoice (v, midiNote, velocity, /*legatoGlide=*/false);
    }

    void noteOff (int midiNote) noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t) i];
            if (v.midiNote == midiNote && v.held)
            {
                v.held = false;
                v.amp.noteOff();
                v.filterEnv.noteOff();
                v.midiNote = -1;   // no longer matchable; grains/amp finish out release
            }
        }
    }

    void allNotesOff() noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[(size_t) i];
            v.held = false;
            v.amp.noteOff();
            v.filterEnv.noteOff();
            v.midiNote = -1;
        }
    }

    // =========================================================================
    //  Render one sample-accurate sub-slice.
    // =========================================================================
    void render (juce::AudioBuffer<float>& out, int startSample, int numSamples)
    {
        if (numSamples <= 0) return;

        const int outCh = juce::jmin (numOut, out.getNumChannels());
        float* L = out.getWritePointer (0, startSample);
        float* R = (outCh > 1) ? out.getWritePointer (1, startSample) : nullptr;

        // Borrow a stable view of the source for the whole slice (block-local ref
        // hold: even if the processor atomic_stores a new source mid-block, the old
        // buffer stays alive under in-flight grains because `src` holds a ref).
        std::shared_ptr<const SourceSample> src = source;
        const bool haveSrc = src && src->lengthSamples > 1;

        const auto& p = params;
        const float invSr = 1.0f / (float) sr;

        // --- PER-BLOCK: resolve global coefficients / rates once ---------------
        const float srcGain = juce::Decibels::decibelsToGain (p.srcGainDb);
        const float outGain = juce::Decibels::decibelsToGain (p.volumeDb);
        const float ceilingGain = 1.0f;

        // Grain length in samples (only sets winInc).
        const float grainLenSamples =
            juce::jmax (2.0f, (p.grainSizeMs * 0.001f) * (float) sr);
        const float winInc = 1.0f / grainLenSamples;

        // Grains-per-second (density) depends on mode.
        float grainsPerSec;
        if ((DensityMode) p.densityMode == DensityMode::Overlap)
        {
            const float overlap = juce::jmax (0.1f, p.density);          // 0.1..8 x
            grainsPerSec = overlap / juce::jmax (1.0e-4f, p.grainSizeMs * 0.001f);
        }
        else
        {
            grainsPerSec = juce::jlimit (0.1f, 200.0f, p.density);       // 1..100 grains/s
        }
        const float spawnInterval = juce::jmax (1.0f, (float) sr / juce::jmax (0.01f, grainsPerSec));

        // Overlap for gain normalisation = grains overlapping at once.
        const float overlapCount = juce::jmax (1.0f, grainsPerSec * (grainLenSamples / (float) sr));
        const float overlapNormGain = 1.0f / std::sqrt (overlapCount);

        // Source window [loStart, hiEnd] in samples, honouring start/end (+reverse).
        const int   srcLen = haveSrc ? src->lengthSamples : 0;
        float regionLo = juce::jlimit (0.0f, 1.0f, p.srcStart) * (float) juce::jmax (0, srcLen - 1);
        float regionHi = juce::jlimit (0.0f, 1.0f, p.srcEnd)   * (float) juce::jmax (0, srcLen - 1);
        if (regionHi < regionLo + 1.0f) regionHi = juce::jmin ((float) juce::jmax (0, srcLen - 1), regionLo + 1.0f);

        const float srcSrRatio = haveSrc ? (float) (src->sourceSampleRate / sr) : 1.0f;

        // Global LFO1 / LFO2 (one instance each; ticked once per sample below and
        // applied to their fixed-list destination). LFO1's rate also drives the
        // per-voice position-scrub LFO.
        lfo1.shape = p.lfo1Shape; lfo1.inc = lfoIncrement (p.lfo1Sync, p.lfo1RateHz, p.lfo1Div) * invSr;
        lfo2.shape = p.lfo2Shape; lfo2.inc = lfoIncrement (p.lfo2Sync, p.lfo2RateHz, p.lfo2Div) * invSr;
        const float lfo1Inc = lfo1.inc;

        // Filter env / amp env dt is per-sample.
        // Position scan advance per sample (Static/FreeScan/tempo-sync).
        const float scanInc = positionScanInc (p) * invSr;

        // Pitch LFO / pos LFO increments (per sample).
        const float pitchLfoInc = juce::jlimit (0.0f, 0.5f, p.pitchLfoRateHz * invSr);

        // Filter cutoff base (per block) — key track / env applied per voice.
        const float baseCutoff = juce::jlimit (20.0f, (float) (0.49 * sr), p.cutoff);
        const float resoQ = 0.5f + juce::jlimit (0.0f, 1.0f, p.reso) * 9.5f; // Q 0.5..10

        GrainVizFrame viz;
        uint32_t vizGrainCount = 0;

        // Zero any channels beyond the two we drive; L/R are fully written below.
        for (int ch = 2; ch < out.getNumChannels(); ++ch)
            out.clear (ch, startSample, numSamples);

        // Advance envelopes / glide / scan per sample; spawn grains per voice; then
        // render every active grain into the (per-voice, but summed) output.

        // --- Per-voice PER-BLOCK prep, spawn scheduling, and modulation --------
        for (int s = 0; s < numSamples; ++s)
        {
            float mixL = 0.0f, mixR = 0.0f;

            // Global LFOs ticked ONCE per sample; their fixed-list destination is
            // resolved into per-sample modulation multipliers/offsets.
            const float lfo1V = lfo1.tick();   // -1..+1
            const float lfo2V = lfo2.tick();   // -1..+1
            const float cutoffLfoOct = lfoDestOctaves ((LfoDest) p.lfo1Dest, lfo1V, p.lfo1Amt)
                                     + lfoDestOctaves ((LfoDest) p.lfo2Dest, lfo2V, p.lfo2Amt);
            const float ampLfoGain   = lfoDestAmp ((LfoDest) p.lfo1Dest, lfo1V, p.lfo1Amt)
                                     * lfoDestAmp ((LfoDest) p.lfo2Dest, lfo2V, p.lfo2Amt);

            for (int vi = 0; vi < kMaxVoices; ++vi)
            {
                Voice& v = voices[(size_t) vi];

                // A voice is "live" while its amp env is running (held or releasing).
                // Even after it idles, any grains it already spawned must keep
                // running to the end of their windows — so we only skip the slot
                // entirely when it is neither live NOR holding in-flight grains.
                const bool voiceLive = v.active();
                if (! voiceLive && ! voiceHasGrain (vi))
                    continue;

                const float dt = invSr;

                float filtEnv = 0.0f, voiceAmp = 0.0f;
                if (voiceLive)
                {
                    // ---- envelopes (per sample) ----------------------------
                    v.amp.a = p.ampA * 0.001f; v.amp.d = p.ampD * 0.001f;
                    v.amp.s = p.ampS;          v.amp.r = p.ampR * 0.001f;
                    v.filterEnv.a = p.fltA * 0.001f; v.filterEnv.d = p.fltD * 0.001f;
                    v.filterEnv.s = p.fltS;          v.filterEnv.r = p.fltR * 0.001f;
                    v.pitchEnv.a = p.pitchEnvAttackMs * 0.001f;
                    v.pitchEnv.d = p.pitchEnvDecayMs  * 0.001f;
                    v.modEnv.a = p.modEnvA * 0.001f; v.modEnv.d = p.modEnvD * 0.001f;

                    const float ampEnv    = v.amp.tick (dt);
                    filtEnv               = v.filterEnv.tick (dt);
                    const float pitchEnvV = v.pitchEnv.tick (dt);
                    const float modEnvV   = v.modEnv.tick (dt);

                    // velocity scaling of the amp env
                    const float vel = 1.0f - p.envVelSens * (1.0f - v.velocity);
                    voiceAmp = ampEnv * vel;

                    // ---- glide toward target (per sample) ------------------
                    const float glideDelta = v.glideTargetRatio - v.glideCurrentRatio;
                    if (std::fabs (glideDelta) > 1.0e-7f)
                    {
                        const float glideTime = 0.001f + juce::jlimit (0.0f, 1.0f, p.glide) * 0.30f;
                        const float coeff = 1.0f - std::exp (-1.0f / ((float) sr * glideTime));
                        v.glideCurrentRatio += coeff * glideDelta;
                    }

                    // ---- LFOs (ticked ONCE per sample; read at spawn) ------
                    v.pitchLfo.inc = pitchLfoInc;
                    const float pitchLfoV = v.pitchLfo.tick();  // -1..+1
                    v.posLfo.inc = lfo1Inc;   // File-Position scrub LFO (LFO1 rate)
                    const float posLfoV = v.posLfo.tick();      // -1..+1

                    // ---- File-Position free-scan / tempo-sync phase advance
                    v.posScanPhase += scanInc;
                    v.posScanPhase -= std::floor (v.posScanPhase);

                    // ---- SPAWN SCHEDULER (fractional accumulator) ----------
                    // New grains only spawn while the note is HELD. On release the
                    // cloud stops regenerating; the in-flight grains finish their
                    // windows and die, so teardown is deterministic (the free ring
                    // is fully restored once the last grain's window completes).
                    v.spawnInterval = spawnInterval;
                    v.spawnAccumulator += 1.0f;
                    while (v.spawnAccumulator >= v.spawnInterval)
                    {
                        v.spawnAccumulator -= v.spawnInterval;
                        if (haveSrc && v.held && voiceAmp > 1.0e-4f)
                            spawnGrain (v, vi, p, regionLo, regionHi, srcLen,
                                        srcSrRatio, winInc, overlapNormGain, voiceAmp,
                                        pitchEnvV, pitchLfoV, posLfoV, modEnvV);
                    }
                }

                // ---- filter coefficients (per sample; cutoff = base * keytrack * env) ----
                float cutoff = baseCutoff;
                if (p.filterKeyTrack > 0.0f && v.midiNote >= 0)
                {
                    const float keyOct = (float) (v.midiNote - 60) / 12.0f;
                    cutoff *= std::pow (2.0f, keyOct * juce::jlimit (0.0f, 1.0f, p.filterKeyTrack));
                }
                else if (p.filterKeyTrack > 0.0f)
                {
                    const float keyOct = (float) (lastNoteFor (v) - 60) / 12.0f;
                    cutoff *= std::pow (2.0f, keyOct * juce::jlimit (0.0f, 1.0f, p.filterKeyTrack));
                }
                if (std::fabs (p.filterEnvAmt) > 1.0e-4f)
                {
                    const float envOct = (p.filterEnvAmt / 100.0f) * 8.0f * filtEnv; // +-8 oct
                    cutoff *= std::pow (2.0f, envOct);
                }
                if (std::fabs (cutoffLfoOct) > 1.0e-6f)
                    cutoff *= std::pow (2.0f, cutoffLfoOct);   // LFO -> Cutoff destination
                cutoff = juce::jlimit (20.0f, (float) (0.49 * sr), cutoff);

                // ---- render this voice's grains for the current sample -----
                float vL = 0.0f, vR = 0.0f;
                for (int gi = 0; gi < kMaxGrains; ++gi)
                {
                    Grain& g = grains[(size_t) gi];
                    if (! g.active || g.voiceIndex != vi) continue;

                    const float sample = readSource (*src, g);
                    const float w = windowValue (p.window, g.winPhase);
                    const float sN = sample * w * g.gain;

                    vL += sN * g.panL;
                    vR += sN * g.panR;

                    // advance grain (per-sample accumulator adds)
                    g.srcPos   += g.srcInc;
                    g.winPhase += g.winInc;

                    // death: window complete or walked off the region with loop Off
                    bool dead = (g.winPhase >= 1.0f);
                    if (! dead)
                    {
                        if ((LoopMode) p.srcLoopMode == LoopMode::Loop)
                        {
                            const float span = g.hiEnd - g.loStart;
                            if (span > 1.0f)
                            {
                                if (g.srcPos > g.hiEnd) g.srcPos -= span;
                                else if (g.srcPos < g.loStart) g.srcPos += span;
                            }
                        }
                        else
                        {
                            if (g.srcPos < g.loStart || g.srcPos > g.hiEnd)
                                dead = true;
                        }
                    }
                    if (dead)
                        killGrain (gi);
                }

                // ---- sub oscillator (gated by the amp env via voiceAmp) ----
                if (p.subOsc && p.subLevel > 0.0f)
                {
                    const float noteHz = midiToHz ((float) lastNoteFor (v));
                    const float subHz  = noteHz * std::pow (2.0f, (float) juce::jlimit (-2, 0, p.subOct));
                    const float phaseInc = juce::jlimit (0.0f, 0.5f, subHz * invSr);
                    v.subPhase += phaseInc;
                    if (v.subPhase >= 1.0f) v.subPhase -= std::floor (v.subPhase);
                    float sub;
                    if ((SubWave) p.subWave == SubWave::Triangle)
                        sub = 4.0f * std::fabs (v.subPhase - 0.5f) - 1.0f;
                    else
                        sub = std::sin (v.subPhase * juce::MathConstants<float>::twoPi);
                    const float subOut = sub * p.subLevel * voiceAmp;
                    vL += subOut;
                    vR += subOut;
                }

                // ---- per-voice TPT SVF filter ------------------------------
                filterSample (v, p, cutoff, resoQ, vL, vR);

                // LFO -> Amp destination (tremolo) applied to the voice sum.
                vL *= ampLfoGain;
                vR *= ampLfoGain;

                mixL += vL;
                mixR += (outCh > 1) ? vR : vL;

                // capture viz grain positions (up to kVizGrains)
                if (s == numSamples - 1)
                {
                    for (int gi = 0; gi < kMaxGrains && vizGrainCount < (uint32_t) kVizGrains; ++gi)
                    {
                        const Grain& g = grains[(size_t) gi];
                        if (g.active && g.voiceIndex == vi)
                        {
                            const float span = juce::jmax (1.0f, (float) juce::jmax (1, srcLen - 1));
                            viz.grainPos[vizGrainCount] = juce::jlimit (0.0f, 1.0f, g.srcPos / span);
                            viz.grainAge[vizGrainCount] = juce::jlimit (0.0f, 1.0f, g.winPhase);
                            ++vizGrainCount;
                        }
                    }
                }
            }

            // ---- global source-gain pre-trim, write to output --------------
            mixL *= srcGain;
            mixR *= srcGain;

            L[s] = mixL;
            if (R) R[s] = mixR;
        }

        // --- PER-BLOCK: advance posBase (Static/Envelope/LFO/FreeScan) --------
        advancePositionBase (numSamples);

        // --- width / tone (global M/S on the cloud sum) -----------------------
        applyWidthTone (L, R, outCh, numSamples, p);

        // --- master gain + soft-ceiling limiter + NaN/denormal flush ----------
        for (int s = 0; s < numSamples; ++s)
        {
            float l = L[s] * outGain * ceilingGain;
            l = softCeiling (l);
            l = flush (l);
            L[s] = l;
            if (R)
            {
                float r = R[s] * outGain * ceilingGain;
                r = softCeiling (r);
                r = flush (r);
                R[s] = r;
            }
        }

        // --- retire fully-idle voices -----------------------------------------
        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            Voice& v = voices[(size_t) vi];
            if (v.midiNote < 0 && v.amp.idle() && ! voiceHasGrain (vi))
                v.reset();
        }

        // --- publish viz frame ------------------------------------------------
        viz.nActiveGrains = countActiveGrains();
        viz.nActiveVoices = countActiveVoices();
        viz.posNorm  = representativePos();
        viz.scanNorm = representativeScan();
        lastViz = viz;
    }

    GrainVizFrame getDebugFrame() const noexcept { return lastViz; }

private:
    // =========================================================================
    //  Window LUT
    // =========================================================================
    void buildWindowLuts() noexcept
    {
        for (int i = 0; i < kWindowSize; ++i)
        {
            const float x = (float) i / (float) (kWindowSize - 1);   // 0..1

            // Hann
            hannLut[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * x);

            // Gaussian (sigma = 0.4, centred), normalised so edges ~0
            const float t = (x - 0.5f) / 0.4f;
            gaussLut[(size_t) i] = std::exp (-0.5f * t * t);

            // Tukey (tapered cosine, taper fraction 0.5)
            const float alpha = 0.5f;
            float tk;
            if (x < alpha * 0.5f)
                tk = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (2.0f * x / alpha - 1.0f)));
            else if (x > 1.0f - alpha * 0.5f)
                tk = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (2.0f * x / alpha - 2.0f / alpha + 1.0f)));
            else
                tk = 1.0f;
            tukeyLut[(size_t) i] = tk;
        }
    }

    float windowValue (int windowChoice, float phase01) const noexcept
    {
        const float ph = juce::jlimit (0.0f, 1.0f, phase01);
        const float fi = ph * (float) (kWindowSize - 1);
        const int   i0 = (int) fi;
        const int   i1 = juce::jmin (kWindowSize - 1, i0 + 1);
        const float frac = fi - (float) i0;
        const float* lut = hannLut.data();
        if ((Window) windowChoice == Window::Gauss) lut = gaussLut.data();
        else if ((Window) windowChoice == Window::Tukey) lut = tukeyLut.data();
        return lut[(size_t) i0] + frac * (lut[(size_t) i1] - lut[(size_t) i0]);
    }

    // =========================================================================
    //  Grain pool + free-list ring
    // =========================================================================
    void resetAll() noexcept
    {
        for (auto& g : grains) g = Grain{};
        // rebuild free ring with every index available
        for (int i = 0; i < kMaxGrains; ++i) freeRing[(size_t) i] = i;
        freeHead = 0;
        freeCount = kMaxGrains;
        for (auto& v : voices) v.reset();
        ageCounter = 0;
        lfo1 = LfoRunner{}; lfo2 = LfoRunner{};
        lfo2.rng = 0x1234567u;   // decorrelate LFO2's S&H/Random stream from LFO1
        tiltStateL = tiltStateR = 0.0f;
        lastViz = GrainVizFrame{};
    }

    int popFreeGrain() noexcept
    {
        if (freeCount <= 0) return -1;              // pool exhausted -> caller drops
        const int idx = freeRing[(size_t) freeHead];
        freeHead = (freeHead + 1) % kMaxGrains;
        --freeCount;
        return idx;
    }

    void killGrain (int idx) noexcept
    {
        if (idx < 0 || idx >= kMaxGrains) return;
        Grain& g = grains[(size_t) idx];
        if (! g.active) return;
        g.active = false;
        g.voiceIndex = -1;
        // push back to the free ring tail
        const int tail = (freeHead + freeCount) % kMaxGrains;
        freeRing[(size_t) tail] = idx;
        ++freeCount;
    }

    // =========================================================================
    //  Voice lifecycle helpers
    // =========================================================================
    int firstActiveVoice() const noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[(size_t) i].active()) return i;
        return -1;
    }

    int oldestVoiceIndex() const noexcept
    {
        int oldest = 0; uint64_t best = ~0ull;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const Voice& v = voices[(size_t) i];
            if (v.active() && v.age < best) { best = v.age; oldest = i; }
        }
        return oldest;
    }

    int lastNoteFor (const Voice& v) const noexcept
    {
        // basePitchRatio = 2^((note - rootNote)/12); invert for key-track when the
        // note field was cleared on release.
        if (v.midiNote >= 0) return v.midiNote;
        const int root = source ? source->rootNote : 60;
        return root + (int) std::lround (12.0f * std::log2 (juce::jmax (1.0e-6f, v.basePitchRatio)));
    }

    void startVoice (Voice& v, int midiNote, float velocity, bool legatoGlide) noexcept
    {
        const auto& p = params;

        // Resolve the key-snap root/scale (bus overrides when followKey is on).
        int    root  = p.keyRoot;
        ScaleId scale = (ScaleId) juce::jlimit (0, (int) ScaleId::HarmMinor, p.keyScaleId);
        if (p.followKey)
        {
            root  = busRootNote;
            scale = (ScaleId) juce::jlimit (0, (int) ScaleId::HarmMinor, busScaleId);
        }

        // Key-snap applied ONCE, note-level (pre-ratio).
        const int noteForRatio = p.keySnap
            ? snapMidiToScale (midiNote, root, scale)
            : midiNote;

        const int rootNote = source ? source->rootNote : 60;
        const float newBase = std::pow (2.0f, (float) (noteForRatio - rootNote) / 12.0f);

        const bool reuse = (&v == &voices[(size_t) firstActiveVoiceOrThis (v)]);
        (void) reuse;

        if (legatoGlide && ! v.amp.idle())
        {
            // Keep the sounding pitch, glide the base up/down to the new note.
            v.glideTargetRatio = 1.0f;
            const float prevBase = v.basePitchRatio;
            v.glideCurrentRatio *= (prevBase / juce::jmax (1.0e-6f, newBase));
            v.basePitchRatio = newBase;
            v.midiNote = midiNote;
            v.velocity = velocity;
            v.held = true;
            v.age = ++ageCounter;
            // do NOT retrigger amp env in legato
        }
        else
        {
            v.basePitchRatio = newBase;
            v.glideCurrentRatio = 1.0f;
            v.glideTargetRatio  = 1.0f;
            v.midiNote = midiNote;
            v.velocity = velocity;
            v.held = true;
            v.age = ++ageCounter;
            v.amp.value = (v.amp.stage == 0) ? 0.0f : v.amp.value; // click-free steal ramp
            v.amp.noteOn();
            v.filterEnv.value = 0.0f;
            v.filterEnv.noteOn();
            v.pitchEnv.trigger();
            v.modEnv.trigger();
            v.spawnAccumulator = 0.0f;
            v.subPhase = 0.0f;
            v.posBase  = juce::jlimit (0.0f, 1.0f, p.posBase);
            v.posScanPhase = 0.0f;
            v.pitchLfo.phase = 0.0f;
            v.posLfo.phase   = 0.0f;
            v.fL = SvfState{}; v.fR = SvfState{};
        }
        v.sourceVersion = source ? source->version : 0;
    }

    int firstActiveVoiceOrThis (const Voice& v) const noexcept
    {
        for (int i = 0; i < kMaxVoices; ++i)
            if (&voices[(size_t) i] == &v) return i;
        return 0;
    }

    bool voiceHasGrain (int vi) const noexcept
    {
        for (int gi = 0; gi < kMaxGrains; ++gi)
            if (grains[(size_t) gi].active && grains[(size_t) gi].voiceIndex == vi)
                return true;
        return false;
    }

    // =========================================================================
    //  Grain spawn (PER-GRAIN-SPAWN modulation + full pitch chain)
    // =========================================================================
    void spawnGrain (Voice& v, int vi, const EngineParams& p,
                     float regionLo, float regionHi, int /*srcLen*/,
                     float srcSrRatio, float winInc, float overlapNormGain,
                     float voiceAmp, float pitchEnvV, float pitchLfoV,
                     float posLfoV, float modEnvV) noexcept
    {
        // --- per-spawn random draws (spray, pan, pitch spread) ----------------
        // Drawn ONCE per spawn, before any pool pop, so the RNG stream is
        // independent of the unison count — unison never perturbs the base grain.
        const float sprayRand  = randBipolar (v);    // -1..+1
        const float panRand    = randBipolar (v);    // -1..+1
        const float spreadRand = randBipolar (v);    // -1..+1

        // File Position (normalised) with the shared mod AD env + position LFO.
        // (Both were ticked ONCE this sample and passed in; never re-tick here.)
        float posN = v.posBase
                   + p.posEnvAmt * modEnvV       // shared AD mod env sweep
                   + p.posLfoAmt * posLfoV;      // cyclic scrub
        posN = juce::jlimit (0.0f, 1.0f, posN);

        // Spray jitter (0..1 -> up to ~250 ms each side), clamped to the region.
        const float regionSpan = juce::jmax (1.0f, regionHi - regionLo);
        const float sprayMax = juce::jmin (regionSpan, juce::jlimit (0.0f, 1.0f, p.spray) * 0.25f * (float) sr);
        float startPos = regionLo + posN * regionSpan + sprayRand * sprayMax;
        startPos = juce::jlimit (regionLo, regionHi, startPos);

        // --- PITCH CHAIN (multiplicative, ratio domain) -----------------------
        // Everything EXCEPT the per-unison detune offset, which is folded in per
        // copy below. For unison==1 the offset is exactly 2^0 == 1.0f, so `ratio`
        // and the resulting `srcInc` are bit-identical to the pre-unison engine.
        float ratio = v.basePitchRatio;
        ratio *= std::pow (2.0f, (float) juce::jlimit (-24, 24, p.tuneCoarse) / 12.0f);
        ratio *= std::pow (2.0f, juce::jlimit (-100.0f, 100.0f, p.tuneFine) / 1200.0f);
        ratio *= std::pow (2.0f, (pitchEnvV * p.pitchEnvSemis) / 12.0f);
        ratio *= std::pow (2.0f, (pitchLfoV * p.pitchLfoCents) / 1200.0f);
        ratio *= v.glideCurrentRatio;
        ratio *= std::pow (2.0f, (spreadRand * juce::jlimit (0.0f, 100.0f, p.pitchSpreadCents)) / 1200.0f);

        // --- base equal-power pan (grainPan = random spread) ------------------
        const float panAmt = juce::jlimit (0.0f, 1.0f, p.grainPan);
        const float basePan01 = 0.5f + 0.5f * panRand * panAmt;   // 0..1

        // --- unison stack config ----------------------------------------------
        const int   uVoices = juce::jlimit (1, 4, p.unisonVoices);
        const float uDetune = juce::jlimit (0.0f, 50.0f, p.unisonDetuneCents);
        const float uWidth  = juce::jlimit (0.0f, 1.0f, p.unisonWidth);
        // Constant-power gain compensation: unison==1 -> exactly 1.0f (bit-exact).
        const float uGain   = (uVoices <= 1) ? 1.0f : 1.0f / std::sqrt ((float) uVoices);

        // Spawn each stacked copy. Copy 0 is the centre voice (detune/pan offset
        // exactly zero); the remaining copies fan out symmetrically. A pool that
        // cannot supply a slot simply DROPS that unison copy — never allocates.
        for (int u = 0; u < uVoices; ++u)
        {
            const int idx = popFreeGrain();
            if (idx < 0) return;             // pool exhausted -> drop remaining copies

            // Symmetric detune/pan factor in [-1, +1]. Single voice -> 0.
            // With N voices the copies sit at evenly-spaced points spanning [-1,+1]
            // ( {0}, {-1,+1}, {-1,0,+1}, {-1,-1/3,+1/3,+1} ), so ±(uDetune/2) at
            // the edges gives a total spread of uDetune cents across the stack.
            const float spread = (uVoices <= 1)
                ? 0.0f
                : ((float) u / (float) (uVoices - 1)) * 2.0f - 1.0f;

            float uRatio = ratio;
            if (uVoices > 1)
                uRatio *= std::pow (2.0f, (spread * (uDetune * 0.5f)) / 1200.0f);
            uRatio = juce::jlimit (0.03125f, 32.0f, uRatio);   // +-5 octaves clamp

            float inc = uRatio * srcSrRatio;
            if (p.srcReverse) inc = -inc;    // read backwards toward loStart

            // Pan: base random pan, then push the copy by ±unisonWidth (0.5 span
            // each side at full width). unison==1 leaves basePan01 untouched.
            float pan01 = basePan01;
            if (uVoices > 1)
                pan01 += spread * uWidth * 0.5f;
            pan01 = juce::jlimit (0.0f, 1.0f, pan01);
            const float theta = pan01 * juce::MathConstants<float>::halfPi;

            Grain& g = grains[(size_t) idx];
            g.panL = std::cos (theta);
            g.panR = std::sin (theta);
            g.active   = true;
            g.srcPos   = startPos;   // both directions start at the sprayed read head
            g.srcInc   = inc;
            g.winPhase = 0.0f;
            g.winInc   = winInc;
            g.loStart  = regionLo;
            g.hiEnd    = regionHi;
            g.gain     = overlapNormGain * voiceAmp * uGain;
            g.voiceIndex = vi;
        }
    }

    // Cheap per-voice xorshift; returns -1..+1.
    float randBipolar (Voice& v) noexcept
    {
        // fold voice RNG state into the pitch LFO's rng to avoid extra members
        uint32_t& r = v.pitchLfo.rng;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return (float) (r & 0xFFFFFFu) / (float) 0x800000u - 1.0f;
    }

    // =========================================================================
    //  Source read (linear / cubic interpolation, mono or stereo -> mono grain)
    // =========================================================================
    float readSource (const SourceSample& src, const Grain& g) const noexcept
    {
        const int len = src.lengthSamples;
        if (len < 2) return 0.0f;

        float pos = g.srcPos;
        if (pos < 0.0f) pos = 0.0f;
        if (pos > (float) (len - 1)) pos = (float) (len - 1);

        const int   i0 = (int) pos;
        const float frac = pos - (float) i0;

        const int numCh = src.audio.getNumChannels();
        auto readCh = [&] (int ch) -> float
        {
            const float* d = src.audio.getReadPointer (ch);
            const int i1 = juce::jmin (len - 1, i0 + 1);
            if (interp == Interp::Cubic)
            {
                const int im1 = juce::jmax (0, i0 - 1);
                const int i2  = juce::jmin (len - 1, i0 + 2);
                const float ym1 = d[im1], y0 = d[i0], y1 = d[i1], y2 = d[i2];
                const float c0 = y0;
                const float c1 = 0.5f * (y1 - ym1);
                const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
                const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
                return ((c3 * frac + c2) * frac + c1) * frac + c0;
            }
            return d[i0] + frac * (d[i1] - d[i0]);
        };

        if (numCh <= 1) return readCh (0);
        return 0.5f * (readCh (0) + readCh (1));
    }

    // =========================================================================
    //  Per-voice TPT / Zavalishin state-variable filter
    // =========================================================================
    void filterSample (Voice& v, const EngineParams& p, float cutoff, float q,
                       float& l, float& r) const noexcept
    {
        const float wd = juce::MathConstants<float>::pi * (cutoff / (float) sr);
        const float g = std::tan (juce::jlimit (0.0001f, 1.55f, wd));
        const float k = 1.0f / juce::jmax (0.05f, q);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        auto proc = [&] (SvfState& st, float x) -> float
        {
            const float v3 = x - st.ic2eq;
            const float v1 = a1 * st.ic1eq + a2 * v3;
            const float v2 = st.ic2eq + a2 * st.ic1eq + a3 * v3;
            st.ic1eq = 2.0f * v1 - st.ic1eq;
            st.ic2eq = 2.0f * v2 - st.ic2eq;
            switch ((FilterType) p.filterType)
            {
                case FilterType::HP12: return x - k * v1 - v2;          // high-pass
                case FilterType::BP:   return v1;                        // band-pass
                case FilterType::LP24:
                default:               return v2;                        // low-pass
            }
        };

        float outL = proc (v.fL, l);
        float outR = proc (v.fR, r);

        // LP24 = two cascaded LP12 passes for a 24 dB/oct slope.
        if ((FilterType) p.filterType == FilterType::LP24)
        {
            outL = proc (v.fL, outL);
            outR = proc (v.fR, outR);
        }
        l = outL; r = outR;
    }

    // =========================================================================
    //  Position runner (per-block posBase advance) + tempo division table
    // =========================================================================
    float positionScanInc (const EngineParams& p) const noexcept
    {
        // Returns cycles-per-second for the scan phase (FreeScan / tempo sync).
        if (p.posScanSync)
        {
            const double beats = divisionBeats (p.posScanDiv);
            if (beats <= 0.0 || hostBpm <= 1.0) return 0.0f;
            const double cycleSeconds = beats * (60.0 / hostBpm);
            return (float) (1.0 / cycleSeconds);
        }
        return juce::jlimit (-2.0f, 2.0f, p.posScanRate); // free-run Hz, sign = direction
    }

    void advancePositionBase (int numSamples) noexcept
    {
        const auto& p = params;
        const float rateHz = positionScanInc (p);
        if (std::fabs (rateHz) < 1.0e-6f) return;
        const float delta = rateHz * (float) numSamples / (float) sr;
        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            Voice& v = voices[(size_t) vi];
            if (! v.active()) continue;
            v.posBase += delta;
            // wrap into 0..1 (scanning loops the region)
            v.posBase -= std::floor (v.posBase);
        }
    }

    // Beats (quarter = 1.0) for the File-Position / LFO divisions.
    //  {4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/4 dot, 1/8, 1/8 dot, 1/8 trip, 1/16}
    static double divisionBeats (int div) noexcept
    {
        switch (div)
        {
            case 0:  return 16.0;        // 4 bars
            case 1:  return 8.0;         // 2 bars
            case 2:  return 4.0;         // 1 bar
            case 3:  return 2.0;         // 1/2
            case 4:  return 1.0;         // 1/4
            case 5:  return 1.5;         // 1/4 dotted
            case 6:  return 0.5;         // 1/8
            case 7:  return 0.75;        // 1/8 dotted
            case 8:  return 1.0 / 3.0;   // 1/8 triplet
            case 9:  return 0.25;        // 1/16
            default: return 4.0;
        }
    }

    float lfoIncrement (bool sync, float rateHz, int div) const noexcept
    {
        if (sync)
        {
            const double beats = divisionBeats (div);
            if (beats <= 0.0 || hostBpm <= 1.0) return 0.0f;
            return (float) (hostBpm / 60.0 / beats);   // cycles/sec
        }
        return juce::jlimit (0.0f, 30.0f, rateHz);
    }

    // LFO -> Cutoff destination expressed in octaves of cutoff shift (0 for any
    // non-cutoff destination). Continuous, applied per-sample.
    static float lfoDestOctaves (LfoDest dest, float lfoV, float amt) noexcept
    {
        if (dest != LfoDest::Cutoff) return 0.0f;
        return lfoV * juce::jlimit (0.0f, 1.0f, amt) * 4.0f;   // +-4 octaves at full depth
    }

    // LFO -> Amp destination expressed as a unipolar tremolo gain multiplier
    // (1.0 for any non-amp destination). Continuous, applied per-sample.
    static float lfoDestAmp (LfoDest dest, float lfoV, float amt) noexcept
    {
        if (dest != LfoDest::Amp) return 1.0f;
        const float depth = juce::jlimit (0.0f, 1.0f, amt);
        return 1.0f - depth * 0.5f * (1.0f - lfoV);           // 1..(1-depth)
    }

    // =========================================================================
    //  Global width / tone (M/S width + tilt EQ)
    // =========================================================================
    void applyWidthTone (float* L, float* R, int outCh, int n, const EngineParams& p) const noexcept
    {
        // one-pole tilt: brightness = +tone lifts highs, -tone lifts lows.
        const float tilt = juce::jlimit (-1.0f, 1.0f, p.tone);
        const float width = juce::jlimit (0.0f, 2.0f, p.width);

        if (outCh > 1 && R != nullptr && width != 1.0f)
        {
            for (int s = 0; s < n; ++s)
            {
                const float mid  = 0.5f * (L[s] + R[s]);
                const float side = 0.5f * (L[s] - R[s]) * width;
                L[s] = mid + side;
                R[s] = mid - side;
            }
        }

        if (std::fabs (tilt) > 1.0e-4f)
        {
            const float coeff = 0.2f;
            for (int s = 0; s < n; ++s)
            {
                tiltStateL += coeff * (L[s] - tiltStateL);
                const float lowL = tiltStateL, highL = L[s] - lowL;
                L[s] = L[s] + tilt * (highL - lowL);
                if (R)
                {
                    tiltStateR += coeff * (R[s] - tiltStateR);
                    const float lowR = tiltStateR, highR = R[s] - lowR;
                    R[s] = R[s] + tilt * (highR - lowR);
                }
            }
        }
    }

    // =========================================================================
    //  Output safety: tanh soft-ceiling + NaN/denormal flush
    // =========================================================================
    static float softCeiling (float x) noexcept
    {
        // Auto Pitch / Sub Conjurer pattern: identity below the knee, tanh soft-knee
        // asymptoting to just under 0 dBFS above it.
        constexpr float knee = 0.75f, ceiling = 0.988f, range = ceiling - knee;
        const float a = std::fabs (x);
        if (a <= knee) return x;
        return std::copysign (knee + range * std::tanh ((a - knee) / range), x);
    }

    static float flush (float x) noexcept
    {
        if (! std::isfinite (x)) return 0.0f;
        if (std::fabs (x) < 1.0e-15f) return 0.0f;   // denormal flush
        return x;
    }

    // =========================================================================
    //  Small metrics for the viz frame
    // =========================================================================
    uint32_t countActiveGrains() const noexcept
    {
        uint32_t n = 0;
        for (const auto& g : grains) if (g.active) ++n;
        return n;
    }
    uint32_t countActiveVoices() const noexcept
    {
        uint32_t n = 0;
        for (const auto& v : voices) if (v.active()) ++n;
        return n;
    }
    float representativePos() const noexcept
    {
        for (const auto& v : voices) if (v.active()) return juce::jlimit (0.0f, 1.0f, v.posBase);
        return juce::jlimit (0.0f, 1.0f, params.posBase);
    }
    float representativeScan() const noexcept
    {
        for (const auto& v : voices) if (v.active()) return v.posScanPhase;
        return 0.0f;
    }

    // =========================================================================
    //  State
    // =========================================================================
    double sr = 48000.0;
    int    numOut = 2;
    Interp interp = Interp::Linear;

    EngineParams params;
    std::shared_ptr<const SourceSample> source;

    std::array<Grain, kMaxGrains> grains {};
    std::array<int,   kMaxGrains> freeRing {};
    int freeHead = 0, freeCount = kMaxGrains;

    std::array<Voice, kMaxVoices> voices {};
    uint64_t ageCounter = 0;

    // Global (per-instance) modulation LFOs — one instance each, free-running.
    LfoRunner lfo1, lfo2;

    std::array<float, kWindowSize> hannLut {};
    std::array<float, kWindowSize> gaussLut {};
    std::array<float, kWindowSize> tukeyLut {};

    // bus / host transport state (fed per block)
    int    busRootNote = 0, busScaleId = (int) ScaleId::Major;
    double hostBpm = 120.0, hostPpq = 0.0;
    bool   hostPlaying = false;

    // tilt EQ one-pole state (per output pass)
    mutable float tiltStateL = 0.0f, tiltStateR = 0.0f;

    GrainVizFrame lastViz {};
};

} // namespace sattari::dsp
