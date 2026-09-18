#include "PluginProcessor.h"
#include <sattari_audio_core/SattariArrangementClip.h>
#include "PluginEditor.h"
#include <SattariAutoPitchModelData.h>

#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <complex>
#include <cstring>
#include <functional>
#include <limits>

namespace
{
constexpr int pitchFrameSize = 2048;
constexpr int pitchHopSize = 256;
// Fixed DSP-control grid: host callback sizes must not change pitch automation.
constexpr int rubberBandAutomationQuantum = 128;
constexpr double rubberBandStationaryCents = 1.0;
// R3's multi-resolution vocal window has additional reconstruction group delay
// beyond getStartDelay(). The API value describes start trimming; it is not the
// half-rise group delay a host must compensate for a continuous insert. Calibrated
// against both a forced unity render and a genuinely tuned onset, 0.65 of R3's
// start delay centres the two measurements within 0.6 ms at 48 kHz. Deriving the
// value from getStartDelay() keeps the compensation proportional if R3 selects a
// larger analysis window at a higher sample rate.
// R3/Finer Short's reconstructed onset is not a single impulse delay. At
// 48 kHz/512, forced-unity measures 2,848 samples and a genuinely tuned onset
// measures 2,922. Report their midpoint (2,885): each path is then within 37
// samples / 0.77 ms of host PDC, instead of the Standard-window calibration's
// 3,392-sample report placing corrected audio about 10 ms early.
constexpr double rubberBandR3GroupDelayFraction = 0.25390625;
constexpr double minVoiceHz = 70.0;
constexpr double maxVoiceHz = 950.0;
constexpr double silenceRms = 0.0045;
constexpr double minConfidence = 0.58;

const juce::StringArray rootChoices { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
// The original Scale parameter must remain exactly three choices: JUCE stores
// AudioParameterChoice values normalised, so appending choices would silently
// turn an old saved "minor" (0.5) into Mixolydian. scaleMode is the extended,
// local/manual palette; Scale remains the compatibility + suite-bus value.
const juce::StringArray legacyScaleChoices { "major", "minor", "chromatic" };
const juce::StringArray scaleChoices {
    "major", "minor", "chromatic", "dorian", "phrygian", "lydian",
    "mixolydian", "locrian", "harmonic minor", "melodic minor",
    "major pentatonic", "minor pentatonic", "blues"
};
const juce::StringArray styleChoices { "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" };
constexpr std::array<const char*, 4> harmonyEnableIds { "h1Enable", "h2Enable", "h3Enable", "h4Enable" };
constexpr std::array<const char*, 4> harmonySoloIds { "h1Solo", "h2Solo", "h3Solo", "h4Solo" };
constexpr std::array<const char*, 4> harmonyRoleIds { "h1Role", "h2Role", "h3Role", "h4Role" };
constexpr std::array<const char*, 4> harmonyDegreeIds { "h1Degrees", "h2Degrees", "h3Degrees", "h4Degrees" };
constexpr std::array<const char*, 4> harmonyOctaveIds { "h1Octave", "h2Octave", "h3Octave", "h4Octave" };
constexpr std::array<const char*, 4> harmonyLevelIds { "h1Level", "h2Level", "h3Level", "h4Level" };
constexpr std::array<const char*, 4> harmonyPanIds { "h1Pan", "h2Pan", "h3Pan", "h4Pan" };
constexpr std::array<const char*, 4> harmonyDelayIds { "h1Delay", "h2Delay", "h3Delay", "h4Delay" };
constexpr std::array<const char*, 4> harmonyDetuneIds { "h1Detune", "h2Detune", "h3Detune", "h4Detune" };
constexpr std::array<const char*, 4> harmonyVibratoIds { "h1Vibrato", "h2Vibrato", "h3Vibrato", "h4Vibrato" };

constexpr std::array<unsigned short, 13> scaleMasks {
    // Major, natural minor, chromatic, church modes, harmonic/melodic minor,
    // pentatonics and blues. Bit n means scale degree n is allowed.
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    0x0fffu,
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9),
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10),
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)
};

// Output ceiling: identity below the knee, then a tanh soft-knee asymptoting to just
// under 0 dBFS. It is a SAFETY ceiling, not a tone stage — the knee sits at -0.7 dBFS so
// it is inaudible on any normal vocal (a hot -1 dBFS take is untouched) and only engages
// on genuine overshoot from the pitch shift / Style stages, keeping it under 0 dBFS.
//
// The knee used to sit at -2.5 dBFS (0.75), which soft-clipped every loud syllable of a
// mastered-hot vocal and measured 0.6-2.5% THD across -1..0 dBFS — audible as the
// "distortion/clipping" it was supposed to prevent. At 0.92 that same range is 0-0.7%.
inline float softCeiling (float x) noexcept
{
    constexpr float knee = 0.92f, ceiling = 0.9955f, range = ceiling - knee;
    const float a = std::fabs (x);
    if (a <= knee) return x;
    return std::copysign (knee + range * std::tanh ((a - knee) / range), x);
}

juce::String readObjectString (const juce::var& object, const juce::Identifier& key, const juce::String& fallback = {})
{
    if (auto* dynamicObject = object.getDynamicObject())
        if (dynamicObject->hasProperty (key))
            return dynamicObject->getProperty (key).toString();

    return fallback;
}

double readObjectDouble (const juce::var& object, const juce::Identifier& key, double fallback = 0.0)
{
    if (auto* dynamicObject = object.getDynamicObject())
        if (dynamicObject->hasProperty (key))
            return (double) dynamicObject->getProperty (key);

    return fallback;
}

juce::var readObjectVar (const juce::var& object, const juce::Identifier& key)
{
    if (auto* dynamicObject = object.getDynamicObject())
        if (dynamicObject->hasProperty (key))
            return dynamicObject->getProperty (key);

    return {};
}

double midiFromHz (double frequencyHz, double referenceHz = 440.0)
{
    if (frequencyHz <= 0.0 || referenceHz <= 0.0)
        return 0.0;
    return 69.0 + 12.0 * std::log2 (frequencyHz / referenceHz);
}

bool scaleAllowsDegree (int degree, const juce::String& scaleName)
{
    const int idx = juce::jmax (0, scaleChoices.indexOf (scaleName));
    return (scaleMasks[(size_t) juce::jlimit (0, (int) scaleMasks.size() - 1, idx)]
            & (1u << juce::jlimit (0, 11, degree))) != 0;
}

int nearestScaleMidi (double sourceMidi, int rootIndex, const juce::String& scaleName)
{
    if (scaleName == "chromatic")
        return (int) std::round (sourceMidi);

    int bestMidi = (int) std::round (sourceMidi);
    double bestDistance = std::numeric_limits<double>::max();

    const int centre = (int) std::round (sourceMidi);
    for (int candidate = centre - 12; candidate <= centre + 12; ++candidate)
    {
        const int degree = ((candidate - rootIndex) % 12 + 12) % 12;
        if (! scaleAllowsDegree (degree, scaleName))
            continue;

        const double distance = std::abs ((double) candidate - sourceMidi);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestMidi = candidate;
        }
    }

    return bestMidi;
}

float readLinearInterpolated (const juce::AudioBuffer<float>& buffer, int channel, double position)
{
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return 0.0f;

    const int i0 = juce::jlimit (0, numSamples - 1, (int) std::floor (position));
    const int i1 = juce::jlimit (0, numSamples - 1, i0 + 1);
    const float frac = (float) (position - (double) i0);
    const float a = buffer.getSample (channel, i0);
    const float b = buffer.getSample (channel, i1);
    return a + (b - a) * frac;
}
}

SattariAutoPitchAudioProcessor::SattariAutoPitchAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Engine-select params change the reported latency (PSOLA vs Rubber Band + FIFO
    // cushion). Listen so a runtime toggle refreshes the host delay-comp figure via
    // handleAsyncUpdate() on the message thread (setLatencySamples must not run on
    // the audio thread).
    apvts.addParameterListener ("liveEngine", this);
    apvts.addParameterListener ("liveMode", this);
    apvts.addParameterListener ("liveProfile", this);
    apvts.addParameterListener ("extremeLive", this);
    apvts.addParameterListener ("lowLatency", this);
    apvts.addParameterListener ("scale", this);
    // Polyphonic engages the phase-vocoder path, whose STFT latency differs from
    // the mono engines — listen so a toggle refreshes the host delay-comp figure.
    apvts.addParameterListener ("polyphonic", this);
    // These also switch the active engine class (and thus the real latency): Transpose
    // forces Rubber Band whatever else is on, and turning correction off drops the
    // Polyphonic engine back to the mono path. Listen so the reported latency follows.
    apvts.addParameterListener ("xposeMode", this);
    apvts.addParameterListener ("enableCorrection", this);
    apvts.addParameterListener ("graphTimeCorrection", this);

    // Publish the correction key to the cross-plugin bus on a message-thread heartbeat,
    // so followers keep tracking even with the editor closed and no neural model. The
    // publish self-throttles (CAS, ~1 Hz) and no-ops unless AutoKey / Key Map is active.
    startTimerHz (2);
}

SattariAutoPitchAudioProcessor::~SattariAutoPitchAudioProcessor()
{
    stopTimer();   // bus-publish heartbeat
    apvts.removeParameterListener ("liveEngine", this);
    apvts.removeParameterListener ("liveMode", this);
    apvts.removeParameterListener ("liveProfile", this);
    apvts.removeParameterListener ("extremeLive", this);
    apvts.removeParameterListener ("lowLatency", this);
    apvts.removeParameterListener ("scale", this);
    apvts.removeParameterListener ("polyphonic", this);
    apvts.removeParameterListener ("xposeMode", this);
    apvts.removeParameterListener ("enableCorrection", this);
    apvts.removeParameterListener ("graphTimeCorrection", this);
    // A latency-refresh may be queued on the message thread; cancel it before we
    // start tearing members down.
    cancelPendingUpdate();

    // Stop the CREPE inference worker before any members it touches are destroyed.
    crepeThreadRun.store (false);
    if (crepeThread.joinable())
        crepeThread.join();

    // Same for the full-song analysis worker (it owns a format reader / file handle).
    fileWorkerCancel.store (true, std::memory_order_release);
    if (fileWorker.joinable())
        fileWorker.join();

    // Stop and join the offline scan thread cleanly so we never leak a worker
    // across plugin unloads (the worker holds a juce::AudioFormatReader that
    // owns a file handle). Signal cancel first so the join returns promptly.
    keyMapScanCancel.store (true);
    if (keyMapScanThread && keyMapScanThread->joinable())
        keyMapScanThread->join();
}

void SattariAutoPitchAudioProcessor::crepeWorkerLoop()
{
    using namespace std::chrono_literals;
    while (crepeThreadRun.load (std::memory_order_relaxed))
    {
        // Only run inference when the user has enabled Neural Pitch and a model
        // is loaded. The heavy ONNX Run() happens HERE, off the audio thread. When
        // offline, the audio thread drives inference synchronously instead, so the
        // worker steps aside (offlineMode) — the mutex guards the hand-off window.
        if (crepeModelPresent
            && ! offlineMode.load (std::memory_order_relaxed)
            && apvts.getRawParameterValue ("neuralPitch")->load() > 0.5f)
        {
            const std::lock_guard<std::mutex> lk (crepeMutex);
            const auto r = crepe.analyse (0.5f);
            crepeResultHz.store (r.frequencyHz, std::memory_order_relaxed);
            crepeResultConf.store (r.confidence, std::memory_order_relaxed);
            crepeResultVoiced.store (r.voiced, std::memory_order_relaxed);
        }

        // Live neural key (AutoKey): consume the latest chroma feature stashed by
        // the audio thread and infer the key off-thread. Self-throttled — the audio
        // thread refreshes the feature at most ~4x/sec, so this fires only that often.
        // Skipped when offline: the audio thread drives the key inference synchronously
        // there (runChromaAnalysis), so the two never call keyNet concurrently.
        if (keyNetPresent
            && ! offlineMode.load (std::memory_order_relaxed)
            && keyNetFeatReady.exchange (false, std::memory_order_acquire))
        {
            std::array<float, 90> feat {};
            {
                const std::lock_guard<std::mutex> lk (keyNetFeatMutex);
                feat = keyNetFeat;
            }
            const auto kn = keyNet.detectTTA (feat.data());
            if (kn.valid)
            {
                keyNetLiveRoot.store  (kn.root,          std::memory_order_relaxed);
                keyNetLiveMinor.store (kn.minor ? 1 : 0, std::memory_order_relaxed);
                keyNetLiveValid.store (true,             std::memory_order_release);
            }
        }

        // Live AutoKey via the spectrogram model (the better one). Runs once we have a
        // full ~30 s window and then at most every kAkSpecIntervalSec of AUDIO — counting
        // samples rather than iterations keeps the cadence the same however often this
        // loop happens to spin. Skipped offline for the same reason as keyNet above.
        if (keyNetSpecPresent
            && ! offlineMode.load (std::memory_order_relaxed)
            && akSpecRing != nullptr
            && (int) akSpecMono.size() >= akSpecRingSize)
        {
            const long seen = akSpecFilled.load (std::memory_order_relaxed);
            const long interval = (long) (kAkSpecIntervalSec * juce::jmax (1.0, currentSampleRate));
            if (seen >= (long) akSpecRingSize && (akSpecLastRun < 0 || seen - akSpecLastRun >= interval))
            {
                akSpecLastRun = seen;
                const int sp = akSpecWritePos.load (std::memory_order_acquire);
                for (int i = 0; i < akSpecRingSize; ++i)   // linearise oldest-first
                    akSpecMono[(size_t) i] = akSpecRing[(size_t) ((sp + i) % akSpecRingSize)].load (std::memory_order_relaxed);

                if (logSpec.compute (akSpecMono.data(), akSpecRingSize, currentSampleRate, akSpecFeat))
                {
                    const auto ks = keyNetSpec.detectTTA (akSpecFeat.data());
                    if (ks.valid)
                    {
                        akSpecRoot.store  (ks.root,          std::memory_order_relaxed);
                        akSpecMinor.store (ks.minor ? 1 : 0, std::memory_order_relaxed);
                        akSpecValid.store (true,             std::memory_order_release);
                    }
                }
            }
        }

        //  Sattari Key live estimator: drain the SAME ring incrementally. akSpecFilled
        //  is monotonic, so cursor arithmetic turns the rolling window into a stream.
        //  Unlike the model above this needs no presence check — it is plain DSP and
        //  is always compiled in. Skipped offline like every live detector here (the
        //  offline bounce path is deterministic and uses the histogram fallback).
        if (! offlineMode.load (std::memory_order_relaxed)
            && akSpecRing != nullptr && akSpecRingSize > 0 && ! songKeyDrain.empty())
        {
            if (songKeyRescan.exchange (false, std::memory_order_acq_rel))
            {
                songKeyLive.reset();
                skValid.store (false, std::memory_order_relaxed);
                skFrames.store (0, std::memory_order_relaxed);
                songKeyReadCursor = akSpecFilled.load (std::memory_order_acquire);
            }
            const long filled = akSpecFilled.load (std::memory_order_acquire);
            long available = filled - songKeyReadCursor;
            //  Keep clear of the live write position: skipping to exactly
            //  filled - ringSize would start the read on the very slots the audio
            //  thread is overwriting during the multi-millisecond drain that
            //  follows. Half a second of forfeited history buys a clean read.
            const long lapMargin = (long) juce::jmin (akSpecRingSize / 4,
                                                      (int) (currentSampleRate / 2.0));
            if (available > (long) akSpecRingSize - lapMargin)
            {
                songKeyReadCursor = filled - ((long) akSpecRingSize - lapMargin);
                available = (long) akSpecRingSize - lapMargin;
            }
            if (available > 0)
            {
                //  The ring's write position IS filled % size, so the oldest unread
                //  sample sits at cursor % size.
                const int count = (int) juce::jmin (available, (long) songKeyDrain.size());
                for (int i = 0; i < count; ++i)
                    songKeyDrain[(size_t) i] = akSpecRing[(size_t) ((songKeyReadCursor + i) % akSpecRingSize)]
                                                   .load (std::memory_order_relaxed);
                songKeyReadCursor += count;
                songKeyLive.addSamples (songKeyDrain.data(), count);
                skFrames.store (songKeyLive.framesAnalysed(), std::memory_order_relaxed);

                const auto r = songKeyLive.result();
                if (r.valid)
                {
                    skRoot.store  (r.root, std::memory_order_relaxed);
                    skMinor.store (r.minor ? 1 : 0, std::memory_order_relaxed);
                    //  The REAL margin, never a forced 1.0 — the publish gate at
                    //  kMinBusKeyConfidence depends on this being honest.
                    skConf.store  ((float) r.confidence, std::memory_order_relaxed);
                    skValid.store (true, std::memory_order_release);
                }
            }
        }

        std::this_thread::sleep_for (12ms);   // ~83 Hz refresh; plenty for pitch
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariAutoPitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("keyRoot", "Key Root", rootChoices, 9));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", legacyScaleChoices, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scaleMode", "Scale Mode", scaleChoices, 1));
    // Default to Natural. Natural, Classic and Modern Rap are all clean tuning
    // laws; only explicitly effect-named modes (Robot / Big Harmony) colour audio.
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("style", "Style", styleChoices, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("enableCorrection", "Enable Correction", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("autoKey", "AutoKey Follow", false));
    // Follow Song Key defaults OFF so the header Key & Scale menu is authoritative
    // out of the box: a user dropping Auto Pitch on a vocal picks a key and it
    // tunes to it immediately. Turning this ON is the explicit opt-in for the
    // whole-suite workflow (Sattari Key on the master drives every plugin); when
    // it's on, the editor disables + annotates the menu so the override is never
    // silent. Defaulting ON is what made "the menu not work" — the bus quietly
    // overrode the dropdowns with no visual feedback.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followSongKey", "Follow Song Key", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("amount", "Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("speed", "Speed",
        juce::NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.85f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("doNoHarm", "Do No Harm", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("formantPreserve", "Formant Preserve", true));
    // "Sound Source" replaces the vocals-only "Vocal Range" param. Adding
    // explicit Instrument bands so the same correction engine can target a
    // bass / lead / lead-high source (electric bass, monophonic synth lead,
    // brass / sax / flute, etc.) — same monophonic algorithm, just a wider
    // tracking range and formant preserve defaults to OFF for instruments.
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("vocalRange", "Sound Source",
        juce::StringArray {
            "Wide",
            "Bass Voice", "Baritone", "Tenor", "Alto / Tenor", "Alto", "Soprano",
            "Instrument: Bass", "Instrument: Mid", "Instrument: High"
        }, 4));
    // Low Latency: engage the near-zero-latency monitor path. It shares the Live
    // Engine's streaming PSOLA shifter — turning it ON swaps the Rubber Band print
    // engine for PSOLA at runtime so monitoring latency drops immediately, and the
    // reported latency (prepareToPlay) tracks the active engine. (It is effectively
    // an alias for Live Engine's low-latency path; kept as its own control because
    // the editor exposes both. See processBlock's engine select + prepareToPlay's
    // reported-latency block.)
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "lowLatency", "Legacy Low Latency (inactive)", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Live Engine (PSOLA): swap the Rubber Band phase vocoder for a streaming
    // TD-PSOLA shifter for near-zero-latency monitoring while tracking. Rubber
    // Band stays the default for the higher-fidelity printed sound.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "liveEngine", "Legacy Live Engine (inactive)", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Live Mode: the causal low-latency path (SattariLivePitchEngine). This is a
    // NEW parameter rather than a revival of `liveEngine` above, deliberately. That
    // one meant the legacy PSOLA engine, which was locked off after it failed
    // real-vocal validation; reusing the id would silently hand a different engine
    // to any old session that happened to save it on. Legacy stays inert, and the
    // new control carries the new behaviour under its own name.
    //
    // Default OFF: the print path is the qualification candidate and must not move
    // because a live path was added beside it.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "liveMode", "Live Mode (low latency)", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "liveProfile", "Live Profile",
        juce::StringArray { "Ultra (C3+)", "Extended Range" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "extremeLive", "Extreme Live (2 ms)", true));

    //  Follow Song Map: the correction target tracks the analysed song's scale
    //  regions as the transport moves, so a verse in C natural minor and a
    //  chorus in C harmonic minor are BOTH legal note sets in their own places.
    //  Default OFF — it changes what notes are legal, which is exactly the kind
    //  of thing that must be opted into rather than discovered.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "followSongMap", "Follow Song Map", false));

    //  --- HARMONY ---------------------------------------------------------------
    //  Four continuous, independently mixed vocal objects. Stable IDs are kept
    //  separate from the correction parameters so old sessions load unchanged.
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        "harmonyMode", "Harmony Mode",
        juce::StringArray { "Intelligent", "Interval", "MIDI" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyMix", "Harmony Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyLead", "Lead Level",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "harmonyVoiceOnly", "Harmony Voice Only", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyGlide", "Harmony Glide",
        juce::NormalisableRange<float> (1.5f, 180.0f, 0.5f, 0.45f), 28.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyHumanPitch", "Harmony Pitch Humanize",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 5.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyHumanTime", "Harmony Timing Humanize",
        juce::NormalisableRange<float> (0.0f, 35.0f, 0.1f), 7.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyVibratoRate", "Harmony Vibrato Rate",
        juce::NormalisableRange<float> (3.0f, 8.0f, 0.01f), 5.2f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyEnsemble", "Harmony Ensemble",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.20f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyEnsembleDepth", "Ensemble Depth",
        juce::NormalisableRange<float> (0.0f, 18.0f, 0.1f), 7.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyEnsembleRate", "Ensemble Rate",
        juce::NormalisableRange<float> (0.05f, 2.0f, 0.01f, 0.5f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyEnsembleSpread", "Ensemble Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyLowCut", "Harmony Low Cut",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.45f), 90.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "harmonyHighCut", "Harmony High Cut",
        juce::NormalisableRange<float> (3000.0f, 20000.0f, 10.0f, 0.55f), 16000.0f));
    for (int v = 1; v <= 4; ++v)
    {
        const auto id = juce::String ("h") + juce::String (v);
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            id + "Enable", "Harmony " + juce::String (v) + " Enable", false));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            id + "Role", "Harmony " + juce::String (v) + " Role",
            juce::StringArray { "Upper", "Lower", "High", "Low",
                                "Octave Up", "Octave Down", "Choir" },
            v == 1 ? 0 : v == 2 ? 1 : v == 3 ? 2 : 3));
        //  Interval mode's actual control: diatonic degrees. Without it every
        //  voice planned the identical note, making the mode indistinguishable
        //  from a single-voice transposer.
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            id + "Degrees", "Harmony " + juce::String (v) + " Interval",
            -7, 7, v == 1 ? 2 : v == 2 ? -2 : v == 3 ? 4 : -4));
        params.push_back (std::make_unique<juce::AudioParameterInt> (
            id + "Octave", "Harmony " + juce::String (v) + " Octave", -1, 1, 0));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            id + "Solo", "Harmony " + juce::String (v) + " Solo", false));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            id + "Level", "Harmony " + juce::String (v) + " Level",
            juce::NormalisableRange<float> (-60.0f, 3.0f, 0.1f), -9.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            id + "Pan", "Harmony " + juce::String (v) + " Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f),
            v == 1 ? -0.35f : v == 2 ? 0.35f : v == 3 ? -0.72f : 0.72f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            id + "Delay", "Harmony " + juce::String (v) + " Delay",
            juce::NormalisableRange<float> (0.0f, 80.0f, 0.1f), (float) (v - 1) * 4.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            id + "Detune", "Harmony " + juce::String (v) + " Detune",
            juce::NormalisableRange<float> (-30.0f, 30.0f, 0.1f),
            v == 1 ? -3.0f : v == 2 ? 3.0f : v == 3 ? -6.0f : 6.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            id + "Vibrato", "Harmony " + juce::String (v) + " Vibrato",
            juce::NormalisableRange<float> (0.0f, 80.0f, 0.1f), 0.0f));
    }

    // Neural Pitch (CREPE): opt-in for difficult breathy/noisy sources. The tiny
    // CNN costs roughly a quarter of one performance core at an 83 Hz cadence on
    // the reference Mac, so a new session stays on efficient YIN until requested.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("neuralPitch", "Neural Pitch", false));

    // Transpose to Key (whole-mix): instead of per-note correction, shift the ENTIRE
    // signal by a fixed number of semitones via Rubber Band (formant-aware) so a whole
    // beat / instrumental / sample lands in a target key. xposeSemis is the shift; the
    // editor's "Match Key" button sets it from a completed original-source scan
    // to the suite/manual target. This moves everything, NOT individual notes.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("xposeMode", "Transpose to Key", false));
    params.push_back (std::make_unique<juce::AudioParameterInt>  ("xposeSemis", "Transpose", -12, 12, 0));

    // Preserve Vibrato: 0 = full snap (kills vibrato under hard tune), 1 = keep
    // the singer's fast pitch oscillation while still correcting the note centre.
    // Default OFF: on real input the pitch tracker jitters frame-to-frame, and
    // re-injecting that jitter (not just musical vibrato) muddies the corrected
    // tone. Opt in when you want expressive vibrato preserved on sustained notes.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("vibrato", "Preserve Vibrato",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

    // Synthetic vibrato (ADD, not preserve). Separate from "Preserve Vibrato"
    // above: this GENERATES a pitch LFO on the corrected note, like a singer's
    // natural vibrato, fading in over the onset time from each new note. Default
    // depth 0 => strictly inert (the golden null render is bit-identical).
    //   addVibrato   : depth in cents (0..100), 0 = off
    //   vibratoRate  : LFO rate in Hz (0.1..12)
    //   vibratoOnset : fade-in time in ms (0..2000), measured from each note onset
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("addVibrato", "Add Vibrato",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("vibratoRate", "Vibrato Rate",
        juce::NormalisableRange<float> (0.1f, 12.0f, 0.01f), 5.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("vibratoOnset", "Vibrato Onset",
        juce::NormalisableRange<float> (0.0f, 2000.0f, 1.0f), 300.0f));

    // Reference tuning parity: manual +/-100 cents like a traditional pro tuner,
    // plus an opt-in path that consumes the referenceHz published by Sattari Key.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("referenceDetune", "Reference Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followReference", "Follow Song Reference", false));

    // When armed, held MIDI notes override scale snapping and become exact note
    // targets. This remains independent of the existing MIDI Learn layer.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("midiTarget", "Target Notes from MIDI", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("capturePitchEdits", "Capture Pitch Edits", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("usePitchEdits", "Use Pitch Edits", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("graphTimeCorrection", "Graph Time Correction", false));

    // --- Antares-class behavioural controls ----------------------------------
    // These model the *feel* of Auto-Tune Pro, not its exact code. Each is real,
    // audible DSP, documented honestly:

    // Classic Mode: the hard "Auto-Tune 5 / T-Pain" character. When ON, correction
    // is always-on at full strength, retune is near-instant, and Flex/Do-No-Harm
    // expressive gating is bypassed. This is the deliberate robotic snap, not a
    // transparent correction. (We don't ship Antares' actual A5 resampler; this
    // reproduces its behavioural signature: zero-transition hard pull.)
    params.push_back (std::make_unique<juce::AudioParameterBool> ("classicMode", "Classic Mode", false));

    // Flex-Tune: correction only engages as the voice clearly heads toward a
    // target. Inside a "flex zone" around each note centre, expressive pitch
    // (scoops, vibrato, slides) passes through untouched; only clearly-off pitch
    // gets pulled. 0 = always correct (rigid). 1 = very permissive. This is our
    // analogue of Antares Flex-Tune / Graillon's "smooth".
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("flexTune", "Flex-Tune",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));

    // Transition: note-onset retune modeling. Right after a note change the
    // engine retunes fast to catch the new note; while sustaining it relaxes so
    // vibrato and natural drift survive. This is the single biggest contributor
    // to "natural vs robotic" feel. 0 = uniform speed (old behaviour). 1 = strong
    // onset snap + slow sustain.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("transition", "Transition",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

    // Throat: vocal-tract-length modeling. Independent of pitch, this scales the
    // formant envelope to make the voice sound like it comes from a longer
    // (darker, bigger) or shorter (brighter, smaller) throat — Antares'
    // "Throat Length" analogue, driven through Rubber Band's formant scale.
    // 1.0 = natural. <1 = shorter/brighter, >1 = longer/darker.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("throat", "Throat",
        juce::NormalisableRange<float> (0.5f, 2.0f, 0.01f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("useKeyMap", "Use Key Map", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("humanize", "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    // 12-bit mask of enabled pitch classes (bit 0 = C, bit 11 = B). Default
    // 4095 = all 12 notes valid. The Edit Notes pill row clicks toggle bits
    // (shift-click sets root, plain click blocks/unblocks the note). When a
    // note is blocked, correction snaps to the next nearest enabled note.
    params.push_back (std::make_unique<juce::AudioParameterInt> ("enabledNotes", "Enabled Notes",
        0, 4095, 4095));
    // Polyphonic: retunes EACH note of a chord/pad independently to its nearest scale
    // tone via a per-peak phase-locked (Laroche-Dolson) vocoder — not one global ratio.
    // Benchmark (AutoPitchBench poly): 96-98% per-note tuning on separated chords up to
    // 4 notes, harmonic-rich, opposing detunes. Known limit: it does NOT split partials
    // that two notes SHARE (an exact unison/octave collides — the octave case scores
    // ~46%); true source separation (Melodyne-class) is a future build.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("polyphonic", "Polyphonic", false));
    // Poly Harmony: in Polyphonic mode, add an in-key harmony voice a diatonic third above
    // every detected note, phase-coherent via a dedicated accumulator so it never perturbs
    // the corrected voice. 0 = off (the harmony pass is skipped entirely). Best-effort on
    // dense material — it inherits the "shared partials" limit of the poly engine above.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("polyHarmony", "Poly Harmony",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    // Poly HF Focus: on a full instrumental the high end drifts out of key because high
    // "orphan" peaks — ones not grouped as a harmonic of a lower note — get snapped to a
    // scale tone on their own, but those are usually masked partials or non-tonal content
    // (hats/air), not real fundamentals. This rolls off their correction above ~1.3 kHz so
    // they aren't mis-tuned. 1 = full protection (default), 0 = legacy (snap everything).
    // Harmonics grouped under a low root are unaffected — they ride the root's ratio.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("polyHFGate", "Poly HF Focus",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    // Tonal Focus: on a full instrumental, tune only the TONAL (harmonic) content and leave
    // the percussive/noise content (hats, air, transients) exactly where it is. A causal
    // HPSS mask scores each spectral bin harmonic-vs-percussive; as this rises, bins that
    // read predominantly percussive are routed to the unshifted path instead of being
    // pitch-corrected. 0 = tune everything (legacy, default — preserves the poly benchmark);
    // 1 = fully protect noise/transients. This is the real high-end-in-key lever (T2).
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tonalFocus", "Tonal Focus",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    // High Tracking: how far up the spectrum the poly engine follows a corrected note's
    // harmonics. Legacy stops at 2.5 kHz, so above that a note's overtones stay frozen at
    // their ORIGINAL pitch after the fundamental is retuned — the audible "high end out of
    // key" on an instrumental. Raising this lets those tonal overtones ride the correction.
    // 0 = 2.5 kHz (legacy, default); 1 = 7 kHz. Pair with Tonal Focus so only tonal high
    // content follows and hats/air stay put.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("hiTrack", "High Tracking",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("formantShift", "Formant Shift",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("inputGain", "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("outputGain", "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("traceConfidence", "Trace Confidence",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

void SattariAutoPitchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (samplesPerBlock, 512);
    sessionReader.start();   // Follow Song Key reads the bus published by Sattari Key
    {
        const juce::ScopedLock l (pitchEditLock);
        pitchEdits.reserve (4096);
    }
    resetPitchState (preparedBlockSize);
    {
        const auto offsetText = juce::SystemStats::getEnvironmentVariable (
            "SATTARI_AUTOPITCH_CLASSIC_CONTROL_MS", "-20");
        //  getDoubleValue() returns 0.0 for anything unparseable, which would have
        //  silently DISABLED the lead compensation rather than falling back to it -
        //  a typo in the override would look like a working build with the feature
        //  quietly off. Require a genuine number; an explicit "0" still disables.
        const auto trimmedOffset = offsetText.trim();
        const bool parseable = trimmedOffset.containsOnly ("0123456789.eE+-")
                            && trimmedOffset.isNotEmpty()
                            && trimmedOffset.containsAnyOf ("0123456789");
        const double offsetMs = parseable ? trimmedOffset.getDoubleValue() : -20.0;
        classicControlOffsetSeconds = 0.001 * juce::jlimit (-60.0, 60.0, offsetMs);
    }
    graphTimeRingSize = juce::jmax (preparedBlockSize * 2, (int) std::ceil (currentSampleRate * 1.05) + preparedBlockSize);
    graphTimeRing.setSize (juce::jmax (1, getTotalNumOutputChannels()), graphTimeRingSize);
    graphTimeRing.clear();
    graphTimeWrite = 0;
    graphTimeDelaySamples = 0.5 * currentSampleRate;
    graphTimeWasActive = false;
#if JucePlugin_Enable_ARA
    prepareToPlayForARA (currentSampleRate, preparedBlockSize,
                         juce::jmax (1, getTotalNumOutputChannels()), getProcessingPrecision());
#endif

    // Preserved legacy live-monitor PSOLA engine, one shifter per output channel.
    // The public Live/Low-Latency controls are inactive in RC3, but old sessions
    // still deserialize safely. Prepare the dormant path with a
    // min-Hz that matches the SELECTED Sound Source's low bound so it stays
    // pitch-synchronous down to that source's floor (Instrument: Bass -> ~40 Hz)
    // instead of the old fixed 70 Hz, which mis-tracked bass fundamentals. PSOLA's
    // fixed latency is ~2 periods of min-Hz, so a lower floor buys correct bass
    // tracking at the cost of more latency (~50 ms at 40 Hz vs ~28 ms at 70 Hz).
    // The floor is clamped to [40,70] Hz to keep the ring + latency bounded, so a
    // voice source is unchanged (70 Hz). This is sampled at prepare; a runtime
    // Sound Source change is picked up on the next prepareToPlay (re-preparing the
    // shifter allocates, so it can't be done from the audio thread).
    {
        const bool lowLatOn = apvts.getRawParameterValue ("lowLatency") != nullptr
                           && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
        preparePsolaInto (psola, lowLatOn);
        psolaLiveLowLat.store (lowLatOn, std::memory_order_relaxed);
        psolaSwapReady.store (false, std::memory_order_relaxed);
    }

    //  Live Mode engines, one per output channel. Prepared unconditionally so that
    //  toggling Live Mode never allocates on the audio thread; an unused engine
    //  costs memory and nothing else.
    //
    //  The tracker range is the Wide vocal span. Unlike the legacy PSOLA path there
    //  is no min-Hz floor to raise for latency, because the epoch shifter locks its
    //  analysis marks to the waveform rather than to a fixed grid.
    const auto liveChannels = (size_t) juce::jmax (1, getTotalNumOutputChannels());
    liveEngines.resize (liveChannels);
    for (auto& e : liveEngines)
    {
        // 128 Hz keeps the 1.5-period waveform-locked shifter below the strict
        // 12 ms gate at 48 kHz: ceil (1.5 * 48000 / 128) + 8 = 571 samples.
        e.prepare (sampleRate, samplesPerBlock, 128.0, 1100.0);
        e.reset();
    }
    extremeLiveEngines.resize (liveChannels);
    for (auto& e : extremeLiveEngines)
    {
        e.prepare (sampleRate, samplesPerBlock, 70.0, 1100.0);
        e.reset();
    }
    liveExtendedEngines.resize (liveChannels);
    for (auto& e : liveExtendedEngines)
    {
        e.prepare (sampleRate, samplesPerBlock, 70.0, 1100.0);
        e.reset();
    }
    // Harmony uses the same waveform-locked latency classes as the live lead.
    // The Range bank also serves Studio; its shorter voice delay is compensated
    // inside renderHarmonyInto against the actual print-engine delay.
    const int harmonyAlignmentCapacity = (int) std::ceil (currentSampleRate * 0.35)
                                       + preparedBlockSize + 64;
    harmonyUltraRenderer.prepare (currentSampleRate, preparedBlockSize,
                                  128.0, 1100.0, harmonyAlignmentCapacity);
    harmonyRangeRenderer.prepare (currentSampleRate, preparedBlockSize,
                                  70.0, 1100.0, harmonyAlignmentCapacity);
    harmonyExtremeRenderer.prepare (currentSampleRate, preparedBlockSize,
                                    70.0, 1100.0, harmonyAlignmentCapacity, true);
    harmonyRenderPlanner.reset();
    harmonyWasActive = false;
    harmonyRendererBank = -1;
    lastLiveModeActive = liveModeActive();
    lastLiveProfile = extremeLiveProfileActive() ? 0 : ultraLiveProfileActive() ? 1 : 2;

    // CREPE model: every ONNX-enabled shipping build uses the same provenance-
    // locked tiny model embedded in its binary. Developers can explicitly point
    // SATTARI_AUTOPITCH_CREPE_MODEL at another contract-compatible model; an old
    // file in Application Support must never silently replace the release model.
    //
    // Stop + JOIN the inference worker BEFORE touching the tracker: crepe.prepare()
    // and crepe.loadModel() reconfigure model/buffer state that the worker's
    // analyse() reads, so a worker still running from a previous prepareToPlay would
    // race the reload (torn reads of the model being swapped, use of half-resized
    // buffers). Joining first guarantees no analyse() overlaps the reconfigure; the
    // worker is then restarted below only if a model is present.
    crepeThreadRun.store (false);
    if (crepeThread.joinable()) crepeThread.join();

    crepe.prepare (currentSampleRate);
    crepeModelPresent = false;
    {
        const auto overridePath = juce::SystemStats::getEnvironmentVariable ("SATTARI_AUTOPITCH_CREPE_MODEL", {});
        const juce::File overrideFile (overridePath);
        if (overridePath.isNotEmpty() && overrideFile.existsAsFile())
            crepeModelPresent = crepe.loadModel (overrideFile.getFullPathName().toStdString());
       #if SATTARI_HAS_ONNX
        if (! crepeModelPresent)
            crepeModelPresent = crepe.loadModel (SattariAutoPitchModelData::crepe_onnx,
                                                 (std::size_t) SattariAutoPitchModelData::crepe_onnxSize);
       #endif
    }
    // Release key-quality lock. The optional key models remain load-compatible code,
    // but are not activated: on the only genuinely held-out evaluation the Aarden
    // chroma path scored 85.4% exact versus 37.5% for keynet_spec, and the model's
    // confidence was not calibrated. Auto Pitch and Sattari Key therefore consume
    // the same shared, deterministic KeyAnalysisEngine configuration until a model
    // wins on held-out pop/rock/electronic material.
    keyNetPresent = false;
    keyNetSpecPresent = false;

    const int scratchChannels = juce::jmax (2, getTotalNumOutputChannels());
    formantScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    harmonyScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    harmonyWetScratch.setSize (2, preparedBlockSize, false, false, true);
    formantScratch.clear();
    harmonyScratch.clear();
    harmonyWetScratch.clear();

    // Chroma FFT setup (allocation-free after this). Hann-windowed magnitude
    // spectrum is mapped to 12 pitch classes for polyphonic AutoKey on the SC.
    chromaFft = std::make_unique<juce::dsp::FFT> (chromaFftOrder);
    // Warm the whitening-envelope buffer off the audio thread so the first AutoKey
    // chroma window never allocates in accumulate().
    autoKeyHpcp.reserveBins (chromaFftSize / 2 + 1);
    chromaRing.assign ((size_t) chromaFftSize, 0.0f);
    chromaFftScratch.assign ((size_t) (chromaFftSize * 2), 0.0f);
    chromaHannWindow.assign ((size_t) chromaFftSize, 0.0f);
    for (int i = 0; i < chromaFftSize; ++i)
        chromaHannWindow[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                              * (float) i / (float) (chromaFftSize - 1));
    chromaWritePos = 0;
    chromaSamplesSinceFft = 0;
    sidechainSeenAudio = false;
    sidechainHoldSamples = 0;
    scMonoScratch.assign ((size_t) preparedBlockSize, 0.0f);

    // Reset the live neural-key accumulators so a previous session's key can't linger.
    knWeight = 0.0;
    for (int i = 0; i < 12; ++i) { knMean[i] = 0.0; for (int j = 0; j < 12; ++j) knOuter[i][j] = 0.0; }
    keyNetFeatReady.store (false, std::memory_order_relaxed);

    // Long ring + scratch for the live spectrogram AutoKey (allocated once per prepare).
    akSpecRingSize = juce::jmax (1, (int) (currentSampleRate * kAkSpecSeconds));
    akSpecRing = std::make_unique<std::atomic<float>[]> ((size_t) akSpecRingSize);
    for (int i = 0; i < akSpecRingSize; ++i)
        akSpecRing[(size_t) i].store (0.0f, std::memory_order_relaxed);
    akSpecWritePos.store (0);
    akSpecFilled.store (0);
    akSpecMono.assign ((size_t) akSpecRingSize, 0.0f);
    //  Sattari Key live path: open-ended stream (never finalise), reset with the
    //  ring. The drain scratch is the ring's own size — the largest one drain can be.
    songKeyLive.prepare (currentSampleRate, 0);
    songKeyLive.reset();
    songKeyReadCursor = 0;
    songKeyDrain.assign ((size_t) akSpecRingSize, 0.0f);
    skValid.store (false, std::memory_order_relaxed);
    skFrames.store (0, std::memory_order_relaxed);
    akSpecFeat.assign ((size_t) (sattari::dsp::LogSpec::kBins
                               * sattari::dsp::LogSpec::kFrames), 0.0f);
    akSpecLastRun = -1;

    // (Re)start the worker LAST, after every buffer it reads is rebuilt. The first
    // version restarted it right after the model load, 30 lines above the akSpec
    // ring and estimator re-init — so on any SECOND prepare (a sample-rate or
    // buffer-size change), the fresh worker could drain last session's leftover
    // cursor state while this thread was freeing and reallocating the very ring it
    // was reading: a use-after-free found in adversarial review, not in testing.
    //
    // Started unconditionally now, not only when an ONNX model is present: the
    // SongKeyEstimator drain is plain DSP and must not silently die with the model
    // — this repo has already documented rebuilds dropping onnx self-containment.
    // An idle worker costs one 12 ms poll.
    crepeThreadRun.store (true);
    crepeThread = std::thread ([this] { crepeWorkerLoop(); });
    akSpecValid.store (false);
    keyNetLiveValid.store (false, std::memory_order_relaxed);

    // Real polyphonic phase-vocoder: preallocate all STFT buffers so the audio
    // thread never allocates when Polyphonic is engaged. Sized for the output
    // channel count (matches the mono engines' channel handling).
    preparePolyVocoder (juce::jmax (1, getTotalNumOutputChannels()));
    lastPolyActive = (apvts.getRawParameterValue ("polyphonic") != nullptr
                      && apvts.getRawParameterValue ("polyphonic")->load() > 0.5f)
                  && (apvts.getRawParameterValue ("enableCorrection") == nullptr
                      || apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f);

#if SATTARI_AUTOPITCH_HAS_ELASTIQUE
    // --- zplane elastique v3 init -------------------------------------------
    // TODO: replace stub with real API once SDK is in third_party/elastique/.
    //
    // Intended init (kV3mobile = lowest latency, tonal/vocal optimised):
    //
    //   if (elastique)
    //       CElastiqueV3If::DestroyInstance (static_cast<CElastiqueV3If*> (elastique));
    //   elastique = nullptr;
    //   const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    //   CElastiqueV3If* eq = nullptr;
    //   CElastiqueV3If::CreateInstance (eq, numCh, (float) currentSampleRate,
    //                                   CElastiqueV3If::kV3mobile);
    //   elastique = eq;
    //   elastiqueOutput.setSize (numCh, preparedBlockSize);
    //   elastiqueScratchIn.assign  (numCh, std::vector<float> (preparedBlockSize, 0.f));
    //   elastiqueScratchOut.assign (numCh, std::vector<float> (preparedBlockSize, 0.f));
    //
    //   // elastique reports its own startup latency:
    //   reportedLatencySamples = eq ? eq->GetFramesNeeded (preparedBlockSize) : 0;
    //   setLatencySamples (reportedLatencySamples);

    //  The stub is never dispatched (processBlock's print chain deliberately skips
    //  it - see the MEASUREMENT WARNING there), so it must not publish a latency
    //  either. The authoritative figure is computed once at the end of prepare from
    //  the engine that actually runs; writing 0 here only ever shadowed it.
    (void) preparedBlockSize;   // suppress unused until stub is filled in

#endif

#if SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    {
        const int numCh = juce::jmax (1, getTotalNumOutputChannels());
        const bool lowLat = apvts.getRawParameterValue ("lowLatency") != nullptr
                            && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;

        // Live mode: small window for low latency (~23ms total at 44100Hz).
        // Standard mode: larger window for higher quality studio correction.
        if (lowLat)
            ssStretch.configure (numCh, 1024, 256);
        else
            ssStretch.configure (numCh, 4096, 1024);

        ssStretch.reset();
        ssOutput.setSize (numCh, preparedBlockSize);
        ssInputPtrs.resize  ((size_t) numCh);
        ssOutputPtrs.resize ((size_t) numCh);

        //  Record it for activeEngineLatencySamples(); do NOT publish here. The
        //  end-of-prepare recompute (and handleAsyncUpdate on every engine-parameter
        //  change) is the single authority, and it previously overwrote this correct
        //  figure with a RubberBand-only calculation that returned 0 in a signalsmith
        //  build - the host was told 0 while the wet path ran ~4096 samples late.
        signalsmithLatencySamples = (int) (ssStretch.inputLatency() + ssStretch.outputLatency());
    }

#endif

//  RubberBand initialises WHENEVER it is compiled in, never as the `#elif` tail of
//  an engine chain. Transpose-to-Key dispatches processWithRubberBand
//  unconditionally under its own `#if SATTARI_AUTOPITCH_HAS_RUBBERBAND` - and that
//  macro is set by CMake independently of the signalsmith one - so making this init
//  exclusive left the stretcher vector empty in a signalsmith build. The xpose path
//  then hit processWithRubberBand's empty-vector guard and returned dry: whole-mix
//  transpose was a silent no-op, with a first-block heap allocation on the audio
//  thread because rubberBandOutput was never sized either.
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    rubberBand.clear();
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());

    // Rubber Band is the high-fidelity PRINT engine and always uses the
    // high-quality window. Low latency is no longer bought by degrading Rubber
    // Band's window (a prepare-only switch with no runtime effect); it is a
    // runtime engine swap to the streaming PSOLA shifter — see the Low Latency /
    // Live Engine handling in processBlock and the reported-latency block below.
    auto opts = RubberBand::RubberBandStretcher::OptionProcessRealTime
              // R3/Finer is Rubber Band's highest-quality engine and is explicitly
              // recommended for vocals, soft onsets and smooth pitch changes. The
              // old path set quality/consistency flags but silently stayed on the
              // default R2 engine, leaving formant-scale control ineffective.
              | RubberBand::RubberBandStretcher::OptionEngineFiner
              // Sustained, pitched vocal material: TransientsSmooth avoids the
              // phase-reset Rubber Band performs at each detected "transient",
              // which on a held note fires spuriously and punches an amplitude
              // dropout / click into the tune. Smooth keeps the phase continuous.
              | RubberBand::RubberBandStretcher::OptionTransientsSmooth
              | RubberBand::RubberBandStretcher::OptionDetectorSoft
              // Auto Pitch changes the pitch scale every block; without this,
              // Rubber Band emits a phase discontinuity (click) at each fast
              // pitch move (e.g. hard note transitions). HighConsistency uses a
              // method built for dynamic pitch changes with no discontinuities.
              | RubberBand::RubberBandStretcher::OptionPitchHighConsistency
              | RubberBand::RubberBandStretcher::OptionPitchHighQuality
              | RubberBand::RubberBandStretcher::OptionWindowShort;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto stretcher = std::make_unique<RubberBand::RubberBandStretcher> (
            currentSampleRate, 1, opts, 1.0, 1.0);
        jassert (stretcher->getEngineVersion() >= 3);
        // Stationary ratios use chunks up to 512 samples; moving ratios use the
        // finer automation grid below. This cap is independent of host buffers.
        stretcher->setMaxProcessSize (512);
        stretcher->setPitchScale (1.0);
        rubberBand.push_back (std::move (stretcher));
    }
    // rubberBandOutput doubles as the per-iteration drain chunk buffer, so give
    // it headroom for one full block of Rubber Band output at a time.
    rubberBandOutput.setSize (numChannels, preparedBlockSize);
    rubberBandScratch.resize ((size_t) preparedBlockSize, 0.0f);

    // --- Output FIFO ---------------------------------------------------------
    // We keep a one-block cushion in the ring so Rubber Band's per-call
    // burstiness never starves the output (which used to be papered over by
    // splicing dry signal onto the block tail -> crackle). Capacity covers the
    // cushion, a full host block, the start delay and generous slack.
    // Cushion has to absorb Rubber Band's transient burstiness during pitch-scale
    // automation (at a hard note transition it momentarily delivers fewer than N
    // samples). Sizing the cushion to RB's own start delay gives ample headroom
    // so the pop path never starves — starvation would zero-pad the block tail
    // and click. A one-block floor covers the trivial (unity) case.
    const int startDelay = rubberBand.empty() ? 0 : (int) rubberBand.front()->getStartDelay();
    rbLatency = startDelay;
    // Fixed latency floor: a host's declared buffer size must not change the
    // engine's delay or the amount of automation queued ahead of playback.
    rbCushion = juce::jmax (512, startDelay);
    rbFifoCap = rbCushion + startDelay + 8 * preparedBlockSize + 16384;
    rbFifo.setSize (numChannels, rbFifoCap);
    resetRbFifo();
#endif

    // Always run a sample-identical dry reference at the exact monophonic engine
    // latency. Correction Off and a genuine zero-amount Natural setting can select
    // it without asking Rubber Band to reconstruct a unity-ratio signal. Allocate
    // enough for an oversized host block while keeping every audio-thread operation
    // bounded and allocation-free.
    //  The delayed dry reference is what Correction Off and Amount 0 return, so it
    //  has to sit at whichever engine's delay is being REPORTED. Pinning it to the
    //  Rubber Band figure would make the bypass 60 ms late in Live Mode — the exact
    //  class of latency-report mismatch that failed Gate 4 test A at 0.4.1.
    dryDelaySamples = liveModeActive() ? liveEngineLatencySamples()
                                       : printEngineLatencySamples();
    //  Size for the LARGEST delay either engine can ask for, not the current one.
    //  Live Mode can be toggled while the transport rolls, and that must move a read
    //  pointer rather than resize a buffer on the audio thread.
    const int maximumLiveLatency = juce::jmax (
        extremeLiveEngines.empty() ? 0 : extremeLiveEngines.front().getLatencySamples(),
        juce::jmax (liveEngines.empty() ? 0 : liveEngines.front().getLatencySamples(),
                    liveExtendedEngines.empty() ? 0
                                                : liveExtendedEngines.front().getLatencySamples()));
    dryDelayRingSize = juce::jmax (juce::jmax (juce::jmax (monoEngineLatencySamples(),
                                                           printEngineLatencySamples()),
                                               maximumLiveLatency)
                                       + preparedBlockSize + 1,
                                   preparedBlockSize * 2 + 1);
    dryDelayRing.setSize (juce::jmax (1, getTotalNumOutputChannels()), dryDelayRingSize,
                          false, false, true);
    dryDelayOutput.setSize (juce::jmax (1, getTotalNumOutputChannels()), preparedBlockSize,
                            false, false, true);
    dryDelayRing.clear();
    dryDelayOutput.clear();
    dryDelayWrite = 0;

    // Report latency so the host delay-compensates. The figure tracks whichever
    // engine is active (activeEngineLatencySamples()): the streaming PSOLA path
    // (Low Latency / Live Engine) has a small fixed delay; the Rubber Band print
    // path adds its pipeline delay plus the FIFO cushion. This makes Low Latency an
    // HONEST control instead of a dead toggle. The engine can also be swapped live,
    // in which case parameterChanged() -> handleAsyncUpdate() re-reports the new
    // latency on the message thread (setLatencySamples must not run on the audio
    // thread). Computed here AFTER the PSOLA (min-Hz) and Rubber Band FIFO setup, so
    // it reflects the actual prepared engines.
    reportedLatencySamples = activeEngineLatencySamples();
    setLatencySamples (reportedLatencySamples);

    // Seed the engine-swap detector with the engine that will run first, so the
    // first processBlock after prepare doesn't spuriously flush both paths (they
    // are already freshly primed here).
    lastUseLiveEngine = false;
    // Seed the transpose seam too (siblings above are seeded), so a project restored
    // with Transpose already ON doesn't fire a spurious engine flush on the first block.
    lastXposeActive = (apvts.getRawParameterValue ("xposeMode") != nullptr
                       && apvts.getRawParameterValue ("xposeMode")->load() > 0.5f);
}

// Active engine's reported latency (samples). PSOLA (Low Latency / Live Engine)
// reports its small fixed delay; otherwise the Rubber Band pipeline delay plus the
// FIFO cushion. Const + allocation-free so it is safe to call from prepareToPlay
// and from handleAsyncUpdate() (message thread).
// Min-Hz floor for the live PSOLA shifter. Low-latency vocal mode raises it (PSOLA
// latency is ~2 periods of the floor), trading tracking of very low fundamentals for
// a much shorter delay — ideal for live vocal monitoring.
double SattariAutoPitchAudioProcessor::psolaFloorHz (bool lowLatency) const
{
    double srcMinHz = minVoiceHz, srcMaxHz = maxVoiceHz;
    voicedRangeHzForSource (srcMinHz, srcMaxHz);
    return lowLatency ? juce::jlimit (120.0, 260.0, juce::jmax (srcMinHz, 130.0))
                      : juce::jlimit (40.0, 70.0, srcMinHz);
}

void SattariAutoPitchAudioProcessor::preparePsolaInto (std::vector<sattari::dsp::PsolaShifter>& dst, bool lowLatency)
{
    double srcMinHz = minVoiceHz, srcMaxHz = maxVoiceHz;
    voicedRangeHzForSource (srcMinHz, srcMaxHz);
    const double psolaMinHz = psolaFloorHz (lowLatency);
    const double psolaMaxHz = juce::jmax (1000.0, srcMaxHz);   // voice stays 1000 Hz; instruments extend up
    const int numCh = juce::jmax (1, getTotalNumOutputChannels());
    dst.assign ((size_t) numCh, sattari::dsp::PsolaShifter());
    for (auto& p : dst) p.prepare (currentSampleRate, psolaMinHz, psolaMaxHz);
}

// Analytic PSOLA latency (matches PsolaShifter::prepare): 2 max-periods + 1. Lets the
// host be told the new latency the instant Low Latency toggles, before the audio-thread
// shifter swap actually lands.
int SattariAutoPitchAudioProcessor::psolaLatencyFor (bool lowLatency) const
{
    const double sr = juce::jmax (1.0, currentSampleRate);
    const double maxPeriod = sr / juce::jmax (1.0, psolaFloorHz (lowLatency));
    return (int) std::ceil (maxPeriod * 2.0) + 1;
}

int SattariAutoPitchAudioProcessor::activeEngineLatencySamples() const
{
    const bool polyOn    = apvts.getRawParameterValue ("polyphonic") != nullptr
                           && apvts.getRawParameterValue ("polyphonic")->load() > 0.5f;
    const bool corrOn    = apvts.getRawParameterValue ("enableCorrection") == nullptr
                           || apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f;
    const bool xposeOn   = apvts.getRawParameterValue ("xposeMode") != nullptr
                           && apvts.getRawParameterValue ("xposeMode")->load() > 0.5f;
    const int rbLat = monoEngineLatencySamples();
    //  The PRINT engine is not always Rubber Band: processBlock's final branch is
    //  `#if HAS_SIGNALSMITH -> signalsmith #elif HAS_RUBBERBAND -> rubberband`, and
    //  in a signalsmith build every RubberBand-derived figure here is 0 (rbLatency
    //  and rbCushion are only written by the RubberBand prepare block). Reporting
    //  rbLat for that branch told the host 0 while the audio ran a full signalsmith
    //  pipeline late.
    const int printLat =
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
        signalsmithLatencySamples;
#else
        rbLat;
#endif
    // Mirror processBlock's engine precedence EXACTLY, or the host is delay-compensated
    // for the wrong engine:
    //   Transpose-to-Key always runs Rubber Band, whatever else is on;
    //   then Polyphonic (phase vocoder); otherwise the print engine above.
    int engineLatency = printLat;
    if (xposeOn)                 engineLatency = rbLat;
    else if (polyOn && corrOn)   engineLatency = pvInFifoLatency;
    else if (liveModeActive())   engineLatency = liveEngineLatencySamples();
    const bool timeOn = apvts.getRawParameterValue ("graphTimeCorrection") != nullptr
                     && apvts.getRawParameterValue ("graphTimeCorrection")->load() > 0.5f;
    return engineLatency + (timeOn ? (int) std::round (0.5 * currentSampleRate) : 0);
}

bool SattariAutoPitchAudioProcessor::liveModeActive() const noexcept
{
    //  Live Mode yields to the two engines that are not interchangeable with it:
    //  Transpose-to-Key is a whole-mix Rubber Band shift, and Polyphonic is the
    //  phase vocoder. Both do things the mono live path cannot, so neither is
    //  silently downgraded by enabling Live Mode.
    auto on = [this] (const char* id)
    {
        auto* v = apvts.getRawParameterValue (id);
        return v != nullptr && v->load() > 0.5f;
    };
    if (on ("xposeMode")) return false;
    if (on ("polyphonic") && (apvts.getRawParameterValue ("enableCorrection") == nullptr
                              || apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f))
        return false;
    return on ("liveMode");
}

bool SattariAutoPitchAudioProcessor::ultraLiveProfileActive() const noexcept
{
    const auto* profile = apvts.getRawParameterValue ("liveProfile");
    return profile == nullptr || profile->load() < 0.5f;
}

bool SattariAutoPitchAudioProcessor::extremeLiveProfileActive() const noexcept
{
    const auto* extreme = apvts.getRawParameterValue ("extremeLive");
    return ultraLiveProfileActive() && (extreme == nullptr || extreme->load() > 0.5f);
}

std::vector<sattari::dsp::LivePitchEngine>&
SattariAutoPitchAudioProcessor::activeLiveEngines() noexcept
{
    return ultraLiveProfileActive() ? liveEngines : liveExtendedEngines;
}

const std::vector<sattari::dsp::LivePitchEngine>&
SattariAutoPitchAudioProcessor::activeLiveEngines() const noexcept
{
    return ultraLiveProfileActive() ? liveEngines : liveExtendedEngines;
}

int SattariAutoPitchAudioProcessor::liveEngineLatencySamples() const noexcept
{
    if (extremeLiveProfileActive())
        return extremeLiveEngines.empty() ? 0 : extremeLiveEngines.front().getLatencySamples();
    const auto& engines = activeLiveEngines();
    return engines.empty() ? 0 : engines.front().getLatencySamples();
}

//  Delay imposed by whichever engine the print chain actually dispatches to.
//  The dry reference that Correction Off / Amount 0 returns has to sit at THIS
//  delay: pinning it to the RubberBand figure in a signalsmith build left the
//  bypass undelayed against a wet path a full pipeline late, so toggling
//  correction jumped the vocal in time - the same class of mismatch that failed
//  Gate 4 test A at 0.4.1.
int SattariAutoPitchAudioProcessor::printEngineLatencySamples() const noexcept
{
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    return signalsmithLatencySamples;
#else
    return monoEngineLatencySamples();
#endif
}

int SattariAutoPitchAudioProcessor::monoEngineLatencySamples() const noexcept
{
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    const int reconstructionGroupDelay =
        (int) std::ceil ((double) rbLatency * rubberBandR3GroupDelayFraction);
    return rbLatency + rbCushion + reconstructionGroupDelay;
#else
    return 0;   // no Rubber Band: the formant-guide fallback is block-local
#endif
}

void SattariAutoPitchAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    // Preserve automation/session compatibility for the original 3-choice Scale
    // parameter. New UI selections write scaleMode immediately afterwards, while
    // old hosts/presets that only know Scale still select the expected base mode.
    //
    //  parameterChanged can be called from the AUDIO thread (host automation), and
    //  setValueNotifyingHost sends a host notification and re-enters this listener -
    //  neither is legal or RT-safe there. Record the wanted value and perform the
    //  mirror on the message thread in handleAsyncUpdate(), the same deferral the
    //  latency re-report below already relies on.
    if (parameterID == "scale")
    {
        const int wantScaleMode = juce::jlimit (0, 2, juce::roundToInt (newValue));
        //  Deferring UNCONDITIONALLY would break every context that has no message
        //  loop pumping - an offline/headless render sets `scale` and would never
        //  see scaleMode mirrored, silently correcting to the wrong scale. Mirror
        //  inline when we are already on the message thread (the UI and host-load
        //  cases), and defer only when we might be on the audio thread.
        if (juce::MessageManager::existsAndIsCurrentThread())
        {
            if (auto* extended = apvts.getParameter ("scaleMode"))
                extended->setValueNotifyingHost (extended->convertTo0to1 ((float) wantScaleMode));
        }
        else
        {
            pendingScaleMirror.store (wantScaleMode, std::memory_order_release);
            triggerAsyncUpdate();
        }
    }

    // Engine toggle: the reported latency must change to match the now-active
    // engine. setLatencySamples() -> updateHostDisplay() must run on the message
    // thread, and parameterChanged may be called from the audio thread (host
    // automation), so defer to handleAsyncUpdate(). Only posts on an actual change,
    // never per block.
    if (parameterID == "liveEngine" || parameterID == "lowLatency" || parameterID == "liveMode"
        || parameterID == "liveProfile" || parameterID == "extremeLive"
        || parameterID == "polyphonic"
        || parameterID == "xposeMode" || parameterID == "enableCorrection"
        || parameterID == "graphTimeCorrection")
        triggerAsyncUpdate();
}

void SattariAutoPitchAudioProcessor::timerCallback()
{
    //  A dropped file's verdict must reach correction and the bus even while the
    //  transport is STOPPED — the classic "drop the song, get the key" workflow.
    //  correlateAutoKeyHistogram (the only writer of the detected latch) runs off
    //  voiced frames and sidechain audio, neither of which exists in a stopped
    //  session, so the file worker's result would otherwise sit in its atomics
    //  until the user happened to sing. Applying it here mirrors exactly what the
    //  next correlate call would do, minus the parts only live audio can supply.
    //  The Key Map keeps right of way, same as in the correlate chain.
    if (fileKeyValid.load (std::memory_order_acquire) && ! keyMapDriving())
    {
        const float conf = fileKeyConf.load (std::memory_order_relaxed);
        if (conf >= kMinBusKeyConfidence)
        {
            const int scaleIndex = juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load());
            const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
            const bool modal = (scaleName == "major" || scaleName == "minor");
            //  One publish, so the audio thread never observes this message-thread
            //  update as a half-applied verdict.
            publishDetectedAutoKey (fileKeyRoot.load (std::memory_order_relaxed),
                                    modal ? fileKeyMinor.load (std::memory_order_relaxed)
                                          : effectiveScaleIndex(),
                                    conf);
        }
    }

    // Editor-independent, model-independent bus heartbeat. publishCurrentKeyToBus()
    // self-throttles and returns early unless AutoKey / Key Map is the active source.
    publishCurrentKeyToBus();
}

void SattariAutoPitchAudioProcessor::handleAsyncUpdate()
{
    //  Apply a Scale -> scaleMode mirror requested from parameterChanged. Done here
    //  because setValueNotifyingHost notifies the host and re-enters the listener,
    //  neither of which may happen on the audio thread.
    {
        const int wantScaleMode = pendingScaleMirror.exchange (-1, std::memory_order_acquire);
        if (wantScaleMode >= 0)
            if (auto* extended = apvts.getParameter ("scaleMode"))
                extended->setValueNotifyingHost (extended->convertTo0to1 ((float) wantScaleMode));
    }

    // If the Low Latency floor changed, build a fresh PSOLA set on THIS (message)
    // thread and hand it to the audio thread to swap in — the allocation never touches
    // the audio thread, and the swap itself is an O(1) pointer exchange.
    // Compare the desired state against the LIVE shifter's state (updated by the audio
    // thread on swap), not a message-thread flag — otherwise a fast off/on/off toggle
    // could leave the wrong shifter live. If a build is already pending we skip; the
    // audio thread re-triggers this after the swap if the param moved again meanwhile,
    // so it always converges to the current parameter.
    const bool lowLatOn = apvts.getRawParameterValue ("lowLatency") != nullptr
                       && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
    if (lowLatOn != psolaLiveLowLat.load (std::memory_order_acquire)
        && ! psolaSwapReady.load (std::memory_order_acquire))
    {
        preparePsolaInto (pendingPsola, lowLatOn);
        pendingLowLat = lowLatOn;
        psolaSwapReady.store (true, std::memory_order_release);
    }

    reportedLatencySamples = activeEngineLatencySamples();
    setLatencySamples (reportedLatencySamples);
}

void SattariAutoPitchAudioProcessor::releaseResources()
{
#if JucePlugin_Enable_ARA
    releaseResourcesForARA();
#endif
    sessionReader.stop();
    crepeThreadRun.store (false);
    if (crepeThread.joinable())
        crepeThread.join();

#if SATTARI_AUTOPITCH_HAS_ELASTIQUE
    // TODO: CElastiqueV3If::DestroyInstance (static_cast<CElastiqueV3If*> (elastique));
    elastique = nullptr;
    elastiqueOutput.setSize (0, 0);
    elastiqueScratchIn.clear();
    elastiqueScratchOut.clear();
#endif

#if SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    ssStretch.reset();
    ssOutput.setSize (0, 0);
    ssInputPtrs.clear();
    ssOutputPtrs.clear();
#endif

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    rubberBand.clear();
    rubberBandOutput.setSize (0, 0);
    rubberBandScratch.clear();
#endif
}

double SattariAutoPitchAudioProcessor::getTailLengthSeconds() const
{
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    return 0.1;
#else
    return 0.0;
#endif
}

// Host transport relocate / loop point. Without this, only prepareToPlay flushed
// the streaming state, so after a loop wrap or a playhead jump the Rubber Band FIFO
// and PSOLA rings would replay ~one pipeline's worth of stale audio and the
// smoothers would carry the pre-jump pitch scale (an audible click/dropout at the
// seam). resetPitchState() clears the smoothers + PSOLA rings + RB FIFO (and
// re-primes); we additionally reset the Rubber Band stretchers so their internal
// phase state starts clean. Allocation-free (resetPitchState reuses same-size
// storage; the stretcher reset() does not allocate), so this is RT-safe even when a
// host calls reset() from the audio thread.
void SattariAutoPitchAudioProcessor::reset()
{
    lastCapturedPitchEdit = -1;
    lastCapturedEndSec = -1.0;
    resetPitchState (preparedBlockSize);
    graphTimeRing.clear();
    graphTimeWrite = 0;
    graphTimeDelaySamples = 0.5 * currentSampleRate;
    graphTimeWasActive = false;
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    for (auto& s : rubberBand)
        if (s != nullptr)
            s->reset();
#endif
}

void SattariAutoPitchAudioProcessor::resetPitchState (int)
{
    pitchFrameBuffer.assign ((size_t) pitchFrameSize, 0.0f);
    pitchWriteIndex = 0;
    pitchSamplesUntilAnalysis = pitchHopSize;
    liveAutoKeySamplesUntilAnalysis = pitchHopSize;
    lastPitchFrame = {};
    cachedAppliedPitchEditValid = false;
    pitchFusion.reset();
    smoothedPitchScale = 1.0;
    lastFedPitchScale = 1.0;
    //  Seed the transpose ratio at the ALREADY-SELECTED transposition, not unity.
    //  reset() is the host's transport-relocate / loop-wrap / bypass-off hook, and
    //  the transpose branch glides from whatever this holds toward pow(2, semis/12)
    //  over 60 ms - so resetting to 1.0 made every loop wrap and every playhead
    //  move start with an audible octave swoop up to the setting the user had not
    //  changed. Unlike smoothedPitchScale (where re-converging from unity is
    //  correct, because correction should re-earn its target), the transpose ratio
    //  is a static user setting with nothing to re-approach.
    {
        auto* xposeSemisParam = apvts.getRawParameterValue ("xposeSemis");
        const int seedSemis = xposeSemisParam != nullptr
            ? juce::jlimit (-12, 12, (int) std::round (xposeSemisParam->load()))
            : 0;
        smoothedXposeScale = std::pow (2.0, (double) seedSemis / 12.0);
    }
    autoKeyHistogram.fill (0.0);
    publishDetectedAutoKey (9, 1, 0.0f);
    //  The live estimator accumulates whole-session chroma, so everything that
    //  clears the histogram must clear it too — via the flag, because it is
    //  worker-thread-owned and this can run on the audio thread.
    songKeyRescan.store (true, std::memory_order_release);
    lastTargetMidiForTransition = -1000;
    transitionBoost = 0.0;
    classicControlPrevCents = 0.0;
    classicControlValid = false;
    flexEngageState = 0.0;
    vibratoCenterMidi = -1000.0;
    // Vibrato-boundary hysteresis + Intent v2 state clear with everything else,
    // or a stale bias survives a re-prepare.
    hysteresisAltTarget = -1000;
    hysteresisAge       = 999.0;
    intentState.reset();
    // Synthetic vibrato generator: clear phase + onset ramp.
    addVibratoPhase = 0.0;
    addVibratoRampSamples = 0.0;
    resetPolyVocoder();
    // Humanize random-walk state: clear so a stale bias from a previous prepare
    // can't leak into the first blocks after re-prepare.
    humanizeSmoothedCents = 0.0;
    humanizeTargetCents = 0.0;
    humanizeBlocksUntilNewTarget = 0;
    for (auto& p : psola) p.reset();
    for (auto& e : extremeLiveEngines) e.reset();
    for (auto& e : liveEngines) e.reset();
    for (auto& e : liveExtendedEngines) e.reset();
    harmonyExtremeRenderer.reset();
    harmonyUltraRenderer.reset();
    harmonyRangeRenderer.reset();
    harmonyRenderPlanner.reset();
    harmonyWasActive = false;
    harmonyRendererBank = -1;
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    resetRbFifo();
#endif
    if (dryDelayRingSize > 0)
    {
        dryDelayRing.clear();
        dryDelayOutput.clear();
        dryDelayWrite = 0;
    }
    publishUiFrame();
}

void SattariAutoPitchAudioProcessor::renderDelayedDryReference (const juce::AudioBuffer<float>& input)
{
    const int channels = juce::jmin (input.getNumChannels(), dryDelayRing.getNumChannels());
    const int samples = juce::jmin (input.getNumSamples(), dryDelayOutput.getNumSamples());
    if (channels <= 0 || samples <= 0 || dryDelayRingSize <= 0)
        return;

    jassert (input.getNumSamples() <= dryDelayOutput.getNumSamples());
    for (int i = 0; i < samples; ++i)
    {
        int read = dryDelayWrite - dryDelaySamples;
        if (read < 0) read += dryDelayRingSize;
        for (int ch = 0; ch < channels; ++ch)
        {
            const float in = input.getSample (ch, i);
            const float delayed = dryDelaySamples == 0 ? in : dryDelayRing.getSample (ch, read);
            dryDelayOutput.setSample (ch, i, delayed);
            dryDelayRing.setSample (ch, dryDelayWrite, in);
        }
        if (++dryDelayWrite >= dryDelayRingSize)
            dryDelayWrite = 0;
    }
}

void SattariAutoPitchAudioProcessor::copyDelayedDryReferenceTo (juce::AudioBuffer<float>& output) const
{
    const int channels = juce::jmin (output.getNumChannels(), dryDelayOutput.getNumChannels());
    const int samples = juce::jmin (output.getNumSamples(), dryDelayOutput.getNumSamples());
    for (int ch = 0; ch < channels; ++ch)
        output.copyFrom (ch, 0, dryDelayOutput, ch, 0, samples);
    for (int ch = channels; ch < output.getNumChannels(); ++ch)
        output.clear (ch, 0, output.getNumSamples());
    if (samples < output.getNumSamples())
        for (int ch = 0; ch < channels; ++ch)
            output.clear (ch, samples, output.getNumSamples() - samples);
}

void SattariAutoPitchAudioProcessor::publishUiFrame() noexcept
{
    UiFrame frame;
    frame.midi = lastPitchFrame.midi;
    frame.targetMidi = lastPitchFrame.targetMidi;
    frame.correctionCents = lastPitchFrame.correctionCents;
    frame.intentMidi = lastPitchFrame.voiced
        ? juce::jlimit (0, 127, (int) std::llround (lastPitchFrame.targetMidi)) : -1;
    frame.intentConfidence = intentConfidenceUi.load (std::memory_order_relaxed);
    frame.confidence = lastPitchFrame.confidence;
    frame.fedPitchScale = lastFedPitchScale;
    //  Coherent snapshot: this is the single point at which the frame captures the
    //  AutoKey verdict, and everything downstream (correction root/scale and the
    //  cross-plugin bus publication) reads it from here. Two independent loads
    //  could pair a root from one detector with a mode from another.
    {
        int snapRoot = 0, snapScale = 0;
        float snapConf = 0.0f;
        readDetectedAutoKey (snapRoot, snapScale, snapConf);
        frame.detectedRoot  = snapRoot;
        frame.detectedScale = snapScale;
    }
    frame.voiced = lastPitchFrame.voiced;
    uiFrame.store (frame);
}

bool SattariAutoPitchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut || mainOut.isDisabled())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void SattariAutoPitchAudioProcessor::pushPitchSample (float sample)
{
    pitchFrameBuffer[(size_t) pitchWriteIndex] = sample;
    pitchWriteIndex = (pitchWriteIndex + 1) % pitchFrameSize;
}

// Source-aware voiced pitch range (Hz). Voice ranges keep the vocal-tuned
// [70..950 Hz] window; instrument ranges widen low (40 Hz for bass) or high
// (2.2 kHz for whistles / brass tops). One source of truth so the YIN gate and
// the CREPE override reject exactly the same out-of-range pitches.
void SattariAutoPitchAudioProcessor::voicedRangeHzForSource (double& minHz, double& maxHz) const
{
    const int sourceIdx = juce::roundToInt (apvts.getRawParameterValue ("vocalRange")->load());
    minHz = minVoiceHz;
    maxHz = maxVoiceHz;
    switch (sourceIdx)
    {
        case 0: minHz =  55.0; maxHz = 1100.0; break;   // Wide
        case 1: minHz =  65.0; maxHz =  380.0; break;   // Bass Voice
        case 2: minHz =  75.0; maxHz =  480.0; break;   // Baritone
        case 3: minHz =  90.0; maxHz =  650.0; break;   // Tenor
        case 4: minHz =  90.0; maxHz =  850.0; break;   // Alto / Tenor
        case 5: minHz = 120.0; maxHz =  950.0; break;   // Alto
        case 6: minHz = 160.0; maxHz = 1200.0; break;   // Soprano
        case 7: minHz =  40.0; maxHz =  500.0; break;   // Instrument: Bass
        case 8: minHz =  60.0; maxHz = 1500.0; break;   // Instrument: Mid
        case 9: minHz = 110.0; maxHz = 2200.0; break;   // Instrument: High
        default: break;
    }
}

// Auto Pitch's pitch follower uses YIN (de Cheveigné & Kawahara 2002) with a
// causal octave-continuity prior - "pYIN-flavoured". The full pYIN runs an
// offline forward-backward HMM over candidate distributions; we can't do that
// in a realtime block, so we keep YIN's robust core (cumulative-mean-normalised
// difference function + absolute threshold) and add a cheap online continuity
// bias toward the previous frame's pitch. This is the part that tracks breathy /
// noisy material far better than plain autocorrelation: the CMNDF divides out
// amplitude and suppresses the zero-lag and sub-harmonic biases that make
// autocorrelation octave-jump on weakly-pitched vocals.
//
// Honest scope: this is real YIN, NOT CREPE (which is a trained CNN needing
// bundled weights + inference) and NOT the full offline pYIN HMM. It is a clear
// step up from autocorrelation on rough sources and at least as accurate on
// clean tones.
SattariAutoPitchAudioProcessor::PitchFrame SattariAutoPitchAudioProcessor::analyseCurrentPitchFrame() const
{
    PitchFrame result;
    if (currentSampleRate <= 0.0 || pitchFrameBuffer.empty())
        return result;

    // YIN works on the raw (unwindowed) frame - the difference function already
    // handles edge effects, and windowing would bias d(tau) at larger lags.
    std::array<float, pitchFrameSize> frame {};
    double rms = 0.0;
    for (int i = 0; i < pitchFrameSize; ++i)
    {
        const int index = (pitchWriteIndex + i) % pitchFrameSize;
        const float sample = pitchFrameBuffer[(size_t) index];
        frame[(size_t) i] = sample;
        rms += (double) sample * (double) sample;
    }

    rms = std::sqrt (rms / (double) pitchFrameSize);
    result.rms = rms;
    if (rms < silenceRms)
        return result;

    // Source-aware pitch tracking range. Voice ranges keep the vocal-tuned
    // [70..950 Hz] window; instrument ranges widen low (40 Hz for bass) or
    // high (2 kHz for whistles / brass tops). The shapes are conservative —
    // wider than typical fundamentals so vibrato + bends stay inside.
    double rangeMinHz = minVoiceHz, rangeMaxHz = maxVoiceHz;
    voicedRangeHzForSource (rangeMinHz, rangeMaxHz);
    const int minLag = juce::jlimit (1, pitchFrameSize - 2, (int) std::floor (currentSampleRate / rangeMaxHz));
    const int maxLag = juce::jlimit (minLag + 1, pitchFrameSize - 2, (int) std::ceil (currentSampleRate / rangeMinHz));

    // --- YIN step 1+2: difference function and cumulative-mean normalisation ---
    std::array<double, pitchFrameSize> dprime {};
    dprime[0] = 1.0;
    double runningSum = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double d = 0.0;
        const int limit = pitchFrameSize - tau;
        for (int i = 0; i < limit; ++i)
        {
            const double diff = (double) frame[(size_t) i] - (double) frame[(size_t) (i + tau)];
            d += diff * diff;
        }
        runningSum += d;
        // CMNDF: d'(tau) = d(tau) / ((1/tau) * sum_{j=1..tau} d(j)).
        dprime[(size_t) tau] = (runningSum > 0.0) ? (d * (double) tau / runningSum) : 1.0;
    }

    // --- YIN step 3: absolute threshold (shortest period below threshold) -------
    // Classic YIN: walk tau upward and take the FIRST dip below the threshold,
    // stepping to its local minimum. Taking the shortest qualifying period is
    // what defeats sub-harmonic / octave-down errors by construction - a longer
    // (deeper) dip at 2T/3T never wins over the true fundamental at T.
    constexpr double yinThreshold = 0.15;      // classic YIN absolute threshold
    const double prevHz = lastPitchFrame.voiced ? lastPitchFrame.frequencyHz : 0.0;

    int chosenTau = -1;
    for (int tau = minLag; tau < maxLag; ++tau)
    {
        if (dprime[(size_t) tau] < yinThreshold)
        {
            // Step forward to the bottom of this dip.
            while (tau + 1 < maxLag && dprime[(size_t) (tau + 1)] < dprime[(size_t) tau])
                ++tau;
            chosenTau = tau;
            break;
        }
    }

    // No dip cleared the threshold: fall back to the global CMNDF minimum (lower
    // confidence, but still the best periodic guess for weakly-pitched frames).
    if (chosenTau < 0)
    {
        double mn = 1.0e18;
        for (int tau = minLag; tau <= maxLag; ++tau)
            if (dprime[(size_t) tau] < mn) { mn = dprime[(size_t) tau]; chosenTau = tau; }
    }
    if (chosenTau < minLag) chosenTau = minLag;

    // Continuity prior (online stand-in for pYIN's HMM transition smoothing):
    // if there is an EQUALLY-VALID shorter-period candidate near the previous
    // pitch, prefer it. This only ever pulls toward the previous octave when the
    // CMNDF there is also below threshold, so it smooths octave flicker without
    // forcing a wrong octave. (No effect on the very first voiced frame.)
    if (prevHz > 0.0)
    {
        const int prevTau = (int) std::lround (currentSampleRate / prevHz);
        for (int k = 2; k <= 4; ++k)
        {
            const int shorter = chosenTau / k;           // candidate higher octave
            if (shorter < minLag) break;
            if (std::abs (shorter - prevTau) <= 2
                && dprime[(size_t) shorter] < yinThreshold * 1.5)
            {
                chosenTau = shorter;
                break;
            }
        }
        // Mirror the test above for octave-UP mistakes. A strong upper harmonic
        // can make YIN choose T/2 even though the equally-periodic T candidate is
        // the one continuous with the previous frame. The old one-sided guard
        // fixed octave-down flicker only, leaving upward jumps to drive the pitch
        // shifter into a brief metallic chirp.
        for (int k = 2; k <= 4; ++k)
        {
            const int longer = chosenTau * k;
            if (longer > maxLag) break;
            if (std::abs (longer - prevTau) <= 2
                && dprime[(size_t) longer] < yinThreshold * 1.5)
            {
                chosenTau = longer;
                break;
            }
        }
    }

    // --- YIN step 4: parabolic interpolation on d' around the chosen tau --------
    double refinedTau = chosenTau;
    if (chosenTau > minLag && chosenTau < maxLag)
    {
        const double s0 = dprime[(size_t) (chosenTau - 1)];
        const double s1 = dprime[(size_t) chosenTau];
        const double s2 = dprime[(size_t) (chosenTau + 1)];
        const double denom = s0 - 2.0 * s1 + s2;
        if (std::abs (denom) > 1.0e-12)
            refinedTau += 0.5 * (s0 - s2) / denom;
    }

    // Confidence is periodicity = 1 - aperiodicity (the CMNDF value at the dip).
    const double periodicity = juce::jlimit (0.0, 1.0, 1.0 - dprime[(size_t) chosenTau]);
    if (periodicity < minConfidence)
    {
        result.confidence = periodicity;
        return result;
    }

    const double frequencyHz = currentSampleRate / refinedTau;
    result.frequencyHz = frequencyHz;
    result.midi = midiFromHz (frequencyHz, currentReferenceHz());
    result.confidence = periodicity;
    result.voiced = frequencyHz >= rangeMinHz && frequencyHz <= rangeMaxHz;
    return result;
}

double SattariAutoPitchAudioProcessor::currentReferenceHz() const noexcept
{
    if (apvts.getRawParameterValue ("followReference")->load() > 0.5f)
        if (const auto s = sessionReader.current(); s.valid && s.referenceHz >= 400.0 && s.referenceHz <= 480.0)
            return s.referenceHz;

    const double cents = (double) apvts.getRawParameterValue ("referenceDetune")->load();
    return 440.0 * std::pow (2.0, cents / 1200.0);
}

void SattariAutoPitchAudioProcessor::updateMidiTargets (const juce::MidiBuffer& midi) noexcept
{
    int mostRecent = midiTargetNote.load (std::memory_order_relaxed);
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const int note = juce::jlimit (0, 127, message.getNoteNumber());
            midiNotesHeld[(size_t) note] = true;
            mostRecent = note;
        }
        else if (message.isNoteOff())
        {
            const int note = juce::jlimit (0, 127, message.getNoteNumber());
            midiNotesHeld[(size_t) note] = false;
            if (mostRecent == note)
            {
                mostRecent = -1;
                for (int n = 127; n >= 0; --n)
                    if (midiNotesHeld[(size_t) n]) { mostRecent = n; break; }
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            midiNotesHeld.fill (false);
            mostRecent = -1;
        }
    }
    midiTargetNote.store (mostRecent, std::memory_order_relaxed);
    int heldIndex = 0;
    for (int note = 0; note < 128 && heldIndex < 4; ++note)
        if (midiNotesHeld[(size_t) note])
            harmonyHeldMidi[(size_t) heldIndex++].store (note, std::memory_order_relaxed);
    while (heldIndex < 4)
        harmonyHeldMidi[(size_t) heldIndex++].store (-1, std::memory_order_relaxed);
}

int SattariAutoPitchAudioProcessor::currentCorrectionRootIndex() const
{
    // Follow Song Key (from Sattari Key on the master) takes precedence over the
    // local AutoKey: the project's one detected key drives the correction.
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f)
        if (const auto s = sessionReader.current(); s.valid)
            return juce::jlimit (0, 11, s.root);

    // Track Key Map: an offline scan can drive the correction key by host
    // playhead position. This must work independently of AutoKey; the previous
    // path only wrote detectedAutoRoot, which currentCorrectionRootIndex ignored
    // unless AutoKey was also on, so Use Key Map looked alive but did nothing.
    int detectedRoot = 9;
    [[maybe_unused]] int detectedScale = 1;
    float detectedConfidence = 0.0f;
    readDetectedAutoKey (detectedRoot, detectedScale, detectedConfidence);

    if (keyMapDriving())
        return detectedRoot;

    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f
        && detectedConfidence >= kMinBusKeyConfidence)
        return detectedRoot;

    return juce::jlimit (0, 11, juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load()));
}

int SattariAutoPitchAudioProcessor::effectiveScaleIndex() const
{
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f)
        if (const auto s = sessionReader.current(); s.valid)
            // Bus convention: 0 major, 1 minor, 2 chromatic. Honour chromatic as a
            // real "snap to nearest semitone / all 12 pitch classes" scale instead
            // of collapsing it to major (which used to force diatonic snapping and
            // disagree with the UI, which reports chromatic — see currentKeyOrigin).
            // buildCorrectionFrame maps index 2 -> "chromatic" -> nearest-semitone.
            return s.scale == 1 ? 1 : (s.scale == 2 ? 2 : 0);

    [[maybe_unused]] int detectedRoot = 9;
    int detectedScale = 1;
    float detectedConfidence = 0.0f;
    readDetectedAutoKey (detectedRoot, detectedScale, detectedConfidence);

    if (keyMapDriving())
        return detectedScale;

    const int userScale = juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load());
    // AutoKey auto-detects the mode too (unless the user picked Chromatic, which
    // means "no scale snapping" and has no major/minor to detect).
    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f
        && detectedConfidence >= kMinBusKeyConfidence
        && (userScale == 0 || userScale == 1))
        return juce::jlimit (0, 1, detectedScale);

    return juce::jlimit (0, scaleChoices.size() - 1, userScale);
}

bool SattariAutoPitchAudioProcessor::keyMapDriving() const
{
    //  Two time-aware sources now drive the correction target: the scanned Key
    //  Map, and Follow Song Map (the analysed song's scale regions). Either one
    //  outranks a single global AutoKey verdict, because both know something a
    //  global answer cannot: WHERE in the song the singer currently is.
    const bool anySource = apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f
                        || apvts.getRawParameterValue ("followSongMap")->load() > 0.5f;
    return anySource && keyMapCurrentSegment.load (std::memory_order_relaxed) >= 0;
}

SattariAutoPitchAudioProcessor::KeyOrigin SattariAutoPitchAudioProcessor::currentKeyOrigin() const
{
    KeyOrigin o;
    int detectedRoot = 9, detectedScale = 1;
    float detectedConfidence = 0.0f;
    readDetectedAutoKey (detectedRoot, detectedScale, detectedConfidence);

    // Same precedence as currentCorrectionRootIndex() / effectiveScaleIndex(),
    // but returns *why* along with the effective key so the UI can be honest.
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f)
        if (const auto s = sessionReader.current(); s.valid)
        {
            o.source = KeyOrigin::Source::FollowBus;
            o.root   = juce::jlimit (0, 11, s.root);
            o.scale  = s.scale == 1 ? 1 : (s.scale == 2 ? 2 : 0);
            // Locked publishers are explicit session truth. Dynamic publishers
            // also pass a confidence gate, but preserve their actual decision
            // margin so the singer can see that the key is being followed rather
            // than mistake an automatic update for a manual certainty.
            o.confidence = s.dynamicKey && s.keyConfidence > 0.0f
                         ? s.keyConfidence : 1.0f;
            o.dynamic = s.dynamicKey;
            o.revision = s.keyRevision;
            o.windowSeconds = (float) s.keyWindowSeconds;
            return o;
        }

    if (keyMapDriving())
    {
        o.source = KeyOrigin::Source::KeyMap;
        o.root   = detectedRoot;
        o.scale  = detectedScale;
        o.confidence = detectedConfidence;
        return o;
    }

    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f)
    {
        o.source = KeyOrigin::Source::AutoKey;
        // Below the calibrated acceptance floor AutoKey is explicitly a No Result:
        // keep the user's manual key as the safe correction fallback instead of
        // silently tuning the vocal to an untrusted guess.
        o.root   = detectedConfidence >= kMinBusKeyConfidence
                 ? detectedRoot
                 : juce::jlimit (0, 11, juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load()));
        const int userScale = juce::jlimit (0, scaleChoices.size() - 1,
            juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load()));
        o.scale = detectedConfidence >= kMinBusKeyConfidence
                  && (userScale == 0 || userScale == 1)
                ? juce::jlimit (0, 1, detectedScale)
                : userScale;
        o.confidence = detectedConfidence;
        return o;
    }

    o.source = KeyOrigin::Source::Manual;
    o.root   = juce::jlimit (0, 11, juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load()));
    o.scale  = juce::jlimit (0, scaleChoices.size() - 1,
                             juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load()));
    return o;
}

void SattariAutoPitchAudioProcessor::updateAutoKeyHistogram (const PitchFrame& frame)
{
    if (! frame.voiced || frame.confidence < minConfidence)
        return;

    const int note = ((int) std::round (frame.midi) % 12 + 12) % 12;
    const double weight = juce::jlimit (0.0, 1.0, frame.confidence) * juce::jlimit (0.0, 1.0, frame.rms * 30.0);
    for (auto& value : autoKeyHistogram)
        value *= 0.995;
    autoKeyHistogram[(size_t) note] += weight;

    correlateAutoKeyHistogram();
}

void SattariAutoPitchAudioProcessor::correlateAutoKeyHistogram()
{
    const int scaleIndex = juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load());
    const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];

    int bestRoot = 9;
    //  Staged verdict: filled by whichever detector wins below, published as one
    //  unit at the end. Seeded from the current values so a branch that only
    //  decides part of the verdict does not invent the rest.
    int   pendingScale = 1;
    float pendingConf  = 0.0f;
    readDetectedAutoKey (bestRoot, pendingScale, pendingConf);

    // The two absorbed Sattari Key detectors sit ABOVE the scale branch: they only
    // know major/minor, but their ROOT is the best root estimate for ANY scale, so
    // a dorian or harmonic-minor user still benefits — only the mode readout is
    // reserved for the major/minor modes, where AutoKey chooses the mode itself.
    //
    // Order is by MEASURED accuracy: the finalised whole-file verdict, then the
    // live SongKeyEstimator once warm (57.6% exact / 65.12 MIREX on 1337 labelled
    // tracks). The spectrogram model below is now a warm-up fallback — its 72.1
    // MIREX claim did not survive held-out evaluation (37.5% exact, last of six).
    {
        constexpr int kSongKeyWarmFrames = 16;   // ~4 s of 256 ms frames
        const bool modal = (scaleName == "major" || scaleName == "minor");
        //  Both carry Sattari Key's No Result policy: a verdict below the 0.10
        //  margin is DISPLAYED by the UI but never drives correction or the bus.
        //  A user-dropped file does not get a trust exemption — the analysis being
        //  explicit does not make its answer more reliable.
        if (fileKeyValid.load (std::memory_order_acquire)
            && fileKeyConf.load (std::memory_order_relaxed) >= kMinBusKeyConfidence)
        {
            publishDetectedAutoKey (fileKeyRoot.load (std::memory_order_relaxed),
                                    modal ? fileKeyMinor.load (std::memory_order_relaxed)
                                          : pendingScale,
                                    fileKeyConf.load (std::memory_order_relaxed));
            return;
        }
        if (skValid.load (std::memory_order_acquire)
            && skFrames.load (std::memory_order_relaxed) >= kSongKeyWarmFrames
            && skConf.load (std::memory_order_relaxed) >= kMinBusKeyConfidence)
        {
            publishDetectedAutoKey (skRoot.load (std::memory_order_relaxed),
                                    modal ? skMinor.load (std::memory_order_relaxed)
                                          : pendingScale,
                                    skConf.load (std::memory_order_relaxed));
            return;
        }
    }

    if (scaleName == "major" || scaleName == "minor")
    {
        // AutoKey picks BOTH the root AND the mode (major vs minor); detectedAutoScale
        // then drives the effective scale (see effectiveScaleIndex). Prefer the NEURAL
        // key when the sidechain (full-mix) path has produced a valid inference — it's
        // ~58% exact vs ~45% for the profile on real mixes. Absent a sidechain the
        // neural result never validates and we fall back to the pitch-histogram
        // detector, which is the right tool for a monophonic vocal.
        if (keyNetSpecPresent && akSpecValid.load (std::memory_order_acquire))
        {
            bestRoot = akSpecRoot.load (std::memory_order_relaxed);
            pendingScale = akSpecMinor.load (std::memory_order_relaxed);
            //  A validated model inference is a real detection. The spectrogram
            //  model reports no margin of its own, so its validity flag stands in.
            pendingConf = 1.0f;
        }
        else if (keyNetPresent && keyNetLiveValid.load (std::memory_order_acquire))
        {
            bestRoot = keyNetLiveRoot.load (std::memory_order_relaxed);
            pendingScale = keyNetLiveMinor.load (std::memory_order_relaxed);
            pendingConf = 1.0f;
        }
        else
        {
            const auto kr = keyAnalysis.analyseChroma (autoKeyHistogram.data());
            if (kr.valid)
            {
                bestRoot = kr.root;
                pendingScale = kr.minor ? 1 : 0;
                //  KEEP THE MARGIN. This is the line whose absence was the defect:
                //  kr.confidence was computed on every call and thrown away, so a
                //  histogram with no tonal information produced a key that looked
                //  exactly like a confident one downstream.
                pendingConf = (float) kr.confidence;
            }
            else
            {
                pendingConf = 0.0f;
            }
        }
    }
    else
    {
        // Chromatic / non-modal: "which root maximises in-scale energy".
        double bestScore = -2.0, secondScore = -2.0;
        for (int root = 0; root < 12; ++root)
        {
            double score = 0.0;
            for (int noteIndex = 0; noteIndex < 12; ++noteIndex)
            {
                const int degree = ((noteIndex - root) % 12 + 12) % 12;
                score += autoKeyHistogram[(size_t) noteIndex]
                       * (scaleAllowsDegree (degree, scaleName) ? 1.0 : -0.18);
            }
            if (score > bestScore) { secondScore = bestScore; bestScore = score; bestRoot = root; }
            else if (score > secondScore) { secondScore = score; }
        }
        //  This branch used to leave detectedAutoKeyConf untouched, so the publish
        //  gate rode whatever confidence a DIFFERENT detector had stored before the
        //  user switched scale — a 0.25 major-mode margin could keep publishing a
        //  dorian argmax forever. The winner-minus-runner-up separation below is
        //  uncalibrated, but it is at least a margin measured by THIS detector on
        //  THIS audio, and it goes to zero when the histogram stops caring.
        const double denom = std::abs (bestScore) + std::abs (secondScore);
        pendingConf = denom > 1.0e-9
                          ? (float) juce::jlimit (0.0, 1.0, (bestScore - secondScore) / denom)
                          : 0.0f;
    }

    //  One publish for the whole verdict. Previously mode and confidence were
    //  stored inside the branches while the root landed here, so every call left a
    //  window in which the triple described no detector's actual answer.
    publishDetectedAutoKey (bestRoot, pendingScale, pendingConf);
}

// Publish the AutoKey verdict as one lock-free 64-bit value. Unlike a seqlock,
// this remains valid with the audio and message threads publishing concurrently.
void SattariAutoPitchAudioProcessor::publishDetectedAutoKey (int root, int scale, float confidence) noexcept
{
    const int safeRoot = juce::jlimit (0, 11, root);
    const int safeScale = juce::jlimit (0, 2, scale);
    const float safeConfidence = std::isfinite (confidence) ? confidence : 0.0f;
    std::uint32_t confidenceBits = 0;
    std::memcpy (&confidenceBits, &safeConfidence, sizeof (confidenceBits));
    const auto packed = (std::uint64_t (confidenceBits) << 32)
                      | (std::uint64_t (safeScale) << 4)
                      | std::uint64_t (safeRoot);
    detectedAutoKeyVerdict.store (packed, std::memory_order_release);
}

void SattariAutoPitchAudioProcessor::readDetectedAutoKey (int& root, int& scale, float& confidence) const noexcept
{
    const auto packed = detectedAutoKeyVerdict.load (std::memory_order_acquire);
    root = int (packed & 0x0fu);
    scale = int ((packed >> 4) & 0x03u);
    const auto confidenceBits = std::uint32_t (packed >> 32);
    std::memcpy (&confidence, &confidenceBits, sizeof (confidence));
}

bool SattariAutoPitchAudioProcessor::matchScannedSourceToTargetKey (juce::String& error)
{
    error.clear();
#if ! SATTARI_AUTOPITCH_HAS_RUBBERBAND
    error = "Whole-mix key matching requires the Rubber Band engine in this build.";
    return false;
#else
    if (fileProgress.load (std::memory_order_acquire) < 1.0f || ! fileKeyValid.load()
        || ! std::isfinite (fileKeyConf.load()) || fileKeyConf.load() < kMinBusKeyConfidence)
    {
        error = "Scan or drop the original beat/instrument into Auto Pitch first. "
                "Match Key needs a completed, confident source-key analysis, not an instantaneous note.";
        return false;
    }
    int targetRoot = juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load());
    int targetScale = juce::roundToInt (apvts.getRawParameterValue ("scaleMode")->load());
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f)
    {
        // This is a UI command, not an audio callback: read the current authority
        // directly, including expiry, instead of falling back to a hidden local key.
        const auto session = sattari::xplugin::readSession();
        if (! session || (session->dynamicKey && session->keyConfidence < kMinBusKeyConfidence))
        {
            error = "No fresh, trusted session key. Lock and publish the reference song in Sattari Key, then match again.";
            return false;
        }
        targetRoot = session->root;
        targetScale = session->scale;
    }
    else if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f
             || apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f
             || apvts.getRawParameterValue ("followSongMap")->load() > 0.5f)
    {
        error = "Use Follow Song for the reference key, or turn AutoKey and key-map following off to choose a manual destination. The scanned beat is the source, not its own target.";
        return false;
    }
    if (targetScale != 0 && targetScale != 1)
    {
        error = "Whole-mix matching needs a major or minor target. Chromatic/custom-scale note correction is a different operation.";
        return false;
    }
    if ((fileKeyMinor.load() != 0) != (targetScale == 1))
    {
        error = "Source and target modes differ. A fixed transpose preserves major/minor chord relationships; "
                "changing those requires editing the individual notes or stems.";
        return false;
    }
    int delta = (targetRoot - fileKeyRoot.load() + 12) % 12;
    if (delta > 6) delta -= 12;
    auto* shift = apvts.getParameter ("xposeSemis");
    shift->beginChangeGesture();
    shift->setValueNotifyingHost (shift->convertTo0to1 ((float) delta));
    shift->endChangeGesture();
    return true;
#endif
}

void SattariAutoPitchAudioProcessor::publishCurrentKeyToBus()
{
    // Throttle to once a second so we never thrash the bus file. Per-instance
    // (member, not a function-local static) so multiple Auto Pitch instances do
    // not share / corrupt each other's publish timing.
    // When following the bus, don't republish — Sattari Key (the master scanner)
    // is the authority; republishing would fight it for the one bus slot.
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f) return;

    // Only broadcast when AutoKey is actively DETECTING a key from audio. A
    // manually-selected key is a local choice, not session-wide truth — publishing
    // it would stomp Sattari Key (or another detector) on the single bus slot, and
    // a lone instance would end up following its own echo. So: detector → source,
    // manual → passive.
    const bool autoKeyOn = apvts.getRawParameterValue ("autoKey")->load() > 0.5f;
    const bool mapOn = keyMapDriving();
    if (! autoKeyOn && ! mapOn) return;

    //  DO NOT PUBLISH A GUESS. The detector's margin was previously computed and
    //  discarded, so a histogram carrying no tonal information still produced a
    //  key on the bus, and Harmony, Maqam, Scaler and every other follower took
    //  it as session truth. Measured on three real solo vocals: margins of
    //  0.067, 0.053 and -0.031, against 0.157 minimum across 48 known-correct
    //  melodies. Below the gate we publish NOTHING, which leaves followers on
    //  their own key rather than on a coin flip -- Compass may enhance, it must
    //  not create fragile basic functionality.
    //
    //  Silence is the correct failure here: the bus has one slot, and occupying
    //  it with a guess is worse than leaving it empty.
    const auto origin = currentKeyOrigin();
    if (origin.confidence < kMinBusKeyConfidence)
    {
        busKeySuppressed.store (true, std::memory_order_relaxed);
        return;
    }
    busKeySuppressed.store (false, std::memory_order_relaxed);

    // CAS throttle: the editor timer and the worker thread both call this, so claim the
    // 1 s window atomically — only the thread that wins the exchange writes the file.
    const auto now = juce::Time::currentTimeMillis();
    juce::int64 prev = lastBusPublishMs.load (std::memory_order_relaxed);
    if (now - prev < 1000) return;
    if (! lastBusPublishMs.compare_exchange_strong (prev, now, std::memory_order_relaxed)) return;

    const int root = origin.root;
    const int localScale = origin.scale;
    // The cross-plugin bus intentionally remains the stable major/minor/
    // chromatic contract. Map extended modes to their closest tonal family.
    const int scale = localScale == 2 ? 2
                    : (localScale == 1 || localScale == 3 || localScale == 4
                       || localScale == 7 || localScale == 8 || localScale == 9
                       || localScale == 11 || localScale == 12) ? 1 : 0;
    // Publish the full session (key + host tempo + transient pulse) so other
    // Sattari plugins can follow the key AND lock to tempo / react to onsets.
    // Backward-compatible: key-only readers still parse this fine.
    sattari::xplugin::publishSession ("auto-pitch", root, scale,
                                      hostBpmForBus.load(),
                                      sessionTransientId.load(), {}, busOwnerToken);
}

void SattariAutoPitchAudioProcessor::pushSidechainBlock (const float* mono, int numSamples)
{
    if (chromaRing.empty())
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        chromaRing[(size_t) chromaWritePos] = mono[i];
        chromaWritePos = (chromaWritePos + 1) % chromaFftSize;
    }
    chromaSamplesSinceFft += numSamples;

    // Same mono feed also fills the long ring the spectrogram key model reads.
    if (akSpecRing != nullptr && akSpecRingSize > 0)
    {
        int sp = akSpecWritePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            akSpecRing[(size_t) sp].store (mono[i], std::memory_order_relaxed);
            sp = (sp + 1) % akSpecRingSize;
        }
        akSpecWritePos.store (sp, std::memory_order_release);
        //  RELEASE, not relaxed: the songKey drain acquire-loads THIS counter to
        //  learn how far it may read. A relaxed increment has no happens-before
        //  edge to the sample stores above, so on ARM the drain could see the new
        //  count before the new samples — and feed last lap's 30-second-old audio
        //  into an estimator whose histogram never decays.
        akSpecFilled.fetch_add (numSamples, std::memory_order_release);
    }

    // Run the FFT roughly every 256 ms (chroma changes slowly; this keeps the
    // CPU cost negligible). Skip if the buffer hasn't seen non-silence yet.
    const int hop = (int) (currentSampleRate * 0.256);
    if (chromaSamplesSinceFft >= hop && sidechainSeenAudio)
    {
        chromaSamplesSinceFft = 0;   // reset regardless, so the counter can't grow unbounded
        // correlateAutoKeyHistogram() writes detectedAutoRoot/Scale — the SECOND writer
        // besides the mono histogram. Like that one, it must not run while the Key Map is
        // driving, or the sidechain/poly chroma detector overwrites the map's key.
        if (! keyMapDriving())
        {
            runChromaAnalysis();
            correlateAutoKeyHistogram();
        }
    }
}

void SattariAutoPitchAudioProcessor::runChromaAnalysis()
{
    if (chromaFft == nullptr || chromaFftScratch.empty())
        return;

    // Copy the ring into the scratch (oldest -> newest), Hann-window it, FFT it.
    for (int i = 0; i < chromaFftSize; ++i)
    {
        const float s = chromaRing[(size_t) ((chromaWritePos + i) % chromaFftSize)];
        chromaFftScratch[(size_t) i] = s * chromaHannWindow[(size_t) i];
    }
    // Zero the imaginary half required by performFrequencyOnlyForwardTransform.
    std::fill (chromaFftScratch.begin() + chromaFftSize, chromaFftScratch.end(), 0.0f);
    chromaFft->performFrequencyOnlyForwardTransform (chromaFftScratch.data());

    // HPCP chroma of this window: tuning-corrected + harmonic-summed + peak/whitened,
    // which is what makes full-mix key detection work (naive per-bin power sits near
    // chance). Reset per window so it follows modulations; the histogram blend below
    // smooths across windows. finish() returns a normalised 12-bin chroma.
    const double binHz = currentSampleRate / (double) chromaFftSize;
    std::array<double, 12> pcAccum {};
    autoKeyHpcp.reset();
    autoKeyHpcp.accumulate (chromaFftScratch.data(), chromaFftSize / 2 + 1, binHz);
    autoKeyHpcp.finish (pcAccum.data());

    double total = 0.0;
    for (double v : pcAccum) total += v;
    if (total < 1.0e-9) return;   // silent window -> leave the histogram untouched

    constexpr double blend = 0.35;
    for (size_t i = 0; i < autoKeyHistogram.size(); ++i)
        autoKeyHistogram[i] = (1.0 - blend) * autoKeyHistogram[i] + blend * pcAccum[i];

    // --- Live neural key: decayed mean + covariance of the L1-normalised chroma.
    // Same feature as the offline Key Map / training (chroma summed to 1 per frame),
    // just accumulated online. The decay lets it track modulations; ~1s of windows
    // gives a stable covariance. The heavy inference happens on the worker — here we
    // only update sums (12 + 144 mults) and stash the feature under a non-blocking
    // try_lock, so the audio thread never waits on the worker.
    if (keyNetPresent)
    {
        std::array<double, 12> ch {};
        for (int i = 0; i < 12; ++i) ch[(size_t) i] = pcAccum[(size_t) i] / total;

        constexpr double decay = 0.97;
        knWeight = knWeight * decay + 1.0;
        for (int i = 0; i < 12; ++i)
        {
            knMean[i] = knMean[i] * decay + ch[(size_t) i];
            for (int j = 0; j < 12; ++j)
                knOuter[i][j] = knOuter[i][j] * decay + ch[(size_t) i] * ch[(size_t) j];
        }

        if (knWeight > 4.0)   // ~1s of 256 ms windows -> covariance has settled
        {
            double m[12];
            for (int i = 0; i < 12; ++i) m[i] = knMean[i] / knWeight;
            float feat[90]; int fi = 0;
            for (int i = 0; i < 12; ++i) feat[fi++] = (float) m[i];
            for (int i = 0; i < 12; ++i)
                for (int j = i; j < 12; ++j)
                    feat[fi++] = (float) (knOuter[i][j] / knWeight - m[i] * m[j]);

            if (offlineMode.load (std::memory_order_relaxed))
            {
                // Offline bounce: the worker can't keep pace with instant block
                // processing (same reason CREPE degrades offline), so run the key
                // inference SYNCHRONOUSLY here. The worker skips its key block while
                // offlineMode is set, so the two never touch keyNet concurrently.
                const auto kn = keyNet.detectTTA (feat);
                if (kn.valid)
                {
                    keyNetLiveRoot.store  (kn.root,          std::memory_order_relaxed);
                    keyNetLiveMinor.store (kn.minor ? 1 : 0, std::memory_order_relaxed);
                    keyNetLiveValid.store (true,             std::memory_order_release);
                }

                // Drive the SPECTROGRAM model synchronously here too, on the same
                // sample-counted cadence the worker uses. Without this an offline bounce
                // would pick its key with the HPCP model while realtime playback used the
                // spectrogram one — the same project could render in a different key than
                // it played in. Same 30 s readiness rule; skipped until the ring is full.
                if (keyNetSpecPresent && akSpecRing != nullptr
                    && (int) akSpecMono.size() >= akSpecRingSize)
                {
                    const long seen = akSpecFilled.load (std::memory_order_relaxed);
                    const long interval = (long) (kAkSpecIntervalSec * juce::jmax (1.0, currentSampleRate));
                    if (seen >= (long) akSpecRingSize && (akSpecLastRun < 0 || seen - akSpecLastRun >= interval))
                    {
                        akSpecLastRun = seen;
                        const int sp = akSpecWritePos.load (std::memory_order_acquire);
                        for (int i = 0; i < akSpecRingSize; ++i)
                            akSpecMono[(size_t) i] = akSpecRing[(size_t) ((sp + i) % akSpecRingSize)].load (std::memory_order_relaxed);
                        if (logSpec.compute (akSpecMono.data(), akSpecRingSize, currentSampleRate, akSpecFeat))
                        {
                            const auto ks = keyNetSpec.detectTTA (akSpecFeat.data());
                            if (ks.valid)
                            {
                                akSpecRoot.store  (ks.root,          std::memory_order_relaxed);
                                akSpecMinor.store (ks.minor ? 1 : 0, std::memory_order_relaxed);
                                akSpecValid.store (true,             std::memory_order_release);
                            }
                        }
                    }
                }
            }
            else if (keyNetFeatMutex.try_lock())   // real-time: hand to worker, never block
            {
                std::copy (feat, feat + 90, keyNetFeat.begin());
                keyNetFeatMutex.unlock();
                keyNetFeatReady.store (true, std::memory_order_release);
            }
        }
    }
}

// ============================================================================
// Real polyphonic retune — per-peak phase-locked phase vocoder
// ============================================================================
// Standard analysis/synthesis STFT with 4x-overlap Hann overlap-add (the smb /
// Bernsee streaming FIFO layout), extended so that EACH spectral peak carries
// its OWN pitch-shift ratio (snapped to its nearest enabled in-scale tone) and
// its region of influence is translated independently. Peaks use phase-vocoder
// phase accumulation across frames (horizontal coherence); their neighbouring
// bins are identity-phase-locked to the peak (vertical coherence) — the classic
// Laroche-Dolson phase-locked vocoder. Everything runs from preallocated
// buffers; no allocation or locking on the audio thread.
void SattariAutoPitchAudioProcessor::preparePolyVocoder (int numChannels)
{
    const int N = pvFrameSize;
    const int half = N / 2;
    pvInFifoLatency = N - pvHop;

    pvFft = std::make_unique<juce::dsp::FFT> (pvFftOrder);

    // Hann window (period N, so it is COLA at the 4x hop). Applied on BOTH
    // analysis and synthesis; the OLA gain is the sum of w^2 at the hop offsets.
    pvWindow.assign ((size_t) N, 0.0f);
    for (int k = 0; k < N; ++k)
        pvWindow[(size_t) k] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) k / (float) N);
    double acc = 0.0;
    for (int m = 0; m < pvOverlap; ++m)
    {
        const double w = (double) pvWindow[(size_t) (m * pvHop)];
        acc += w * w;
    }
    pvOlaNorm = (float) juce::jmax (1.0e-6, acc);

    const int nc = juce::jmax (1, numChannels);
    pvInFifo.assign   ((size_t) nc, std::vector<float> ((size_t) N, 0.0f));
    pvOutFifo.assign  ((size_t) nc, std::vector<float> ((size_t) N, 0.0f));
    pvOutAccum.assign ((size_t) nc, std::vector<float> ((size_t) N, 0.0f));
    pvLastPhase.assign((size_t) nc, std::vector<float> ((size_t) (half + 1), 0.0f));
    pvSumPhase.assign ((size_t) nc, std::vector<float> ((size_t) (half + 1), 0.0f));
    pvSumPhaseHarm.assign ((size_t) nc, std::vector<float> ((size_t) (half + 1), 0.0f));

    pvFftIn.assign  ((size_t) N, std::complex<float> {});
    pvFftOut.assign ((size_t) N, std::complex<float> {});
    pvMag.assign     ((size_t) (half + 1), 0.0f);
    pvPhase.assign   ((size_t) (half + 1), 0.0f);
    pvFreq.assign    ((size_t) (half + 1), 0.0f);
    pvSynMag.assign  ((size_t) (half + 1), 0.0f);
    pvSynPhase.assign((size_t) (half + 1), 0.0f);
    pvBinPeak.assign ((size_t) (half + 1), -1);
    pvPeakBins.clear();     pvPeakBins.reserve     ((size_t) (half + 1));
    pvPeakRatio.clear();    pvPeakRatio.reserve    ((size_t) (half + 1));
    // pvPeakFund + pvPeakHarmRatio are rebuilt per frame via push_back/assign; reserve
    // them here too so a frame with many peaks never reallocates on the audio thread.
    pvPeakFund.clear();     pvPeakFund.reserve     ((size_t) (half + 1));
    pvPeakHarmRatio.clear(); pvPeakHarmRatio.reserve ((size_t) (half + 1));

    // Tonal Focus: one causal HPSS mask per channel (own history), shared per-frame scratch.
    // Freq radius ~ a few semitones' worth of bins at this resolution isolates a partial from
    // broadband neighbours; 15 time frames ≈ 0.35 s of history at this hop marks steady tones.
    pvHarmMask.assign ((size_t) nc, sattari::dsp::HarmonicMask {});
    for (auto& m : pvHarmMask) m.prepare (half + 1, 15, 15);
    pvTonal.assign ((size_t) (half + 1), 1.0f);

    pvRover = pvInFifoLatency;
}

void SattariAutoPitchAudioProcessor::resetPolyVocoder()
{
    for (auto& v : pvInFifo)    std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : pvOutFifo)   std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : pvOutAccum)  std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : pvLastPhase) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : pvSumPhase)  std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : pvSumPhaseHarm) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& m : pvHarmMask) m.reset();
    pvRover = pvInFifoLatency;
}

// Analyse + synthesise ONE overlap-add frame for a single channel. Reads the
// channel's input FIFO, writes pvHop finished samples to its output FIFO, and
// advances both the OLA accumulator and the per-channel phase memory. Uses the
// shared per-frame scratch (safe because channels are processed sequentially).
void SattariAutoPitchAudioProcessor::polyVocoderFrame (int channel)
{
    if (pvFft == nullptr || pvInFifo.empty() || channel >= (int) pvInFifo.size())
        return;

    const int    N    = pvFrameSize;
    const int    half = N / 2;
    const double sr   = juce::jmax (1.0, currentSampleRate);
    const double freqPerBin = sr / (double) N;
    const double expct = juce::MathConstants<double>::twoPi * (double) pvHop / (double) N;

    auto& inF    = pvInFifo[(size_t) channel];
    auto& lastPh = pvLastPhase[(size_t) channel];
    auto& sumPh  = pvSumPhase[(size_t) channel];

    // --- Forward transform of the windowed frame ---------------------------
    for (int k = 0; k < N; ++k)
        pvFftIn[(size_t) k] = std::complex<float> (inF[(size_t) k] * pvWindow[(size_t) k], 0.0f);
    pvFft->perform (pvFftIn.data(), pvFftOut.data(), false);

    // --- Analysis: magnitude, phase, phase-vocoder true frequency ----------
    for (int k = 0; k <= half; ++k)
    {
        const float re = pvFftOut[(size_t) k].real();
        const float im = pvFftOut[(size_t) k].imag();
        const float mag = std::sqrt (re * re + im * im);
        const float ph  = std::atan2 (im, re);
        pvMag[(size_t) k]   = mag;
        pvPhase[(size_t) k] = ph;

        // Phase advance since last frame, minus the expected advance for bin k,
        // wrapped to (-pi, pi] -> instantaneous-frequency deviation (in bins).
        double tmp = (double) ph - (double) lastPh[(size_t) k];
        lastPh[(size_t) k] = ph;
        tmp -= (double) k * expct;
        long qpd = (long) (tmp / juce::MathConstants<double>::pi);
        if (qpd >= 0) qpd += qpd & 1; else qpd -= qpd & 1;
        tmp -= juce::MathConstants<double>::pi * (double) qpd;
        tmp = (double) pvOverlap * tmp / juce::MathConstants<double>::twoPi;   // bins
        pvFreq[(size_t) k] = (float) (((double) k + tmp) * freqPerBin);        // Hz
    }

    // Tonal Focus: classify every analysis bin harmonic-vs-percussive from this channel's
    // own recent-spectra history. Computed every frame (cheap, causal, no latency) so the
    // history stays warm and the mask is ready the instant the user raises Tonal Focus.
    if (channel < (int) pvHarmMask.size())
        pvHarmMask[(size_t) channel].process (pvMag.data(), pvTonal.data());

    // --- Peak picking in the correctable band ------------------------------
    const int firstBin = juce::jmax (2, (int) std::ceil (60.0   / freqPerBin));
    // High Tracking extends the peak-follow ceiling so a corrected note's HIGH harmonics
    // move with their fundamental instead of staying frozen (out of tune relative to the
    // now-corrected root). 0 = 2.5 kHz (legacy); 1 = 7 kHz. Tonal Focus keeps the added
    // high-band shifting off percussive/noise content.
    const double hiTrack   = (double) apvts.getRawParameterValue ("hiTrack")->load();
    const double trackCeil = 2500.0 + hiTrack * 4500.0;
    const int lastBin  = juce::jmin (half - 2, (int) std::floor (trackCeil / freqPerBin));
    float gmax = 0.0f;
    for (int k = firstBin; k <= lastBin; ++k) gmax = juce::jmax (gmax, pvMag[(size_t) k]);

    pvPeakBins.clear();
    if (gmax > 1.0e-6f)
    {
        const float floorMag = gmax * 0.02f;   // ignore peaks > ~34 dB below the loudest
        for (int k = firstBin; k <= lastBin; ++k)
        {
            const float m = pvMag[(size_t) k];
            if (m > floorMag
                && m >= pvMag[(size_t) (k - 1)] && m >= pvMag[(size_t) (k - 2)]
                && m >= pvMag[(size_t) (k + 1)] && m >= pvMag[(size_t) (k + 2)])
                pvPeakBins.push_back (k);
        }
    }

    // --- Per-peak retune ratio (snap the peak's freq to nearest scale tone) -
    const int  rootIndex   = currentCorrectionRootIndex();
    const int  scaleIndex  = effectiveScaleIndex();
    const auto scaleName   = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    const int  enabledMask = juce::jlimit (0, 4095, (int) apvts.getRawParameterValue ("enabledNotes")->load());
    const double amount    = (double) apvts.getRawParameterValue ("amount")->load();
    const float  polyHarmonyAmt = apvts.getRawParameterValue ("polyHarmony")->load();
    const double tonalFocus = (double) apvts.getRawParameterValue ("tonalFocus")->load();
    auto pcEnabled = [enabledMask] (int midi)
        { return enabledMask == 0 ? 1 : ((enabledMask >> (((midi % 12) + 12) % 12)) & 1); };

    // Harmonic-comb grouping: assign each peak to a NOTE (its fundamental peak). A peak
    // is a harmonic of an earlier note-root if its true freq is a near-integer multiple
    // of that root's freq. Grouping lets a note's upper partials inherit the FUNDAMENTAL's
    // retune ratio, so the note transposes rigidly instead of each harmonic snapping to a
    // scale tone on its own (which mis-tuned buried harmonics — the octave-collision case).
    pvPeakFund.clear();
    for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
    {
        const double f = juce::jmax (1.0, (double) pvFreq[(size_t) pvPeakBins[(size_t) pi]]);
        int fund = pi;
        for (int pj = pi - 1; pj >= 0; --pj)
        {
            if (pvPeakFund[(size_t) pj] != pj) continue;   // combs grow from note roots only
            const double fj = juce::jmax (1.0, (double) pvFreq[(size_t) pvPeakBins[(size_t) pj]]);
            const double r = f / fj;
            const int h = (int) std::lround (r);
            if (h >= 2 && h <= 12 && std::abs (r - (double) h) < 0.03 * (double) h) { fund = pj; break; }
        }
        pvPeakFund.push_back (fund);
    }

    // Per-note retune ratio: snap the NOTE's FUNDAMENTAL freq to the nearest scale tone;
    // every partial of the note uses that same ratio (h*fund * ratio = h*correctedFund).
    pvPeakRatio.assign (pvPeakBins.size(), 1.0f);
    pvPeakHarmRatio.assign (pvPeakBins.size(), 0.0f);
    const double hfGate = (double) apvts.getRawParameterValue ("polyHFGate")->load();
    //  Tune to the SAME absolute grid as every other path in the plugin. This was
    //  hardcoded to 440 while the monophonic path resolved midiFromHz against
    //  currentReferenceHz(), so a session using Reference Detune (or Follow Song
    //  Reference off the bus) had the poly engine snapping peaks to 440 while the
    //  mono engine and the readout worked off the requested reference - toggling
    //  Polyphonic shifted the absolute tuning of the track mid-session.
    const double polyRefHz = currentReferenceHz();
    // Pitch-class mask for the harmony walk — same for every peak, so build it once.
    bool harmonyScaleMask[12] = {};
    if (polyHarmonyAmt > 0.0f)
        for (int pc = 0; pc < 12; ++pc)
            harmonyScaleMask[pc] = scaleAllowsDegree ((((pc - rootIndex) % 12) + 12) % 12, scaleName) != 0;
    for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
    {
        const int fund = pvPeakFund[(size_t) pi];
        const double f = juce::jmax (1.0, (double) pvFreq[(size_t) pvPeakBins[(size_t) fund]]);
        const double srcMidi = 69.0 + 12.0 * std::log2 (f / polyRefHz);
        int target = nearestScaleMidi (srcMidi, rootIndex, scaleName);   // honours chromatic
        if (! pcEnabled (target))
            for (int r = 1; r <= 12; ++r)
            {
                if (pcEnabled (target - r)) { target = target - r; break; }
                if (pcEnabled (target + r)) { target = target + r; break; }
            }
        const double targetFreq = polyRefHz * std::pow (2.0, ((double) target - 69.0) / 12.0);
        // Amount scales the correction in the log domain (1 = full snap, 0 = none).
        // Poly HF Focus: an "orphan" root (fund == pi — a peak that wasn't grouped as the
        // harmonic of any lower note) that sits high in the spectrum is almost never a real
        // fundamental — it's a masked partial or non-tonal air/hats. Snapping it to a scale
        // tone is what drags an instrumental's high end out of key. Roll its correction off
        // from ~1.3 kHz up to ~5 kHz (down to ~8% there), modulated by the HF Focus amount.
        // Grouped harmonics (fund != pi) are untouched — they ride their root's ratio.
        double effAmount = amount;
        if (hfGate > 0.0 && fund == pi && f > 1300.0)
        {
            const double t = juce::jlimit (0.0, 1.0, (f - 1300.0) / (5000.0 - 1300.0));
            effAmount = amount * (1.0 - hfGate * 0.92 * t);
        }
        double ratio = std::pow (targetFreq / f, effAmount);
        ratio = juce::jlimit (0.5, 2.0, ratio);
        pvPeakRatio[(size_t) pi] = (float) ratio;

        // Poly Harmony: ratio to a diatonic third above the corrected note (two scale
        // degrees up, or +4 st in chromatic). Used only when the harmony pass is on.
        if (polyHarmonyAmt > 0.0f)
        {
            // Two scale steps up, via the shared in-key walk (contract-tested); a chromatic
            // key falls back to a fixed major third.
            const int hm = sattari::dsp::scaleStepsAbove (target, harmonyScaleMask, 2, 4);
            const double hFreq = polyRefHz * std::pow (2.0, ((double) hm - 69.0) / 12.0);
            pvPeakHarmRatio[(size_t) pi] = (float) juce::jlimit (0.5, 4.0, hFreq / f);
        }
    }

    // Assign every bin to the peak whose region of influence contains it (the
    // bins between a peak and the midpoints to its neighbours). Bins outside the
    // correctable band get -1 (identity: passed through unshifted, clean).
    for (int k = 0; k <= half; ++k) pvBinPeak[(size_t) k] = -1;
    for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
    {
        const int kp = pvPeakBins[(size_t) pi];
        int lo = (pi == 0) ? firstBin : (pvPeakBins[(size_t) (pi - 1)] + kp) / 2 + 1;
        int hi = (pi == (int) pvPeakBins.size() - 1) ? lastBin : (pvPeakBins[(size_t) (pi + 1)] + kp) / 2;
        lo = juce::jlimit (0, half, lo);
        hi = juce::jlimit (0, half, hi);
        for (int k = lo; k <= hi; ++k) pvBinPeak[(size_t) k] = pi;
    }

    // Tonal Focus routing: a peak-owned bin that reads predominantly PERCUSSIVE gets sent
    // back to the identity (unshifted) path, so hats/air/transients keep their natural
    // pitch while tonal partials are still tuned. The threshold scales with the parameter:
    // at tonalFocus = 1 any bin more noise than tone (w < 0.5) passes through; at 0 nothing
    // is rerouted, so the poly benchmark is byte-identical to before this feature existed.
    if (tonalFocus > 0.0)
    {
        const float thresh = (float) (tonalFocus * 0.5);
        for (int k = 0; k <= half; ++k)
            if (pvBinPeak[(size_t) k] != -1 && pvTonal[(size_t) k] < thresh)
                pvBinPeak[(size_t) k] = -1;
    }

    // --- Synthesis ---------------------------------------------------------
    for (int b = 0; b <= half; ++b) pvSynMag[(size_t) b] = 0.0f;

    // (a) Identity bins (no owning peak): standard phase-vocoder accumulation at
    //     ratio 1, so the low end / air / inter-peak energy passes through clean.
    for (int k = 0; k <= half; ++k)
    {
        if (pvBinPeak[(size_t) k] != -1) continue;
        double tmp = ((double) pvFreq[(size_t) k] - (double) k * freqPerBin) / freqPerBin; // bins dev
        tmp *= juce::MathConstants<double>::twoPi / (double) pvOverlap;
        tmp += (double) k * expct;
        sumPh[(size_t) k] += (float) tmp;
        pvSynMag[(size_t) k]   += pvMag[(size_t) k];
        pvSynPhase[(size_t) k]  = sumPh[(size_t) k];
    }

    // (b) Peak-owned bins: translate each region by its peak ratio; accumulate
    //     the peak channel's synthesis phase, then identity-phase-lock the region
    //     bins to it (neighbour synth phase = peak synth phase + analysis-phase
    //     difference). This keeps each shifted partial phase-coherent.
    for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
    {
        const int    kp    = pvPeakBins[(size_t) pi];
        const double ratio = (double) pvPeakRatio[(size_t) pi];
        const int    nkp   = juce::jlimit (0, half, (int) std::lround ((double) kp * ratio));

        const double fSyn = (double) pvFreq[(size_t) kp] * ratio;   // scaled true freq (Hz)
        double tmp = (fSyn - (double) nkp * freqPerBin) / freqPerBin;
        tmp *= juce::MathConstants<double>::twoPi / (double) pvOverlap;
        tmp += (double) nkp * expct;
        sumPh[(size_t) nkp] += (float) tmp;
        const double peakSyn = (double) sumPh[(size_t) nkp];
        const double phPeak  = (double) pvPhase[(size_t) kp];

        int lo = (pi == 0) ? firstBin : (pvPeakBins[(size_t) (pi - 1)] + kp) / 2 + 1;
        int hi = (pi == (int) pvPeakBins.size() - 1) ? lastBin : (pvPeakBins[(size_t) (pi + 1)] + kp) / 2;
        lo = juce::jlimit (0, half, lo);
        hi = juce::jlimit (0, half, hi);
        for (int k = lo; k <= hi; ++k)
        {
            // Tonal Focus may have rerouted this bin to the identity path (percussive):
            // it was already passed through unshifted in (a), so skip it here to avoid
            // double-counting its energy. At tonalFocus = 0 nothing is rerouted.
            if (pvBinPeak[(size_t) k] != pi) continue;
            const int nb = juce::jlimit (0, half, (int) std::lround ((double) k * ratio));
            pvSynMag[(size_t) nb] += pvMag[(size_t) k];
            const double locked = peakSyn + ((double) pvPhase[(size_t) k] - phPeak);
            pvSynPhase[(size_t) nb] = (float) locked;
            sumPh[(size_t) nb]      = (float) locked;   // seed next frame's accumulation
        }
    }

    // Build the Hermitian-symmetric complex spectrum and inverse-transform.
    for (int b = 0; b <= half; ++b)
    {
        const float m  = pvSynMag[(size_t) b];
        const float ph = pvSynPhase[(size_t) b];
        pvFftIn[(size_t) b] = std::complex<float> (m * std::cos (ph), m * std::sin (ph));
    }

    // (c) Poly Harmony — add an in-key harmony a diatonic third above each note directly
    //     into the complex spectrum, with a DEDICATED phase accumulator, so it is
    //     phase-coherent yet can never perturb the corrected voice built above. The whole
    //     pass is skipped when off, so it has zero effect on the benchmarked main voice.
    if (polyHarmonyAmt > 0.0f)
    {
        auto& sumPhH = pvSumPhaseHarm[(size_t) channel];
        for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
        {
            const double hratio = (double) pvPeakHarmRatio[(size_t) pi];
            if (hratio <= 0.0) continue;
            const int    kp   = pvPeakBins[(size_t) pi];
            const int    nkp  = juce::jlimit (0, half, (int) std::lround ((double) kp * hratio));
            const double fSyn = (double) pvFreq[(size_t) kp] * hratio;
            double tmp = (fSyn - (double) nkp * freqPerBin) / freqPerBin;
            tmp *= juce::MathConstants<double>::twoPi / (double) pvOverlap;
            tmp += (double) nkp * expct;
            sumPhH[(size_t) nkp] += (float) tmp;
            const double peakSyn = (double) sumPhH[(size_t) nkp];
            const double phPeak  = (double) pvPhase[(size_t) kp];

            int lo = (pi == 0) ? firstBin : (pvPeakBins[(size_t) (pi - 1)] + kp) / 2 + 1;
            int hi = (pi == (int) pvPeakBins.size() - 1) ? lastBin : (pvPeakBins[(size_t) (pi + 1)] + kp) / 2;
            lo = juce::jlimit (0, half, lo);
            hi = juce::jlimit (0, half, hi);
            for (int k = lo; k <= hi; ++k)
            {
                const int nb = juce::jlimit (0, half, (int) std::lround ((double) k * hratio));
                const double locked = peakSyn + ((double) pvPhase[(size_t) k] - phPeak);
                sumPhH[(size_t) nb] = (float) locked;
                const float m = pvMag[(size_t) k] * polyHarmonyAmt;
                pvFftIn[(size_t) nb] += std::complex<float> (m * std::cos ((float) locked), m * std::sin ((float) locked));
            }
        }
    }

    for (int b = half + 1; b < N; ++b)
        pvFftIn[(size_t) b] = std::conj (pvFftIn[(size_t) (N - b)]);
    pvFft->perform (pvFftIn.data(), pvFftOut.data(), true);   // inverse (JUCE scales by 1/N)

    // Windowed overlap-add into this channel's accumulator.
    auto& accum = pvOutAccum[(size_t) channel];
    auto& outF  = pvOutFifo[(size_t) channel];
    for (int k = 0; k < N; ++k)
        accum[(size_t) k] += pvWindow[(size_t) k] * pvFftOut[(size_t) k].real() / pvOlaNorm;

    // Publish pvHop finished samples, then slide the accumulator and input FIFO.
    for (int k = 0; k < pvHop; ++k) outF[(size_t) k] = accum[(size_t) k];
    std::memmove (accum.data(), accum.data() + pvHop, sizeof (float) * (size_t) (N - pvHop));
    for (int k = N - pvHop; k < N; ++k) accum[(size_t) k] = 0.0f;
    std::memmove (inF.data(), inF.data() + pvHop, sizeof (float) * (size_t) (N - pvHop));
}

// Drive the phase vocoder across a host block, in place. Channels advance in
// lockstep (shared rover), so a frame fires for all channels at once. Applies
// the input drive last, matching the mono engines (processWithPsola et al.).
void SattariAutoPitchAudioProcessor::processPolyphonicRetune (juce::AudioBuffer<float>& buffer, float linearGain)
{
    const int n = buffer.getNumSamples();
    if (pvInFifo.empty() || pvFft == nullptr) { buffer.applyGain (linearGain); return; }

    const int usableCh = juce::jmin (buffer.getNumChannels(), (int) pvInFifo.size());
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < usableCh; ++ch)
        {
            pvInFifo[(size_t) ch][(size_t) pvRover] = buffer.getSample (ch, i);
            buffer.setSample (ch, i, pvOutFifo[(size_t) ch][(size_t) (pvRover - pvInFifoLatency)]);
        }
        if (++pvRover >= pvFrameSize)
        {
            pvRover = pvInFifoLatency;
            for (int ch = 0; ch < usableCh; ++ch)
                polyVocoderFrame (ch);
        }
    }
    buffer.applyGain (linearGain);
}

// ============================================================================
// Track Key Map
// ============================================================================
//  ============================================================================
//  Sattari Key, absorbed: whole-song key + tempo analysis of a dropped file.
//  Ported from SongKeyProcessor::analyseFile — decode chunked, feed a FINALISED
//  SongKeyEstimator (prepared with the exact length for deterministic end
//  handling), build a spectral-flux onset envelope for the TempoEstimator, and
//  latch the verdict where correlateAutoKeyHistogram() outranks every live
//  detector with it. Distinct from the Key Map scan on purpose: drop = ONE
//  whole-song key + tempo; SCAN TRACK = per-section key segments. useKeyMap
//  still outranks AutoKey entirely, so the two features compose instead of
//  fighting over detectedAutoRoot.
//  ============================================================================
bool SattariAutoPitchAudioProcessor::isSupportedAudioFile (const juce::File& file) const
{
    return file.existsAsFile()
        && file.hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;aac;ogg");
}

void SattariAutoPitchAudioProcessor::clearSongFileResult()
{
    // Do not retain a previous clip's detected latch while replacing/rescanning
    // its source. The user's manual key is the safe fallback until a new result.
    songSourceIsClip.store (false);
    songSourceStartSeconds.store (0.0);
    songSourceDurationSeconds.store (0.0);
    fileKeyValid.store (false, std::memory_order_release);
    fileBpm.store (0.0f, std::memory_order_relaxed);
    fileProgress.store (0.0f, std::memory_order_relaxed);
    {
        //  The follow table is derived from the analysis; clearing one without
        //  the other left a previous song's key map driving correction while the
        //  UI showed no analysis at all.
        const juce::ScopedLock kl (keyMapLock);
        songMapEntries.clear();
        keyMapCurrentSegment.store (-1);
        // Reset after acquiring the map lock: an audio callback finishing its
        // old map lookup must not restore that verdict after it was revoked.
        publishDetectedAutoKey (9, 1, 0.0f);
    }
    sattari::xplugin::withdrawSessionState ("auto-pitch", {}, busOwnerToken);
    const std::lock_guard<std::mutex> lock (songAnalysisMutex);
    songAnalysis = {};
    songAnalysisRevision.fetch_add (1);
}

void SattariAutoPitchAudioProcessor::analyseSongFileAsync (const juce::File& file)
{
    startSongSourceAnalysis (file, {}, file.getFileNameWithoutExtension(), 0.0, false);
}

bool SattariAutoPitchAudioProcessor::analyseSelectedArrangementClip (juce::String& error)
{
#if JucePlugin_Enable_ARA
    auto request = sattari::arrangement::selectedClip (getEditorView());
    error = request.error;
    if (request.reader == nullptr) return false;
    const bool started = analyseSongClipAsync (std::move (request.reader), request.name,
                                               request.timelineStartSeconds);
    songClipWatch = std::move (request.watch);
    songClipWatch->onInvalidated = [this] { invalidateArrangementAnalysis(); };
    return started;
#else
    error = sattari::arrangement::unavailableMessage();
    return false;
#endif
}

bool SattariAutoPitchAudioProcessor::analyseSongClipAsync (
    std::unique_ptr<juce::AudioFormatReader> reader,
    const juce::String& name, double timelineStartSeconds)
{
    if (reader == nullptr || ! std::isfinite (timelineStartSeconds)) return false;
    startSongSourceAnalysis ({}, std::move (reader), name, timelineStartSeconds, true);
    return true;
}

void SattariAutoPitchAudioProcessor::startSongSourceAnalysis (
    const juce::File& file, std::unique_ptr<juce::AudioFormatReader> clipReader,
    const juce::String& name, double timelineStartSeconds, bool isClip)
{
    //  Serialise: one file analysis at a time, and never concurrently with a dying
    //  predecessor (the reader owns a file handle).
    fileWorkerCancel.store (true, std::memory_order_release);
    if (fileWorker.joinable())
        fileWorker.join();
    // ARA readers register model listeners. Their ownership remains on the
    // message thread; the worker only borrows them until it has been joined.
#if JucePlugin_Enable_ARA
    songClipWatch.reset();
#endif
    songClipReader = std::move (clipReader);
    fileWorkerCancel.store (false, std::memory_order_relaxed);
    clearSongFileResult();
    songSourceStartSeconds.store (timelineStartSeconds);
    songSourceIsClip.store (isClip);
    {
        const juce::ScopedLock lock (songSourceLock);
        songSourceName = name;
    }

    fileWorker = std::thread ([this, file, isClip, timelineStartSeconds]
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> fileReader;
        if (! isClip) fileReader.reset (formats.createReaderFor (file));
        auto* reader = isClip ? songClipReader.get() : fileReader.get();
        if (reader == nullptr || reader->lengthInSamples <= 0
            || reader->numChannels == 0 || reader->sampleRate <= 0.0)
        {
            fileProgress.store (-1.0f, std::memory_order_release);   // error
            return;
        }

        constexpr int order = 12, fft = 1 << order, hop = fft / 2;
        constexpr int chunkSamples = 65536;
        const int channels = juce::jmax (1, (int) reader->numChannels);

        juce::AudioBuffer<float> decoded (channels, chunkSamples);
        std::vector<float> mono ((size_t) chunkSamples + fft, 0.0f);
        std::vector<float> spectrum ((size_t) fft * 2, 0.0f);
        std::vector<float> window ((size_t) fft, 0.0f);
        for (int i = 0; i < fft; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * (float) i / (float) (fft - 1));

        juce::dsp::FFT fileFft (order);
        sattari::dsp::HpcpChromagram fileHpcp;
        sattari::dsp::SongKeyEstimator fileKey;
        fileHpcp.reserveBins (fft / 2 + 1);

        //  Chord timeline accumulators (worker-local, like everything else here).
        constexpr int kChordFrames = 16;                 // hops per chord frame
        sattari::dsp::HpcpChromagram chordHpcp;
        chordHpcp.reserveBins (fft / 2 + 1);
        chordHpcp.reset();
        sattari::dsp::ChordAnalyzer chordAnalyzer;
        std::vector<sattari::dsp::ChordResult> chordFrames;
        int chordFrameFill = 0;
        const double chordFrameSeconds =
            (double) (kChordFrames * hop) / reader->sampleRate;
        fileKey.prepare (reader->sampleRate, (std::int64_t) reader->lengthInSamples);

        std::vector<float> previousSpectrum ((size_t) fft / 2 + 1, 0.0f);
        std::vector<float> onsetEnvelope;
        onsetEnvelope.reserve ((size_t) std::ceil (
            (double) reader->lengthInSamples / (double) hop));
        int frames = 0;

        //  Display waveform (peak) + level envelope (RMS) for the song model.
        //  Fixed bin counts: the UI draws them at any width, and the section
        //  deriver compares 8-bin windows, so resolution is a display decision.
        constexpr int kWaveBins = 1024, kRmsBins = 256;
        std::vector<float> wavePeaks ((size_t) kWaveBins, 0.0f);
        std::vector<double> rmsAccum ((size_t) kRmsBins, 0.0);
        std::vector<long>   rmsCount ((size_t) kRmsBins, 0);
        const double samplesPerWaveBin = (double) reader->lengthInSamples / (double) kWaveBins;
        const double samplesPerRmsBin  = (double) reader->lengthInSamples / (double) kRmsBins;

        auto analyseFrame = [&] (const float* samples)
        {
            for (int i = 0; i < fft; ++i)
                spectrum[(size_t) i] = samples[i] * window[(size_t) i];
            std::fill (spectrum.begin() + fft, spectrum.end(), 0.0f);
            fileFft.performFrequencyOnlyForwardTransform (spectrum.data());
            fileHpcp.accumulate (spectrum.data(), fft / 2 + 1,
                                 reader->sampleRate / (double) fft);
            //  Chord evidence: a SEPARATE short-window chroma. The global HPCP
            //  above accumulates the whole song and must not be reset, so chords
            //  get their own accumulator, finished every kChordFrames hops
            //  (~0.7 s at 4096/2048 — long enough to be stable, short enough to
            //  land on real chord changes).
            chordHpcp.accumulate (spectrum.data(), fft / 2 + 1,
                                  reader->sampleRate / (double) fft);
            if (++chordFrameFill >= kChordFrames)
            {
                double c[12] = {};
                chordHpcp.finish (c);
                chordFrames.push_back (chordAnalyzer.analyse (c));
                chordHpcp.reset();
                chordFrameFill = 0;
            }
            //  Spectral flux on log magnitudes (SongKeyProcessor::onsetStrength).
            double flux = 0.0;
            for (int bin = 2; bin < fft / 2 + 1; ++bin)
            {
                const float value = std::log1p (juce::jmax (0.0f, spectrum[(size_t) bin]));
                flux += juce::jmax (0.0f, value - previousSpectrum[(size_t) bin]);
                previousSpectrum[(size_t) bin] = value;
            }
            onsetEnvelope.push_back ((float) (flux / (double) (fft / 2 - 1)));
            ++frames;
        };

        int carried = 0;
        juce::int64 readPosition = 0;
        while (readPosition < reader->lengthInSamples)
        {
            if (fileWorkerCancel.load (std::memory_order_acquire))
                return;
            const int count = (int) juce::jmin ((juce::int64) chunkSamples,
                                                reader->lengthInSamples - readPosition);
            decoded.clear();
            if (! reader->read (&decoded, 0, count, readPosition, true, true))
            {
                fileProgress.store (-1.0f, std::memory_order_release);
                return;
            }
            for (int i = 0; i < count; ++i)
            {
                float sum = 0.0f;
                for (int channel = 0; channel < channels; ++channel)
                    sum += decoded.getSample (channel, i);
                mono[(size_t) carried + (size_t) i] = sum / (float) channels;
            }
            fileKey.addSamples (mono.data() + carried, count);

            for (int i = 0; i < count; ++i)
            {
                const float v = mono[(size_t) carried + (size_t) i];
                const auto globalIndex = (double) (readPosition + i);
                const int wb = juce::jlimit (0, kWaveBins - 1, (int) (globalIndex / samplesPerWaveBin));
                const int rb = juce::jlimit (0, kRmsBins - 1,  (int) (globalIndex / samplesPerRmsBin));
                wavePeaks[(size_t) wb] = juce::jmax (wavePeaks[(size_t) wb], std::abs (v));
                rmsAccum[(size_t) rb] += (double) v * v;
                ++rmsCount[(size_t) rb];
            }

            const int available = carried + count;
            int cursor = 0;
            while (cursor + fft <= available)
            {
                analyseFrame (mono.data() + cursor);
                cursor += hop;
            }
            carried = available - cursor;
            if (carried > 0)
                std::memmove (mono.data(), mono.data() + cursor,
                              (size_t) carried * sizeof (float));
            readPosition += count;
            fileProgress.store ((float) (0.98 * (double) readPosition
                                               / (double) reader->lengthInSamples),
                                std::memory_order_release);
        }
        if (carried > 0 && (frames == 0 || carried >= hop / 2))
        {
            std::fill (mono.begin() + carried, mono.begin() + fft, 0.0f);
            analyseFrame (mono.data());
        }
        fileKey.finalise();

        const auto key = fileKey.result();
        if (frames == 0 || ! key.valid)
        {
            fileProgress.store (-1.0f, std::memory_order_release);
            return;
        }
        const auto tempo = sattari::dsp::TempoEstimator::analyse (
            onsetEnvelope, reader->sampleRate / (double) hop);

        if (fileWorkerCancel.load (std::memory_order_acquire)
            || ! sattari::arrangement::isCurrent (reader)) return;
        songSourceDurationSeconds.store ((double) reader->lengthInSamples / reader->sampleRate);

        fileKeyRoot.store (key.root, std::memory_order_relaxed);
        fileKeyMinor.store (key.minor ? 1 : 0, std::memory_order_relaxed);
        fileKeyConf.store ((float) key.confidence, std::memory_order_relaxed);
        fileDetuneCents.store ((float) (fileHpcp.tuningOffsetSemitones() * 100.0),
                               std::memory_order_relaxed);
        fileBpm.store (tempo.valid ? (float) tempo.bpm : 0.0f, std::memory_order_relaxed);
        fileKeyValid.store (true, std::memory_order_release);

        //  Publish the shared song model — the time-aware view every workspace
        //  reads. Scale regions come from the SAME finalised pass (the estimator
        //  keeps a verdict per ~15 s internally); sections are neutral A/B/C from
        //  scale changes and sustained level shifts, never guessed names.
        {
            sattari::dsp::SongAnalysis a;
            a.valid = true;
            a.durationSec = (double) reader->lengthInSamples / reader->sampleRate;
            a.globalKey   = key;
            a.alternative = fileKey.alternative();
            a.bpm = tempo.valid ? tempo.bpm : 0.0;
            a.tempoConfidence = tempo.confidence;
            a.tempoOctaveAmbiguous = tempo.octaveAmbiguous;
            a.detuneCents = fileHpcp.tuningOffsetSemitones() * 100.0;

            const double sectionSeconds =
                (double) sattari::dsp::SongKeyEstimator::kSectionFrames
                * (double) sattari::dsp::SongKeyEstimator::kFftSize
                / sattari::dsp::SongKeyEstimator::kAnalysisSampleRate;
            a.scaleRegions = sattari::dsp::songmodel::buildScaleRegions (
                fileKey.sections(), sectionSeconds, a.durationSec);

            std::vector<float> rms ((size_t) kRmsBins, 0.0f);
            for (int i = 0; i < kRmsBins; ++i)
                rms[(size_t) i] = rmsCount[(size_t) i] > 0
                    ? (float) std::sqrt (rmsAccum[(size_t) i] / (double) rmsCount[(size_t) i])
                    : 0.0f;
            a.sections = sattari::dsp::songmodel::deriveSections (a.scaleRegions, rms, a.durationSec);

            //  Phase 2: chord timeline + what each scale boundary actually IS.
            a.chordRegions = sattari::dsp::songmodel::buildChordRegions (
                chordFrames, chordFrameSeconds, a.durationSec);
            //  Mark each chord diatonic or borrowed against the scale region it
            //  sits in — the distinction the UI needs to explain itself.
            for (auto& cr : a.chordRegions)
            {
                int sr = a.globalKey.valid ? a.globalKey.root : -1;
                bool sm = a.globalKey.minor;
                for (const auto& reg : a.scaleRegions)
                    if (cr.startSec >= reg.startSec && cr.startSec < reg.endSec)
                    { sr = reg.root; sm = reg.minor; break; }
                cr.diatonic = sr < 0
                    || sattari::dsp::ChordAnalyzer::chordIsDiatonic (
                           cr.chord.root, cr.chord.quality, sr, sm);
            }
            a.modulations = sattari::dsp::songmodel::classifyModulations (a.scaleRegions);
            a.waveform = std::move (wavePeaks);

            //  Feed the analysed scale regions into the SAME keyMap the Key Map
            //  follower already drives from. That follower is validated code
            //  (binary search per block, RT-safe try-lock, no allocation); giving
            //  it a second source is strictly better than writing a parallel
            //  follower that could disagree with it.
            {
                std::vector<KeyMapEntry> entries;
                entries.reserve (a.scaleRegions.size());
                for (const auto& r : a.scaleRegions)
                    entries.push_back ({ timelineStartSeconds + r.startSec, juce::jlimit (0, 11, r.root),
                                         r.minor ? 1 : 0, (float) r.confidence });
                const juce::ScopedLock kl (keyMapLock);
                songMapEntries = std::move (entries);
            }

            const std::lock_guard<std::mutex> lock (songAnalysisMutex);
            //  Carry USER-SET regions across re-analysis. The UI promises a manual
            //  key outranks detection permanently, and wholesale replacement broke
            //  that promise the moment a second song was analysed.
            for (const auto& oldRegion : songAnalysis.scaleRegions)
            {
                if (! oldRegion.userSet) continue;
                for (auto& r : a.scaleRegions)
                    if (r.startSec < oldRegion.endSec && r.endSec > oldRegion.startSec)
                    { r.root = oldRegion.root; r.minor = oldRegion.minor;
                      r.userSet = true; r.confidence = 1.0; }
            }
            songAnalysis = std::move (a);
            songAnalysisRevision.fetch_add (1);
        }

        fileProgress.store (1.0f, std::memory_order_release);
    });
}

void SattariAutoPitchAudioProcessor::invalidateArrangementAnalysis()
{
    fileWorkerCancel.store (true, std::memory_order_release);
    if (fileWorker.joinable()) fileWorker.join();
    songClipReader.reset();
    clearSongFileResult();
    fileProgress.store (-1.0f, std::memory_order_release);
}

bool SattariAutoPitchAudioProcessor::startKeyMapScan (const juce::File& audioFile)
{
    if (! audioFile.existsAsFile()) return false;
    if (keyMapScanning.load()) return false;     // already running

    // Join any finished thread first (its progress is at 100 or it errored out).
    if (keyMapScanThread && keyMapScanThread->joinable())
        keyMapScanThread->join();
    keyMapScanThread.reset();

    keyMapScanCancel.store (false);   // fresh scan: clear any pending cancel request
    keyMapScanning.store (true);
    keyMapProgress.store (0);
    {
        const juce::ScopedLock l (keyMapLock);
        keyMapLastError.clear();
    }
    keyMapScanThread = std::make_unique<std::thread> ([this, audioFile] { runKeyMapScan (audioFile); });
    return true;
}

void SattariAutoPitchAudioProcessor::clearKeyMap()
{
    // Cancellation is asynchronous: a neural window can take long enough that joining
    // here visibly freezes the editor. startKeyMapScan joins the completed worker before
    // launching the next one; the destructor still joins before processor teardown.
    const bool scanWasRunning = keyMapScanning.load (std::memory_order_acquire);
    keyMapScanCancel.store (true);
    const juce::ScopedLock l (keyMapLock);
    keyMap.clear();
    keyMapWave.clear();
    keyMapDurationSec = 0.0;
    keyMapFileName.clear();
    keyMapProgress.store (-1);
    keyMapCurrentSegment.store (-1);
    if (! scanWasRunning)
        keyMapScanning.store (false);
}

// --- Key Map visualisation accessors (message thread; copy under the lock) ------
std::vector<SattariAutoPitchAudioProcessor::KeyMapEntry>
SattariAutoPitchAudioProcessor::getKeyMapSegments() const
{
    const juce::ScopedLock l (keyMapLock);
    return keyMap;
}

std::vector<float> SattariAutoPitchAudioProcessor::getKeyMapWaveform() const
{
    const juce::ScopedLock l (keyMapLock);
    return keyMapWave;
}

double SattariAutoPitchAudioProcessor::getKeyMapDurationSec() const
{
    const juce::ScopedLock l (keyMapLock);
    return keyMapDurationSec;
}

SattariAutoPitchAudioProcessor::KeyMapStatus SattariAutoPitchAudioProcessor::getKeyMapStatus() const
{
    KeyMapStatus s;
    const juce::ScopedLock l (keyMapLock);
    s.hasMap = ! keyMap.empty();
    s.numSegments = (int) keyMap.size();
    s.currentSegment = keyMapCurrentSegment.load();
    s.scanProgress = keyMapProgress.load();
    s.loadedFileName = keyMapFileName;
    s.lastError = keyMapLastError;
    return s;
}

void SattariAutoPitchAudioProcessor::runKeyMapScan (juce::File audioFile)
{
    juce::AudioFormatManager fmtMgr;
    fmtMgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmtMgr.createReaderFor (audioFile));
    if (reader == nullptr)
    {
        {
            const juce::ScopedLock l (keyMapLock);
            keyMapLastError = "Could not open audio file";
        }
        keyMapProgress.store (-1);
        keyMapScanning.store (false);
        return;
    }

    const double sr = reader->sampleRate;
    const juce::int64 totalLen = reader->lengthInSamples;

    // Window the scan: ~3.5 second analysis windows hop every ~1 second so the
    // map captures mid-song key changes without spamming segments.
    // The SPECTROGRAM key model (72.1 MIREX) is trained on 30 s..120 s excerpts, so it
    // needs a window in that range — feeding it 3.5 s would be a domain mismatch. Key
    // changes are structural (sections last far longer), so a 30 s window on a 15 s hop
    // still resolves a modulation to within ~15 s. Without the model we keep the chroma
    // scan's fine 3.5 s / 1 s grid.
    const bool useSpecScan  = keyNetSpecPresent;
    const int windowSamples = useSpecScan ? (int) (sr * 30.0) : juce::jmax (8192, (int) (sr * 3.5));
    const int hopSamples    = useSpecScan ? (int) (sr * 15.0) : juce::jmax (1024, (int) (sr * 1.0));
    const double hopSec     = (sr > 0.0) ? (double) hopSamples / sr : 1.0;

    juce::AudioBuffer<float> readBuf (2, windowSamples);
    std::vector<float> windowMono ((size_t) windowSamples, 0.0f);

    // Local FFT (same params as the live chroma; built fresh so we never touch
    // the audio thread's FFT state).
    const int fftOrder = 12;
    const int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);
    std::vector<float> fftBuf ((size_t) (fftSize * 2), 0.0f);
    std::vector<float> hann   ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                   * (float) i / (float) (fftSize - 1));


    std::vector<KeyMapEntry> built;
    std::vector<KeyMapEntry> wins;   // every window's raw key estimate (smoothed after)
    sattari::dsp::HpcpChromagram hpcp;
    juce::int64 pos = 0;
    std::vector<float> wave;                 // peak envelope for the editor timeline
    wave.reserve ((size_t) (totalLen / juce::jmax (1, windowSamples) + 1) * 16);

    // Analyse at least one zero-padded window. The old full-window-only loop
    // returned an empty Key Map for every neural-mode clip shorter than 30 s
    // (and every fallback clip shorter than 3.5 s), which made File/Key Map mode
    // appear broken on loops and samples.
    while (pos < totalLen)
    {
        // setStateInformation() (or teardown/clear) can ask us to stop so a scan
        // that started before the host restored state doesn't clobber the restored
        // map. Poll every window and bail before publishing.
        if (keyMapScanCancel.load (std::memory_order_relaxed))
            break;

        readBuf.clear();
        const int validSamples = (int) juce::jmin<juce::int64> ((juce::int64) windowSamples,
                                                                 totalLen - pos);
        reader->read (&readBuf, 0, validSamples, pos, true, true);
        // Mono-sum the window.
        for (int i = 0; i < windowSamples; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < readBuf.getNumChannels(); ++ch)
                m += readBuf.getSample (ch, i);
            m /= (float) juce::jmax (1, readBuf.getNumChannels());
            windowMono[(size_t) i] = m;
        }

        // Peak envelope for the editor timeline. Captured over the HOP region only, so
        // overlapping windows don't double-count and the envelope stays time-linear at
        // ~0.2 s per point regardless of the window/hop grid in use.
        {
            const int envSpan = juce::jmin (hopSamples, validSamples);
            const int buckets = juce::jmax (1, (int) (envSpan / juce::jmax (1.0, sr * 0.2)));
            const int per = juce::jmax (1, envSpan / buckets);
            for (int b = 0; b < buckets; ++b)
            {
                const int s0 = b * per, s1 = juce::jmin (envSpan, s0 + per);
                float pk = 0.0f;
                for (int i = s0; i < s1; ++i) pk = juce::jmax (pk, std::abs (windowMono[(size_t) i]));
                wave.push_back (juce::jlimit (0.0f, 1.0f, pk));
            }
        }

        // Spectrogram key model path: one pseudo-CQT + TTA inference per 30 s window.
        if (useSpecScan)
        {
            std::vector<float> feat;
            int specRoot = wins.empty() ? 0 : wins.back().rootIdx;
            int specMode = wins.empty() ? 0 : wins.back().scaleIdx;
            float specConf = 0.0f;
            if (logSpec.compute (windowMono.data(), windowSamples, sr, feat))
            {
                const auto kn = keyNetSpec.detectTTA (feat.data());
                if (kn.valid) { specRoot = kn.root; specMode = kn.minor ? 1 : 0; specConf = kn.confidence; }
            }
            wins.push_back ({ (double) pos / sr, specRoot, specMode, specConf });
            pos += juce::jmin<juce::int64> ((juce::int64) hopSamples, totalLen - pos);
            keyMapProgress.store (juce::jlimit (0, 100, totalLen > 0 ? (int) (100.0 * (double) pos / (double) totalLen) : 0));
            continue;
        }

        // Per-frame HPCP chroma over the whole window -> mean(12) + covariance(12x12).
        // The covariance (note co-occurrence) is the feature the NEURAL key model needs;
        // it's computed exactly like key_dataset_eval's fileChromaHpcpCov so training and
        // inference match. The mean alone still feeds the profile fallback.
        std::array<double,12> mean {}; double cov[12][12] = {};
        int frames = 0;
        const int fhop = fftSize / 2;
        for (int fs = 0; fs + fftSize <= windowSamples; fs += fhop)
        {
            for (int i = 0; i < fftSize; ++i)
                fftBuf[(size_t) i] = windowMono[(size_t) (fs + i)] * hann[(size_t) i];
            std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftBuf.data());
            std::array<double,12> ch {};
            hpcp.reset();
            hpcp.accumulate (fftBuf.data(), fftSize / 2 + 1, sr / (double) fftSize);
            hpcp.finish (ch.data());
            double s = 0; for (double v : ch) s += v;
            if (s < 1.0e-9) continue;
            for (auto& v : ch) v /= s;
            ++frames;
            for (int i = 0; i < 12; ++i) { mean[(size_t) i] += ch[(size_t) i]; for (int j = 0; j < 12; ++j) cov[i][j] += ch[(size_t) i] * ch[(size_t) j]; }
        }

        int   bestRoot  = wins.empty() ? 0 : wins.back().rootIdx;
        int   bestMode  = 0;
        float bestScore = 0.0f;
        if (frames > 0)
        {
            for (auto& v : mean) v /= frames;
            for (int i = 0; i < 12; ++i) for (int j = 0; j < 12; ++j) cov[i][j] = cov[i][j] / frames - mean[(size_t) i] * mean[(size_t) j];

            if (keyNetPresent)   // neural key model (58% exact vs 45% profile)
            {
                float feat[90]; int fi = 0;
                for (int i = 0; i < 12; ++i) feat[fi++] = (float) mean[(size_t) i];
                for (int i = 0; i < 12; ++i) for (int j = i; j < 12; ++j) feat[fi++] = (float) cov[i][j];
                const auto kn = keyNet.detectTTA (feat);
                if (kn.valid) { bestRoot = kn.root; bestMode = kn.minor ? 1 : 0; bestScore = kn.confidence; }
            }
            else                 // fixed-profile fallback (shared with live AutoKey)
            {
                const auto kr = keyAnalysis.analyseChroma (mean.data());
                if (kr.valid) { bestRoot = kr.root; bestMode = kr.minor ? 1 : 0; bestScore = (float) kr.confidence; }
            }
        }

        wins.push_back ({ (double) pos / sr, bestRoot, bestMode, bestScore });

        pos += juce::jmin<juce::int64> ((juce::int64) hopSamples, totalLen - pos);
        const int pct = totalLen > 0 ? (int) (100.0 * (double) pos / (double) totalLen) : 0;
        keyMapProgress.store (juce::jlimit (0, 100, pct));
    }

    // Stabilise the raw per-window estimates into a usable timeline. A real song ran the
    // naive change-on-every-flicker logic to ~1 segment/second of unrelated keys; now we
    //   (1) majority-vote each window over +/- K neighbours (kills detector flicker), then
    //   (2) run-length encode and DROP any segment shorter than minSeg (merge the flicker
    //       back into the surrounding key) -> a handful of real sections.
    {
        // Vote span is defined in SECONDS (~13 s), not windows, so it holds on both the
        // chroma grid (1 s hop -> K=6) and the spectrogram grid (15 s hop -> K=0, where
        // each estimate already integrates 30 s and needs no de-flickering).
        const int K = juce::jmax (0, (int) std::lround (6.5 / juce::jmax (0.001, hopSec)));
        const double minSeg = 10.0;      // seconds; sections are longer than this
        const int n = (int) wins.size();
        for (int i = 0; i < n; ++i)
        {
            int votes[24] = { 0 };
            for (int j = juce::jmax (0, i - K); j <= juce::jmin (n - 1, i + K); ++j)
                votes[wins[(size_t) j].rootIdx * 2 + wins[(size_t) j].scaleIdx]++;
            int bestK = 0;
            for (int k = 1; k < 24; ++k) if (votes[k] > votes[bestK]) bestK = k;
            wins[(size_t) i].rootIdx  = bestK / 2;
            wins[(size_t) i].scaleIdx = bestK % 2;
        }
        for (int i = 0; i < n; ++i)
        {
            if (built.empty())
            {
                built.push_back (wins[(size_t) i]);
                built.back().timeSec = 0.0;   // first segment always starts at 0
            }
            else if (wins[(size_t) i].rootIdx != built.back().rootIdx
                  || wins[(size_t) i].scaleIdx != built.back().scaleIdx)
            {
                if (wins[(size_t) i].timeSec - built.back().timeSec >= minSeg)
                    built.push_back (wins[(size_t) i]);   // else: too short -> stay in current key
            }
        }
    }

    // Cancelled mid-scan (e.g. by setStateInformation restoring a map): do NOT
    // publish `built`, or it would clobber the just-restored/cleared timeline.
    // Leave keyMap untouched and mark the scan idle.
    if (keyMapScanCancel.load (std::memory_order_relaxed))
    {
        keyMapScanning.store (false);
        return;
    }

    // Publish (map + the waveform envelope the editor timeline draws).
    {
        const juce::ScopedLock l (keyMapLock);
        keyMap = std::move (built);
        keyMapWave = std::move (wave);
        keyMapDurationSec = (sr > 0.0) ? (double) totalLen / sr : 0.0;
        keyMapFileName = audioFile.getFileName();
        keyMapCurrentSegment.store (-1);
    }
    keyMapProgress.store (100);
    keyMapScanning.store (false);
}

// Called from processBlock when useKeyMap is on and the playhead has a position.
// Looks up the segment covering `seconds` and publishes its complete key verdict.
SattariAutoPitchAudioProcessor::KeyMapKey
SattariAutoPitchAudioProcessor::keyMapKeyAtSeconds (double seconds) const
{
    const juce::ScopedTryLock l (keyMapLock);
    if (! l.isLocked() || keyMap.empty())
        return {};
    int lo = 0, hi = (int) keyMap.size() - 1, idx = 0;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (keyMap[(size_t) mid].timeSec <= seconds) { idx = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return { juce::jlimit (0, 11, keyMap[(size_t) idx].rootIdx),
             juce::jlimit (0, 2,  keyMap[(size_t) idx].scaleIdx) };
}

void SattariAutoPitchAudioProcessor::applyKeyMapAtSeconds (double seconds)
{
    const juce::ScopedTryLock l (keyMapLock);
    if (! l.isLocked())
        return;

    //  Source precedence: an explicit SCAN TRACK key map is a deliberate act and
    //  outranks the song analysis, which the user may simply have left loaded.
    const bool useScan = apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f
                      && ! keyMap.empty();
    const bool useSong = ! useScan
                      && apvts.getRawParameterValue ("followSongMap")->load() > 0.5f
                      && ! songMapEntries.empty()
                      && (! songSourceIsClip.load()
                          || (seconds >= songSourceStartSeconds.load()
                              && seconds < songSourceStartSeconds.load() + songSourceDurationSeconds.load()));
    if (! useScan && ! useSong)
    {
        //  Clear the latch. Returning without this left keyMapCurrentSegment at
        //  its last value while keyMapDriving() ORs in followSongMap, so the
        //  correction key froze on a dead segment FOREVER: AutoKey stays
        //  suppressed, and nothing else ever writes this atomic during playback.
        keyMapCurrentSegment.store (-1);
        return;
    }
    const auto& table = useScan ? keyMap : songMapEntries;

    // Binary search for the latest entry whose timeSec <= seconds.
    int lo = 0, hi = (int) table.size() - 1, idx = 0;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (table[(size_t) mid].timeSec <= seconds)
        {
            idx = mid;
            lo = mid + 1;
        }
        else hi = mid - 1;
    }

    const auto& e = table[(size_t) idx];
    //  A region below the trust gate must not silently steer correction. Report
    //  no segment, so currentKeyOrigin falls back exactly as when AutoKey is
    //  uncertain.
    //
    //  The threshold is applied ONLY to the song-map source. Scan entries carry
    //  a confidence produced by a different detector on a different scale, and
    //  judging them by the chroma-margin gate would reject or admit them for
    //  reasons unrelated to how good they are. The scan path keeps the
    //  behaviour it was validated with.
    if (useSong && e.confidence < kMinBusKeyConfidence)
    {
        keyMapCurrentSegment.store (-1);
        return;
    }
    publishDetectedAutoKey (e.rootIdx, e.scaleIdx, e.confidence);
    keyMapCurrentSegment.store (idx);
}

std::vector<SattariAutoPitchAudioProcessor::PitchEditEntry>
SattariAutoPitchAudioProcessor::getPitchEdits() const
{
    const juce::ScopedLock l (pitchEditLock);
    return pitchEdits;
}

void SattariAutoPitchAudioProcessor::clearPitchEdits()
{
    const juce::ScopedLock l (pitchEditLock);
    if (! pitchEdits.empty()) pushPitchEditUndoLocked();
    pitchEdits.clear();
    lastCapturedPitchEdit = -1;
    lastCapturedEndSec = -1.0;
    pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
}

void SattariAutoPitchAudioProcessor::pushPitchEditUndoLocked()
{
    pitchEditUndo.push_back (pitchEdits);
    if (pitchEditUndo.size() > 64) pitchEditUndo.erase (pitchEditUndo.begin());
    pitchEditRedo.clear();
}

void SattariAutoPitchAudioProcessor::markPitchEditChangedLocked()
{
    pitchEditGestureChanged = pitchEditGestureActive;
    pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
}

void SattariAutoPitchAudioProcessor::beginPitchEditGesture()
{
    const juce::ScopedLock l (pitchEditLock);
    if (pitchEditGestureActive) return;
    pushPitchEditUndoLocked();
    pitchEditGestureActive = true;
    pitchEditGestureChanged = false;
}

void SattariAutoPitchAudioProcessor::endPitchEditGesture()
{
    const juce::ScopedLock l (pitchEditLock);
    if (! pitchEditGestureActive) return;
    pitchEditGestureActive = false;
    if (! pitchEditGestureChanged && ! pitchEditUndo.empty())
        pitchEditUndo.pop_back();
}

bool SattariAutoPitchAudioProcessor::setPitchEditEntry (juce::int64 id, const PitchEditEntry& requested)
{
    const juce::ScopedLock l (pitchEditLock);
    const auto it = std::find_if (pitchEdits.begin(), pitchEdits.end(),
                                  [id] (const auto& e) { return e.id == id; });
    if (it == pitchEdits.end()) return false;
    if (! pitchEditGestureActive) pushPitchEditUndoLocked();
    auto changed = requested;
    changed.id = id;
    changed.startSec = juce::jmax (0.0, changed.startSec);
    changed.endSec = juce::jmax (changed.startSec + 0.001, changed.endSec);
    changed.sourceMidi = juce::jlimit (0, 127, changed.sourceMidi);
    changed.targetMidi = juce::jlimit (0, 127, changed.targetMidi);
    changed.confidence = juce::jlimit (0.0f, 1.0f, changed.confidence);
    changed.amount = changed.amount < 0.0f ? -1.0f : juce::jlimit (0.0f, 1.0f, changed.amount);
    changed.retune = changed.retune < 0.0f ? -1.0f : juce::jlimit (0.05f, 1.0f, changed.retune);
    changed.transition = changed.transition < 0.0f ? -1.0f : juce::jlimit (0.0f, 1.0f, changed.transition);
    changed.humanize = changed.humanize < 0.0f ? -1.0f : juce::jlimit (0.0f, 1.0f, changed.humanize);
    changed.vibrato = changed.vibrato < 0.0f ? -1.0f : juce::jlimit (0.0f, 1.0f, changed.vibrato);
    changed.formantSemitones = changed.formantSemitones < -12.0f
        ? -100.0f : juce::jlimit (-12.0f, 12.0f, changed.formantSemitones);
    changed.gainDb = juce::jlimit (-24.0f, 12.0f, changed.gainDb);
    changed.timeOffsetSec = juce::jlimit (-0.5, 0.5, changed.timeOffsetSec);
    *it = changed;
    markPitchEditChangedLocked();
    return true;
}

bool SattariAutoPitchAudioProcessor::splitPitchEdit (juce::int64 id, double splitSec)
{
    const juce::ScopedLock l (pitchEditLock);
    const auto it = std::find_if (pitchEdits.begin(), pitchEdits.end(), [id] (const auto& e) { return e.id == id; });
    if (it == pitchEdits.end()) return false;
    const double split = splitSec > it->startSec + 0.01 && splitSec < it->endSec - 0.01
        ? splitSec : 0.5 * (it->startSec + it->endSec);
    if (split <= it->startSec + 0.001 || split >= it->endSec - 0.001 || pitchEdits.size() >= 4096) return false;
    pushPitchEditUndoLocked();
    auto right = *it;
    right.id = nextPitchEditId++;
    right.startSec = split;
    it->endSec = split;
    pitchEdits.insert (std::next (it), right);
    markPitchEditChangedLocked();
    return true;
}

bool SattariAutoPitchAudioProcessor::joinPitchEdit (juce::int64 id)
{
    const juce::ScopedLock l (pitchEditLock);
    auto left = std::find_if (pitchEdits.begin(), pitchEdits.end(), [id] (const auto& e) { return e.id == id; });
    if (left == pitchEdits.end()) return false;
    auto right = pitchEdits.end();
    for (auto it = pitchEdits.begin(); it != pitchEdits.end(); ++it)
        if (it != left && it->startSec >= left->startSec && (right == pitchEdits.end() || it->startSec < right->startSec)) right = it;
    if (right == pitchEdits.end()) return false;
    pushPitchEditUndoLocked();
    const double leftLength = juce::jmax (0.001, left->endSec - left->startSec);
    const double rightLength = juce::jmax (0.001, right->endSec - right->startSec);
    left->sourceMidi = juce::roundToInt ((left->sourceMidi * leftLength + right->sourceMidi * rightLength) / (leftLength + rightLength));
    left->confidence = juce::jmax (left->confidence, right->confidence);
    left->endSec = juce::jmax (left->endSec, right->endSec);
    pitchEdits.erase (right);
    markPitchEditChangedLocked();
    return true;
}

bool SattariAutoPitchAudioProcessor::deletePitchEdit (juce::int64 id)
{
    const juce::ScopedLock l (pitchEditLock);
    const auto it = std::find_if (pitchEdits.begin(), pitchEdits.end(), [id] (const auto& e) { return e.id == id; });
    if (it == pitchEdits.end()) return false;
    pushPitchEditUndoLocked();
    pitchEdits.erase (it);
    lastCapturedPitchEdit = -1;
    markPitchEditChangedLocked();
    return true;
}

bool SattariAutoPitchAudioProcessor::undoPitchEdit()
{
    const juce::ScopedLock l (pitchEditLock);
    if (pitchEditUndo.empty()) return false;
    pitchEditRedo.push_back (pitchEdits);
    pitchEdits = std::move (pitchEditUndo.back());
    pitchEditUndo.pop_back();
    //  Restore the audio thread's allocation-free contract. The snapshots pushed
    //  by pushPitchEditUndoLocked are COPIES, so their capacity is only their
    //  element count; move-assigning one in discards the 4096-element buffer
    //  prepareToPlay reserved, and the next capturePitchEditAtSeconds push_back
    //  from processBlock would malloc + copy + free on the audio thread.
    //  setStateInformation already re-reserves after its own move for this reason.
    pitchEdits.reserve (4096);
    lastCapturedPitchEdit = -1;
    pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
    return true;
}

bool SattariAutoPitchAudioProcessor::redoPitchEdit()
{
    const juce::ScopedLock l (pitchEditLock);
    if (pitchEditRedo.empty()) return false;
    pitchEditUndo.push_back (pitchEdits);
    pitchEdits = std::move (pitchEditRedo.back());
    pitchEditRedo.pop_back();
    //  Restore the audio thread's allocation-free contract. The snapshots pushed
    //  by pushPitchEditUndoLocked are COPIES, so their capacity is only their
    //  element count; move-assigning one in discards the 4096-element buffer
    //  prepareToPlay reserved, and the next capturePitchEditAtSeconds push_back
    //  from processBlock would malloc + copy + free on the audio thread.
    //  setStateInformation already re-reserves after its own move for this reason.
    pitchEdits.reserve (4096);
    lastCapturedPitchEdit = -1;
    pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
    return true;
}

bool SattariAutoPitchAudioProcessor::canUndoPitchEdit() const { const juce::ScopedLock l (pitchEditLock); return ! pitchEditUndo.empty(); }
bool SattariAutoPitchAudioProcessor::canRedoPitchEdit() const { const juce::ScopedLock l (pitchEditLock); return ! pitchEditRedo.empty(); }

void SattariAutoPitchAudioProcessor::capturePitchEditAtSeconds (
    double seconds, double blockDuration, const PitchFrame& frame)
{
    const juce::ScopedTryLock l (pitchEditLock);
    if (! l.isLocked()) return;

    if (! frame.voiced || seconds < 0.0)
    {
        lastCapturedPitchEdit = -1;
        return;
    }

    const int source = juce::jlimit (0, 127, (int) std::round (frame.midi));
    const int target = juce::jlimit (0, 127, (int) std::round (frame.targetMidi));
    const double end = seconds + juce::jmax (0.001, blockDuration);
    const bool canExtend = lastCapturedPitchEdit >= 0
        && lastCapturedPitchEdit < (int) pitchEdits.size()
        && pitchEdits[(size_t) lastCapturedPitchEdit].sourceMidi == source
        && seconds >= pitchEdits[(size_t) lastCapturedPitchEdit].startSec
        && seconds <= lastCapturedEndSec + 0.18;

    if (canExtend)
    {
        auto& e = pitchEdits[(size_t) lastCapturedPitchEdit];
        e.endSec = juce::jmax (e.endSec, end);
        e.confidence = juce::jmax (e.confidence, (float) frame.confidence);
    }
    else if (pitchEdits.size() < 4096)
    {
        PitchEditEntry entry;
        entry.id = nextPitchEditId++;
        entry.startSec = seconds; entry.endSec = end;
        entry.sourceMidi = source; entry.targetMidi = target;
        entry.confidence = (float) juce::jlimit (0.0, 1.0, frame.confidence);
        entry.amount = apvts.getRawParameterValue ("amount")->load();
        entry.retune = apvts.getRawParameterValue ("speed")->load();
        entry.transition = apvts.getRawParameterValue ("transition")->load();
        entry.humanize = apvts.getRawParameterValue ("humanize")->load();
        entry.vibrato = apvts.getRawParameterValue ("vibrato")->load();
        entry.formantSemitones = apvts.getRawParameterValue ("formantShift")->load();
        pitchEdits.push_back (entry);
        lastCapturedPitchEdit = (int) pitchEdits.size() - 1;
    }
    lastCapturedEndSec = end;
    pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
}

void SattariAutoPitchAudioProcessor::applyPitchEditAtSeconds (double seconds, PitchFrame& frame) const
{
    if (! frame.voiced || seconds < 0.0) return;

    auto applyEntry = [&frame] (const PitchEditEntry& e)
    {
        if (! e.enabled)
        {
            frame.targetMidi = frame.midi;
            frame.correctionCents = 0.0;
        }
        else
        {
            frame.targetMidi = (double) e.targetMidi;
            frame.correctionCents = (frame.targetMidi - frame.midi) * 100.0;
        }
        frame.objectAmount = e.amount;
        frame.objectRetune = e.retune;
        frame.objectTransition = e.transition;
        frame.objectHumanize = e.humanize;
        frame.objectVibrato = e.vibrato;
        frame.objectFormantSemitones = e.formantSemitones;
        frame.objectGainDb = e.gainDb;
        frame.objectActive = true;
        frame.objectEnabled = e.enabled;
    };

    const juce::ScopedTryLock l (pitchEditLock);
    if (! l.isLocked())
    {
        // The editor may hold the lock for one block while dragging or changing
        // per-note controls. Keep applying the last matching immutable copy so a
        // GUI gesture cannot punch a dry/correction-free hole into the vocal.
        if (cachedAppliedPitchEditValid
            && seconds >= cachedAppliedPitchEdit.startSec
            && seconds <= cachedAppliedPitchEdit.endSec)
            applyEntry (cachedAppliedPitchEdit);
        return;
    }
    if (pitchEdits.empty())
    {
        cachedAppliedPitchEditValid = false;
        return;
    }

    // Reverse scan makes the common playback case cheap. Do not early-break:
    // capturing another pass after a host relocate can append an earlier-time
    // region, so storage order is not guaranteed to remain chronological.
    for (int i = (int) pitchEdits.size() - 1; i >= 0; --i)
    {
        const auto& e = pitchEdits[(size_t) i];
        if (seconds >= e.startSec && seconds <= e.endSec)
        {
            cachedAppliedPitchEdit = e;
            cachedAppliedPitchEditValid = true;
            applyEntry (e);
            return;
        }
    }
    cachedAppliedPitchEditValid = false;
}

SattariAutoPitchAudioProcessor::PitchFrame SattariAutoPitchAudioProcessor::buildCorrectionFrame (PitchFrame detected) const
{
    if (! detected.voiced)
        return detected;

    // Capture one coherent key decision for the entire frame. Reading root and
    // scale through separate helpers allowed a publish between the two calls.
    const auto origin = currentKeyOrigin();
    const int rootIndex = origin.root;
    const int scaleIndex = origin.scale;
    const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    int targetMidi = nearestScaleMidi (detected.midi, rootIndex, scaleName);

    if (apvts.getRawParameterValue ("midiTarget")->load() > 0.5f)
    {
        const int held = midiTargetNote.load (std::memory_order_relaxed);
        if (held >= 0)
            targetMidi = held;
    }

    // Edit Notes mask: bit-N of `enabledNotes` enables pitch-class N. If the
    // scale snap landed on a blocked note, search outward for the nearest
    // enabled-AND-in-scale note. If the user has blocked literally every note
    // (mask = 0) we fall back to the un-masked target so we never produce
    // silence-on-pitch as a "feature".
    const int mask = juce::jlimit (0, 4095, (int) apvts.getRawParameterValue ("enabledNotes")->load());
    if (mask != 0)
    {
        auto pcEnabled = [mask] (int midi) { return (mask >> (((midi % 12) + 12) % 12)) & 1; };
        if (! pcEnabled (targetMidi))
        {
            int searchTarget = targetMidi;
            for (int radius = 1; radius <= 12 && ! pcEnabled (searchTarget); ++radius)
            {
                const int down = targetMidi - radius;
                const int up   = targetMidi + radius;
                // Honour scale: candidate must be in-scale AND enabled. Chromatic
                // mode passes the in-scale check trivially.
                auto inScale = [&] (int m) {
                    if (scaleName == "chromatic") return true;
                    const int deg = ((m - rootIndex) % 12 + 12) % 12;
                    return scaleAllowsDegree (deg, scaleName);
                };
                if (inScale (down) && pcEnabled (down)) { searchTarget = down; break; }
                if (inScale (up)   && pcEnabled (up))   { searchTarget = up;   break; }
            }
            targetMidi = searchTarget;
        }
    }

    detected.targetMidi = (double) targetMidi;
    detected.correctionCents = (detected.targetMidi - detected.midi) * 100.0;
    return detected;
}

void SattariAutoPitchAudioProcessor::setRegionKey (int regionIndex, int root, bool minor)
{
    {
        const std::lock_guard<std::mutex> lock (songAnalysisMutex);
        if (regionIndex < 0 || regionIndex >= (int) songAnalysis.scaleRegions.size()) return;
        auto& r = songAnalysis.scaleRegions[(size_t) regionIndex];
        r.root = juce::jlimit (0, 11, root);
        r.minor = minor;
        r.userSet = true;
        //  Full confidence: a manual key is not a guess, and it must clear every
        //  trust gate the detected ones have to pass.
        r.confidence = 1.0;
    }
    //  Republish the follow table so the change takes effect immediately.
    {
        const auto model = songAnalysisCopy();
        std::vector<KeyMapEntry> entries;
        entries.reserve (model.scaleRegions.size());
        for (const auto& r : model.scaleRegions)
            entries.push_back ({ songSourceStartSeconds.load() + r.startSec, juce::jlimit (0, 11, r.root),
                                 r.minor ? 1 : 0, (float) r.confidence });
        const juce::ScopedLock kl (keyMapLock);
        songMapEntries = std::move (entries);
    }
}

void SattariAutoPitchAudioProcessor::updateHarmonyChordAtSeconds (double seconds) noexcept
{
    seconds -= songSourceStartSeconds.load();
    // Song analysis is background-owned. The audio thread may sample it, but it
    // never waits for a file scan or UI copy: if the mutex is busy, the previous
    // coherent chord remains valid for one more block.
    std::unique_lock<std::mutex> lock (songAnalysisMutex, std::try_to_lock);
    if (! lock.owns_lock()) return;

    int pcs[4] { -1, -1, -1, -1 };
    int count = 0;
    float confidence = 0.0f;
    for (const auto& region : songAnalysis.chordRegions)
    {
        if (seconds < region.startSec || seconds >= region.endSec) continue;
        for (int i = 0; i < 4 && count < 4; ++i)
            if (region.chord.notes[(size_t) i] >= 0)
                pcs[count++] = region.chord.notes[(size_t) i];
        confidence = (float) region.chord.confidence;
        break;
    }
    for (int i = 0; i < 4; ++i)
        harmonyChordPcs[(size_t) i].store (pcs[i], std::memory_order_relaxed);
    harmonyChordConfidence.store (confidence, std::memory_order_relaxed);
    harmonyChordCount.store (count, std::memory_order_release);
}

SattariAutoPitchAudioProcessor::HarmonyReadout
SattariAutoPitchAudioProcessor::planHarmonyNow() const
{
    HarmonyReadout out;
    const auto snap = getLiveSnapshot();
    if (! snap.voiced || snap.detectedMidi < 0) return out;
    out.leadMidi = snap.detectedMidi;

    sattari::dsp::HarmonyVoiceSettings settings[4];
    for (int v = 0; v < 4; ++v)
    {
        const auto id = juce::String ("h") + juce::String (v + 1);
        settings[v].enabled = apvts.getRawParameterValue (id + "Enable")->load() > 0.5f;
        settings[v].role = (sattari::dsp::HarmonyRole)
            juce::jlimit (0, 6, (int) std::lround (apvts.getRawParameterValue (id + "Role")->load()));
        settings[v].intervalDegrees =
            juce::jlimit (-7, 7, (int) std::lround (apvts.getRawParameterValue (id + "Degrees")->load()));
        settings[v].octaveOffset =
            juce::jlimit (-1, 1, (int) std::lround (apvts.getRawParameterValue (id + "Octave")->load()));
    }

    const auto origin = currentKeyOrigin();
    //  scaleIdFromCHOICE, not FromBus: origin.scale is an index into Auto Pitch's
    //  13-entry scaleChoices, while scaleIdFromBus understands only the 3-value
    //  bus code — every mode beyond major/minor/chromatic silently became Major.
    const auto scaleId = sattari::harmony::scaleIdFromChoice (origin.scale);
    const auto mode = (sattari::dsp::HarmonyMode)
        juce::jlimit (0, 2, (int) std::lround (apvts.getRawParameterValue ("harmonyMode")->load()));
    //  UI order is Intelligent/Interval/MIDI; the enum is interval/intelligent/midi.
    const auto realMode = mode == (sattari::dsp::HarmonyMode) 0 ? sattari::dsp::HarmonyMode::intelligent
                        : mode == (sattari::dsp::HarmonyMode) 1 ? sattari::dsp::HarmonyMode::interval
                                                                : sattari::dsp::HarmonyMode::midi;

    //  The current chord, from the analysed song model at the playhead. Absent a
    //  chord the planner falls back to the scale and says so via confidence.
    int pcs[4]; int numPcs = 0; double chordConf = 0.0;
    {
        const auto model = songAnalysisCopy();
        const double at = getSongPlayheadSeconds();
        for (const auto& cr : model.chordRegions)
        {
            if (at < cr.startSec || at >= cr.endSec) continue;
            for (int i = 0; i < 4; ++i)
                if (cr.chord.notes[(size_t) i] >= 0) pcs[numPcs++] = cr.chord.notes[(size_t) i];
            chordConf = cr.chord.confidence;
            break;
        }
    }

    //  MIDI mode needs the notes actually held. Without this it planned nothing
    //  while the UI still reported a plan — a mode that silently cannot work.
    int held[4]; int numHeld = 0;
    if (realMode == sattari::dsp::HarmonyMode::midi)
    {
        for (const auto& note : harmonyHeldMidi)
        {
            const int value = note.load (std::memory_order_relaxed);
            if (value >= 0) held[numHeld++] = value;
        }
    }

    const auto plan = harmonyPlanner.plan (out.leadMidi, realMode, settings, 4,
                                           origin.root, scaleId,
                                           numPcs > 0 ? pcs : nullptr, numPcs, chordConf,
                                           numHeld > 0 ? held : nullptr, numHeld);
    out.heldNotes = numHeld;
    for (int v = 0; v < 4; ++v)
    {
        out.active[(size_t) v] = plan.voices[(size_t) v].active;
        out.midi[(size_t) v]   = plan.voices[(size_t) v].midi;
        out.confidence = juce::jmax (out.confidence, plan.voices[(size_t) v].confidence);
    }
    out.fromChord = numPcs > 0;
    out.valid = true;
    return out;
}

std::vector<SattariAutoPitchAudioProcessor::HarmonyEditOverlay>
SattariAutoPitchAudioProcessor::getHarmonyEditOverlays() const
{
    const auto edits = getPitchEdits();
    std::vector<HarmonyEditOverlay> overlays;
    overlays.reserve (edits.size() * 4);
    if (edits.empty()) return overlays;

    sattari::dsp::HarmonyVoiceSettings settings[4];
    bool anySolo = false;
    for (int v = 0; v < 4; ++v)
    {
        const auto id = juce::String ("h") + juce::String (v + 1);
        anySolo = anySolo || apvts.getRawParameterValue (id + "Solo")->load() > 0.5f;
    }
    for (int v = 0; v < 4; ++v)
    {
        const auto id = juce::String ("h") + juce::String (v + 1);
        const bool solo = apvts.getRawParameterValue (id + "Solo")->load() > 0.5f;
        settings[v].enabled = apvts.getRawParameterValue (id + "Enable")->load() > 0.5f
                           && (! anySolo || solo);
        settings[v].role = (sattari::dsp::HarmonyRole) juce::jlimit (0, 6,
            (int) std::lround (apvts.getRawParameterValue (id + "Role")->load()));
        settings[v].intervalDegrees = juce::jlimit (-7, 7,
            (int) std::lround (apvts.getRawParameterValue (id + "Degrees")->load()));
        settings[v].octaveOffset = juce::jlimit (-1, 1,
            (int) std::lround (apvts.getRawParameterValue (id + "Octave")->load()));
    }

    const int uiMode = juce::jlimit (0, 2,
        (int) std::lround (apvts.getRawParameterValue ("harmonyMode")->load()));
    const auto mode = uiMode == 0 ? sattari::dsp::HarmonyMode::intelligent
                    : uiMode == 1 ? sattari::dsp::HarmonyMode::interval
                                  : sattari::dsp::HarmonyMode::midi;
    const auto model = songAnalysisCopy();
    const auto defaultOrigin = currentKeyOrigin();
    sattari::dsp::HarmonyVoicePlanner timelinePlanner;
    int held[4], numHeld = 0;
    if (mode == sattari::dsp::HarmonyMode::midi)
        for (const auto& note : harmonyHeldMidi)
        {
            const int value = note.load (std::memory_order_relaxed);
            if (value >= 0) held[numHeld++] = value;
        }

    for (const auto& edit : edits)
    {
        const double at = 0.5 * (edit.startSec + edit.endSec) - songSourceStartSeconds.load();
        int root = defaultOrigin.root, scaleChoice = defaultOrigin.scale;
        for (const auto& region : model.scaleRegions)
            if (at >= region.startSec && at < region.endSec)
            {
                root = region.root;
                scaleChoice = region.minor ? 1 : 0;
                break;
            }
        int pcs[4], numPcs = 0; double chordConfidence = 0.0;
        for (const auto& region : model.chordRegions)
            if (at >= region.startSec && at < region.endSec)
            {
                for (int i = 0; i < 4; ++i)
                    if (region.chord.notes[(size_t) i] >= 0)
                        pcs[numPcs++] = region.chord.notes[(size_t) i];
                chordConfidence = region.chord.confidence;
                break;
            }
        const auto plan = timelinePlanner.plan (
            edit.targetMidi, mode, settings, 4, root,
            sattari::harmony::scaleIdFromChoice (scaleChoice),
            numPcs > 0 ? pcs : nullptr, numPcs, chordConfidence,
            numHeld > 0 ? held : nullptr, numHeld);
        for (int v = 0; v < 4; ++v)
            if (plan.voices[(size_t) v].active)
                overlays.push_back ({ edit.id, v, edit.startSec + edit.timeOffsetSec,
                                      edit.endSec + edit.timeOffsetSec,
                                      plan.voices[(size_t) v].midi });
    }
    return overlays;
}

double SattariAutoPitchAudioProcessor::estimateIntentConfidence (const PitchFrame& frame) const noexcept
{
    if (! frame.voiced) return 0.0;

    //  Four independent reasons to doubt that the detected pitch is heading for
    //  the chosen target. Each is a 0..1 factor; they multiply, so any one of
    //  them alone can hold correction back. This is the "false corrections are
    //  more damaging than missed corrections" rule made numeric.

    //  A DELIBERATE target is not a guess. MIDI Target and captured Pitch Edits
    //  are the user naming the note, and those corrections are LARGE by design —
    //  the first version of this gate throttled them to 20% because it treated
    //  their size as evidence of doubt. Anything the user asked for explicitly
    //  bypasses the gate entirely.
    if (frame.objectActive
        || apvts.getRawParameterValue ("midiTarget")->load() > 0.5f)
        return 1.0;

    //  1. DISTANCE. Sitting a long way from the nearest legal note usually means
    //     the singer is between notes, not badly off one. Full trust within a
    //     quarter tone, easing to a floor rather than to zero — a semitone-off
    //     note is still a note, and refusing to correct it is its own failure.
    const double cents = std::abs (frame.correctionCents);
    const double distance = cents <= 50.0 ? 1.0
                          : juce::jlimit (0.55, 1.0, 1.0 - (cents - 50.0) / 110.0);

    //  2. DETECTOR CONFIDENCE. A weak pitch estimate is a weak basis for moving
    //     someone's voice at all.
    const double detector = juce::jlimit (0.0, 1.0, frame.confidence / 0.6);

    //  3. STABILITY. A pitch travelling fast is a slide, and the spec says to
    //     preserve those. But transitionBoost spikes at EVERY note change, so
    //     scaling by it directly under-corrected the attack of every single note
    //     and systematically weakened fast passages — the opposite of the intent.
    //     Only sustained motion counts, and even then it eases rather than cuts.
    const double motion = juce::jlimit (0.0, 1.0, (transitionBoost - 0.55) / 0.45);
    const double stability = 1.0 - 0.30 * motion;

    //  4. KEY TRUST. If the note set itself came from a low-confidence DETECTION,
    //     every target derived from it inherits that doubt. Crucially this must
    //     key off where the correction key ACTUALLY came from, not merely whether
    //     the AutoKey switch is on: a manual, bus, or song-map key is not made
    //     doubtful by an unrelated AutoKey estimate running alongside it.
    const auto origin = currentKeyOrigin();
    const double keyTrust = origin.source == KeyOrigin::Source::AutoKey
        ? juce::jlimit (0.35, 1.0, (double) origin.confidence / (double) kMinBusKeyConfidence)
        : 1.0;

    return juce::jlimit (0.0, 1.0, distance * detector * stability * keyTrust);
}

// ---------------------------------------------------------------------------
// Intent Engine v2 — shadow / active experimental targeting
// Strategy G correction dynamics (transitionBoost, hysteresis) are FROZEN.
// This function may only override frame.targetMidi and frame.correctionCents.
// ---------------------------------------------------------------------------
SattariAutoPitchAudioProcessor::PitchFrame
SattariAutoPitchAudioProcessor::applyIntentV2 (PitchFrame frame,
                                               int rootIndex,
                                               const juce::String& scaleName,
                                               double blockTimeSec)
{
    auto& s = intentState;
    s.prodTarget = (int) std::llround (frame.targetMidi);

    if (! frame.voiced)
    {
        // Unvoiced gap: reset history and dwell, keep stableTarget for persistence
        s.histFill = s.histPos = 0;
        s.resetDwell();
        s.lastVoicedPitch = -1.0;
        s.stableSec = 0.0;
        s.v2Target  = -1;
        s.reason    = IntentReasonCode::LOW_CONFIDENCE_FALLBACK;
        s.fallback  = false;
        s.motion.reset();
        s.lastMotion = {};
        return frame;
    }

    const double det  = frame.midi;
    const double conf = frame.confidence;
    s.pushPitch (det, blockTimeSec);

    // Phase 1+2 — continuous motion path: push raw frequencyHz, not rounded midi.
    // midiFloat = 69 + 12·log2(f/440) computed in double precision inside CME.
    if (frame.frequencyHz > 0.0)
        s.motion.push (frame.frequencyHz, blockTimeSec);
    // Bench-validated defaults: FirstDerivative @ 20ms gave best overall RMSE
    // (1.788 MIDI/sec) on the Phase 3 deterministic suite.
    s.lastMotion = s.motion.analyse();

    // Nearest in-scale candidate to current detected pitch
    const int nearNote = nearestScaleMidi (det, rootIndex, scaleName);
    s.incrementDwell (nearNote);

    const double svel  = s.smoothedVelocity();
    const bool   consistent = s.directionConsistent();
    const bool   oscillating = s.hasSignReversal();   // vibrato guard
    const double dir   = s.directionSign();
    const double dwellSec = s.getDwell (nearNote) * blockTimeSec;

    // ---- Decision tiers (mirrors Python v2 logic exactly) ----
    int    chosen = s.prodTarget;
    float  iconf  = (float) (conf * 0.6);
    IntentReasonCode rsn = IntentReasonCode::LOW_CONFIDENCE_FALLBACK;

    // 1a. SLIDE_DESTINATION — disabled (integer-MIDI path).
    //
    // Rationale: velocity derived from rounded MIDI produces ~20–30 MIDI/sec spikes
    // during any pitch transition, causing systematic scoop/fall overshoot in the
    // active audio bench.  The continuous-motion path (1b below) replaces this.

    // 1b. SLIDE_DESTINATION_V2 — Phase 4: continuous-motion estimator.
    //
    // Gate: kSlideDestinationV2Enabled remains false until MotionBench validates
    // velocity RMSE ≤ 2 MIDI/sec on the deterministic trajectory suite.
    //
    // When enabled, the estimator predicts the slide destination 80 ms ahead
    // and snaps to the nearest in-scale note — giving the correction engine
    // a trustworthy target before YIN fully converges.  All guards:
    //   · MotionBench validated (compile-time flag)
    //   · Not recognised as vibrato by the CME
    //   · Consistent directional history (integer-path cross-check)
    //   · Frame YIN confidence ≥ 0.60
    //   · Motion estimator agreement ≥ 0.65
    //   · Continuous velocity above slide threshold
    //   · No sign reversal in recent history
    //   · Destination is ≥ 1 semitone from current nearest and within ±4 semitones
    [&]() {
        if (! IntentV2State::kSlideDestinationV2Enabled) return;
        if (IntentV2State::kLivePitchEnabled)            return; // live pitch: suppress look-ahead
        if (s.lastMotion.isVibrato)  return;
        if (! consistent)            return;
        if (conf < 0.60)             return;
        if (s.lastMotion.confidence < 0.65) return;
        if (std::abs (s.lastMotion.velocityMidiPerSec) < 3.0) return;  // 3 MIDI/sec ≈ semitone/333ms
        if (oscillating)             return;

        constexpr double kExtrapolationSec = 0.080;
        constexpr double kMaxSlideDistance = 4.0;
        const double rawDest = s.lastMotion.midiFloat
                               + s.lastMotion.velocityMidiPerSec * kExtrapolationSec;
        const double bounded = juce::jlimit (det - kMaxSlideDistance,
                                              det + kMaxSlideDistance,
                                              rawDest);
        const int snap = nearestScaleMidi (bounded, rootIndex, scaleName);
        if (snap == nearNote || std::abs (snap - nearNote) < 1) return;

        chosen = snap;
        iconf  = (float) (s.lastMotion.confidence * 0.85);
        rsn    = IntentReasonCode::SLIDE_DESTINATION_V2;
    }();

    // 2–5: run only if tier 1b did not fire.
    if (rsn == IntentReasonCode::LOW_CONFIDENCE_FALLBACK)
    {
    // 2. Passing-tone suppressor: fast + short dwell + direction consistent + stable anchor known.
    // Directional escape guard: suppress stale-anchor hold when pitch is consistently moving
    // AWAY from stableTarget (e.g. ascending scale run starting from a previously-stable note).
    if (consistent
        && dwellSec < IntentV2State::kPassDwell
        && std::abs (svel) > 0.35
        && s.stableTarget != -1000
        && dir * (det - s.stableTarget) <= 0.5)  // pitch must not be departing the anchor
    {
        chosen = s.stableTarget;
        iconf  = 0.72f;
        rsn    = IntentReasonCode::PASSING_TONE;
    }
    // 3. Stability anchor
    else if (s.stableSec >= IntentV2State::kStabSec
             && conf >= 0.55
             && std::abs (svel) < 0.5)
    {
        const int candidate = nearestScaleMidi (det, rootIndex, scaleName);
        // Target persistence: resist switching while stable + slow motion
        if (s.stableTarget != -1000
            && s.stableTarget != candidate
            && std::abs (svel) < 1.2
            && s.stableSec < IntentV2State::kPersistSec * 3.0)
        {
            chosen = s.stableTarget;
            iconf  = 0.80f;
            rsn    = IntentReasonCode::VIBRATO_HOLD;
        }
        else
        {
            chosen = candidate;
            s.stableTarget = candidate;
            iconf  = (float) std::min (0.95, conf);
            rsn    = IntentReasonCode::STABILITY_ANCHOR;
        }
    }
    // 4. Known stable target + low velocity → hold
    else if (s.stableTarget != -1000 && std::abs (svel) < 0.8)
    {
        chosen = s.stableTarget;
        iconf  = (float) std::min (0.78, conf * 1.1);
        rsn    = IntentReasonCode::VIBRATO_HOLD;
    }
    // 5. Nearest scale note — also seeds stableTarget so stability accumulates
    else
    {
        chosen = nearNote;
        iconf  = (float) (conf * 0.70);
        if (iconf >= 0.50f)
        {
            rsn = IntentReasonCode::NEAREST_STABLE;
            s.stableTarget = nearNote;   // seed so stableSec can build up
        }
        else
        {
            // Low confidence → fall back to production target
            chosen = s.prodTarget;
            iconf  = (float) (conf * 0.50);
            rsn    = IntentReasonCode::LOW_CONFIDENCE_FALLBACK;
        }
    }
    } // end tiers 2–5

    // Update stability counter
    if (chosen == s.stableTarget && std::abs (svel) < 0.8)
        s.stableSec += blockTimeSec;
    else if (rsn != IntentReasonCode::PASSING_TONE && rsn != IntentReasonCode::VIBRATO_HOLD)
        s.stableSec = 0.0;

    s.lastVoicedPitch = det;
    s.v2Target   = chosen;
    s.intentConf = iconf;
    s.reason     = rsn;

    // CONFIDENCE SAFETY GATING
    // HIGH (≥0.68): v2 may drive audio
    // MEDIUM (≥0.50): require stability, vibrato-hold, or passing-tone evidence
    // LOW (<0.50): fall back to production
    constexpr float kHighConf   = 0.68f;
    constexpr float kMedConf    = 0.50f;

    const IntentMode mode = (IntentMode) intentMode.load (std::memory_order_relaxed);

    bool driving = false;
    if (mode == IntentMode::V2_ACTIVE)
    {
        if (iconf >= kHighConf)
        {
            driving = true;   // full v2 control
        }
        else if (iconf >= kMedConf
                 && (rsn == IntentReasonCode::STABILITY_ANCHOR
                     || rsn == IntentReasonCode::VIBRATO_HOLD
                     || rsn == IntentReasonCode::PASSING_TONE))
        {
            driving = true;   // conservative v2 ok with medium confidence
        }
        // LOW confidence: driving stays false → production target used
    }

    s.fallback = (mode == IntentMode::V2_ACTIVE) && !driving;

    if (driving)
    {
        if (chosen != (int) std::llround (frame.targetMidi))
            ++s.targetChanges;
        frame.targetMidi     = (double) chosen;
        frame.correctionCents = (frame.targetMidi - frame.midi) * 100.0;
    }

    return frame;
}

float SattariAutoPitchAudioProcessor::styleCorrectionStrength() const
{
    return styleCorrectionStrengthFor (apvts.getRawParameterValue ("amount")->load());
}

float SattariAutoPitchAudioProcessor::styleCorrectionStrengthFor (float amount) const
{
    const auto style = currentStyleChoice();
    amount = juce::jlimit (0.0f, 1.0f, amount);
    // Amount is a real 0..1 control: at 1.0 EVERY style corrects FULLY (in-tune),
    // so a user who cranks Amount is actually in tune. The style shapes the CURVE
    // (how gentle the low/mid of the knob is), not a hidden ceiling on max
    // correction. The gentler styles use a convex power curve so they stay
    // transparent at low Amount but still reach 1.0 at the top; the hard-tune
    // styles keep their eager linear-with-clip mapping (Modern Rap / Robot are the
    // golden-render path — left byte-for-byte unchanged).
    if (style == "Natural")
        return std::pow (amount, 2.0f);          // transparent knee, full at max
    if (style == "Classic")
        return std::pow (amount, 1.4f);
    if (style == "Modern Rap")
        return juce::jmin (1.0f, amount * 1.08f);
    if (style == "Robot")
        return juce::jmin (1.0f, amount * 1.25f);
    return std::pow (amount, 1.25f);             // Big Harmony + fallback
}

float SattariAutoPitchAudioProcessor::styleRetuneCoefficient() const
{
    return styleRetuneCoefficientFor (apvts.getRawParameterValue ("speed")->load());
}

float SattariAutoPitchAudioProcessor::styleRetuneCoefficientFor (float speed) const
{
    const auto style = currentStyleChoice();
    speed = juce::jlimit (0.05f, 1.0f, speed);
    if (style == "Natural")
        return juce::jlimit (0.03f, 0.32f, speed * 0.22f);
    if (style == "Classic")
        return juce::jlimit (0.04f, 0.46f, speed * 0.36f);
    if (style == "Modern Rap")
        return juce::jlimit (0.08f, 0.78f, speed * 0.74f);
    if (style == "Robot")
        return juce::jlimit (0.12f, 0.92f, speed * 0.9f);
    return juce::jlimit (0.05f, 0.58f, speed * 0.48f);
}

juce::String SattariAutoPitchAudioProcessor::currentStyleChoice() const
{
    const int styleIndex = juce::roundToInt (apvts.getRawParameterValue ("style")->load());
    return styleChoices[juce::jlimit (0, styleChoices.size() - 1, styleIndex)];
}

double SattariAutoPitchAudioProcessor::pitchScaleForFrame (const PitchFrame& frame) const
{
    if (! frame.voiced || apvts.getRawParameterValue ("enableCorrection")->load() < 0.5f)
        return 1.0;

    const bool classic = apvts.getRawParameterValue ("classicMode")->load() > 0.5f;

    // Do No Harm gating is bypassed in Classic Mode (the hard sound corrects
    // everything, including low-confidence frames - that grit is the point).
    if (! classic
        && apvts.getRawParameterValue ("doNoHarm")->load() > 0.5f
        && (frame.confidence < minConfidence || frame.rms < silenceRms))
        return 1.0;

    const float selectedAmount = frame.objectActive && frame.objectAmount >= 0.0f
        ? frame.objectAmount : apvts.getRawParameterValue ("amount")->load();
    double strength = (double) styleCorrectionStrengthFor (selectedAmount);

    //  INTENT GATE. Scale the correction by how confident we are that this note
    //  is heading where we think it is. Classic Mode is exempt by design — its
    //  whole identity is the unconditional hard pull, and a user asking for that
    //  sound is not asking for the engine's second thoughts.
    if (! classic)
    {
        const double intent = estimateIntentConfidence (frame);
        intentConfidenceUi.store ((float) intent, std::memory_order_relaxed);
        //  Never below 0.2 of the requested amount: the gate exists to soften a
        //  doubtful correction, not to make Amount silently stop working.
        strength *= juce::jlimit (0.2, 1.0, intent);
    }
    else
    {
        intentConfidenceUi.store (1.0f, std::memory_order_relaxed);
    }

    if (classic)
    {
        // Always-on hard pull. Strength clamped high so even in-tune notes get
        // snapped to centre - the Auto-Tune 5 behavioural signature.
        strength = 1.0;
    }
    else
    {
        // Flex-Tune: inside a cents "flex zone" around the target, expressive
        // pitch passes through (strength scales down toward the centre); outside
        // it, full correction. Quadratic so the release feels natural, not gated.
        const double flex = (double) apvts.getRawParameterValue ("flexTune")->load();
        if (flex > 0.0001)
        {
            const double errCents = std::abs (frame.correctionCents);
            const double zone     = flex * 45.0;        // up to ±45 cents of freedom
            if (errCents < zone)
            {
                const double t = errCents / zone;        // 0 at perfect, 1 at zone edge
                strength *= t * t;                       // soft engage
            }
        }
    }

    // Scale correction stays conservative, while explicit MIDI/graph targets may
    // request a full octave. The shifters are already bounded to 0.5..2.0.
    const bool exactTarget = apvts.getRawParameterValue ("midiTarget")->load() > 0.5f
                          || apvts.getRawParameterValue ("usePitchEdits")->load() > 0.5f;
    const double limit = exactTarget ? 1200.0 : 700.0;
    const double limitedCents = juce::jlimit (-limit, limit, frame.correctionCents * strength);
    return std::pow (2.0, limitedCents / 1200.0);
}

void SattariAutoPitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
#if JucePlugin_Enable_ARA
    // When bound by an ARA-capable host this replaces the ordinary insert input
    // with the host-managed source regions; the existing tuning DSP then runs on
    // that audio, so ARA and insert mode stay sonically identical.
    processBlockForARA (buffer, isRealtime(), getPlayHead());
#endif
    updateMidiTargets (midi);
    midiLearn.processMidi (midi);

    // Refresh the offline flag FIRST, before any sidechain/chroma work — pushSidechainBlock
    // -> runChromaAnalysis reads offlineMode to decide sync-vs-async ONNX, so a stale value
    // (e.g. still true from a just-finished bounce) would run synchronous inference on the
    // realtime thread for the first playback block. Computed once here and reused below.
    const bool offline = isNonRealtime();
    offlineMode.store (offline, std::memory_order_relaxed);

    // Low-latency toggle: swap in the message-thread-built PSOLA set (O(1), no alloc).
    // The old shifters stay in pendingPsola and are freed on the next message-thread
    // rebuild, so nothing is deallocated on the audio thread.
    //  The flag is also the OWNERSHIP token for pendingPsola: while it is set, the
    //  audio thread owns that vector. Clearing it with exchange() up front released
    //  ownership before the swap had happened, so handleAsyncUpdate could observe
    //  (flag clear, psolaLiveLowLat not yet updated) and call preparePsolaInto on
    //  pendingPsola while this thread was still swapping it - a data race on the
    //  shifter vector. Read it here, and clear it only after the swap and the
    //  psolaLiveLowLat publication are both complete.
    if (psolaSwapReady.load (std::memory_order_acquire))
    {
        psola.swap (pendingPsola);
        for (auto& p : psola) p.reset();
        psolaLiveLowLat.store (pendingLowLat, std::memory_order_release);
        psolaSwapReady.store (false, std::memory_order_release);
        // If the param moved again while this build was pending, request another rebuild
        // so the live shifter converges to the current Low Latency state.
        const bool wantLowLat = apvts.getRawParameterValue ("lowLatency") != nullptr
                             && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
        if (wantLowLat != pendingLowLat)
            triggerAsyncUpdate();
    }

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // Bus-aware: main input/output is bus 0, sidechain (optional) is bus 1.
    // The monophonic vocal pitch tracker only sees the main input; the sidechain
    // is read-only and feeds the polyphonic-AutoKey chroma analyser.
    auto mainBuffer = getBusBuffer (buffer, true, 0);
    const int numChannels = mainBuffer.getNumChannels();
    if (numChannels <= 0)
        return;

    // The dry reference must sit at the ACTIVE engine's delay, and Live Mode can be
    // toggled while the transport rolls. Re-read the tap here, before the ring is
    // written, so the very first block after a toggle is already aligned. The ring
    // was sized in prepareToPlay for the larger of the two engines, so this only
    // ever moves a read offset — no allocation on the audio thread.
    {
        const int wanted = liveModeActive() ? liveEngineLatencySamples()
                                            : printEngineLatencySamples();
        if (wanted != dryDelaySamples && dryDelayRingSize > 0)
            dryDelaySamples = juce::jlimit (0, dryDelayRingSize - 1, wanted);
    }

    // Capture the untouched insert input before analysis or any DSP mutates it.
    // The reference ring runs continuously so Correction Off can become a true,
    // sample-identical bypass without changing the plugin's declared latency.
    renderDelayedDryReference (mainBuffer);
    // Keep an untouched vocal source for harmony. Pitching an already-corrected
    // lead again compounds formant/phase processing; every generated singer is
    // therefore rendered once from the original insert input and aligned later.
    if (numSamples <= harmonyScratch.getNumSamples())
        for (int ch = 0; ch < juce::jmin (numChannels, harmonyScratch.getNumChannels()); ++ch)
            harmonyScratch.copyFrom (ch, 0, mainBuffer, ch, 0, numSamples);

    const bool autoKeyOn = apvts.getRawParameterValue ("autoKey")->load() > 0.5f;
    const bool useKeyMap = apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f;
    const bool followSongMapOn = apvts.getRawParameterValue ("followSongMap")->load() > 0.5f;
    double hostTimeSeconds = -1.0;
    bool chromaSourceActiveThisBlock = false;
    bool sidechainHasAudioThisBlock = false;

    if (! autoKeyOn)
    {
        sidechainHoldSamples = 0;
        sidechainSeenAudio = false;
    }

    // Key Map: when on and the host gives us a playhead position, the live
    // correction key follows the offline-scanned map. This is how a single
    // Auto Pitch instance keeps the vocal in the right key as the song
    // modulates - no per-section automation needed.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            // Cache host tempo for the cross-plugin session bus (read by the
            // editor timer, published off the audio thread).
            if (auto bpm = pos->getBpm())
                hostBpmForBus.store (*bpm);
            if (auto t = pos->getTimeInSeconds())
            {
                hostTimeSeconds = *t;
                // Always publish the playhead so the editor's Key Map timeline can
                // draw a position marker even when Use Key Map is off.
                keyMapPlayheadSec.store (*t, std::memory_order_relaxed);
                //  Either time-aware source arms the follower. Gating this on
                //  useKeyMap alone made Follow Song Map a dead automatable
                //  parameter — the exact defect class this project already had
                //  to fix once with liveEngine/lowLatency.
                if (useKeyMap || followSongMapOn)
                    applyKeyMapAtSeconds (*t);
                updateHarmonyChordAtSeconds (*t);
            }
        }

    // Pull sidechain (master) audio if the bus is enabled. When AutoKey is on,
    // route a mono sum into the chroma analyser. This is how AutoKey "listens to
    // the whole master track" — the user sidechains the master bus into us.
    if (autoKeyOn && getBusCount (true) > 1)
    {
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto scBuffer = getBusBuffer (buffer, true, 1);
            const int scCh = scBuffer.getNumChannels();
            if (scCh > 0)
            {
                // scMonoScratch is sized to preparedBlockSize in prepareToPlay. If a
                // host ever hands us a block larger than it declared, clamp to the
                // scratch capacity instead of reallocating on the audio thread (the
                // old assign() here was a real-time allocation). An oversized block's
                // tail is dropped from the SLOW chroma analysis only — inaudible; the
                // poly/crepe paths below already guard on the same capacity.
                const int scN = juce::jmin (numSamples, (int) scMonoScratch.size());
                float energy = 0.0f;
                for (int i = 0; i < scN; ++i)
                {
                    float m = 0.0f;
                    for (int ch = 0; ch < scCh; ++ch)
                        m += scBuffer.getSample (ch, i);
                    m /= (float) scCh;
                    scMonoScratch[(size_t) i] = m;
                    energy += m * m;
                }
                if (energy > 1.0e-6f)
                {
                    sidechainHasAudioThisBlock = true;
                    chromaSourceActiveThisBlock = true;
                }
                pushSidechainBlock (scMonoScratch.data(), scN);
            }
        }
    }

    // Polyphonic: when ON, correction is done by the per-peak phase-locked
    // vocoder (processPolyphonicRetune below) reading mainBuffer directly.
    const bool polyOn = apvts.getRawParameterValue ("polyphonic")->load() > 0.5f;

    // Polyphonic AutoKey with NO sidechain: the material being tuned lives on the
    // MAIN input, so feed IT into the chroma / neural-key analyser. Without this,
    // AutoKey on a dense instrumental degrades to monophonic pitch-tracking of the
    // mix and mis-detects the key (measured A# vs the true G on a real track), so the
    // per-peak retune snaps toward the WRONG scale. Feeding the main input lets the
    // neural key model read the actual harmony. Gated to poly, so a monophonic vocal
    // (main = single voice) still uses the pitch-histogram detector unchanged.
    const bool haveLiveSidechain = getBusCount (true) > 1
        && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled()
        && sidechainHasAudioThisBlock;
    if (autoKeyOn && polyOn && ! haveLiveSidechain)
    {
        const int n = juce::jmin (numSamples, (int) scMonoScratch.size());
        float energy = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                m += mainBuffer.getSample (ch, i);
            m /= (float) numChannels;
            scMonoScratch[(size_t) i] = m;
            energy += m * m;
        }
        if (energy > 1.0e-6f)
            chromaSourceActiveThisBlock = true;
        pushSidechainBlock (scMonoScratch.data(), n);
    }

    // Prefer full-mix chroma briefly after real analysis audio, then expire. The old
    // one-way boolean permanently disabled the monophonic histogram after a sidechain
    // was muted or disconnected, leaving AutoKey stuck on its last full-mix verdict.
    if (chromaSourceActiveThisBlock)
        sidechainHoldSamples = juce::jmax (sidechainHoldSamples,
                                           (int) std::round (currentSampleRate * 0.75));
    else
        sidechainHoldSamples = juce::jmax (0, sidechainHoldSamples - numSamples);
    sidechainSeenAudio = autoKeyOn && sidechainHoldSamples > 0;

    // Computed once per block: neural pitch feeds BOTH the correction override
    // (below) and the AutoKey histogram (in the loop) when a CREPE model is active.
    const bool neuralOn   = apvts.getRawParameterValue ("neuralPitch")->load() > 0.5f;
    const bool neuralHist = neuralOn && crepeModelPresent;

    // Live Mode owns a causal tracker inside LivePitchEngine. Running the Studio
    // YIN pass as well duplicated the dominant analysis work and missed the
    // 48 kHz / 64-sample callback deadline. Neural fusion still needs the Studio
    // frame, so only the deterministic YIN-only Live path elides the duplicate.
    const bool useLiveTrackerOnly = liveModeActive() && ! polyOn && ! neuralHist;
    if (! useLiveTrackerOnly)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
                mono += mainBuffer.getSample (channel, i);
            pushPitchSample (mono / (float) numChannels);

            if (--pitchSamplesUntilAnalysis <= 0)
            {
                auto detected = analyseCurrentPitchFrame();
                // The mono histogram and the Key Map both write the detected-key
                // latch. While the map drives, updating the histogram here would
                // clobber the map with the singer's own pitch.
                if (! keyMapDriving() && ! sidechainSeenAudio)
                    updateAutoKeyHistogram (detected);
                lastPitchFrame = buildCorrectionFrame (detected);
                // Intent Engine v2: override targetMidi when mode != PRODUCTION
                if (intentMode.load (std::memory_order_relaxed) != 0)
                {
                    const int root = currentCorrectionRootIndex();
                    const int sc   = juce::roundToInt (apvts.getRawParameterValue ("scale")->load());
                    const auto sn  = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, sc)];
                    const double bts = (double) pitchHopSize / currentSampleRate;
                    lastPitchFrame = applyIntentV2 (lastPitchFrame, root, sn, bts);
                }
                pitchSamplesUntilAnalysis = pitchHopSize;
            }
        }
    }

    // Neural pitch (CREPE) fusion: the CNN refines an agreeing YIN estimate and
    // can rescue high-confidence breathy/noisy frames that YIN declines. It does
    // not blindly override YIN: conflicting or octave-displaced estimates are
    // rejected by the conservative shared fusion state below.
    if (neuralHist && (int) scMonoScratch.size() >= numSamples)
    {
        // Audio thread: only feed the ring (lock-free) — the worker thread runs
        // the ONNX inference and publishes its result into the atomics we read.
        for (int i = 0; i < numSamples; ++i)
        {
            float m = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) m += mainBuffer.getSample (ch, i);
            scMonoScratch[(size_t) i] = m / (float) numChannels;
        }
        crepe.pushBlock (scMonoScratch.data(), numSamples);

        // Offline (host bounce): the worker can't keep up, so run the ONNX inference
        // synchronously here — otherwise the neural detector silently degrades to YIN
        // on a render. Realtime path is unchanged (reads the worker's result atomics).
        // `offline` was computed at the top of the block (before any chroma work).
        float crHz, crConf; bool crVoiced;
        if (offline)
        {
            const std::lock_guard<std::mutex> lk (crepeMutex);
            const auto r = crepe.analyse (0.5f);
            crHz = r.frequencyHz; crConf = r.confidence; crVoiced = r.voiced;
        }
        else
        {
            crHz      = crepeResultHz.load (std::memory_order_relaxed);
            crConf    = crepeResultConf.load (std::memory_order_relaxed);
            crVoiced  = crepeResultVoiced.load (std::memory_order_relaxed);
        }
        lastCrepeHz = crHz;
        lastCrepeConfidence = crConf;

        double rMinHz = minVoiceHz, rMaxHz = maxVoiceHz;
        voicedRangeHzForSource (rMinHz, rMaxHz);
        const bool neuralInRange = crVoiced && crHz > 0.0f
                                && (double) crHz >= rMinHz && (double) crHz <= rMaxHz
                                && lastPitchFrame.rms >= silenceRms;
        const sattari::dsp::MonophonicPitchEstimate yinEstimate {
            lastPitchFrame.frequencyHz, lastPitchFrame.confidence, lastPitchFrame.voiced
        };
        const sattari::dsp::MonophonicPitchEstimate neuralEstimate {
            neuralInRange ? (double) crHz : 0.0,
            neuralInRange ? (double) crConf : 0.0,
            neuralInRange
        };
        const auto fused = pitchFusion.fuse (yinEstimate, neuralEstimate, currentReferenceHz());
        if (fused.voiced)
        {
            PitchFrame nf = lastPitchFrame;
            nf.frequencyHz = fused.frequencyHz;
            nf.midi        = midiFromHz (fused.frequencyHz, currentReferenceHz());
            nf.confidence  = fused.confidence;
            nf.voiced      = true;
            lastPitchFrame = buildCorrectionFrame (nf);
        }
        else
        {
            lastPitchFrame.voiced = false;
        }
    }
    else if (! neuralHist)
    {
        pitchFusion.reset();
    }

    const double blockDuration = (double) numSamples / juce::jmax (1.0, currentSampleRate);
    if (apvts.getRawParameterValue ("capturePitchEdits")->load() > 0.5f)
        capturePitchEditAtSeconds (hostTimeSeconds, blockDuration, lastPitchFrame);
    if (apvts.getRawParameterValue ("usePitchEdits")->load() > 0.5f)
        applyPitchEditAtSeconds (hostTimeSeconds, lastPitchFrame);

    // Polyphonic mode retunes per-peak in the spectral vocoder below, so the mono
    // single-ratio smoother is held at unity and left unused in that mode.
    const bool correctionOn = apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f;
    double targetPitchScale = 1.0;
    if (correctionOn)
    {
        targetPitchScale = polyOn ? 1.0
                                  : pitchScaleForFrame (lastPitchFrame);
    }
    else
    {
        // A user-facing Correction Off switch should be immediate; reset the mono
        // smoothers so no stale correction ratio coasts through after the toggle.
        smoothedPitchScale = 1.0;
        lastFedPitchScale = 1.0;
    }

    // --- Note-transition retune modeling -------------------------------------
    // Detect a target-note change. On a change, spike transitionBoost so the
    // engine retunes FAST to catch the new note, then decay so the sustain
    // retune is SLOW (letting vibrato / natural drift survive). This per-note
    // speed shaping is the core of a natural Auto-Tune "feel".
    const bool classicMode = apvts.getRawParameterValue ("classicMode")->load() > 0.5f;
    const double transition = (double) (lastPitchFrame.objectActive && lastPitchFrame.objectTransition >= 0.0f
        ? lastPitchFrame.objectTransition : apvts.getRawParameterValue ("transition")->load());

    bool targetNoteChanged = false;
    const double blockTimeSec = (double) numSamples / currentSampleRate;
    if (lastPitchFrame.voiced)
    {
        const int tgt = (int) std::llround (lastPitchFrame.targetMidi);
        if (tgt != lastTargetMidiForTransition)
        {
            targetNoteChanged = true;
            // Clamp the boost when the same two targets alternate rapidly
            // (< 150 ms between changes). This prevents vibrato oscillating
            // across a scale-note boundary from spiking transitionBoost=1.0
            // on every crossing and producing audible stepping artifacts.
            //
            // This complements the intent gate's stability factor, which reads
            // transitionBoost: with the spike clamped at the source, sustained
            // motion is what remains, which is exactly what that factor wants.
            const bool isRapidAlternation = (tgt == hysteresisAltTarget)
                                         && (hysteresisAge < 0.150);
            transitionBoost = isRapidAlternation ? std::min (transitionBoost, 0.30) : 1.0;
            hysteresisAltTarget        = lastTargetMidiForTransition;
            hysteresisAge              = 0.0;
            lastTargetMidiForTransition = tgt;
            if (! isRapidAlternation)
                sessionTransientId.fetch_add (1);  // pulse for the cross-plugin bus
            addVibratoRampSamples = 0.0;           // synthetic vibrato blooms afresh on each note
        }
    }
    hysteresisAge += blockTimeSec;
    // Decay the boost once per block (~time constant 60 ms).
    transitionBoost *= std::exp (-blockTimeSec / 0.060);

    // --- Vibrato preservation -------------------------------------------------
    // Correct the note CENTRE to the scale, but re-inject a fraction of the
    // singer's fast pitch oscillation so hard tuning doesn't flatten expressive
    // vibrato. vibratoCenterMidi is a slow low-pass of the detected pitch (tracks
    // the note centre, lags 5-7 Hz vibrato); the residual (detected - centre) is
    // the vibrato we keep. Reset on note onset so new notes still snap; disabled
    // in Classic Mode (the deliberate dead-flat sound).
    const double vibRetain = (double) (lastPitchFrame.objectActive && lastPitchFrame.objectVibrato >= 0.0f
        ? lastPitchFrame.objectVibrato : apvts.getRawParameterValue ("vibrato")->load());
    if (vibRetain > 0.001 && lastPitchFrame.voiced && ! classicMode && ! polyOn)
    {
        if (vibratoCenterMidi < -900.0 || transitionBoost > 0.6)
            vibratoCenterMidi = lastPitchFrame.midi;         // arm / re-snap on onset
        const double blockTime = (double) numSamples / currentSampleRate;
        const double a = 1.0 - std::exp (-blockTime / 0.120);
        vibratoCenterMidi += a * (lastPitchFrame.midi - vibratoCenterMidi);

        // Keep a fraction of the vibrato swing (clamped to +/-1 semitone so note
        // jumps / detector errors aren't amplified), faded out during onsets.
        const double vibSemis = juce::jlimit (-1.0, 1.0, lastPitchFrame.midi - vibratoCenterMidi);
        // CREPE already applies a causal median contour before this stage. Its
        // correction path therefore leaves more of the original fast modulation
        // intact than raw YIN; reinjecting the full YIN-calibrated residual can
        // exaggerate neural vibrato to ~150% of the singer's depth. Restore only
        // the measured missing fraction on that path.
        const double keep = vibRetain * (1.0 - transitionBoost) * vibSemis;
        targetPitchScale *= std::pow (2.0, keep / 12.0);
        targetPitchScale = juce::jlimit (0.5, 2.0, targetPitchScale);
    }
    else
    {
        vibratoCenterMidi = -1000.0;   // disarmed when off
    }

    // R3's analysis window has a finite response time. On the intentionally
    // dead-flat Classic path, offset only the MOVING part of the requested curve
    // so fast vibrato is cancelled at the correct phase. Steady correction remains
    // mathematically unchanged, and fresh note transitions bypass compensation so
    // a melodic jump cannot overshoot. The extra term is bounded to 75 cents.
    //
    // RE-CALIBRATED 2026-08-20 to -20 ms. The original -30 ms was chosen on ONE
    // controlled 5.5 Hz / +/-40-cent synthetic tone, where it is genuinely best
    // (4.0 cents remaining vs 5.1 at -20). On the six-clip REAL corpus it is not:
    // over-leading pushes the correction past the target on material whose
    // vibrato rate and depth differ from the calibration tone, and on the heavy
    // vibrato clip it made Hard mode correct LESS than Natural — a visible
    // inversion of what the control means.
    //
    //   Hard-mode cents-to-scale, median, by offset:
    //     clip             0ms   -10   -15   -20   -30
    //     slay-demons     16.2  12.1   9.4   7.4   8.7
    //     guidos-pizza    11.5   7.6   5.7   4.2   4.6
    //     dickey-prairie   8.4   6.4   6.3   6.1   6.3
    //     vs-belt-m6       5.8   4.4   3.7   2.7   2.3
    //     vs-vibrato-f5   18.5  14.6  13.7  14.1  16.7
    //     vs-melisma-m3   21.3  15.6  13.3  11.1  11.4
    //
    // -20 wins 4 of 6 outright and wins on both mean (7.60 vs 8.33) and median
    // (6.75 vs 7.50), while still passing the controlled release gate with margin
    // (5.1 ct against its 6.9 ct limit). -15 scores better on the vibrato clip
    // alone but FAILS that gate at 7.3, so it is rejected: tuning to the one clip
    // that exposed the bug would repeat the original error inverted.
    //  BUFFER INDEPENDENCE. targetPitchScale is a pure function of lastPitchFrame
    //  here (the vibrato-retention term, the intent gate and Flex-Tune are all
    //  disabled for classic, and Humanize is applied later), and lastPitchFrame
    //  only refreshes once per pitchHopSize = 256 samples. Dividing the cents
    //  delta by the HOST BLOCK time therefore inflated the velocity by
    //  hop/blockSize at any buffer below the hop: at 64 samples three blocks in
    //  four saw delta = 0 and contributed no lead at all, while the fourth
    //  saturated the +/-75 ct clamp - the compensation degenerated into a clipped
    //  step once per hop, and the sweep above (measured at 512) did not describe
    //  what a live-performance buffer produced. Measure the velocity across the
    //  interval in which the frame actually moved, and hold the resulting lead
    //  between frames so every block gets the same continuous lead.
    if (classicMode && lastPitchFrame.voiced)
    {
        const double rawCents = 1200.0 * std::log2 (juce::jmax (1.0e-6, targetPitchScale));
        const double blockTime = (double) numSamples / juce::jmax (1.0, currentSampleRate);
        classicControlElapsedSec += blockTime;

        if (classicControlValid && ! targetNoteChanged)
        {
            //  A new analysis frame has landed only when the cents actually moved.
            //  Recompute the lead then, over the true elapsed interval; otherwise
            //  keep applying the lead already established for this glide.
            if (std::abs (rawCents - classicControlPrevCents) > 1.0e-9)
            {
                const double velocityCentsPerSec = (rawCents - classicControlPrevCents)
                                                 / juce::jmax (1.0e-6, classicControlElapsedSec);
                classicControlLeadCents = juce::jlimit (-75.0, 75.0,
                                                        velocityCentsPerSec * classicControlOffsetSeconds);
                classicControlPrevCents = rawCents;
                classicControlElapsedSec = 0.0;
            }
            targetPitchScale = std::pow (2.0, (rawCents + classicControlLeadCents) / 1200.0);
            targetPitchScale = juce::jlimit (0.5, 2.0, targetPitchScale);
        }
        else
        {
            //  First voiced block, or a new target note: start the interval clean
            //  so the first velocity is not measured against a stale timestamp.
            classicControlPrevCents = rawCents;
            classicControlElapsedSec = 0.0;
            classicControlLeadCents = 0.0;
        }
        classicControlValid = true;
    }
    else
    {
        classicControlValid = false;
        classicControlElapsedSec = 0.0;
        classicControlLeadCents = 0.0;
    }

    // Sustain retune from the Style/Speed control; onset retune is much faster.
    const float selectedRetune = lastPitchFrame.objectActive && lastPitchFrame.objectRetune >= 0.0f
        ? lastPitchFrame.objectRetune : apvts.getRawParameterValue ("speed")->load();
    const double sustainRetune = (double) styleRetuneCoefficientFor (selectedRetune);
    const double onsetRetune   = juce::jlimit (0.0, 1.0, sustainRetune + 0.55);
    // Blend by the boost, scaled by the Transition amount. Classic Mode forces
    // near-instant retune regardless (the hard snap).
    double retune = classicMode
        // Rubber Band is most accurate on a moving vocal when the ratio is
        // allowed to settle over a few analysis hops. A 0.95 block coefficient
        // chased every YIN fluctuation and produced metallic chatter; 0.75 is
        // still a sub-20 ms hard snap at normal buffer sizes without that buzz.
        ? 0.75
        : sustainRetune + transition * transitionBoost * (onsetRetune - sustainRetune);
    retune = juce::jlimit (0.01, 0.98, retune);

    // Time-normalize the retune blend, like the sibling smoothers (transition /
    // vibrato / humanize all use 1 - exp(-blockTime/tau)). `retune` is calibrated
    // as the per-block blend fraction AT the reference block: 512 samples @ the
    // 48 kHz golden SR. We convert it to a time constant tau and re-derive the
    // per-block coefficient from the ACTUAL blockTime, so the correction speed is
    // both buffer- and SR-independent. Previously the raw fraction was applied
    // once per block, so the effective time-constant swung ~30x with host buffer
    // size (a 64-sample block corrected ~8x faster than a 512-sample block).
    //   tau : 1 - exp(-refBlockTime/tau) == retune  =>  tau = -refBlockTime / ln(1-retune)
    // At 512@48k, blockTime == refBlockTime, so the coefficient is exactly `retune`
    // again — the golden render (512@48k) is bit-for-bit unchanged.
    const double refBlockTime   = 512.0 / 48000.0;
    const double blockTime      = (double) numSamples / juce::jmax (1.0, currentSampleRate);
    const double retuneTau      = -refBlockTime / std::log (juce::jmax (1.0e-6, 1.0 - retune));
    const double retunePerBlock = 1.0 - std::exp (-blockTime / retuneTau);
    smoothedPitchScale += (targetPitchScale - smoothedPitchScale) * retunePerBlock;
    smoothedPitchScale = juce::jlimit (0.5, 2.0, smoothedPitchScale);

    // Humanize: slow random walk between fresh target cent-offsets, low-pass
    // filtered toward the current target. New target every ~200 ms (humans
    // don't drift faster than that), max ±25 cents at humanize=1. Applied
    // multiplicatively on the smoothed pitch scale so the natural vocal vibrato
    // shape stays intact - we're just biasing the centre of tuning.
    double liveHumanizeCents = 0.0;
    const float humanize = lastPitchFrame.objectActive && lastPitchFrame.objectHumanize >= 0.0f
        ? lastPitchFrame.objectHumanize : apvts.getRawParameterValue ("humanize")->load();
    if (humanize > 0.0001f)
    {
        if (--humanizeBlocksUntilNewTarget <= 0)
        {
            humanizeTargetCents = (humanizeRng.nextDouble() * 2.0 - 1.0) * 25.0;
            const double blocksPer200ms = (currentSampleRate * 0.2) / juce::jmax (1, numSamples);
            humanizeBlocksUntilNewTarget = juce::jmax (1, (int) std::round (blocksPer200ms));
        }
        // One-pole toward the target, time constant ~80 ms.
        const double humanizeBlockTime = (double) numSamples / currentSampleRate;
        const double alpha = 1.0 - std::exp (-humanizeBlockTime / 0.080);
        humanizeSmoothedCents += alpha * (humanizeTargetCents - humanizeSmoothedCents);
        const double centsApplied = humanizeSmoothedCents * (double) humanize;
        liveHumanizeCents = centsApplied;
        smoothedPitchScale *= std::pow (2.0, centsApplied / 1200.0);
        smoothedPitchScale = juce::jlimit (0.5, 2.0, smoothedPitchScale);
    }
    else
    {
        humanizeSmoothedCents *= 0.95;   // drift back to zero when off
    }

    // Glitch guard: Rubber Band phase-glitches (audible click) when the pitch
    // ratio steps too hard between blocks. A large target jump (a melodic leap,
    // or a stray detector octave-blip) combined with fast retune would move the
    // ratio many semitones in a single block. Cap the per-block change in the
    // log domain so such jumps ramp over a couple of blocks instead of stepping.
    // 120 semitones/sec = a full octave in 100 ms (a 2-semitone note-jump in
    // ~17 ms), so hard-tune still feels instant while the jaggedness that makes
    // Rubber Band click is smoothed away.
    {
        const double maxSemisPerSec   = 120.0;
        const double glitchBlockTime  = (double) numSamples / juce::jmax (1.0, currentSampleRate);
        const double maxLog2Step      = (maxSemisPerSec * glitchBlockTime) / 12.0;
        const double desiredLog2       = std::log2 (juce::jmax (1.0e-6, smoothedPitchScale / juce::jmax (1.0e-6, lastFedPitchScale)));
        const double clampedLog2        = juce::jlimit (-maxLog2Step, maxLog2Step, desiredLog2);
        smoothedPitchScale = juce::jlimit (0.5, 2.0, lastFedPitchScale * std::pow (2.0, clampedLog2));
        lastFedPitchScale  = smoothedPitchScale;
    }

    // --- Synthetic vibrato (addVibrato) --------------------------------------
    // ADD an LFO on top of the corrected pitch (a generated vibrato, distinct
    // from the "Preserve Vibrato" of the singer above). It is layered onto a
    // LOCAL copy of the correction scale (engineScale) — never the smoother state
    // itself — so the 5.5 Hz oscillation cannot feed back into the correction
    // loop. cents = depth * ramp * sin(phase); ramp blooms 0->1 over the onset ms
    // from each note onset. Depth 0 is a strict no-op (engineScale == the correction
    // scale unchanged), keeping the golden render bit-identical. Skipped in the
    // poly path (which has its own per-note engine and ignores this scalar).
    double engineScale = smoothedPitchScale;
    double syntheticVibratoCents = 0.0;
    const double addVibDepth = (double) apvts.getRawParameterValue ("addVibrato")->load();   // cents
    if (addVibDepth > 0.0001 && lastPitchFrame.voiced && ! polyOn)
    {
        const double rate  = (double) apvts.getRawParameterValue ("vibratoRate")->load();    // Hz
        const double onset = (double) apvts.getRawParameterValue ("vibratoOnset")->load();   // ms
        const double onsetSamples = juce::jmax (1.0, onset * 0.001 * currentSampleRate);
        const double ramp  = juce::jlimit (0.0, 1.0, addVibratoRampSamples / onsetSamples);
        const double cents = addVibDepth * ramp * std::sin (addVibratoPhase);
        syntheticVibratoCents = cents;
        engineScale *= std::pow (2.0, cents / 1200.0);
        engineScale  = juce::jlimit (0.5, 2.0, engineScale);

        // Advance the LFO phase + onset ramp by exactly this block.
        addVibratoPhase += juce::MathConstants<double>::twoPi * rate * (double) numSamples / currentSampleRate;
        if (addVibratoPhase > juce::MathConstants<double>::twoPi)
            addVibratoPhase -= juce::MathConstants<double>::twoPi;
        addVibratoRampSamples += (double) numSamples;
    }

    // Input drive and output trim are kept SEPARATE. Input gain feeds the pitch
    // engine and the Style stage — turning it up is meant to push the explicit Robot
    // saturator and the harmony blend harder (it is a drive control). Output gain
    // is a clean level trim applied AFTER the Style stage (below), so raising output
    // level never drives the saturators/harmony into extra distortion — it just sets
    // how hot the block leaves the plugin, bounded by the final soft ceiling.
    const float inputDb   = apvts.getRawParameterValue ("inputGain")->load();
    const float outputDb  = apvts.getRawParameterValue ("outputGain")->load();
    const float inputGain  = juce::Decibels::decibelsToGain (inputDb);
    const float outputGain = juce::Decibels::decibelsToGain (outputDb);

    // Quality lock. Legacy sessions still deserialize Live/Low-Latency values,
    // but the current PSOLA path failed real-vocal validation (worse pitch error
    // plus grain). Keep the parameters for compatibility while always rendering
    // monophonic correction through the proven print engine.
    const bool useLiveEngine = false;

    // Engine swapped since last block: flush BOTH paths so the switch is clean. The
    // OUTGOING engine still holds ~one pipeline of already-processed audio (the RB
    // FIFO ~= the start-delay + cushion, or the PSOLA output ring) — without a flush
    // that stale audio would either be replayed on a later switch back or leave the
    // INCOMING engine to re-prime while the block seam splices across a phase
    // discontinuity (an audible ~28 ms dropout / click). Resetting both makes the
    // incoming engine re-prime cleanly from silence (its reported start-up latency)
    // and guarantees the outgoing engine is empty for its next turn. Allocation-free.
    //  Live Mode swapped in or out: flush it and the print pipeline for the same
    //  reason the legacy swap below does. The outgoing engine still holds a pipeline
    //  of processed audio, and the incoming one must re-prime from silence rather
    //  than splice across a phase discontinuity. Allocation-free.
    const bool liveNow = liveModeActive();
    const int liveProfileNow = extremeLiveProfileActive() ? 0
                             : ultraLiveProfileActive() ? 1 : 2;
    if (liveNow != lastLiveModeActive
        || (liveNow && liveProfileNow != lastLiveProfile))
    {
        for (auto& e : liveEngines) e.reset();
        for (auto& e : liveExtendedEngines) e.reset();
        for (auto& e : extremeLiveEngines) e.reset();
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        for (auto& st : rubberBand) if (st != nullptr) st->reset();
        resetRbFifo();
#endif
        lastLiveModeActive = liveNow;
    }
    lastLiveProfile = liveProfileNow;

    if (useLiveEngine != lastUseLiveEngine)
    {
        for (auto& p : psola) p.reset();
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        for (auto& s : rubberBand) if (s != nullptr) s->reset();
        resetRbFifo();
#endif
        lastUseLiveEngine = useLiveEngine;
    }

    // Poly is a third engine (the phase vocoder). Toggling it in/out flushes the
    // mono engines AND the vocoder so neither replays stale pipeline audio at the
    // seam (same rationale as the Live/Rubber Band swap above). Allocation-free.
    const bool polyActive = polyOn && correctionOn;
    if (polyActive != lastPolyActive)
    {
        resetPolyVocoder();
        for (auto& p : psola) p.reset();
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        for (auto& s : rubberBand) if (s != nullptr) s->reset();
        resetRbFifo();
#endif
        lastPolyActive = polyActive;
    }

    // Transpose to Key is a whole-mix engine (Rubber Band, single fixed ratio). Like
    // the Live/Poly swaps above, flush the pipeline on toggle so no stale audio splices.
    const bool xposeActive = apvts.getRawParameterValue ("xposeMode")->load() > 0.5f;
    if (xposeActive != lastXposeActive)
    {
        for (auto& p : psola) p.reset();
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        for (auto& s : rubberBand) if (s != nullptr) s->reset();
        resetRbFifo();
#endif
        resetPolyVocoder();
        lastXposeActive = xposeActive;
    }

    const float selectedAmount = lastPitchFrame.objectActive && lastPitchFrame.objectAmount >= 0.0f
        ? lastPitchFrame.objectAmount : apvts.getRawParameterValue ("amount")->load();
    const bool classicActive = apvts.getRawParameterValue ("classicMode")->load() > 0.5f;
    const bool objectDisablesCorrection = lastPitchFrame.objectActive
                                       && ! lastPitchFrame.objectEnabled;
    const bool pitchEngineBypassed = ! xposeActive && ! polyActive
        && (! correctionOn || objectDisablesCorrection
            || (! classicActive && selectedAmount <= 1.0e-6f));

    if (pitchEngineBypassed)
    {
        // Keep the print engine warm at unity so re-enabling correction does not
        // replay stale FIFO data or impose a fresh 43 ms dropout. Its reconstructed
        // output is intentionally discarded in favour of the delayed dry reference.
        if (liveNow)
        {
            //  Keep the live engine warm at unity for the same reason: re-enabling
            //  correction must not re-prime from silence mid-phrase.
            auto& engines = activeLiveEngines();
            for (int ch = 0; ch < juce::jmin (mainBuffer.getNumChannels(),
                                              (int) engines.size()); ++ch)
                engines[(size_t) ch].process (mainBuffer.getReadPointer (ch),
                                              mainBuffer.getWritePointer (ch),
                                              mainBuffer.getNumSamples(), 1.0);
        }
        else
        {
            //  Keep the engine that will actually resume warm - mirroring the print
            //  chain below. Warming RubberBand in a signalsmith build primed the
            //  wrong pipeline: ssStretch kept its stale pre-bypass tail, so
            //  re-engaging correction replayed audio from before the bypass.
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
            processWithSignalsmith (mainBuffer, 1.0, inputGain);
#elif SATTARI_AUTOPITCH_HAS_RUBBERBAND
            processWithRubberBand (mainBuffer, 1.0, inputGain);
#else
            processFallbackFormantGuide (mainBuffer, 1.0, inputGain);
#endif
        }
        copyDelayedDryReferenceTo (mainBuffer);
        if (std::abs (inputGain - 1.0f) > 1.0e-6f)
            mainBuffer.applyGain (inputGain);
    }
    else if (xposeActive)
    {
        // Whole-mix musical transpose: shift the ENTIRE signal by xposeSemis semitones
        // so a full beat / instrumental / sample moves into a target key. A one-pole
        // glide on the ratio makes integer semitone changes portamento, never click.
        const int semis = juce::jlimit (-12, 12,
            (int) std::round (apvts.getRawParameterValue ("xposeSemis")->load()));
        const double targetScale = std::pow (2.0, (double) semis / 12.0);
        const double glide = (currentSampleRate > 0.0)
            ? 1.0 - std::exp (-(double) mainBuffer.getNumSamples() / (0.060 * currentSampleRate))
            : 1.0;
        smoothedXposeScale += glide * (targetScale - smoothedXposeScale);
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        processWithRubberBand (mainBuffer, smoothedXposeScale, inputGain, /*straightTranspose=*/true);
#else
        processFallbackFormantGuide (mainBuffer, smoothedXposeScale, inputGain);
#endif
    }
    else if (polyActive)
    {
        // Real polyphonic: every spectral peak is retuned independently to its
        // nearest enabled in-scale tone. Replaces the mono single-ratio engine.
        processPolyphonicRetune (mainBuffer, inputGain);
    }
    else if (liveNow)
    {
        //  The live engine plans its own correction from its own causal tracker, so
        //  it takes the note mask rather than engineScale — the print path's scale
        //  is derived from a look-ahead contour this engine deliberately does not
        //  have.
        processWithLiveEngine (mainBuffer, inputGain,
                               syntheticVibratoCents + liveHumanizeCents);
    }
    else if (useLiveEngine)
    {
        processWithPsola (mainBuffer, engineScale, inputGain);
    }
    else
    {
        // Print-engine selection is pinned by CMake. Merely initialising an
        // optional submodule must never change rendered audio. Rubber Band is the
        // qualified release engine; Signalsmith is an explicit experimental build
        // until its own latency, golden, null and listening gates are requalified.
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
        processWithSignalsmith (mainBuffer, engineScale, inputGain);
#elif SATTARI_AUTOPITCH_HAS_RUBBERBAND
        processWithRubberBand (mainBuffer, engineScale, inputGain);
#else
        processFallbackFormantGuide (mainBuffer, engineScale, inputGain);
#endif
    }
    applyModeCharacter (mainBuffer);
    renderHarmonyInto (mainBuffer, harmonyScratch, hostTimeSeconds);

    // Per-object gain is intentionally post-character and pre-master trim: editing
    // one note's level cannot change the saturation character of that note.
    if (lastPitchFrame.objectActive && std::abs (lastPitchFrame.objectGainDb) > 0.001f)
        mainBuffer.applyGain (juce::Decibels::decibelsToGain (lastPitchFrame.objectGainDb));

    applyGraphTimeCorrection (mainBuffer, hostTimeSeconds);

    // Output trim: a pure post-Style level change. Applied here (not folded into the
    // engine drive) so pushing output level cannot deepen the Style saturation — it
    // only scales the finished signal, which the soft ceiling below then bounds.
    if (std::abs (outputGain - 1.0f) > 1.0e-6f)
        mainBuffer.applyGain (outputGain);

    // Final safety ceiling so pitch-shift/Style overshoot never hard-clips the host.
    for (int ch = 0; ch < mainBuffer.getNumChannels(); ++ch)
    {
        float* d = mainBuffer.getWritePointer (ch);
        for (int i = 0; i < mainBuffer.getNumSamples(); ++i)
            d[i] = softCeiling (d[i]);
    }
    publishUiFrame();
}

void SattariAutoPitchAudioProcessor::applyGraphTimeCorrection (juce::AudioBuffer<float>& buffer,
                                                               double hostTimeSeconds)
{
    const bool enabled = apvts.getRawParameterValue ("graphTimeCorrection")->load() >= 0.5f;
    if (! enabled)
    {
        graphTimeWasActive = false;
        return;
    }
    if (graphTimeRingSize <= 1 || graphTimeRing.getNumChannels() < buffer.getNumChannels())
        return;
    if (! graphTimeWasActive)
    {
        graphTimeRing.clear();
        graphTimeWrite = 0;
        graphTimeDelaySamples = 0.5 * currentSampleRate;
        graphTimeWasActive = true;
    }

    const double baseDelay = 0.5 * currentSampleRate;
    double targetDelay = baseDelay;
    if (hostTimeSeconds >= 0.0)
    {
        const double centreTime = hostTimeSeconds + 0.5 * (double) buffer.getNumSamples() / currentSampleRate;
        const juce::ScopedTryLock l (pitchEditLock);
        if (l.isLocked())
        {
            for (auto it = pitchEdits.rbegin(); it != pitchEdits.rend(); ++it)
            {
                // The region is tested at its destination time. Positive offsets
                // delay a note; negative offsets consume the 500 ms look-ahead.
                if (centreTime >= it->startSec + it->timeOffsetSec
                    && centreTime <= it->endSec + it->timeOffsetSec)
                {
                    targetDelay = baseDelay + it->timeOffsetSec * currentSampleRate;
                    break;
                }
            }
        }
    }
    targetDelay = juce::jlimit (0.0, (double) graphTimeRingSize - 3.0, targetDelay);

    const int n = buffer.getNumSamples();
    const double startDelay = graphTimeDelaySamples;
    for (int i = 0; i < n; ++i)
    {
        const double mix = n > 1 ? (double) i / (double) (n - 1) : 1.0;
        const double delay = startDelay + mix * (targetDelay - startDelay);
        double readPosition = (double) graphTimeWrite - delay;
        while (readPosition < 0.0) readPosition += (double) graphTimeRingSize;
        while (readPosition >= (double) graphTimeRingSize) readPosition -= (double) graphTimeRingSize;
        const int r0 = (int) std::floor (readPosition);
        const int r1 = (r0 + 1) % graphTimeRingSize;
        const float frac = (float) (readPosition - (double) r0);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float input = buffer.getSample (ch, i);
            graphTimeRing.setSample (ch, graphTimeWrite, input);
            const float a = graphTimeRing.getSample (ch, r0);
            const float b = graphTimeRing.getSample (ch, r1);
            buffer.setSample (ch, i, a + frac * (b - a));
        }
        graphTimeWrite = (graphTimeWrite + 1) % graphTimeRingSize;
    }
    graphTimeDelaySamples = targetDelay;
}

void SattariAutoPitchAudioProcessor::processWithPsola (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain)
{
    const int nCh = buffer.getNumChannels();
    const int n   = buffer.getNumSamples();

    // Period from the detected pitch; fall back to a mid-vocal value when unvoiced
    // (the shifter clamps it to its supported range internally).
    const double period = (lastPitchFrame.voiced && lastPitchFrame.frequencyHz > 0.0)
                            ? currentSampleRate / lastPitchFrame.frequencyHz
                            : currentSampleRate / 150.0;

    for (int ch = 0; ch < nCh; ++ch)
    {
        if (ch >= (int) psola.size()) break;
        float* d = buffer.getWritePointer (ch);
        // In-place is safe: process() reads the whole input block into its ring
        // before writing any output.
        psola[(size_t) ch].process (d, d, n, pitchScale, period);
    }
    buffer.applyGain (linearGain);
}

//  Live Mode. The causal engine plans its own correction from its own tracker, so
//  this function's real job is to hand it the SAME musical decision the print path
//  makes — which notes are legal — and to keep multiple channels coherent.
//
//  KEY SOURCE. enabledNotes is built from currentCorrectionRootIndex() and
//  effectiveScaleIndex(), which are the same two accessors the print engine uses.
//  Both already honour the full precedence chain — Follow Song Key (the session bus,
//  i.e. Sattari Key) > Key Map > AutoKey > manual — so Live Mode inherits the shared
//  key with no second code path to drift out of agreement. The Edit Notes mask is
//  intersected on top, exactly as buildCorrectionFrame() does it.
void SattariAutoPitchAudioProcessor::processWithLiveEngine (juce::AudioBuffer<float>& buffer,
                                                            float linearGain,
                                                            double pitchOffsetCents)
{
    const bool extreme = extremeLiveProfileActive();
    const int available = extreme ? (int) extremeLiveEngines.size()
                                  : (int) activeLiveEngines().size();
    const int nCh = juce::jmin (buffer.getNumChannels(), available);
    const int n   = buffer.getNumSamples();
    if (nCh <= 0 || n <= 0) return;

    sattari::dsp::LiveCorrectionPlanner::Settings settings;
    //  Same absolute grid as the print path and the readout (Reference Detune /
    //  Follow Song Reference), not a hardcoded 440.
    settings.referenceHz = currentReferenceHz();

    const auto origin = currentKeyOrigin();
    const int rootIndex  = origin.root;
    const int scaleIndex = origin.scale;
    const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    const int editMask   = juce::jlimit (0, 4095, (int) apvts.getRawParameterValue ("enabledNotes")->load());

    // A graph object is an explicit musical instruction, not a suggestion to the
    // scale quantiser. Carry its exact octave-aware target into the causal planner.
    if (lastPitchFrame.objectActive && lastPitchFrame.objectEnabled)
        settings.forcedTargetMidi = juce::jlimit (0, 127,
            (int) std::llround (lastPitchFrame.targetMidi));

    int allowed = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        const int degree = ((pc - rootIndex) % 12 + 12) % 12;
        const bool inScale = (scaleName == "chromatic") || scaleAllowsDegree (degree, scaleName);
        const bool enabled = editMask == 0 || ((editMask >> pc) & 1) != 0;
        settings.enabledNotes[(size_t) pc] = inScale && enabled;
        if (settings.enabledNotes[(size_t) pc]) ++allowed;
    }
    //  Never hand the planner an empty mask: nearestEnabledMidi would have nothing
    //  to snap to. Same reasoning as buildCorrectionFrame's mask == 0 fallback —
    //  a user who disables everything gets no correction, not silence-on-pitch.
    if (allowed == 0)
        settings.enabledNotes.fill (true);

    const float amountParam = lastPitchFrame.objectActive && lastPitchFrame.objectAmount >= 0.0f
        ? lastPitchFrame.objectAmount : apvts.getRawParameterValue ("amount")->load();
    //  `amount` is a 0..1 parameter, NOT 0..100. Dividing by 100 here silently
    //  reduced every correction to 1% of what was asked for, which reads exactly
    //  like "the engine is not correcting".
    settings.amount = juce::jlimit (0.0, 1.0, (double) amountParam);

    //  Speed is the same 0..100 control the print path uses; map it to the retune
    //  time constant. Classic Mode is the deliberate dead-flat sound, so it pins the
    //  fastest retune and drops vibrato retention.
    //  `speed` runs 0.05..1.0, high = fast. Map it onto the retune time constant,
    //  fast end first so the sense of the control is preserved.
    const bool classic = apvts.getRawParameterValue ("classicMode")->load() > 0.5f;
    const float speedParam = lastPitchFrame.objectActive && lastPitchFrame.objectRetune >= 0.0f
        ? lastPitchFrame.objectRetune : apvts.getRawParameterValue ("speed")->load();
    const float speed = juce::jlimit (0.05f, 1.0f, speedParam);
    settings.retuneMilliseconds = classic ? 1.5
                                          : juce::jmap ((double) speed, 0.05, 1.0, 120.0, 3.0);
    const float transitionParam = lastPitchFrame.objectActive
                               && lastPitchFrame.objectTransition >= 0.0f
        ? lastPitchFrame.objectTransition : apvts.getRawParameterValue ("transition")->load();
    settings.transitionAmount = juce::jlimit (0.0, 1.0, (double) transitionParam);
    const float vibratoParam = lastPitchFrame.objectActive
                            && lastPitchFrame.objectVibrato >= 0.0f
        ? lastPitchFrame.objectVibrato : apvts.getRawParameterValue ("vibrato")->load();
    settings.preserveVibrato = classic ? 0.0
        : juce::jlimit (0.0, 1.0, (double) vibratoParam);
    settings.pitchOffsetCents = pitchOffsetCents;

    // Neural Pitch used to keep the expensive Studio analysis alive in Live Mode
    // but the causal planner then ignored the fused frame and planned from its own
    // YIN result anyway. Feed the guarded fusion into note planning while retaining
    // the channel-local tracker for phase-coherent epoch placement.
    sattari::dsp::LivePitchTracker::Result fusedGuide;
    const bool neuralGuideActive = crepeModelPresent
        && apvts.getRawParameterValue ("neuralPitch")->load() > 0.5f
        && lastPitchFrame.voiced && lastPitchFrame.frequencyHz > 0.0;
    if (neuralGuideActive)
    {
        fusedGuide.frequencyHz = lastPitchFrame.frequencyHz;
        fusedGuide.periodSamples = currentSampleRate / lastPitchFrame.frequencyHz;
        fusedGuide.confidence = (float) lastPitchFrame.confidence;
        fusedGuide.rms = (float) lastPitchFrame.rms;
        fusedGuide.voiced = true;
    }

    // Channel 0 plans; the rest follow its exact scale. Both live renderers expose
    // the same contract, so selection changes latency/rendering only—not targeting.
    sattari::dsp::LivePitchTracker::Result tracked;
    int targetMidi = -1;
    double correctionCents = 0.0;
    auto processBank = [&] (auto& engines)
    {
        engines[0].processCorrected (buffer.getReadPointer (0), buffer.getWritePointer (0),
                                     n, settings, neuralGuideActive ? &fusedGuide : nullptr);
        correctionCents = engines[0].getCorrectionCents();
        if (nCh > 1)
        {
            const double scale = std::pow (2.0, correctionCents / 1200.0);
            for (int ch = 1; ch < nCh; ++ch)
                engines[(size_t) ch].process (buffer.getReadPointer (ch),
                                              buffer.getWritePointer (ch), n, scale);
        }
        tracked = engines[0].getPitchResult();
        targetMidi = engines[0].getTargetMidi();
    };
    if (extreme) processBank (extremeLiveEngines);
    else         processBank (activeLiveEngines());

    // Publish the causal track as the visible/debug pitch state. In YIN-only Live
    // Mode this replaces the deliberately skipped duplicate Studio analysis.
    PitchFrame liveFrame = lastPitchFrame; // retain current per-object controls
    liveFrame.frequencyHz = tracked.frequencyHz;
    liveFrame.midi = tracked.voiced
        ? midiFromHz (tracked.frequencyHz, currentReferenceHz()) : 0.0;
    liveFrame.confidence = tracked.confidence;
    liveFrame.rms = tracked.rms;
    liveFrame.voiced = tracked.voiced;
    liveFrame.targetMidi = targetMidi >= 0 ? (double) targetMidi : liveFrame.midi;
    liveFrame.correctionCents = tracked.voiced
        ? correctionCents : 0.0;
    lastPitchFrame = liveFrame;
    lastFedPitchScale = std::pow (2.0, liveFrame.correctionCents / 1200.0);

    liveAutoKeySamplesUntilAnalysis -= n;
    if (liveAutoKeySamplesUntilAnalysis <= 0)
    {
        do liveAutoKeySamplesUntilAnalysis += pitchHopSize;
        while (liveAutoKeySamplesUntilAnalysis <= 0);
        if (tracked.voiced && ! keyMapDriving() && ! sidechainSeenAudio)
            updateAutoKeyHistogram (liveFrame);
    }

    if (std::abs (linearGain - 1.0f) > 1.0e-6f)
        buffer.applyGain (linearGain);
}

void SattariAutoPitchAudioProcessor::processWithRubberBand (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain,
                                                            bool straightTranspose)
{
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Deliver automation on a fixed grid, independent of the host callback. The
    // correction smoother supplies the endpoint for this callback; logarithmic
    // interpolation from the last delivered endpoint reconstructs the glide at
    // 128-sample resolution while the ratio is moving. Sub-one-cent detector
    // jitter is treated as stationary so settled notes retain full-block quality
    // and CPU efficiency. This removes buffer-dependent correction timing and
    // the large ratio steps Rubber Band can render as metallic vibrato chatter.
    // Sub-buffers alias the host buffer, so this path is allocation-free.
    const double ratioMotionLog2 = std::abs (std::log2 (juce::jmax (1.0e-6, pitchScale)
                                                         / juce::jmax (1.0e-6, rubberBandEnginePitchScale)));
    bool movingRatio = false;
    if (numSamples > rubberBandAutomationQuantum)
    {
        if (ratioMotionLog2 * 1200.0 > rubberBandStationaryCents)
            rubberBandAutomationHoldSamples = (int) std::round (0.120 * currentSampleRate);
        else
            rubberBandAutomationHoldSamples = juce::jmax (0, rubberBandAutomationHoldSamples - numSamples);
        movingRatio = rubberBandAutomationHoldSamples > 0;
    }
    // Even a stationary ratio is capped at 512 samples, so a host that violates
    // its declared maximum—or simply runs a 1024/2048 buffer—cannot exceed the
    // Rubber Band process contract or require a host-sized FIFO cushion.
    const int processQuantum = movingRatio ? rubberBandAutomationQuantum : 512;
    if (numSamples > processQuantum)
    {
        const double startLog2 = std::log2 (juce::jmax (1.0e-6, rubberBandEnginePitchScale));
        const double endLog2 = std::log2 (juce::jmax (1.0e-6, pitchScale));
        for (int offset = 0; offset < numSamples; offset += processQuantum)
        {
            const int chunk = juce::jmin (processQuantum, numSamples - offset);
            const double fraction = (double) (offset + chunk) / (double) numSamples;
            const double chunkScale = std::pow (2.0, startLog2 + fraction * (endLog2 - startLog2));
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numChannels, offset, chunk);
            processWithRubberBand (sub, chunkScale, linearGain, straightTranspose);
        }
        return;
    }

    rubberBandOutput.setSize (numChannels, numSamples, false, false, true);
    rubberBandOutput.clear();

    // Formant handling: preserving formants keeps the voice natural under heavy
    // correction (no "chipmunk" colour); the shift control deliberately moves the
    // vocal-tract resonances for a throat/gender effect (semitones -> scale).
    // Straight transpose (Transpose-to-Key) moves the whole spectrum together — formants
    // included — so we force preservation OFF regardless of the vocal Formant Preserve param.
    const bool formantPreserve = ! straightTranspose
                              && apvts.getRawParameterValue ("formantPreserve")->load() > 0.5f;
    const float formantShiftSemis = lastPitchFrame.objectActive && lastPitchFrame.objectFormantSemitones >= -12.0f
        ? lastPitchFrame.objectFormantSemitones : apvts.getRawParameterValue ("formantShift")->load();
    // Throat: vocal-tract-length ratio (1.0 = natural). Combines multiplicatively
    // with the semitone Formant Shift so both controls move the formant envelope
    // together. A throat > 1 lengthens the tract (darker/bigger); < 1 shortens it.
    const double throat = (double) apvts.getRawParameterValue ("throat")->load();
    const double shiftRatio = std::pow (2.0, (double) formantShiftSemis / 12.0);
    const double combinedFormant = throat * shiftRatio;
    // 0.0 tells Rubber Band to auto-preserve (default). We send an explicit scale
    // whenever Throat or Formant Shift departs from neutral.
    //  Rubber Band's formant scale is a PRE-SHIFT envelope warp that the pitch
    //  resampler then multiplies - it is NOT an absolute target. R3Stretcher.cpp
    //  substitutes `1.0 / m_pitchScale` when the scale is 0.0, and that substitution
    //  is exactly what makes 0.0 mean "preserve" - so the net formant multiplier is
    //  always formantScale * pitchScale. Passing combinedFormant raw therefore
    //  rendered combinedFormant * pitchScale: the envelope swung note-by-note with
    //  each note's correction ratio (a requested +7 st landing at +6.2 st on a note
    //  80 cents sharp, +7.4 st on the next note 40 cents flat) - the very chipmunk
    //  modulation Formant Preserve exists to remove, armed the moment Throat or
    //  Formant Shift left neutral. Dividing by the ratio actually in use restores
    //  the absolute target the UI promises, and matches what the signalsmith path
    //  already gets from compensatePitch=true. Neutral still sends 0.0 so Rubber
    //  Band's own auto-preserve stays on the untouched path.
    const bool neutralFormant = (std::abs (combinedFormant - 1.0) < 1.0e-4);
    const double formantScale = neutralFormant
        ? 0.0
        : combinedFormant / juce::jmax (1.0e-6, pitchScale);

    // If for any reason a channel has no stretcher, fall back to dry passthrough
    // for the whole block (keeps channels phase-locked; never mid-block spliced).
    const bool haveAllStretchers =
        (int) rubberBand.size() >= numChannels
        && std::all_of (rubberBand.begin(), rubberBand.begin() + numChannels,
                        [] (const auto& s) { return s != nullptr; });
    if (! haveAllStretchers)
    {
        buffer.applyGain (linearGain);
        rubberBandEnginePitchScale = pitchScale;
        return;
    }

    // 1. Feed the block to every channel with identical settings.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto& stretcher = *rubberBand[(size_t) channel];
        stretcher.setFormantOption (formantPreserve
            ? RubberBand::RubberBandStretcher::OptionFormantPreserved
            : RubberBand::RubberBandStretcher::OptionFormantShifted);
        // 1.0 (shift 0) keeps the natural envelope; other values move formants.
        stretcher.setFormantScale (formantPreserve ? formantScale : 0.0);
        stretcher.setPitchScale (pitchScale);
        const float* input = buffer.getReadPointer (channel);
        stretcher.process (&input, (size_t) numSamples, false);
    }

    // 2. Drain everything Rubber Band produced into the FIFO. All channels share
    //    the same settings/schedule, so available() is identical; take the min
    //    defensively and retrieve equal-length chunks so the ring stays aligned.
    for (;;)
    {
        int avail = std::numeric_limits<int>::max();
        for (int channel = 0; channel < numChannels; ++channel)
            avail = juce::jmin (avail, (int) rubberBand[(size_t) channel]->available());
        if (avail <= 0)
            break;

        const int chunk = juce::jmin (avail, rubberBandOutput.getNumSamples());
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* dst = rubberBandOutput.getWritePointer (channel);
            rubberBand[(size_t) channel]->retrieve (&dst, (size_t) chunk);
        }
        pushRbFifo (rubberBandOutput, 0, chunk);
    }

    // 3. Pop exactly numSamples from the ring. Hold silence until the one-block
    //    cushion has filled (this is the reported start-up latency); after that
    //    the ring always has enough, so output is continuous — no dry splices.
    if (! rbPrimed && rbFifoCount >= rbCushion)
    {
        // Drop the overshoot so the REAL delay equals the REPORTED one. This
        // threshold can only be tested at block boundaries, and Rubber Band's output
        // is bursty, so the ring typically crosses rbCushion by a few hundred
        // samples. In steady state we pop exactly numSamples per block, so whatever
        // surplus is sitting here at priming never drains — it becomes permanent
        // latency on top of activeEngineLatencySamples()'s startDelay + rbCushion.
        // Measured at 48k/512 that was ~239 samples (~5 ms) of UNDER-report, i.e. the
        // host under-compensated and the tuned track sat late against the dry.
        // Discarding the surplus (rather than inflating the reported figure) keeps
        // the pipeline as short as it claims to be.
        const int overshoot = rbFifoCount - rbCushion;
        if (overshoot > 0)
        {
            rbFifoRead   = (rbFifoRead + overshoot) % rbFifoCap;
            rbFifoCount -= overshoot;
        }
        rbPrimed = true;
    }

    if (! rbPrimed)
    {
        buffer.clear();
    }
    else
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            float* out = buffer.getWritePointer (channel);
            const float* ring = rbFifo.getReadPointer (channel);
            int rd = rbFifoRead;
            const int avail = juce::jmin (numSamples, rbFifoCount);
            for (int i = 0; i < avail; ++i)
            {
                out[i] = ring[rd];
                if (++rd >= rbFifoCap) rd = 0;
            }
            for (int i = avail; i < numSamples; ++i) // safety pad (shouldn't trigger)
                out[i] = 0.0f;
        }
        const int consumed = juce::jmin (numSamples, rbFifoCount);
        rbFifoRead = (rbFifoRead + consumed) % rbFifoCap;
        rbFifoCount -= consumed;
    }

    buffer.applyGain (linearGain);
    rubberBandEnginePitchScale = pitchScale;
#else
    juce::ignoreUnused (buffer, pitchScale, linearGain);
#endif
}

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
void SattariAutoPitchAudioProcessor::resetRbFifo()
{
    rbFifo.clear();
    rbFifoRead  = 0;
    rbFifoCount = 0;
    rbPrimed    = false;
    rubberBandEnginePitchScale = 1.0;
    rubberBandAutomationHoldSamples = 0;
}

// Append n samples (per channel) from src[chSrcBase + ch] into the ring. Callers
// guarantee n <= free space (capacity is generously sized in prepareToPlay);
// if that ever fails we drop the oldest samples rather than overwrite live data.
void SattariAutoPitchAudioProcessor::pushRbFifo (const juce::AudioBuffer<float>& src, int chSrcBase, int n)
{
    if (n <= 0 || rbFifoCap <= 0)
        return;

    n = juce::jmin (n, rbFifoCap);
    if (rbFifoCount + n > rbFifoCap) // make room (defensive; not expected in practice)
    {
        const int drop = rbFifoCount + n - rbFifoCap;
        rbFifoRead  = (rbFifoRead + drop) % rbFifoCap;
        rbFifoCount -= drop;
    }

    const int writePos = (rbFifoRead + rbFifoCount) % rbFifoCap;
    const int numCh    = juce::jmin (rbFifo.getNumChannels(), src.getNumChannels());
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = src.getReadPointer (chSrcBase + ch);
        float* ring     = rbFifo.getWritePointer (ch);
        int wp = writePos;
        for (int i = 0; i < n; ++i)
        {
            ring[wp] = in[i];
            if (++wp >= rbFifoCap) wp = 0;
        }
    }
    rbFifoCount += n;
}
#endif

void SattariAutoPitchAudioProcessor::processFallbackFormantGuide (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain)
{
    const int numSamples = buffer.getNumSamples();
    if (std::abs (pitchScale - 1.0) < 0.002)
    {
        buffer.applyGain (linearGain);
        return;
    }

    auto& dry = formantScratch;
    const int dryChannels = buffer.getNumChannels();
    dry.setSize (dryChannels, numSamples, false, false, true); // preallocated: no realloc
    for (int channel = 0; channel < dryChannels; ++channel)
        dry.copyFrom (channel, 0, buffer, channel, 0, numSamples);

    const double centre = (double) (numSamples - 1) * 0.5;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double source = centre + ((double) i - centre) / pitchScale;
            const float shifted = readLinearInterpolated (dry, channel, source);
            const float wet = juce::jlimit (0.0f, 1.0f, std::abs ((float) (pitchScale - 1.0)) * 2.5f);
            buffer.setSample (channel, i, (dry.getSample (channel, i) * (1.0f - wet) + shifted * wet) * linearGain);
        }
    }
}

void SattariAutoPitchAudioProcessor::applyModeCharacter (juce::AudioBuffer<float>& buffer) const
{
    const auto style = currentStyleChoice();
    // Natural, Classic and Modern Rap differ in correction law and retune speed,
    // not in an always-on distortion stage. Classic previously added tanh THD and
    // Modern Rap added a block-reset edge enhancer; both could make an otherwise
    // clean vocal sound clipped or buzzy. Keep explicit FX in Robot/Big Harmony.
    if (style == "Natural" || style == "Classic" || style == "Modern Rap")
        return;

    if (style == "Robot")
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float held = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if ((i % 24) == 0)
                    held = buffer.getSample (channel, i);
                const auto shaped = std::tanh ((buffer.getSample (channel, i) * 0.62f + held * 0.38f) * 1.9f) * 0.72f;
                buffer.setSample (channel, i, shaped);
            }
        }
        return;
    }

    // Big Harmony's old block-centred resampler is intentionally gone. The
    // continuous renderer below owns every audible voice and survives arbitrary
    // DAW callback boundaries without restarting phase.
}

void SattariAutoPitchAudioProcessor::renderHarmonyInto (
    juce::AudioBuffer<float>& lead, const juce::AudioBuffer<float>& untouchedInput,
    double)
{
    const int numSamples = lead.getNumSamples();
    const int numChannels = lead.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0 || numSamples > harmonyWetScratch.getNumSamples())
        return;

    // Harmony is a monophonic-vocal operation. Whole-mix transpose and the
    // polyphonic vocoder take precedence rather than multiplying a complete mix.
    const bool incompatible = apvts.getRawParameterValue ("xposeMode")->load() > 0.5f
                           || (apvts.getRawParameterValue ("polyphonic")->load() > 0.5f
                               && apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f);
    bool anyEnabled = false, anySolo = false;
    for (int v = 0; v < 4; ++v)
    {
        anyEnabled = anyEnabled
                  || apvts.getRawParameterValue (harmonyEnableIds[(size_t) v])->load() > 0.5f;
        anySolo = anySolo
               || apvts.getRawParameterValue (harmonySoloIds[(size_t) v])->load() > 0.5f;
    }
    if (! anyEnabled || incompatible)
    {
        if (harmonyWasActive)
        {
            harmonyExtremeRenderer.reset();
            harmonyUltraRenderer.reset();
            harmonyRangeRenderer.reset();
            harmonyRenderPlanner.reset();
            harmonyWasActive = false;
            harmonyRendererBank = -1;
        }
        return;
    }
    harmonyWasActive = true;

    sattari::dsp::HarmonyVoiceSettings planSettings[4];
    for (int v = 0; v < 4; ++v)
    {
        const bool solo = apvts.getRawParameterValue (harmonySoloIds[(size_t) v])->load() > 0.5f;
        planSettings[v].enabled = apvts.getRawParameterValue (harmonyEnableIds[(size_t) v])->load() > 0.5f
                               && (! anySolo || solo);
        planSettings[v].role = (sattari::dsp::HarmonyRole) juce::jlimit (0, 6,
            (int) std::lround (apvts.getRawParameterValue (harmonyRoleIds[(size_t) v])->load()));
        planSettings[v].intervalDegrees = juce::jlimit (-7, 7,
            (int) std::lround (apvts.getRawParameterValue (harmonyDegreeIds[(size_t) v])->load()));
        planSettings[v].octaveOffset = juce::jlimit (-1, 1,
            (int) std::lround (apvts.getRawParameterValue (harmonyOctaveIds[(size_t) v])->load()));
    }

    const int uiMode = juce::jlimit (0, 2,
        (int) std::lround (apvts.getRawParameterValue ("harmonyMode")->load()));
    const auto mode = uiMode == 0 ? sattari::dsp::HarmonyMode::intelligent
                    : uiMode == 1 ? sattari::dsp::HarmonyMode::interval
                                  : sattari::dsp::HarmonyMode::midi;
    const int leadMidi = lastPitchFrame.voiced
        ? juce::jlimit (0, 127, (int) std::llround (
            apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f
                ? lastPitchFrame.targetMidi : lastPitchFrame.midi))
        : -1;
    const auto origin = currentKeyOrigin();
    int pcs[4], numPcs = juce::jlimit (0, 4,
        harmonyChordCount.load (std::memory_order_acquire));
    for (int i = 0; i < numPcs; ++i)
        pcs[i] = harmonyChordPcs[(size_t) i].load (std::memory_order_relaxed);
    int held[4], numHeld = 0;
    if (mode == sattari::dsp::HarmonyMode::midi)
        for (const auto& note : harmonyHeldMidi)
        {
            const int value = note.load (std::memory_order_relaxed);
            if (value >= 0) held[numHeld++] = value;
        }
    const auto plan = harmonyRenderPlanner.plan (
        leadMidi, mode, planSettings, 4, origin.root,
        sattari::harmony::scaleIdFromChoice (origin.scale),
        numPcs > 0 ? pcs : nullptr, numPcs,
        (double) harmonyChordConfidence.load (std::memory_order_relaxed),
        numHeld > 0 ? held : nullptr, numHeld);

    std::array<sattari::dsp::HarmonyRenderer::Voice, 4> voices;
    for (int v = 0; v < 4; ++v)
    {
        auto& voice = voices[(size_t) v];
        voice.active = plan.voices[(size_t) v].active;
        voice.targetMidi = plan.voices[(size_t) v].midi;
        voice.level = juce::Decibels::decibelsToGain (
            apvts.getRawParameterValue (harmonyLevelIds[(size_t) v])->load(), -60.0f);
        voice.pan = apvts.getRawParameterValue (harmonyPanIds[(size_t) v])->load();
        voice.delayMs = apvts.getRawParameterValue (harmonyDelayIds[(size_t) v])->load();
        voice.detuneCents = apvts.getRawParameterValue (harmonyDetuneIds[(size_t) v])->load();
        voice.vibratoCents = apvts.getRawParameterValue (harmonyVibratoIds[(size_t) v])->load();
    }

    const int requestedBank = liveModeActive()
        ? (extremeLiveProfileActive() ? 0 : ultraLiveProfileActive() ? 1 : 2)
        : 2;
    if (requestedBank != harmonyRendererBank)
    {
        // A dormant bank contains delayed audio from the previous mode. Never
        // resume it into a new transport moment: clear both rings and let the
        // renderer's built-in wet fade establish the newly selected latency.
        harmonyExtremeRenderer.reset();
        harmonyUltraRenderer.reset();
        harmonyRangeRenderer.reset();
        harmonyRendererBank = requestedBank;
    }
    auto& renderer = requestedBank == 0 ? harmonyExtremeRenderer
                   : requestedBank == 1 ? harmonyUltraRenderer
                                        : harmonyRangeRenderer;
    const int leadLatency = liveModeActive() ? liveEngineLatencySamples()
                                             : printEngineLatencySamples();
    sattari::dsp::HarmonyRenderer::Settings renderSettings;
    renderSettings.glideMs = apvts.getRawParameterValue ("harmonyGlide")->load();
    renderSettings.humanPitchCents = apvts.getRawParameterValue ("harmonyHumanPitch")->load();
    renderSettings.humanTimingMs = apvts.getRawParameterValue ("harmonyHumanTime")->load();
    renderSettings.vibratoRateHz = apvts.getRawParameterValue ("harmonyVibratoRate")->load();
    renderSettings.ensemble = apvts.getRawParameterValue ("harmonyEnsemble")->load();
    renderSettings.ensembleDepthMs = apvts.getRawParameterValue ("harmonyEnsembleDepth")->load();
    renderSettings.ensembleRateHz = apvts.getRawParameterValue ("harmonyEnsembleRate")->load();
    renderSettings.ensembleSpread = apvts.getRawParameterValue ("harmonyEnsembleSpread")->load();
    renderSettings.lowCutHz = apvts.getRawParameterValue ("harmonyLowCut")->load();
    renderSettings.highCutHz = apvts.getRawParameterValue ("harmonyHighCut")->load();
    renderSettings.alignmentDelaySamples = juce::jmax (0, leadLatency - renderer.getLatencySamples());

    const int inputChannels = juce::jmin (2, untouchedInput.getNumChannels());
    const float* inputs[2] { untouchedInput.getReadPointer (0),
                             untouchedInput.getReadPointer (juce::jmax (0, inputChannels - 1)) };
    harmonyWetScratch.clear();
    const double sourceMidi = lastPitchFrame.voiced ? lastPitchFrame.midi : 60.0;
    const double sourcePeriod = lastPitchFrame.voiced && lastPitchFrame.frequencyHz > 0.0
        ? currentSampleRate / lastPitchFrame.frequencyHz : currentSampleRate / 180.0;
    renderer.process (inputs, inputChannels,
                      harmonyWetScratch.getWritePointer (0),
                      harmonyWetScratch.getWritePointer (1), numSamples,
                      sourceMidi, sourcePeriod, lastPitchFrame.voiced,
                      voices, renderSettings);

    const float wet = apvts.getRawParameterValue ("harmonyMix")->load()
                    * juce::Decibels::decibelsToGain (
                        apvts.getRawParameterValue ("inputGain")->load());
    const bool voiceOnly = apvts.getRawParameterValue ("harmonyVoiceOnly")->load() > 0.5f;
    const float leadGain = voiceOnly ? 0.0f : apvts.getRawParameterValue ("harmonyLead")->load();
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* harmony = harmonyWetScratch.getReadPointer (juce::jmin (ch, 1));
        float* output = lead.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            output[i] = leadGain * output[i] + wet * harmony[i];
    }
}

juce::String SattariAutoPitchAudioProcessor::midiNoteName (double midi)
{
    if (midi <= 0.0)
        return "-";

    const int rounded = (int) std::round (midi);
    const int note = ((rounded % 12) + 12) % 12;
    const int octave = rounded / 12 - 1;
    return rootChoices[note] + juce::String (octave);
}

juce::String SattariAutoPitchAudioProcessor::getLiveStatusSummary() const
{
    const auto frame = uiFrame.load();
    const auto origin = currentKeyOrigin();
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, origin.root)];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, origin.scale)];
    //  Name the engine that actually runs. Keyed on HAS_RUBBERBAND alone, this
    //  read "Studio HQ" in a signalsmith build too - the readout asserting a
    //  Rubber Band print path while signalsmith was doing the work.
    juce::String engine;
    if (liveModeActive())
        engine = extremeLiveProfileActive() ? "Live Extreme"
               : ultraLiveProfileActive() ? "Live Ultra" : "Live Range";
    else
    {
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
        engine = "Studio SS";
#elif SATTARI_AUTOPITCH_HAS_RUBBERBAND
        engine = "Studio HQ";
#else
        engine = "Guide";
#endif
    }
    const bool neuralActive = crepeModelPresent
        && apvts.getRawParameterValue ("neuralPitch")->load() > 0.5f;
    const juce::String detector = neuralActive ? "Neural" : "YIN";
    const juce::String dot = juce::String::fromUTF8 ("   \xc2\xb7   ");   // "  ·  "

    if (! frame.voiced)
        return engine + dot + detector + dot + root + " " + scale + dot + "waiting for input";

    return engine + dot + detector + dot + root + " " + scale + dot + currentStyleChoice() + dot
        + midiNoteName (frame.midi) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + midiNoteName (frame.targetMidi)
        + dot + juce::String (frame.correctionCents, 1) + " ct"
        + dot + juce::String (frame.confidence * 100.0, 0) + "%";
}

sattari::audio_core::KeySignature SattariAutoPitchAudioProcessor::currentKeySignature() const
{
    const auto frame = uiFrame.load();
    const int scaleIndex = effectiveScaleIndex();
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    sattari::audio_core::MusicalMode mode = sattari::audio_core::MusicalMode::Unknown;
    if (scale == "major")
        mode = sattari::audio_core::MusicalMode::Major;
    else if (scale == "minor")
        mode = sattari::audio_core::MusicalMode::Minor;

    const double traceConfidence = (double) apvts.getRawParameterValue ("traceConfidence")->load();
    const double liveConfidence = frame.voiced ? frame.confidence : 0.0;
    return {
        currentCorrectionRootIndex(),
        mode,
        juce::jlimit (0.0, 1.0, juce::jmax (traceConfidence, liveConfidence))
    };
}

juce::String SattariAutoPitchAudioProcessor::currentSattariWheelCode() const
{
    return sattari::audio_core::toDisplayString (
        sattari::audio_core::toSattariWheelCode (currentKeySignature()));
}

juce::String SattariAutoPitchAudioProcessor::getKeyDetectionTabSummary() const
{
    // Keep the readout musical, but identify the engine honestly. This matters when
    // models are external: the user should never have to guess whether AutoKey is
    // using a neural verdict, the deterministic fallback, a map, or the suite bus.
    const auto key = currentKeySignature();
    const auto origin = currentKeyOrigin();
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, key.tonicPitchClass)];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, effectiveScaleIndex())];
    //  REPORT THE KEY'S OWN CONFIDENCE FOR DETECTOR SOURCES. currentKeySignature()
    //  returns max(traceConfidence, live PITCH-frame confidence) -- a property of
    //  the pitch tracker, not of the key decision. Displaying that under a "key"
    //  label was misleading in both directions: it read 0% on an offline render
    //  that had a perfectly good key, and it would read high on a confidently
    //  tracked note in a key the detector was guessing at.
    const bool detectorSource = (origin.source == KeyOrigin::Source::AutoKey
                                 || origin.source == KeyOrigin::Source::KeyMap
                                 || origin.source == KeyOrigin::Source::FollowBus);
    const int confidence = juce::roundToInt (
        (detectorSource ? juce::jlimit (0.0f, 1.0f, origin.confidence / 0.3f)
                        : (float) key.confidence) * 100.0f);
    juce::String engine = "Manual";
    if (origin.source == KeyOrigin::Source::FollowBus)
        engine = origin.dynamic ? "Dynamic Song Key" : "Song Key";
    else if (origin.source == KeyOrigin::Source::KeyMap)
        engine = keyNetSpecPresent ? "Neural Key Map" : "HPCP Key Map";
    else if (origin.source == KeyOrigin::Source::AutoKey)
    {
        if (keyNetSpecPresent && akSpecValid.load (std::memory_order_acquire))
            engine = "Neural spectrum";
        else if (keyNetPresent && keyNetLiveValid.load (std::memory_order_acquire))
            engine = "Neural chroma";
        else
            engine = "Adaptive fallback";
    }
    if (origin.source == KeyOrigin::Source::AutoKey
        && origin.confidence < kMinBusKeyConfidence)
        return "Key   No Result"
            + juce::String::fromUTF8 ("   \xc2\xb7   ") + "manual " + root + " " + scale + " retained"
            + juce::String::fromUTF8 ("   \xc2\xb7   ") + engine;
    return "Key   " + root + " " + scale
        + juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String (confidence) + "% confidence"
        + juce::String::fromUTF8 ("   \xc2\xb7   ") + engine;
}

int SattariAutoPitchAudioProcessor::rootToChoiceIndex (const juce::String& root)
{
    const auto normalized = root.trim().toUpperCase().replace ("♯", "#").replace ("♭", "B");
    const juce::String flatNormalized = normalized == "DB" ? "C#"
        : normalized == "EB" ? "D#"
        : normalized == "GB" ? "F#"
        : normalized == "AB" ? "G#"
        : normalized == "BB" ? "A#"
        : normalized;

    const int index = rootChoices.indexOf (flatNormalized);
    return index >= 0 ? index : 9;
}

int SattariAutoPitchAudioProcessor::scaleToChoiceIndex (const juce::String& scale)
{
    const int index = scaleChoices.indexOf (scale.trim().toLowerCase());
    return index >= 0 ? index : 1;
}

int SattariAutoPitchAudioProcessor::styleToChoiceIndex (const juce::String& style)
{
    auto normalized = style.trim().toLowerCase().replace ("_", "-").replace (" ", "-");
    if (normalized == "natural-pop")
        normalized = "natural";
    if (normalized == "classic-auto" || normalized == "classic-tune")
        normalized = "classic";
    if (normalized == "modern" || normalized == "modern-tight" || normalized == "tight" || normalized == "rap")
        normalized = "modern-rap";
    if (normalized == "hard" || normalized == "hard-tune" || normalized == "robotic")
        normalized = "robot";
    if (normalized == "harmony" || normalized == "big-harmonies")
        normalized = "big-harmony";

    for (int i = 0; i < styleChoices.size(); ++i)
    {
        const auto choice = styleChoices[i].trim().toLowerCase().replace (" ", "-");
        if (choice == normalized)
            return i;
    }

    const int index = styleChoices.indexOf (normalized);
    return index >= 0 ? index : 1;
}

void SattariAutoPitchAudioProcessor::setParameterValue (const juce::String& parameterID, float value)
{
    if (auto* parameter = apvts.getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

bool SattariAutoPitchAudioProcessor::applyTraceContractJson (const juce::String& traceJson)
{
    const auto parsed = juce::JSON::parse (traceJson);
    if (! parsed.isObject())
    {
        lastTraceSummary = "Trace import failed: JSON root is not an object";
        return false;
    }

    const auto settings = readObjectVar (parsed, "settings");
    const auto keySuggestion = readObjectVar (parsed, "key_suggestion");
    const auto summary = readObjectVar (parsed, "summary");
    const auto compass = readObjectVar (parsed, "pitch_compass");

    const auto root = readObjectString (settings, "root", readObjectString (keySuggestion, "root", "A"));
    const auto scale = readObjectString (settings, "scale", readObjectString (keySuggestion, "scale", "minor"));
    const auto style = readObjectString (settings, "style", "modern");
    const double confidence = readObjectDouble (keySuggestion, "confidence", 0.0);
    const double avgCorrection = readObjectDouble (summary, "avg_abs_correction_cents", 0.0);
    const double doNoHarmEvents = readObjectDouble (summary, "do_no_harm_events", 0.0);

    setParameterValue ("keyRoot", (float) rootToChoiceIndex (root));
    const int extendedScale = scaleToChoiceIndex (scale);
    const int legacyScale = extendedScale == 2 ? 2
                          : (extendedScale == 1 || extendedScale == 3 || extendedScale == 4
                             || extendedScale == 7 || extendedScale == 8 || extendedScale == 9
                             || extendedScale == 11 || extendedScale == 12) ? 1 : 0;
    setParameterValue ("scale", (float) legacyScale);
    setParameterValue ("scaleMode", (float) extendedScale);
    setParameterValue ("style", (float) styleToChoiceIndex (style));
    setParameterValue ("traceConfidence", (float) juce::jlimit (0.0, 1.0, confidence));

    int compassNotes = 0;
    if (auto* compassObject = compass.getDynamicObject())
    {
        const auto notes = compassObject->getProperty ("notes");
        if (notes.isArray())
            compassNotes = notes.getArray()->size();
    }

    lastTraceSummary = "Trace loaded: " + root + " " + scale
        + ", style " + style
        + ", key confidence " + juce::String (confidence, 3)
        + ", avg correction " + juce::String (avgCorrection, 2) + " cents"
        + ", Do No Harm events " + juce::String ((int) doNoHarmEvents)
        + ", compass notes " + juce::String (compassNotes);

    return true;
}

juce::AudioProcessorEditor* SattariAutoPitchAudioProcessor::createEditor()
{
    return new SattariAutoPitchAudioProcessorEditor (*this);
}

void SattariAutoPitchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("lastTraceSummary", lastTraceSummary, nullptr);

    // Persist the Track Key Map so reloading a project restores the timeline.
    juce::ValueTree km ("KeyMap");
    {
        const juce::ScopedLock l (keyMapLock);
        km.setProperty ("file", keyMapFileName, nullptr);
        //  The segments alone are not the timeline. KeyMapTimeline::hasData()
        //  requires durationSec > 0, and setPlayhead() no-ops without it, so a
        //  restored session drew the "Scan a track" placeholder over a map that
        //  demonstrably existed - and neither value could be regenerated, because
        //  only the bare file NAME is stored, never a path to re-scan. 1024 peak
        //  bins is ~5.5 KB base64: cheap enough to carry in the project.
        km.setProperty ("dur", keyMapDurationSec, nullptr);
        if (! keyMapWave.empty())
            km.setProperty ("wave",
                            juce::Base64::toBase64 (keyMapWave.data(),
                                                    keyMapWave.size() * sizeof (float)),
                            nullptr);
        for (const auto& e : keyMap)
        {
            juce::ValueTree seg ("Seg");
            seg.setProperty ("t", e.timeSec, nullptr);
            seg.setProperty ("r", e.rootIdx, nullptr);
            seg.setProperty ("s", e.scaleIdx, nullptr);
            seg.setProperty ("c", e.confidence, nullptr);
            km.appendChild (seg, nullptr);
        }
    }
    state.appendChild (km, nullptr);

    juce::ValueTree edits ("PitchEdits");
    {
        const juce::ScopedLock l (pitchEditLock);
        for (const auto& e : pitchEdits)
        {
            juce::ValueTree note ("Note");
            note.setProperty ("id", e.id, nullptr);
            note.setProperty ("start", e.startSec, nullptr);
            note.setProperty ("end", e.endSec, nullptr);
            note.setProperty ("source", e.sourceMidi, nullptr);
            note.setProperty ("target", e.targetMidi, nullptr);
            note.setProperty ("confidence", e.confidence, nullptr);
            note.setProperty ("enabled", e.enabled, nullptr);
            note.setProperty ("amount", e.amount, nullptr);
            note.setProperty ("retune", e.retune, nullptr);
            note.setProperty ("transition", e.transition, nullptr);
            note.setProperty ("humanize", e.humanize, nullptr);
            note.setProperty ("vibrato", e.vibrato, nullptr);
            note.setProperty ("formant", e.formantSemitones, nullptr);
            note.setProperty ("gain", e.gainDb, nullptr);
            note.setProperty ("timeOffset", e.timeOffsetSec, nullptr);
            edits.appendChild (note, nullptr);
        }
    }
    state.appendChild (edits, nullptr);

    midiLearn.saveTo (state);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SattariAutoPitchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    //  A restored project must not inherit the previous song's dropped-file
    //  verdict: the file key is runtime evidence about audio THIS session heard,
    //  and is deliberately not serialized (same policy as Sattari Key).
    //
    //  The DERIVED latch has to die with it — correlateAutoKeyHistogram already
    //  copied the verdict into the detected key latch, and the ~1 Hz bus heartbeat
    //  republishes on that number. Zeroing the confidence is sufficient: below the
    //  0.10 gate nothing publishes and correction falls back to the manual key,
    //  while live detection re-derives an honest value within seconds. The live
    //  estimator accumulated the PREVIOUS project's audio, so it resets too.
    clearSongFileResult();
    {
        int root = 9, scale = 1;
        float confidence = 0.0f;
        readDetectedAutoKey (root, scale, confidence);
        publishDetectedAutoKey (root, scale, 0.0f);
    }
    songKeyRescan.store (true, std::memory_order_release);

    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto restored = juce::ValueTree::fromXml (*xml);
        bool hasExtendedScale = false;
        std::function<void (const juce::ValueTree&)> findExtendedScale = [&] (const juce::ValueTree& tree)
        {
            if (tree.getProperty ("id").toString() == "scaleMode")
                hasExtendedScale = true;
            for (int i = 0; i < tree.getNumChildren() && ! hasExtendedScale; ++i)
                findExtendedScale (tree.getChild (i));
        };
        findExtendedScale (restored);
        lastTraceSummary = restored.getProperty ("lastTraceSummary", "No trace loaded").toString();

        // Invalidate a scan that predates this restored state, but never join it from
        // the host's state-load call. A 30-second neural window can still be inside file
        // IO / feature extraction / inference, and synchronously waiting here freezes
        // project loading. The worker observes the sticky cancel flag before publishing;
        // the next explicit scan joins the now-finished thread and clears the flag.
        keyMapScanCancel.store (true);

        // Pull any persisted Key Map back into memory.
        auto km = restored.getChildWithName ("KeyMap");
        if (km.isValid())
        {
            std::vector<KeyMapEntry> rebuilt;
            for (int i = 0; i < km.getNumChildren(); ++i)
            {
                auto seg = km.getChild (i);
                KeyMapEntry e;
                e.timeSec    = (double) seg.getProperty ("t", 0.0);
                e.rootIdx    = (int)    seg.getProperty ("r", 0);
                e.scaleIdx   = (int)    seg.getProperty ("s", 0);
                e.confidence = (float)  (double) seg.getProperty ("c", 0.0);
                rebuilt.push_back (e);
            }
            std::vector<float> restoredWave;
            {
                juce::MemoryOutputStream decoded;
                const auto waveB64 = km.getProperty ("wave", "").toString();
                if (waveB64.isNotEmpty() && juce::Base64::convertFromBase64 (decoded, waveB64))
                {
                    const size_t bytes = decoded.getDataSize();
                    if (bytes > 0 && (bytes % sizeof (float)) == 0)
                    {
                        restoredWave.resize (bytes / sizeof (float));
                        std::memcpy (restoredWave.data(), decoded.getData(), bytes);
                    }
                }
            }
            const juce::ScopedLock l (keyMapLock);
            keyMap = std::move (rebuilt);
            keyMapFileName = km.getProperty ("file", "").toString();
            keyMapDurationSec = (double) km.getProperty ("dur", 0.0);
            keyMapWave = std::move (restoredWave);
            keyMapProgress.store (keyMap.empty() ? -1 : 100);
        }

        auto edits = restored.getChildWithName ("PitchEdits");
        {
            // Loading a state without graph data must clear the current graph;
            // otherwise a preset/project from another instance inherits stale notes.
            std::vector<PitchEditEntry> rebuilt;
            if (edits.isValid())
            {
                rebuilt.reserve ((size_t) juce::jmin (4096, edits.getNumChildren()));
                for (int i = 0; i < edits.getNumChildren() && i < 4096; ++i)
                {
                    const auto note = edits.getChild (i);
                    PitchEditEntry e;
                    e.id = (juce::int64) note.getProperty ("id", (juce::int64) (i + 1));
                    e.startSec = (double) note.getProperty ("start", 0.0);
                    e.endSec = juce::jmax (e.startSec, (double) note.getProperty ("end", e.startSec));
                    e.sourceMidi = juce::jlimit (0, 127, (int) note.getProperty ("source", 60));
                    e.targetMidi = juce::jlimit (0, 127, (int) note.getProperty ("target", e.sourceMidi));
                    e.confidence = juce::jlimit (0.0f, 1.0f, (float) (double) note.getProperty ("confidence", 0.0));
                    e.enabled = (bool) note.getProperty ("enabled", true);
                    e.amount = juce::jlimit (-1.0f, 1.0f, (float) (double) note.getProperty ("amount", -1.0));
                    e.retune = juce::jlimit (-1.0f, 1.0f, (float) (double) note.getProperty ("retune", -1.0));
                    e.transition = juce::jlimit (-1.0f, 1.0f, (float) (double) note.getProperty ("transition", -1.0));
                    e.humanize = juce::jlimit (-1.0f, 1.0f, (float) (double) note.getProperty ("humanize", -1.0));
                    e.vibrato = juce::jlimit (-1.0f, 1.0f, (float) (double) note.getProperty ("vibrato", -1.0));
                    e.formantSemitones = juce::jlimit (-100.0f, 12.0f, (float) (double) note.getProperty ("formant", -100.0));
                    e.gainDb = juce::jlimit (-24.0f, 12.0f, (float) (double) note.getProperty ("gain", 0.0));
                    e.timeOffsetSec = juce::jlimit (-0.5, 0.5, (double) note.getProperty ("timeOffset", 0.0));
                    rebuilt.push_back (e);
                }
            }
            const juce::ScopedLock l (pitchEditLock);
            pitchEdits = std::move (rebuilt);
            pitchEdits.reserve (4096);
            nextPitchEditId = 1;
            for (const auto& e : pitchEdits) nextPitchEditId = juce::jmax (nextPitchEditId, e.id + 1);
            pitchEditUndo.clear();
            pitchEditRedo.clear();
            pitchEditGestureActive = false;
            pitchEditRevision.fetch_add (1, std::memory_order_relaxed);
        }

        midiLearn.loadFrom (restored);

        apvts.replaceState (restored);
        if (! hasExtendedScale)
        {
            const int legacy = juce::jlimit (0, 2,
                juce::roundToInt (apvts.getRawParameterValue ("scale")->load()));
            setParameterValue ("scaleMode", (float) legacy);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariAutoPitchAudioProcessor();
}

//  zplane elastique v3 — SCAFFOLD ONLY, deliberately not routed.
//
//  The body below is a TODO awaiting the commercial SDK (third_party/elastique/,
//  absent here, so SATTARI_AUTOPITCH_HAS_ELASTIQUE compiles to 0). It is kept so
//  the integration point survives the merge, but the print path is NOT dispatched
//  through it: with the macro defined and the body still a stub, every corrected
//  block would become a gain-only pass-through — silently, and only on machines
//  that happen to have the SDK. Wire it into processBlock in the same change that
//  fills in the body, not before.

void SattariAutoPitchAudioProcessor::processWithElastique (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain)
{
#if SATTARI_AUTOPITCH_HAS_ELASTIQUE
    // TODO: fill in once SDK is in third_party/elastique/.
    //
    // Intended implementation:
    //
    //   auto* eq = static_cast<CElastiqueV3If*> (elastique);
    //   if (!eq) { buffer.applyGain (linearGain); return; }
    //
    //   const float semitones = 12.0f * std::log2f ((float) pitchScale);
    //   eq->SetPitchModi (semitones);
    //
    //   const bool formantPreserve = apvts.getRawParameterValue ("formantPreserve")->load() > 0.5f;
    //   eq->SetPreserveFormants (formantPreserve);
    //
    //   const int numCh  = buffer.getNumChannels();
    //   const int numOut = buffer.getNumSamples();
    //   const int numIn  = eq->GetFramesNeeded (numOut);
    //
    //   // Build per-channel pointer arrays for elastique's non-interleaved API
    //   for (int ch = 0; ch < numCh; ++ch) {
    //       auto& scrIn = elastiqueScratchIn[(size_t) ch];
    //       scrIn.resize ((size_t) numIn, 0.f);
    //       std::copy (buffer.getReadPointer (ch),
    //                  buffer.getReadPointer (ch) + juce::jmin (numIn, numOut),
    //                  scrIn.data());
    //       elastiqueScratchOut[(size_t) ch].resize ((size_t) numOut, 0.f);
    //   }
    //   std::vector<float*> ppIn (numCh), ppOut (numCh);
    //   for (int ch = 0; ch < numCh; ++ch) {
    //       ppIn [(size_t) ch] = elastiqueScratchIn [(size_t) ch].data();
    //       ppOut[(size_t) ch] = elastiqueScratchOut[(size_t) ch].data();
    //   }
    //   eq->ProcessData (ppIn.data(), numIn, ppOut.data(), numOut);
    //
    //   for (int ch = 0; ch < numCh; ++ch)
    //       buffer.copyFrom (ch, 0, elastiqueScratchOut[(size_t) ch].data(), numOut);
    //
    //   buffer.applyGain (linearGain);

    // Temporary passthrough until SDK is wired:
    juce::ignoreUnused (pitchScale);
    buffer.applyGain (linearGain);
#else
    juce::ignoreUnused (buffer, pitchScale, linearGain);
#endif
}

void SattariAutoPitchAudioProcessor::processWithSignalsmith (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain)
{
#if SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Pitch: setTransposeFactor accepts a linear ratio (2.0 = one octave up).
    ssStretch.setTransposeFactor (static_cast<float> (pitchScale));

    // Formant handling: preserving formants prevents chipmunk effect under large
    // corrections. compensatePitch=true tells signalsmith to factor out the pitch
    // multiplier automatically, so formants land at the intended target position.
    const bool  formantPreserve   = apvts.getRawParameterValue ("formantPreserve")->load() > 0.5f;
    const float formantShiftSemis = apvts.getRawParameterValue ("formantShift")->load();
    const float throat            = apvts.getRawParameterValue ("throat")->load();
    const float shiftRatio        = std::pow (2.0f, formantShiftSemis / 12.0f);
    const float combinedFormant   = throat * shiftRatio;

    if (formantPreserve)
        ssStretch.setFormantFactor (combinedFormant, /*compensatePitch=*/true);
    else
        ssStretch.setFormantFactor (1.0f, /*compensatePitch=*/false);

    //  A host that exceeds the block size it declared at prepare must not make this
    //  allocate on the audio thread. Split the block into chunks the prepared
    //  scratch can hold and recurse - the same contract the Rubber Band path keeps
    //  with its processQuantum. Sub-buffers alias `buffer`, so this stays
    //  allocation-free even in the contract-violating case.
    if (ssOutput.getNumSamples() > 0 && numSamples > ssOutput.getNumSamples())
    {
        const int quantum = ssOutput.getNumSamples();
        for (int offset = 0; offset < numSamples; offset += quantum)
        {
            const int chunk = juce::jmin (quantum, numSamples - offset);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numChannels, offset, chunk);
            processWithSignalsmith (sub, pitchScale, linearGain);
        }
        return;
    }
    if (ssOutput.getNumSamples() < numSamples || ssOutput.getNumChannels() < numChannels)
        ssOutput.setSize (numChannels, numSamples, false, true, true);
    ssOutput.clear();

    // Build channel pointer arrays for the interleave-free signalsmith API.
    ssInputPtrs.resize  ((size_t) numChannels);
    ssOutputPtrs.resize ((size_t) numChannels);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        ssInputPtrs  [(size_t) ch] = buffer.getReadPointer  (ch);
        ssOutputPtrs [(size_t) ch] = ssOutput.getWritePointer (ch);
    }

    // signalsmith-stretch is a streaming processor: N in → N out (with registered latency).
    ssStretch.process (ssInputPtrs, numSamples, ssOutputPtrs, numSamples);

    //  Copy back sample-for-sample. makeCopyOf resizes the destination to the
    //  SOURCE's size, and ssOutput is sized for preparedBlockSize - so on any block
    //  shorter than that it silently detached `buffer` from the host's memory
    //  (reallocating it, since avoidReallocating only skips the realloc when the
    //  existing allocation is large enough) and the host received its own untouched
    //  input instead of the corrected audio. Copy exactly the samples this block
    //  owns and leave the buffer's shape alone.
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom (ch, 0, ssOutput, ch, 0, numSamples);
    buffer.applyGain (linearGain);
#else
    juce::ignoreUnused (buffer, pitchScale, linearGain);
#endif
}
