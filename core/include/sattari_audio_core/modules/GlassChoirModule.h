#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariPitchDetector.h>      // YIN detector, ScaleId, diatonic helpers
#include <sattari_audio_core/SattariPsolaShifter.h>       // low-latency formant-preserving detune
#include <sattari_audio_core/SattariEnvelopeFollower.h>   // atmosphere ducking keyed off the lead
#include <sattari_audio_core/SattariCrossPluginBus.h>     // SessionReader / TempoReader (consumer only)
#include <sattari_audio_core/SattariHarmonyVoicing.h>     // diatonicShift + scaleIdFromChoice/Bus
#include <sattari_audio_core/SattariRbFormant.h>          // hi-fi Rubber Band detune+formant (Studio)

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  GlassChoirModule — Sattari Glass Choir's DSP, extracted so the standalone
//  plugin and a Vocal rack slot run the SAME ensemble.
//
//  A vocal-ensemble EFFECT (audio in, not a synth). One CHARACTER macro walks
//  the whole thing from HUMAN through MASSIVE to DESTROYED, driving voice count,
//  detune spread, timing, harmony mix, the destroy chain, trails and reverb from
//  a single control; the user knobs act as ceilings and offsets on top.
//
//  THE ONE STRUCTURAL DECISION IN THIS EXTRACTION:
//  the plugin has THREE output buses (Lead / Choir / Atmosphere) and
//  SattariModule::process() is handed exactly ONE buffer. So the module treats
//  Choir and Atmosphere as OPTIONAL aux outputs, set by setAuxOutputs():
//
//    * the standalone shell supplies both, and keeps its 3-bus split;
//    * a rack slot supplies neither, and the three stems fold into the lead.
//
//  The fold is not a new code path invented for the rack — it is exactly what
//  the plugin already does when a host gives it a single output bus, which its
//  own contract covers ("2b fold"). setAuxOutputs() lives on THIS class, not on
//  SattariModule, because multi-stem output is a Glass Choir concern and does
//  not belong in an interface every other module has to carry.
// =============================================================================

namespace sattari::dsp
{

class GlassChoirModule final : public SattariModule
{
public:
    static constexpr int kMaxDoubles  = 8;   // ensemble voice cap (2/4/8)
    static constexpr int kNumHarmony  = 4;   // diatonic harmony voices
    static constexpr int kMaxRbVoices = 8;   // RB voices only in Studio; else 4

    GlassChoirModule()
    {
        // Per-voice RNG seeds, walked with an LCG so every ensemble voice drifts
        // independently. This is LOAD-BEARING, not hygiene: juce::Random default-
        // constructs from the system clock, so without it the voices would be
        // non-deterministic run to run AND could seed identically, collapsing the
        // ensemble into one detuned copy.
        juce::uint32 seed = 0x51a55c00;
        for (auto& v : doubles)
        {
            v.rng.setSeed ((juce::int64) seed);
            seed = seed * 1664525u + 1013904223u;
        }
    }

    ~GlassChoirModule() override { sessionReader.stop(); tempoReader.stop(); }

    const char* moduleId()    const noexcept override { return "glasschoir"; }
    const char* displayName() const noexcept override { return "Glass Choir"; }

    // --- Choices (shared with the shell's editor) --------------------------
    static juce::StringArray modeChoices()        { return { "Studio", "Choir", "Glass" }; }
    static juce::StringArray voiceCountChoices()  { return { "2", "4", "8" }; }
    static juce::StringArray engineChoices()      { return { "Live (PSOLA)", "Studio (RB)" }; }
    static juce::StringArray rootChoices()
    {
        return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    }
    static juce::StringArray scaleChoices()
    {
        // Order MUST match sattari::harmony::scaleIdFromChoice.
        return { "Major", "Minor", "Dorian", "Mixolydian", "Harm Minor", "Maj Pent", "Min Pent" };
    }
    static juce::StringArray intervalChoices()
    {
        return { "Octave Below", "6th Below", "5th Below", "4th Below", "3rd Below",
                 "Unison", "3rd Above", "4th Above", "5th Above", "6th Above", "Octave Above" };
    }
    static int intervalDegrees (int choiceIndex) noexcept
    {
        static constexpr int degrees[] = { -7, -5, -4, -3, -2, 0, 2, 3, 4, 5, 7 };
        return juce::isPositiveAndBelow (choiceIndex, (int) std::size (degrees)) ? degrees[choiceIndex] : 0;
    }

    // -- Construction: the 3-output-bus layout ------------------------------------

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using CP = juce::AudioParameterChoice;
        using BP = juce::AudioParameterBool;
        using R  = juce::NormalisableRange<float>;

        // --- Global --------------------------------------------------------------
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "input",  1 }, "Input",  R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "output", 1 }, "Output", R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<CP> (juce::ParameterID { prefix + "mode", 1 }, "Mode", modeChoices(), 0));   // Studio
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "character", 1 }, "Character", R (0.0f, 1.0f, 0.001f), 0.35f));
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "bypass", 1 }, "Bypass", false));

        // --- Ensemble ------------------------------------------------------------
        out.push_back (std::make_unique<CP> (juce::ParameterID { prefix + "voiceCount", 1 }, "Voices", voiceCountChoices(), 1));   // 4
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "detuneSpread", 1 }, "Detune Spread", R (0.0f, 50.0f, 0.1f), 12.0f));   // cents
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "timing", 1 }, "Timing", R (0.0f, 45.0f, 0.1f), 16.0f));   // ms
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "humanize", 1 }, "Humanize", R (0.0f, 1.0f, 0.001f), 0.35f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "formantSpread", 1 }, "Formant Spread", R (0.0f, 12.0f, 0.01f), 2.0f));   // st
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "ensembleLevel", 1 }, "Ensemble Level", R (-24.0f, 12.0f, 0.1f), -3.0f));   // dB
        out.push_back (std::make_unique<CP> (juce::ParameterID { prefix + "engineQuality", 1 }, "Engine", engineChoices(), 1));   // Studio (RB)

        // --- Align (v1: onset cross-correlation nudge only; NO DTW warp) ---------
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "alignOn", 1 }, "Align", true));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "alignAmount", 1 }, "Align Amount", R (0.0f, 1.0f, 0.001f), 0.7f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "alignSearchMs", 1 }, "Align Search", R (10.0f, 150.0f, 1.0f), 60.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "pitchTighten", 1 }, "Pitch Tighten", R (0.0f, 1.0f, 0.001f), 0.6f));

        // --- Harmony -------------------------------------------------------------
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "harmonyOn", 1 }, "Harmony", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "harmonyMix", 1 }, "Harmony Mix", R (0.0f, 1.0f, 0.001f), 0.5f));
        // Default OFF: the manual root/scale menu is authoritative out of the box.
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "followKey", 1 }, "Follow Song Key", true));
        out.push_back (std::make_unique<CP> (juce::ParameterID { prefix + "root",  1 }, "Root",  rootChoices(),  0));
        out.push_back (std::make_unique<CP> (juce::ParameterID { prefix + "scale", 1 }, "Scale", scaleChoices(), 0));

        // h1int..h4int (Oct-down..Oct-up), defaults -7,+5,+3,-5 -> "Octave Below","5th Above","4th Above","5th Below".
        const int   defInt   [kNumHarmony] = { 0, 8, 7, 2 };
        const bool  defOn    [kNumHarmony] = { true, true, false, false };
        const float defLevel [kNumHarmony] = { -4.0f, -4.0f, -4.0f, -4.0f };
        for (int v = 0; v < kNumHarmony; ++v)
        {
            const auto s = juce::String (v + 1);
            out.push_back (std::make_unique<CP> (juce::ParameterID { "h" + s + "int",   1 }, "Voice " + s + " Interval", intervalChoices(), defInt[v]));
            out.push_back (std::make_unique<BP> (juce::ParameterID { "h" + s + "on",    1 }, "Voice " + s + " On", defOn[v]));
            out.push_back (std::make_unique<FP> (juce::ParameterID { "h" + s + "level", 1 }, "Voice " + s + " Level", R (-24.0f, 12.0f, 0.1f), defLevel[v]));
        }

        // --- Destroy -------------------------------------------------------------
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "destroyOn", 1 }, "Destroy", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "destroyMix", 1 }, "Destroy Mix", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "drive",    1 }, "Drive",  R (0.0f, 1.0f, 0.001f), 0.35f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "crush",    1 }, "Crush",  R (0.0f, 1.0f, 0.001f), 0.2f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "vowelAmt", 1 }, "Vowel",  R (0.0f, 1.0f, 0.001f), 0.3f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "ringAmt",  1 }, "Ring",   R (0.0f, 1.0f, 0.001f), 0.15f));

        // --- Reverb / duck -------------------------------------------------------
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "reverbMix",  1 }, "Reverb Mix",  R (0.0f, 1.0f, 0.001f), 0.3f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "reverbSize", 1 }, "Reverb Size", R (0.0f, 1.0f, 0.001f), 0.6f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "reverbDamp", 1 }, "Reverb Damp", R (0.0f, 1.0f, 0.001f), 0.5f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "duckAmount",   1 }, "Duck Amount",   R (0.0f, 1.0f, 0.001f), 0.6f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "duckAttackMs", 1 }, "Duck Attack",   R (1.0f, 50.0f, 0.1f), 8.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "duckReleaseMs",1 }, "Duck Release",  R (20.0f, 500.0f, 1.0f), 180.0f));

        // --- Width ---------------------------------------------------------------
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "width", 1 }, "Width", R (0.0f, 2.0f, 0.01f), 1.2f));
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "lowMono", 1 }, "Low Mono", true));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "lowMonoFreq", 1 }, "Low Mono Freq", R (40.0f, 400.0f, 1.0f, 0.4f), 120.0f));
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "monoAudition", 1 }, "Mono Audition", false));

        // --- Out trims -----------------------------------------------------------
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "leadLevel",  1 }, "Lead Level",  R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "choirLevel", 1 }, "Choir Level", R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "atmosLevel", 1 }, "Atmos Level", R (-24.0f, 12.0f, 0.1f), 0.0f));

        // --- Trails (v1 simple reverse-delay) ------------------------------------
        out.push_back (std::make_unique<BP> (juce::ParameterID { prefix + "trailsOn", 1 }, "Trails", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "trailsMix", 1 }, "Trails Mix", R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "trailReverse", 1 }, "Trail Reverse", R (0.0f, 1.0f, 0.001f), 0.5f));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "trailSizeMs", 1 }, "Trail Size", R (20.0f, 400.0f, 1.0f), 120.0f));

    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sr, int samplesPerBlock, int /*numChannels*/) override
    {
        sampleRate = sr > 0.0 ? sr : 44100.0;
        preparedBlockSize = juce::jmax (1, samplesPerBlock);
        const int nb = preparedBlockSize;

        detector.prepare (sampleRate, 60.0f, 1000.0f, 0.045f);
        analysisHop = juce::jmax (1, (int) (sampleRate * 0.02));   // ~20 ms hop
        analysisCounter = 0;
        leadMidiLive = -1; voicedLive = false; leadHzSmoothed = 0.0f;
        periodSamples = sampleRate / 150.0;

        // Per-voice ensemble engines + timing delay rings.
        delayLen = (int) std::ceil (sampleRate * 0.09) + 64;   // covers max timing + drift
        int maxEngineLatency = 0;
        for (auto& v : doubles)
        {
            v.psola.prepare (sampleRate, 60.0, 1000.0);
            v.rb.prepare (sampleRate, nb);
            v.delay.assign ((size_t) delayLen, 0.0f);
            v.writePos = 0;
            v.driftPos = v.driftTarget = 0.0;
            v.driftCountdown = 0;
            maxEngineLatency = juce::jmax (maxEngineLatency, v.rb.latencySamples(), v.psola.getLatencySamples());
        }

        for (auto& h : harmony)
        {
            h.psola.prepare (sampleRate, 60.0, 1000.0);
            h.rb.prepare (sampleRate, nb);
            h.smoothedRatio = 1.0;
            h.humanizeCents = h.humanizeTarget = 0.0;
            maxEngineLatency = juce::jmax (maxEngineLatency, h.rb.latencySamples(), h.psola.getLatencySamples());
        }

        reverb.setSampleRate (sampleRate);
        reverb.reset();

        duckFollower.prepare (sampleRate, 8.0f, 180.0f);

        for (auto& b : lowMonoHP)  b.reset();
        for (auto& b : destroyF1)  b.reset();
        for (auto& b : destroyF2)  b.reset();
        destroyRingPhase = { 0.0, 0.0 };
        crushHold = { 0.0f, 0.0f };
        crushCount = { 0, 0 };

        // Reverse-delay trail ring (up to 400 ms + slack).
        trailCap = (int) std::ceil (sampleRate * 0.45) + 4;
        for (auto& r : trailRing) r.assign ((size_t) trailCap, 0.0f);
        trailWrite = 0;
        trailReadPos = 0.0;

        // Per-block scratch.
        monoLead.assign ((size_t) nb, 0.0f);
        shiftScratch.assign ((size_t) nb, 0.0f);
        choirBuf.setSize   (2, nb, false, false, true);
        atmosBuf.setSize   (2, nb, false, false, true);
        destroyBuf.setSize (2, nb, false, false, true);
        reverbDry.setSize  (2, nb, false, false, true);

        // Lead dry-delay ring: aligns the lead to the max engine latency.
        reportedLatency = maxEngineLatency;
        const int ldl = juce::jmax (1, reportedLatency + 1);
        for (auto& d : leadDelay) d.assign ((size_t) ldl, 0.0f);
        leadDelayWrite = 0;
        // Latency is reported through latencySamples(); a module never calls
        // setLatencySamples() because it has no AudioProcessor to call it on.

        sessionReader.start();
        tempoReader.start();
    }

    // Reverb tail, trail ring and per-voice delay lines all carry audio across a
    // transport jump, so a real reset is mandatory (pure virtual for this reason).
    void reset() override
    {
        for (auto& v : doubles)
        {
            std::fill (v.delay.begin(), v.delay.end(), 0.0f);
            v.writePos = 0;
            v.driftPos = v.driftTarget = 0.0;
            v.driftCountdown = 0;
        }
        for (auto& h : harmony) { h.smoothedRatio = 1.0; h.humanizeCents = h.humanizeTarget = 0.0; }
        reverb.reset();
        duckFollower.reset (0.0f);
        for (auto& b : lowMonoHP) b.reset();
        for (auto& b : destroyF1) b.reset();
        for (auto& b : destroyF2) b.reset();
        destroyRingPhase = { { 0.0, 0.0 } };
        crushHold  = { { 0.0f, 0.0f } };
        crushCount = { { 0, 0 } };
        for (auto& r : trailRing) std::fill (r.begin(), r.end(), 0.0f);
        trailWrite = 0;
        trailReadPos = 0.0;
        for (auto& d : leadDelay) std::fill (d.begin(), d.end(), 0.0f);
        leadDelayWrite = 0;
        analysisCounter = 0;
        leadMidiLive = -1;
        voicedLive = false;
        leadHzSmoothed = 0.0f;
        choirBuf.clear(); atmosBuf.clear(); destroyBuf.clear(); reverbDry.clear();
    }

    // Lead is delayed to the worst-case engine latency so the stems line up.
    int latencySamples() const noexcept override { return reportedLatency; }

    // Optional multi-stem output — see the header note. nullptr (the rack case)
    // folds Choir and Atmosphere into the lead.
    void setAuxOutputs (juce::AudioBuffer<float>* choir,
                        juce::AudioBuffer<float>* atmos) noexcept
    {
        auxChoir = choir;
        auxAtmos = atmos;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0) return;

        // In module form the LEAD bus is the buffer itself, processed in place: the
        // input is fully read into monoLead before anything is written back, so the
        // aliasing is safe. Choir and Atmosphere are optional AUX outputs the shell
        // supplies; in a rack nobody does, and the three simply fold into the lead —
        // which is the plugin's own single-out behaviour, not a new code path.
        auto& leadBus = buffer;
        const int leadCh = leadBus.getNumChannels();
        const bool choirActive = auxChoir != nullptr;
        const bool atmosActive = auxAtmos != nullptr;

        auto& inBus = buffer;
        const int inCh = inBus.getNumChannels();

        const float inGain   = juce::Decibels::decibelsToGain (param ("input"));
        const float outGain  = juce::Decibels::decibelsToGain (param ("output"));
        const bool  bypass   = param ("bypass") > 0.5f;
        const int   mode     = juce::jlimit (0, 2, (int) std::lround (param ("mode")));
        // Engine quality: Live(PSOLA)=0, Studio(RB)=1. RB voices capped: 8 only in Studio.
        const bool  useRb    = ((int) std::lround (param ("engineQuality")) == 1);

        // Mono-sum the (input-gained) lead for pitch detection + shifting.
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < inCh; ++ch) sum += inBus.getReadPointer (ch)[i];
            monoLead[(size_t) i] = (inCh > 0 ? sum / (float) inCh : 0.0f) * inGain;
        }

        // --- Bypass: main out == input (dry, level-matched), no added energy ------
        if (bypass)
        {
            for (int ch = 0; ch < leadCh; ++ch)
            {
                const int srcCh = juce::jmin (ch, juce::jmax (0, inCh - 1));
                const float* src = inCh > 0 ? inBus.getReadPointer (srcCh) : nullptr;
                float* dst = leadBus.getWritePointer (ch);
                for (int i = 0; i < n; ++i) dst[i] = src ? src[i] : 0.0f;
            }
            // Silence the aux stems if the shell supplied them.
            if (auxChoir != nullptr) auxChoir->clear();
            if (auxAtmos != nullptr) auxAtmos->clear();
            leadDb.store (-100.0f); choirDbAtomic.store (-100.0f); atmosDb.store (-100.0f);
            duckGr.store (1.0f); charAtomic.store (0.0f);
            return;
        }

        // --- Pitch tracking on a hop schedule ------------------------------------
        detector.pushBlock (monoLead.data(), n);
        analysisCounter += n;
        if (analysisCounter >= analysisHop)
        {
            analysisCounter = 0;
            const auto r = detector.analyse (0.3f);
            voicedLive = r.voiced;
            if (r.voiced && r.frequencyHz > 0.0f)
            {
                float hz = r.frequencyHz;
                if (leadHzSmoothed > 0.0f) hz = sattari::dsp::foldToOctaveNear (hz, leadHzSmoothed);
                leadHzSmoothed = leadHzSmoothed > 0.0f ? leadHzSmoothed + 0.5f * (hz - leadHzSmoothed) : hz;
                leadMidiLive = sattari::dsp::nearestMidiNote (leadHzSmoothed);
            }
        }
        periodSamples = (voicedLive && leadHzSmoothed > 0.0f)
                      ? juce::jlimit (40.0, 800.0, sampleRate / (double) leadHzSmoothed)
                      : sampleRate / 150.0;

        const CharacterState cs = resolveCharacter();
        charAtomic.store (param ("character"), std::memory_order_relaxed);

        // --- Key resolution + editor mirror --------------------------------------
        int root = 0, scaleChoice = 0; bool fromBus = false;
        sattari::dsp::ScaleId scaleId = sattari::dsp::ScaleId::Major;
        resolveKey (root, scaleId, scaleChoice, fromBus);
        followingBus.store (fromBus, std::memory_order_relaxed);
        effKeyRoot.store (root, std::memory_order_relaxed);
        effKeyScale.store (fromBus ? ((scaleChoice == 1) ? 1 : 0) : scaleChoice, std::memory_order_relaxed);

        // =========================================================================
        //  1) ENSEMBLE DOUBLES -> choirBuf
        // =========================================================================
        choirBuf.clear();
        float* chL = choirBuf.getWritePointer (0);
        float* chR = choirBuf.getWritePointer (1);

        const int   voiceN     = juce::jlimit (2, kMaxDoubles, cs.voiceCount);
        const float detuneC    = cs.detuneSpread;
        const float timingMs   = cs.timing;
        const float humanize   = cs.humanize;
        const float formantSt  = cs.formantSpread;
        const float ensGain    = juce::Decibels::decibelsToGain (param ("ensembleLevel"));
        const double baseDelay = timingMs * 0.001 * sampleRate;
        const float  maxDrift  = humanize * (float) (0.006 * sampleRate);   // up to ~6 ms wobble
        // Whether RB may be used for THIS voice: RB only in Studio quality; and 8-voice
        // RB only when >4 voices would exceed the 4-voice RB cap -> fall back to PSOLA.
        const int rbVoiceCap = useRb ? (mode == 0 ? kMaxRbVoices : 4) : 0;

        for (int v = 0; v < voiceN; ++v)
        {
            auto& vc = doubles[(size_t) v];

            // Alternating detune + formant spread across the ensemble, scaled by voice.
            const float sign  = (v % 2 == 0) ? +1.0f : -1.0f;
            const float frac  = voiceN > 1 ? (float) v / (float) (voiceN - 1) : 0.0f;
            const double ratio = std::pow (2.0, (double) (sign * detuneC * (0.4f + 0.6f * frac)) / 1200.0);
            const double formantScale = std::pow (2.0, (double) (sign * formantSt * (0.3f + 0.7f * frac)) / 12.0);
            const double delaySamples = baseDelay * (1.0 + 0.28 * frac);

            // Equal-power pan spread out across the stereo field.
            const float pan = voiceN > 1 ? juce::jmap ((float) v, 0.0f, (float) (voiceN - 1), -0.9f, 0.9f) : 0.0f;
            float gl, gr; panGains (pan, gl, gr);

            // Choose engine: RbFormant (hi-fi, has formant scaling) when allowed, else PSOLA.
            const bool voiceUsesRb = (v < rbVoiceCap);
            if (voiceUsesRb)
                vc.rb.process (monoLead.data(), shiftScratch.data(), n, ratio, formantScale);
            else
                vc.psola.process (monoLead.data(), shiftScratch.data(), n, ratio, periodSamples);

            for (int i = 0; i < n; ++i)
            {
                // Slow random-walk timing drift (per-voice breath).
                if (--vc.driftCountdown <= 0)
                {
                    vc.driftTarget    = (vc.rng.nextDouble() * 2.0 - 1.0) * maxDrift;
                    vc.driftCountdown = (int) (sampleRate * (0.18 + 0.12 * vc.rng.nextDouble()));
                }
                vc.driftPos += 0.0006 * (vc.driftTarget - vc.driftPos);

                vc.delay[(size_t) vc.writePos] = shiftScratch[(size_t) i];
                const double readPos = (double) vc.writePos - (delaySamples + vc.driftPos);
                const float d = readDelay (vc.delay, delayLen, readPos) * ensGain;
                chL[i] += gl * d;
                chR[i] += gr * d;
                if (++vc.writePos >= delayLen) vc.writePos = 0;
            }
        }

        // =========================================================================
        //  2) KEY-AWARE HARMONY -> folded into choirBuf when harmonyOn
        // =========================================================================
        const bool  harmonyOn  = param ("harmonyOn") > 0.5f;
        const float harmonyMix = cs.harmonyMix;
        if (harmonyOn && harmonyMix > 1.0e-4f && voicedLive && leadMidiLive >= 0 && leadHzSmoothed > 0.0f)
        {
            for (int v = 0; v < kNumHarmony; ++v)
            {
                if (param (kHarmonyOnIds[(size_t) v]) <= 0.5f) continue;

                const int deg = intervalDegrees ((int) std::round (param (kHarmonyIntervalIds[(size_t) v])));
                const int harmonyMidi = sattari::harmony::diatonicShift (leadMidiLive, root, scaleId, deg);

                // pitchTighten blends transpose (keeps expression) -> hard-tune (exact note).
                const double targetHz       = (double) sattari::dsp::midiToHz ((float) harmonyMidi);
                const double ratioTranspose = std::pow (2.0, (double) (harmonyMidi - leadMidiLive) / 12.0);
                const double ratioHardTune  = targetHz / (double) leadHzSmoothed;
                const double t = (double) cs.pitchTighten;
                double ratio = std::pow (ratioTranspose, 1.0 - t) * std::pow (ratioHardTune, t);

                // Per-voice humanise cents walk (choir isn't robotically unison-locked).
                auto& hv = harmony[(size_t) v];
                if (v == 0 || analysisCounter == 0)
                    hv.humanizeTarget = (humanizeRng.nextFloat() * 2.0f - 1.0f) * 22.0f * humanize;
                hv.humanizeCents += (hv.humanizeTarget - hv.humanizeCents) * 0.05;
                ratio *= std::pow (2.0, hv.humanizeCents / 1200.0);
                hv.smoothedRatio += (ratio - hv.smoothedRatio) * 0.2;

                const float hGain = juce::Decibels::decibelsToGain (param (kHarmonyLevelIds[(size_t) v])) * harmonyMix;
                const float pan = juce::jmap ((float) v, 0.0f, (float) (kNumHarmony - 1), -0.7f, 0.7f);
                float gl, gr; panGains (pan, gl, gr);

                if (useRb)
                    harmony[(size_t) v].rb.process (monoLead.data(), shiftScratch.data(), n, hv.smoothedRatio, 1.0);
                else
                    harmony[(size_t) v].psola.process (monoLead.data(), shiftScratch.data(), n, hv.smoothedRatio, periodSamples);

                for (int i = 0; i < n; ++i)
                {
                    const float d = shiftScratch[(size_t) i] * hGain;
                    chL[i] += gl * d;
                    chR[i] += gr * d;
                }
            }
        }

        // =========================================================================
        //  3) PARALLEL DESTROY (tap of the ensemble) -> mixed back into choirBuf
        // =========================================================================
        const bool  destroyOn  = param ("destroyOn") > 0.5f;
        const float destroyMix = cs.destroyMix;
        if (destroyOn && destroyMix > 1.0e-4f)
        {
            destroyBuf.clear();
            for (int ch = 0; ch < 2; ++ch)
                destroyBuf.copyFrom (ch, 0, choirBuf, ch, 0, n);

            double f1 = 730.0, f2 = 1090.0;
            vowelFormants (param ("vowelAmt"), f1, f2);   // vowel position = the vowel amount knob
            const float vowelAmt = juce::jlimit (0.0f, 1.0f, param ("vowelAmt"));
            const float ringAmt  = cs.ringAmt;
            const double robotInc = 2.0 * juce::MathConstants<double>::pi * 120.0 / sampleRate;   // fixed robot freq
            const float drive    = cs.drive;
            const float driveK   = 1.0f + drive * 24.0f;
            const float crush    = cs.crush;
            const int   crushBits = (int) std::round (juce::jmap (crush, 0.0f, 1.0f, 16.0f, 3.0f));
            const int   crushDown = 1 + (int) std::round (crush * 12.0f);
            const float crushLevels = (float) (1 << juce::jlimit (2, 16, crushBits));

            for (int ch = 0; ch < 2; ++ch)
            {
                destroyF1[(size_t) ch].setBandpass (sampleRate, f1, 6.0);
                destroyF2[(size_t) ch].setBandpass (sampleRate, f2, 9.0);
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = destroyBuf.getWritePointer (ch);
                double rp = destroyRingPhase[(size_t) ch];
                for (int i = 0; i < n; ++i)
                {
                    float x = d[i];
                    if (vowelAmt > 1.0e-4f)
                    {
                        const float formant = 0.6f * destroyF1[(size_t) ch].process (x)
                                            + 0.5f * destroyF2[(size_t) ch].process (x);
                        x = x * (1.0f - vowelAmt) + formant * vowelAmt * 1.8f;
                    }
                    if (ringAmt > 1.0e-4f)
                    {
                        const float carrier = (float) std::sin (rp);
                        x = x * (1.0f - ringAmt) + (x * carrier) * ringAmt;
                    }
                    rp += robotInc; if (rp > 2.0 * juce::MathConstants<double>::pi) rp -= 2.0 * juce::MathConstants<double>::pi;
                    if (drive > 1.0e-4f)
                        x = std::tanh (x * driveK) * (1.0f / std::tanh (driveK)) * (0.6f + 0.4f * drive) + x * (1.0f - drive);
                    if (crush > 1.0e-4f)
                    {
                        if (crushCount[(size_t) ch] <= 0)
                        {
                            crushHold[(size_t) ch] = std::round (x * crushLevels) / crushLevels;
                            crushCount[(size_t) ch] = crushDown;
                        }
                        --crushCount[(size_t) ch];
                        x = x * (1.0f - crush) + crushHold[(size_t) ch] * crush;
                    }
                    d[i] = juce::jlimit (-1.5f, 1.5f, x);
                }
                destroyRingPhase[(size_t) ch] = rp;
            }

            // Parallel blend of the destroyed tap into the choir.
            for (int ch = 0; ch < 2; ++ch)
            {
                float* c = choirBuf.getWritePointer (ch);
                const float* dd = destroyBuf.getReadPointer (ch);
                for (int i = 0; i < n; ++i)
                    c[i] = c[i] * (1.0f - destroyMix) + dd[i] * destroyMix;
            }
        }

        // =========================================================================
        //  4) REVERSE-DELAY TRAIL (v1 simple) -> mixed into choirBuf
        // =========================================================================
        const bool  trailsOn  = param ("trailsOn") > 0.5f;
        const float trailsMix = cs.trailsMix;
        if (trailsOn && trailsMix > 1.0e-4f && trailCap > 0)
        {
            const int trailLen = juce::jlimit (2, trailCap - 1,
                (int) std::lround ((double) param ("trailSizeMs") * 0.001 * sampleRate));
            const float rev = cs.trailReverse;   // 0=forward .. 1=full reverse read
            for (int i = 0; i < n; ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* c = choirBuf.getWritePointer (ch);
                    trailRing[(size_t) ch][(size_t) trailWrite] = c[i];
                }
                // Reverse read walks BACKWARD through the window; forward walks with input.
                const double fwdPos = (double) trailWrite;
                const double revPos = (double) trailWrite - trailReadPos;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* c = choirBuf.getWritePointer (ch);
                    const float fwd = readDelay (trailRing[(size_t) ch], trailCap, fwdPos - trailLen);
                    const float rvs = readDelay (trailRing[(size_t) ch], trailCap, revPos);
                    const float trail = fwd * (1.0f - rev) + rvs * rev;
                    c[i] = c[i] + trail * trailsMix;
                }
                if (++trailWrite >= trailCap) trailWrite = 0;
                trailReadPos -= 1.0;
                if (trailReadPos <= 0.0 || trailReadPos >= trailLen) trailReadPos = trailLen - 1.0;
            }
        }

        // =========================================================================
        //  5) ATMOSPHERE = pure-wet reverb on the choir; ducked by |lead| follower.
        // =========================================================================
        atmosBuf.clear();
        for (int ch = 0; ch < 2; ++ch)
            atmosBuf.copyFrom (ch, 0, choirBuf, ch, 0, n);

        juce::Reverb::Parameters rp;
        rp.roomSize   = juce::jlimit (0.0f, 1.0f, param ("reverbSize"));
        rp.damping    = juce::jlimit (0.0f, 1.0f, param ("reverbDamp"));
        rp.wetLevel   = 1.0f / 3.0f;   // wet is *3 internally -> ~unity pure wet
        rp.dryLevel   = 0.0f;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (atmosBuf.getWritePointer (0), atmosBuf.getWritePointer (1), n);

        // Duck the atmosphere by the lead envelope (side-chain-master pattern).
        duckFollower.setTimes (param ("duckAttackMs"), param ("duckReleaseMs"));
        const float duckAmount = cs.duckAmount;
        float lastDuckGain = 1.0f;
        {
            float* aL = atmosBuf.getWritePointer (0);
            float* aR = atmosBuf.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                const float env = duckFollower.processRectified (monoLead[(size_t) i]);
                const float gr = 1.0f - duckAmount * juce::jlimit (0.0f, 1.0f, env);
                aL[i] *= gr;
                aR[i] *= gr;
                lastDuckGain = gr;
            }
        }
        duckGr.store (lastDuckGain, std::memory_order_relaxed);

        // =========================================================================
        //  6) MONO-SAFE WIDTH on CHOIR + ATMOSPHERE (lead stays mono-centre).
        // =========================================================================
        const float width      = cs.width;
        const bool  lowMono    = param ("lowMono") > 0.5f;
        const float lowMonoHz  = param ("lowMonoFreq");
        const bool  monoAud    = param ("monoAudition") > 0.5f;
        lowMonoHP[0].setHP2 (lowMonoHz, sampleRate);
        lowMonoHP[1].setHP2 (lowMonoHz, sampleRate);

        auto widen = [&] (juce::AudioBuffer<float>& b, int hpIdx)
        {
            float* L = b.getWritePointer (0);
            float* R = b.getWritePointer (1);
            auto& hp = lowMonoHP[(size_t) hpIdx];
            for (int i = 0; i < n; ++i)
            {
                const float mid  = 0.5f * (L[i] + R[i]);   // mid NEVER modified -> mono sum preserved
                float       side = 0.5f * (L[i] - R[i]);
                side *= width;
                if (lowMono) side = (float) hp.process ((double) side);
                float outL = mid + side, outR = mid - side;
                if (monoAud) { outL = mid; outR = mid; }
                L[i] = juce::jlimit (-4.0f, 4.0f, outL);
                R[i] = juce::jlimit (-4.0f, 4.0f, outR);
            }
        };
        widen (choirBuf, 0);
        widen (atmosBuf, 1);

        // =========================================================================
        //  7) LEAD dry, delayed to match max engine latency (report latency).
        // =========================================================================
        const float leadGain  = juce::Decibels::decibelsToGain (param ("leadLevel"));
        const float choirGain = juce::Decibels::decibelsToGain (param ("choirLevel"));
        const float atmosGain = juce::Decibels::decibelsToGain (param ("atmosLevel"));
        const int   ldl       = (int) leadDelay[0].size();

        // Build the delayed dry lead per output channel (mono lead -> centred).
        // We render into a scratch region of choirBuf-independent leadDelay ring.
        // For each sample: write the mono lead, read it delayed by reportedLatency.
        // Then scale by inGain-adjusted lead gain. (monoLead already has inGain.)
        std::array<float, 2> leadReadTmp {};
        juce::ignoreUnused (leadReadTmp);

        // =========================================================================
        //  8) COMPOSE the output buses (+ single-out FOLD-DOWN of inactive aux).
        // =========================================================================
        // Meters.
        float choirPeak = 0.0f, atmosPeak = 0.0f, leadPeak = 0.0f;

        // The lead delay ring: mono in, mono out delayed.
        // We consume it sample by sample below while composing.
        for (int i = 0; i < n; ++i)
        {
            // Advance the lead delay ring (mono).
            const int wr = leadDelayWrite;
            leadDelay[0][(size_t) wr] = monoLead[(size_t) i];
            const int rd = (wr - reportedLatency + ldl) % ldl;
            const float leadDry = leadDelay[0][(size_t) rd];
            if (++leadDelayWrite >= ldl) leadDelayWrite = 0;

            const float leadOut = leadDry * leadGain;
            leadPeak = juce::jmax (leadPeak, std::fabs (leadOut));

            const float cL = chL[i] * choirGain;
            const float cR = chR[i] * choirGain;
            const float aL = atmosBuf.getReadPointer (0)[i] * atmosGain;
            const float aR = atmosBuf.getReadPointer (1)[i] * atmosGain;
            choirPeak = juce::jmax (choirPeak, juce::jmax (std::fabs (cL), std::fabs (cR)));
            atmosPeak = juce::jmax (atmosPeak, juce::jmax (std::fabs (aL), std::fabs (aR)));

            // --- LEAD main bus: mono-centred dry + folded inactive aux -----------
            float mainL = leadOut;
            float mainR = leadOut;
            if (! choirActive) { mainL += cL; mainR += cR; }   // fold choir into lead
            if (! atmosActive) { mainL += aL; mainR += aR; }   // fold atmos into lead

            for (int ch = 0; ch < leadCh; ++ch)
            {
                const float y = softCeiling ((ch == 0 ? mainL : mainR) * outGain);
                leadBus.getWritePointer (ch)[i] = y;
            }
        }

        // --- CHOIR aux bus (if enabled) ------------------------------------------
        if (choirActive)
        {
            auto& cb = *auxChoir;
            for (int ch = 0; ch < cb.getNumChannels(); ++ch)
            {
                const float* src = choirBuf.getReadPointer (juce::jmin (ch, 1));
                float* dst = cb.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    dst[i] = softCeiling (src[i] * choirGain * outGain);
            }
        }

        // --- ATMOSPHERE aux bus (if enabled) -------------------------------------
        if (atmosActive)
        {
            auto& ab = *auxAtmos;
            for (int ch = 0; ch < ab.getNumChannels(); ++ch)
            {
                const float* src = atmosBuf.getReadPointer (juce::jmin (ch, 1));
                float* dst = ab.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    dst[i] = softCeiling (src[i] * atmosGain * outGain);
            }
        }

        // Meters -> atomics (dB).
        auto toDb = [] (float peak) { return peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -100.0f; };
        leadDb.store (toDb (leadPeak), std::memory_order_relaxed);
        choirDbAtomic.store (toDb (choirPeak), std::memory_order_relaxed);
        atmosDb.store (toDb (atmosPeak), std::memory_order_relaxed);
    }

    // -- State --------------------------------------------------------------------

    // --- Lock-free readouts for an editor ----------------------------------
    bool  isFollowingBusKey() const noexcept { return followingBus.load (std::memory_order_relaxed); }
    int   effectiveRoot()     const noexcept { return effKeyRoot.load (std::memory_order_relaxed); }
    int   effectiveScale()    const noexcept { return effKeyScale.load (std::memory_order_relaxed); }
    float leadLevelDb()       const noexcept { return leadDb.load (std::memory_order_relaxed); }
    float choirLevelDb()      const noexcept { return choirDbAtomic.load (std::memory_order_relaxed); }
    float atmosLevelDb()      const noexcept { return atmosDb.load (std::memory_order_relaxed); }
    float duckGainNow()       const noexcept { return duckGr.load (std::memory_order_relaxed); }
    float characterNow()      const noexcept { return charAtomic.load (std::memory_order_relaxed); }

private:
    // --- CHARACTER macro ------------------------------------------------------
    // The per-block anchor evaluation (HUMAN 0 -> MASSIVE .5 -> DESTROYED 1). Every
    // "eff*" field is the effective, macro-driven value the DSP actually uses; the
    // user knobs act as ceilings/offsets on top (see resolveCharacter()).
    struct CharacterState
    {
        int   voiceCount   = 4;
        float detuneSpread = 12.0f;   // cents
        float timing       = 16.0f;   // ms
        float humanize     = 0.35f;
        float formantSpread = 2.0f;   // semitones
        float harmonyMix   = 0.5f;
        float alignAmount  = 0.7f;
        float pitchTighten = 0.6f;
        float destroyMix   = 0.0f;
        float drive        = 0.35f;
        float crush        = 0.2f;
        float ringAmt      = 0.15f;
        float trailsMix    = 0.0f;
        float trailReverse = 0.5f;
        float reverbMix    = 0.3f;
        float duckAmount   = 0.6f;
        float width        = 1.2f;
    };
    // =========================================================================
    //  State
    // =========================================================================
    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sampleRate = 44100.0;
    int    preparedBlockSize = 512;

    // --- Lead pitch tracking (YIN) — drives PSOLA grain spacing + harmony MIDI --
    sattari::dsp::PitchDetector detector;
    int   analysisHop = 0, analysisCounter = 0;
    int   leadMidiLive = -1;
    bool  voicedLive = false;
    float leadHzSmoothed = 0.0f;
    double periodSamples = 300.0;   // detected fundamental period (fallback ~150 Hz)

    // --- Per-voice ensemble double engines -----------------------------------
    // Each double has BOTH a PSOLA shifter (Live/Glass, low-latency) and an
    // RbFormant (Studio, hi-fi). Only one is used per block by engine quality; both
    // are prepared up front. Timing-drift random-walk + a delay ring per voice
    // (the Sattari Double pattern).
    struct DoubleVoice
    {
        sattari::dsp::PsolaShifter psola;
        sattari::dsp::RbFormant rb;
        std::vector<float> delay;      // per-voice timing delay ring
        int    writePos = 0;
        double driftPos = 0.0, driftTarget = 0.0;
        int    driftCountdown = 0;
        juce::Random rng;
    };
    std::array<DoubleVoice, kMaxDoubles> doubles;
    int delayLen = 0;

    // --- Harmony voices (diatonic, RbFormant when RB available else PSOLA) -----
    struct HarmonyVoice
    {
        sattari::dsp::PsolaShifter psola;
        sattari::dsp::RbFormant rb;
        double smoothedRatio = 1.0;
        double humanizeCents = 0.0, humanizeTarget = 0.0;
    };
    std::array<HarmonyVoice, kNumHarmony> harmony;
    juce::Random humanizeRng;

    // --- Reverb (Freeverb pure-wet) on the choir -> atmosphere ---------------
    juce::Reverb reverb;

    // --- Ducking follower keyed off |lead| -----------------------------------
    sattari::dsp::EnvelopeFollower duckFollower;

    // --- Imager-style width filters (M/S low-mono on choir + atmosphere) ------
    // 2nd-order Butterworth HP for the elliptical low-mono side strip. One per
    // widened bus (choir, atmosphere).
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0; }
        void setHP2 (double f, double sr) noexcept
        {
            f = juce::jlimit (20.0, sr * 0.45, f);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double c = std::cos (w0), s = std::sin (w0);
            const double alpha = s / (2.0 * 0.70710678118654752);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    std::array<Biquad, 2> lowMonoHP;   // [0]=choir, [1]=atmosphere

    // --- Warp-style DESTROY chain state (parallel tap) ------------------------
    // Vowel bandpass resonators (F1/F2), ring-mod phase, bit/SR crush hold.
    struct DestroyBiquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() noexcept { z1 = z2 = 0.0; }
        void setBandpass (double sr, double freq, double q) noexcept
        {
            freq = juce::jlimit (20.0, sr * 0.45, freq);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sr;
            const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
            const double cw = std::cos (w0);
            const double a0 = 1.0 + alpha;
            b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
            a1 = (-2.0 * cw) / a0; a2 = (1.0 - alpha) / a0;
        }

        // -- Choice lists -------------------------------------------------------------
        inline float process (float x) noexcept
        {
            const double y = b0 * (double) x + z1;
            z1 = b1 * (double) x - a1 * y + z2;
            z2 = b2 * (double) x - a2 * y;
            return (float) y;
        }
    };
    std::array<DestroyBiquad, 2> destroyF1, destroyF2;   // per channel (L/R)
    std::array<double, 2> destroyRingPhase { { 0.0, 0.0 } };
    std::array<float, 2>  crushHold { { 0.0f, 0.0f } };
    std::array<int, 2>    crushCount { { 0, 0 } };

    // --- Reverse-delay TRAIL (v1 simple; full granular trail = v2) ------------
    // A stereo ring recorded from the choir; a read head walks it in reverse over
    // trailSizeMs, blended by trailsMix.
    std::array<std::vector<float>, 2> trailRing;
    int    trailCap = 0, trailWrite = 0;
    double trailReadPos = 0.0;

    // --- Preallocated per-block scratch (sized in prepareToPlay) --------------
    std::vector<float> monoLead;      // mono-summed lead
    std::vector<float> shiftScratch;  // one voice's shifted signal
    juce::AudioBuffer<float> choirBuf;   // 2 x block — ensemble + harmony sum
    juce::AudioBuffer<float> atmosBuf;   // 2 x block — reverb wet
    juce::AudioBuffer<float> destroyBuf; // 2 x block — parallel destroy tap
    juce::AudioBuffer<float> reverbDry;  // 2 x block — pre-reverb copy for pure-wet mix

    // --- Lead dry-delay ring (aligns lead to max engine latency) --------------
    std::array<std::vector<float>, 2> leadDelay;
    int leadDelayWrite = 0;
    int reportedLatency = 0;

    // --- Cross-plugin buses (consumer only) ----------------------------------
    sattari::xplugin::SessionReader sessionReader;   // song-key bus (Follow Song Key)
    sattari::xplugin::TempoReader   tempoReader;     // tempo bus (synced trail fallback)

    // --- KEY status mirrored for the editor (updated each block) -------------
    std::atomic<bool> followingBus { false };
    std::atomic<int>  effKeyRoot { 0 };
    std::atomic<int>  effKeyScale { 0 };

    // --- Live viz atomics -----------------------------------------------------
    std::atomic<float> leadDb { -100.0f }, choirDbAtomic { -100.0f }, atmosDb { -100.0f };
    std::atomic<float> duckGr { 1.0f }, charAtomic { 0.35f };

        // Male vowel formants (F1, F2) in Hz for A E I O U — the DESTROY vowel morph
        // sweeps across them (mirrors plugins/warp).
        static constexpr int    kNumVowels = 5;
        static constexpr double kVowelF1[kNumVowels] = { 730.0, 530.0, 270.0, 570.0, 300.0 };
        static constexpr double kVowelF2[kNumVowels] = { 1090.0, 1840.0, 2290.0, 840.0, 870.0 };

        static void vowelFormants (double morph, double& f1, double& f2) noexcept
        {
            const double x = juce::jlimit (0.0, 1.0, morph) * (kNumVowels - 1);
            const int    i = juce::jlimit (0, kNumVowels - 2, (int) std::floor (x));
            const double t = x - i;
            f1 = kVowelF1[i] + (kVowelF1[i + 1] - kVowelF1[i]) * t;
            f2 = kVowelF2[i] + (kVowelF2[i + 1] - kVowelF2[i]) * t;
        }

        // Piecewise-linear anchor lerp: HUMAN(0) -> MASSIVE(.5) -> DESTROYED(1).
        static float anchor3 (float c, float lo, float mid, float hi) noexcept
        {
            c = juce::jlimit (0.0f, 1.0f, c);
            return c < 0.5f ? juce::jmap (c, 0.0f, 0.5f, lo, mid)
                            : juce::jmap (c, 0.5f, 1.0f, mid, hi);
        }

        // Equal-power pan: pan in [-1,+1] -> L/R gains.
        static void panGains (float pan, float& l, float& r) noexcept
        {
            const float tt = juce::jlimit (0.0f, 1.0f, (pan + 1.0f) * 0.5f);
            l = std::cos (tt * juce::MathConstants<float>::halfPi);
            r = std::sin (tt * juce::MathConstants<float>::halfPi);
        }

        // Fractional read from a delay ring, pos in samples (may be negative).
        static float readDelay (const std::vector<float>& ring, int len, double pos) noexcept
        {
            if (len <= 0) return 0.0f;
            while (pos < 0.0)  pos += len;
            while (pos >= len) pos -= len;
            const int i0 = (int) pos;
            const int i1 = (i0 + 1) % len;
            const float f = (float) (pos - i0);
            return ring[(size_t) i0] * (1.0f - f) + ring[(size_t) i1] * f;
        }

        static constexpr const char* kHarmonyOnIds[kNumHarmony]       = { "h1on", "h2on", "h3on", "h4on" };
        static constexpr const char* kHarmonyIntervalIds[kNumHarmony] = { "h1int", "h2int", "h3int", "h4int" };
        static constexpr const char* kHarmonyLevelIds[kNumHarmony]    = { "h1level", "h2level", "h3level", "h4level" };

    // -- Biquad coefficient setters -----------------------------------------------

    CharacterState resolveCharacter() const noexcept
    {
        const float c    = juce::jlimit (0.0f, 1.0f, param ("character"));
        const int   mode = juce::jlimit (0, 2, (int) std::lround (param ("mode")));   // 0 Studio,1 Choir,2 Glass

        CharacterState s;

        // Voice count from the anchor 2->4->8, then take the SMALLER of the macro pick
        // and the user's explicit ceiling (voiceCount choice 0/1/2 -> 2/4/8).
        const float vAnchor = anchor3 (c, 2.0f, 4.0f, 8.0f);
        const int userVoices = (int) std::lround (param ("voiceCount"));
        const int userVoiceN = userVoices == 0 ? 2 : (userVoices == 1 ? 4 : 8);
        int macroVoiceN = vAnchor <= 3.0f ? 2 : (vAnchor <= 6.0f ? 4 : 8);
        s.voiceCount = juce::jmin (macroVoiceN, userVoiceN);

        // Spread/timing/humanize/formant: macro anchor CAPPED by the user knob ceiling.
        s.detuneSpread  = juce::jmin (anchor3 (c, 6.0f, 18.0f, 40.0f), param ("detuneSpread"));
        s.timing        = juce::jmin (anchor3 (c, 8.0f, 22.0f, 40.0f), param ("timing"));
        s.humanize      = juce::jmin (anchor3 (c, 0.15f, 0.45f, 0.85f), param ("humanize"));
        s.formantSpread = juce::jmin (anchor3 (c, 0.5f, 3.0f, 9.0f), param ("formantSpread"));

        // Harmony/align/tighten anchors, offset-blended with the user knobs.
        s.harmonyMix   = juce::jmin (anchor3 (c, 0.15f, 0.5f, 0.8f), param ("harmonyMix"));
        s.alignAmount  = juce::jmin (anchor3 (c, 0.9f, 0.6f, 0.25f), param ("alignAmount"));
        s.pitchTighten = juce::jmin (anchor3 (c, 0.85f, 0.5f, 0.1f), param ("pitchTighten"));

        // Destroy row: macro anchor, capped by the user knobs (0 = off).
        s.destroyMix = juce::jmin (anchor3 (c, 0.0f, 0.15f, 0.85f), param ("destroyMix"));
        s.drive      = juce::jmin (anchor3 (c, 0.1f, 0.35f, 0.9f), param ("drive"));
        s.crush      = juce::jmin (anchor3 (c, 0.0f, 0.15f, 0.8f), param ("crush"));
        s.ringAmt    = juce::jmin (anchor3 (c, 0.0f, 0.1f, 0.6f), param ("ringAmt"));

        // Trails + reverb + duck + width.
        s.trailsMix    = juce::jmin (anchor3 (c, 0.0f, 0.2f, 0.7f), param ("trailsMix"));
        s.trailReverse = anchor3 (c, 0.2f, 0.5f, 0.9f);
        s.reverbMix    = juce::jmin (anchor3 (c, 0.15f, 0.35f, 0.6f), param ("reverbMix"));
        s.duckAmount   = juce::jmin (anchor3 (c, 0.7f, 0.6f, 0.4f), param ("duckAmount"));
        s.width        = juce::jmin (anchor3 (c, 1.0f, 1.3f, 1.7f), param ("width"));

        // Mode nudges: Studio pulls destroy DOWN; Choir pushes harmony UP; Glass pushes
        // destroy + trails UP. Small, then clamped to each row's param range.
        if (mode == 0)   // Studio: cleaner
        {
            s.destroyMix *= 0.4f; s.crush *= 0.4f; s.ringAmt *= 0.4f;
        }
        else if (mode == 1)   // Choir: lusher harmony
        {
            s.harmonyMix = juce::jmin (1.0f, s.harmonyMix + 0.15f);
            s.destroyMix *= 0.7f;
        }
        else   // Glass: more destroy + trails
        {
            s.destroyMix = juce::jmin (1.0f, s.destroyMix + 0.15f);
            s.trailsMix  = juce::jmin (1.0f, s.trailsMix + 0.15f);
        }

        // Final clamp to each row's param range.
        s.detuneSpread  = juce::jlimit (0.0f, 50.0f, s.detuneSpread);
        s.timing        = juce::jlimit (0.0f, 45.0f, s.timing);
        s.humanize      = juce::jlimit (0.0f, 1.0f, s.humanize);
        s.formantSpread = juce::jlimit (0.0f, 12.0f, s.formantSpread);
        s.harmonyMix    = juce::jlimit (0.0f, 1.0f, s.harmonyMix);
        s.alignAmount   = juce::jlimit (0.0f, 1.0f, s.alignAmount);
        s.pitchTighten  = juce::jlimit (0.0f, 1.0f, s.pitchTighten);
        s.destroyMix    = juce::jlimit (0.0f, 1.0f, s.destroyMix);
        s.drive         = juce::jlimit (0.0f, 1.0f, s.drive);
        s.crush         = juce::jlimit (0.0f, 1.0f, s.crush);
        s.ringAmt       = juce::jlimit (0.0f, 1.0f, s.ringAmt);
        s.trailsMix     = juce::jlimit (0.0f, 1.0f, s.trailsMix);
        s.trailReverse  = juce::jlimit (0.0f, 1.0f, s.trailReverse);
        s.reverbMix     = juce::jlimit (0.0f, 1.0f, s.reverbMix);
        s.duckAmount    = juce::jlimit (0.0f, 1.0f, s.duckAmount);
        s.width         = juce::jlimit (0.0f, 2.0f, s.width);
        return s;
    }

    // -- Key resolution -----------------------------------------------------------

    void resolveKey (int& root, sattari::dsp::ScaleId& scale,
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

    static float softCeiling (float x) noexcept
    {
        // Gentle tanh ceiling: linear near unity, saturates before ~ +/-2.0.
        if (x > 1.2f || x < -1.2f)
            return std::tanh (x);
        return x;
    }

    float param (const char* id) const noexcept
    {
        if (state == nullptr) return 0.0f;
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioBuffer<float>* auxChoir = nullptr;
    juce::AudioBuffer<float>* auxAtmos = nullptr;
};

} // namespace sattari::dsp
