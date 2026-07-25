#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <complex>
#include <cstring>
#include <limits>

namespace
{
constexpr int pitchFrameSize = 2048;
constexpr int pitchHopSize = 256;
constexpr double minVoiceHz = 70.0;
constexpr double maxVoiceHz = 950.0;
constexpr double silenceRms = 0.0045;
constexpr double minConfidence = 0.58;

const juce::StringArray rootChoices { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::StringArray scaleChoices { "major", "minor", "chromatic" };
const juce::StringArray styleChoices { "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" };

const std::array<int, 7> majorDegrees { 0, 2, 4, 5, 7, 9, 11 };
const std::array<int, 7> minorDegrees { 0, 2, 3, 5, 7, 8, 10 };

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

double midiFromHz (double frequencyHz)
{
    return (double) sattari::dsp::hzToMidi ((float) frequencyHz);
}

bool scaleAllowsDegree (int degree, const juce::String& scaleName)
{
    if (scaleName == "chromatic")
        return true;

    const auto& degrees = scaleName == "major" ? majorDegrees : minorDegrees;
    return std::find (degrees.begin(), degrees.end(), degree) != degrees.end();
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
    apvts.addParameterListener ("lowLatency", this);
    // Polyphonic engages the phase-vocoder path, whose STFT latency differs from
    // the mono engines — listen so a toggle refreshes the host delay-comp figure.
    apvts.addParameterListener ("polyphonic", this);
    // These also switch the active engine class (and thus the real latency): Transpose
    // forces Rubber Band whatever else is on, and turning correction off drops the
    // Polyphonic engine back to the mono path. Listen so the reported latency follows.
    apvts.addParameterListener ("xposeMode", this);
    apvts.addParameterListener ("enableCorrection", this);

    // Publish the correction key to the cross-plugin bus on a message-thread heartbeat,
    // so followers keep tracking even with the editor closed and no neural model. The
    // publish self-throttles (CAS, ~1 Hz) and no-ops unless AutoKey / Key Map is active.
    startTimerHz (2);
}

SattariAutoPitchAudioProcessor::~SattariAutoPitchAudioProcessor()
{
    stopTimer();   // bus-publish heartbeat
    apvts.removeParameterListener ("liveEngine", this);
    apvts.removeParameterListener ("lowLatency", this);
    apvts.removeParameterListener ("polyphonic", this);
    apvts.removeParameterListener ("xposeMode", this);
    apvts.removeParameterListener ("enableCorrection", this);
    // A latency-refresh may be queued on the message thread; cancel it before we
    // start tearing members down.
    cancelPendingUpdate();

    // Stop the CREPE inference worker before any members it touches are destroyed.
    crepeThreadRun.store (false);
    if (crepeThread.joinable())
        crepeThread.join();

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

        std::this_thread::sleep_for (12ms);   // ~83 Hz refresh; plenty for pitch
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariAutoPitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("keyRoot", "Key Root", rootChoices, 9));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", scaleChoices, 1));
    // Default to Natural (clean correction). "Classic" and up apply tanh saturation as a
    // deliberate character — measured 2.4% THD at -6 dBFS, 8% at 0 dBFS — so shipping it as
    // the default made a plain vocal "distort" out of the box. Users who want warmth pick it.
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
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.82f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("speed", "Speed",
        juce::NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.70f));
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
    params.push_back (std::make_unique<juce::AudioParameterBool> ("lowLatency", "Low Latency", false));

    // Live Engine (PSOLA): swap the Rubber Band phase vocoder for a streaming
    // TD-PSOLA shifter for near-zero-latency monitoring while tracking. Rubber
    // Band stays the default for the higher-fidelity printed sound.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("liveEngine", "Live Engine (PSOLA)", false));

    // Neural Pitch (CREPE): the most advanced detector — default ON so new instances
    // use it whenever a crepe.onnx is installed AND the build has ONNX. Fully inert
    // and falls back to YIN when the model is absent, so this default is always safe.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("neuralPitch", "Neural Pitch", true));

    // Transpose to Key (whole-mix): instead of per-note correction, shift the ENTIRE
    // signal by a fixed number of semitones via Rubber Band (formant-aware) so a whole
    // beat / instrumental / sample lands in a target key. xposeSemis is the shift; the
    // editor's "Match Key" button sets it from AutoKey's detected key -> the SET KEY
    // target. This is a musical transpose (moves everything), NOT note correction.
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
    resetPitchState (preparedBlockSize);

    // Live-monitor PSOLA engine, one shifter per output channel. Prepare it with a
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

    // Optional CREPE model: look in the suite's conventional models folder. Absent
    // a model (or an ONNX-less build) this is a no-op and YIN is used.
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
        auto modelFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("crepe.onnx");
        if (modelFile.existsAsFile())
            crepeModelPresent = crepe.loadModel (modelFile.getFullPathName().toStdString());
    }
    // Neural key model (optional). Used by the Key Map scan (offline) for far more
    // accurate key detection than the fixed profile. Absent -> falls back to profiles.
    keyNetPresent = false;
    {
        auto keyFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("keynet.onnx");
        if (keyFile.existsAsFile())
            keyNetPresent = keyNet.loadModel (keyFile.getFullPathName().toStdString());
    }
    // Spectrogram key model (optional, strictly better: 72.1 vs 61.5 MIREX). Used ONLY
    // by the offline Key Map scan — it wants a 30 s window and a pseudo-CQT, so it is
    // not viable on the live path, which keeps the cheap chroma model.
    keyNetSpecPresent = false;
    {
        auto specFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("keynet_spec.onnx");
        if (specFile.existsAsFile())
            keyNetSpecPresent = keyNetSpec.loadModel (specFile.getFullPathName().toStdString());
    }
    // Spin up the inference worker only when there's actually a model to run. It
    // idles cheaply until Neural Pitch is toggled on. (Re)start it fresh here, now
    // that the model + buffers are fully configured.
    if (crepeModelPresent)
    {
        crepeThreadRun.store (true);
        crepeThread = std::thread ([this] { crepeWorkerLoop(); });
    }

    const int scratchChannels = juce::jmax (2, getTotalNumOutputChannels());
    formantScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    harmonyScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    formantScratch.clear();
    harmonyScratch.clear();

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
    scMonoScratch.assign ((size_t) preparedBlockSize, 0.0f);

    // Reset the live neural-key accumulators so a previous session's key can't linger.
    knWeight = 0.0;
    for (int i = 0; i < 12; ++i) { knMean[i] = 0.0; for (int j = 0; j < 12; ++j) knOuter[i][j] = 0.0; }
    keyNetFeatReady.store (false, std::memory_order_relaxed);
    keyNetLiveValid.store (false, std::memory_order_relaxed);

    // Real polyphonic phase-vocoder: preallocate all STFT buffers so the audio
    // thread never allocates when Polyphonic is engaged. Sized for the output
    // channel count (matches the mono engines' channel handling).
    preparePolyVocoder (juce::jmax (1, getTotalNumOutputChannels()));
    lastPolyActive = (apvts.getRawParameterValue ("polyphonic") != nullptr
                      && apvts.getRawParameterValue ("polyphonic")->load() > 0.5f)
                  && (apvts.getRawParameterValue ("enableCorrection") == nullptr
                      || apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f);

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    rubberBand.clear();
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());

    // Rubber Band is the high-fidelity PRINT engine and always uses the
    // high-quality window. Low latency is no longer bought by degrading Rubber
    // Band's window (a prepare-only switch with no runtime effect); it is a
    // runtime engine swap to the streaming PSOLA shifter — see the Low Latency /
    // Live Engine handling in processBlock and the reported-latency block below.
    auto opts = RubberBand::RubberBandStretcher::OptionProcessRealTime
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
              | RubberBand::RubberBandStretcher::OptionPitchHighQuality;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto stretcher = std::make_unique<RubberBand::RubberBandStretcher> (
            currentSampleRate, 1, opts, 1.0, 1.0);
        stretcher->setMaxProcessSize ((size_t) preparedBlockSize);
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
    rbCushion = juce::jmax (preparedBlockSize, startDelay);
    rbFifoCap = rbCushion + startDelay + 8 * preparedBlockSize + 16384;
    rbFifo.setSize (numChannels, rbFifoCap);
    resetRbFifo();
#endif

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
    lastUseLiveEngine = (apvts.getRawParameterValue ("liveEngine") != nullptr
                         && apvts.getRawParameterValue ("liveEngine")->load() > 0.5f)
                     || (apvts.getRawParameterValue ("lowLatency") != nullptr
                         && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f);
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
    const bool lowLatOn  = apvts.getRawParameterValue ("lowLatency") != nullptr
                           && apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
    const bool liveEngOn = apvts.getRawParameterValue ("liveEngine") != nullptr
                           && apvts.getRawParameterValue ("liveEngine")->load() > 0.5f;
    const bool polyOn    = apvts.getRawParameterValue ("polyphonic") != nullptr
                           && apvts.getRawParameterValue ("polyphonic")->load() > 0.5f;
    const bool corrOn    = apvts.getRawParameterValue ("enableCorrection") == nullptr
                           || apvts.getRawParameterValue ("enableCorrection")->load() > 0.5f;
    const bool xposeOn   = apvts.getRawParameterValue ("xposeMode") != nullptr
                           && apvts.getRawParameterValue ("xposeMode")->load() > 0.5f;
    // Analytic (not psola.front()) so a Low Latency toggle reports the new value at
    // once, even in the 1-block window before the freshly-built shifter is swapped in.
    const int psolaLatency = psolaLatencyFor (lowLatOn);
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    const int rbLat = rbLatency + rbCushion;
#else
    const int rbLat = 0;   // no Rubber Band: the formant-guide fallback is block-local
#endif
    // Mirror processBlock's engine precedence EXACTLY, or the host is delay-compensated
    // for the wrong engine:
    //   Transpose-to-Key always runs Rubber Band, whatever else is on;
    //   then Polyphonic (phase vocoder); then PSOLA (Live/Low-Latency); else Rubber Band.
    if (xposeOn)               return rbLat;
    if (polyOn && corrOn)      return pvInFifoLatency;
    if (lowLatOn || liveEngOn) return psolaLatency;
    return rbLat;
}

void SattariAutoPitchAudioProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // Engine toggle: the reported latency must change to match the now-active
    // engine. setLatencySamples() -> updateHostDisplay() must run on the message
    // thread, and parameterChanged may be called from the audio thread (host
    // automation), so defer to handleAsyncUpdate(). Only posts on an actual change,
    // never per block.
    if (parameterID == "liveEngine" || parameterID == "lowLatency" || parameterID == "polyphonic"
        || parameterID == "xposeMode" || parameterID == "enableCorrection")
        triggerAsyncUpdate();
}

void SattariAutoPitchAudioProcessor::timerCallback()
{
    // Editor-independent, model-independent bus heartbeat. publishCurrentKeyToBus()
    // self-throttles and returns early unless AutoKey / Key Map is the active source.
    publishCurrentKeyToBus();
}

void SattariAutoPitchAudioProcessor::handleAsyncUpdate()
{
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
    sessionReader.stop();
    crepeThreadRun.store (false);
    if (crepeThread.joinable())
        crepeThread.join();
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
    resetPitchState (preparedBlockSize);
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
    lastPitchFrame = {};
    smoothedPitchScale = 1.0;
    lastFedPitchScale = 1.0;
    smoothedXposeScale = 1.0;
    autoKeyHistogram.fill (0.0);
    detectedAutoRoot = 9;
    detectedAutoScale = 1;
    lastTargetMidiForTransition = -1000;
    transitionBoost = 0.0;
    flexEngageState = 0.0;
    vibratoCenterMidi = -1000.0;
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
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    resetRbFifo();
#endif
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
        case 7: minHz =  40.0; maxHz =  500.0; break;   // Instrument: Bass
        case 8: minHz =  60.0; maxHz = 1500.0; break;   // Instrument: Mid
        case 9: minHz = 110.0; maxHz = 2200.0; break;   // Instrument: High
        default: break;                                  // voice ranges -> unchanged
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
    result.midi = midiFromHz (frequencyHz);
    result.confidence = periodicity;
    result.voiced = frequencyHz >= rangeMinHz && frequencyHz <= rangeMaxHz;
    return result;
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
    if (keyMapDriving())
        return juce::jlimit (0, 11, detectedAutoRoot);

    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f)
        return juce::jlimit (0, 11, detectedAutoRoot);

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

    if (keyMapDriving())
        return juce::jlimit (0, 2, detectedAutoScale);

    const int userScale = juce::roundToInt (apvts.getRawParameterValue ("scale")->load());
    // AutoKey auto-detects the mode too (unless the user picked Chromatic, which
    // means "no scale snapping" and has no major/minor to detect).
    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f && userScale != 2)
        return juce::jlimit (0, 1, detectedAutoScale);

    return userScale;
}

bool SattariAutoPitchAudioProcessor::keyMapDriving() const
{
    return apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f
        && keyMapCurrentSegment.load (std::memory_order_relaxed) >= 0;
}

SattariAutoPitchAudioProcessor::KeyOrigin SattariAutoPitchAudioProcessor::currentKeyOrigin() const
{
    KeyOrigin o;

    // Same precedence as currentCorrectionRootIndex() / effectiveScaleIndex(),
    // but returns *why* along with the effective key so the UI can be honest.
    if (apvts.getRawParameterValue ("followSongKey")->load() > 0.5f)
        if (const auto s = sessionReader.current(); s.valid)
        {
            o.source = KeyOrigin::Source::FollowBus;
            o.root   = juce::jlimit (0, 11, s.root);
            o.scale  = s.scale == 1 ? 1 : (s.scale == 2 ? 2 : 0);
            return o;
        }

    if (keyMapDriving())
    {
        o.source = KeyOrigin::Source::KeyMap;
        o.root   = juce::jlimit (0, 11, detectedAutoRoot);
        o.scale  = juce::jlimit (0, 2, detectedAutoScale);
        return o;
    }

    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f)
    {
        o.source = KeyOrigin::Source::AutoKey;
        o.root   = juce::jlimit (0, 11, detectedAutoRoot);
        o.scale  = juce::jlimit (0, 2, effectiveScaleIndex());   // AutoKey auto-detects the mode
        return o;
    }

    o.source = KeyOrigin::Source::Manual;
    o.root   = juce::jlimit (0, 11, juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load()));
    o.scale  = juce::jlimit (0, 2, juce::roundToInt (apvts.getRawParameterValue ("scale")->load()));
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
    const int scaleIndex = juce::roundToInt (apvts.getRawParameterValue ("scale")->load());
    const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];

    int bestRoot = detectedAutoRoot;

    if (scaleName == "major" || scaleName == "minor")
    {
        // AutoKey picks BOTH the root AND the mode (major vs minor); detectedAutoScale
        // then drives the effective scale (see effectiveScaleIndex). Prefer the NEURAL
        // key when the sidechain (full-mix) path has produced a valid inference — it's
        // ~58% exact vs ~45% for the profile on real mixes. Absent a sidechain the
        // neural result never validates and we fall back to the pitch-histogram
        // detector, which is the right tool for a monophonic vocal.
        if (keyNetPresent && keyNetLiveValid.load (std::memory_order_acquire))
        {
            bestRoot = keyNetLiveRoot.load (std::memory_order_relaxed);
            detectedAutoScale = keyNetLiveMinor.load (std::memory_order_relaxed);
        }
        else
        {
            const auto kr = sattari::dsp::detectKey (autoKeyHistogram.data());
            if (kr.valid)
            {
                bestRoot = kr.root;
                detectedAutoScale = kr.minor ? 1 : 0;
            }
        }
    }
    else
    {
        // Chromatic / non-modal: "which root maximises in-scale energy".
        double bestScore = -2.0;
        for (int root = 0; root < 12; ++root)
        {
            double score = 0.0;
            for (int noteIndex = 0; noteIndex < 12; ++noteIndex)
            {
                const int degree = ((noteIndex - root) % 12 + 12) % 12;
                score += autoKeyHistogram[(size_t) noteIndex]
                       * (scaleAllowsDegree (degree, scaleName) ? 1.0 : -0.18);
            }
            if (score > bestScore) { bestScore = score; bestRoot = root; }
        }
    }

    detectedAutoRoot = bestRoot;
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

    // CAS throttle: the editor timer and the worker thread both call this, so claim the
    // 1 s window atomically — only the thread that wins the exchange writes the file.
    const auto now = juce::Time::currentTimeMillis();
    juce::int64 prev = lastBusPublishMs.load (std::memory_order_relaxed);
    if (now - prev < 1000) return;
    if (! lastBusPublishMs.compare_exchange_strong (prev, now, std::memory_order_relaxed)) return;

    const int root  = currentCorrectionRootIndex();
    const int scale = juce::jlimit (0, 2, effectiveScaleIndex());
    // Publish the full session (key + host tempo + transient pulse) so other
    // Sattari plugins can follow the key AND lock to tempo / react to onsets.
    // Backward-compatible: key-only readers still parse this fine.
    sattari::xplugin::publishSession ("auto-pitch", root, scale,
                                      hostBpmForBus.load(),
                                      sessionTransientId.load());
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

    // --- Peak picking in the correctable band ------------------------------
    const int firstBin = juce::jmax (2, (int) std::ceil (60.0   / freqPerBin));
    const int lastBin  = juce::jmin (half - 2, (int) std::floor (2500.0 / freqPerBin));
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
    // Pitch-class mask for the harmony walk — same for every peak, so build it once.
    bool harmonyScaleMask[12] = {};
    if (polyHarmonyAmt > 0.0f)
        for (int pc = 0; pc < 12; ++pc)
            harmonyScaleMask[pc] = scaleAllowsDegree ((((pc - rootIndex) % 12) + 12) % 12, scaleName) != 0;
    for (int pi = 0; pi < (int) pvPeakBins.size(); ++pi)
    {
        const int fund = pvPeakFund[(size_t) pi];
        const double f = juce::jmax (1.0, (double) pvFreq[(size_t) pvPeakBins[(size_t) fund]]);
        const double srcMidi = 69.0 + 12.0 * std::log2 (f / 440.0);
        int target = nearestScaleMidi (srcMidi, rootIndex, scaleName);   // honours chromatic
        if (! pcEnabled (target))
            for (int r = 1; r <= 12; ++r)
            {
                if (pcEnabled (target - r)) { target = target - r; break; }
                if (pcEnabled (target + r)) { target = target + r; break; }
            }
        const double targetFreq = 440.0 * std::pow (2.0, ((double) target - 69.0) / 12.0);
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
            const double hFreq = 440.0 * std::pow (2.0, ((double) hm - 69.0) / 12.0);
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
    keyMapLastError.clear();
    keyMapScanThread = std::make_unique<std::thread> ([this, audioFile] { runKeyMapScan (audioFile); });
    return true;
}

void SattariAutoPitchAudioProcessor::clearKeyMap()
{
    keyMapScanCancel.store (true);   // stop an in-progress scan promptly before join
    if (keyMapScanThread && keyMapScanThread->joinable())
        keyMapScanThread->join();
    keyMapScanThread.reset();
    keyMapScanCancel.store (false);
    const juce::ScopedLock l (keyMapLock);
    keyMap.clear();
    keyMapWave.clear();
    keyMapDurationSec = 0.0;
    keyMapFileName.clear();
    keyMapProgress.store (-1);
    keyMapCurrentSegment.store (-1);
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
        keyMapLastError = "Could not open audio file";
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

    while (pos + windowSamples <= totalLen)
    {
        // setStateInformation() (or teardown/clear) can ask us to stop so a scan
        // that started before the host restored state doesn't clobber the restored
        // map. Poll every window and bail before publishing.
        if (keyMapScanCancel.load (std::memory_order_relaxed))
            break;

        reader->read (&readBuf, 0, windowSamples, pos, true, true);
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
            const int envSpan = juce::jmin (hopSamples, windowSamples);
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
            pos += hopSamples;
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
                const auto kr = sattari::dsp::detail::detectKeyWithProfiles (
                                    mean.data(), sattari::dsp::detail::kDefaultMajor, sattari::dsp::detail::kDefaultMinor);
                if (kr.valid) { bestRoot = kr.root; bestMode = kr.minor ? 1 : 0; bestScore = (float) kr.confidence; }
            }
        }

        wins.push_back ({ (double) pos / sr, bestRoot, bestMode, bestScore });

        pos += hopSamples;
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
        keyMapProgress.store (-1);
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
// Looks up the segment covering `seconds` and writes the result to
// `detectedAutoRoot` + (best-effort) the user's scale param.
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
    if (! l.isLocked() || keyMap.empty())
        return;

    // Binary search for the latest entry whose timeSec <= seconds.
    int lo = 0, hi = (int) keyMap.size() - 1, idx = 0;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        if (keyMap[(size_t) mid].timeSec <= seconds)
        {
            idx = mid;
            lo = mid + 1;
        }
        else hi = mid - 1;
    }

    const auto& e = keyMap[(size_t) idx];
    detectedAutoRoot = juce::jlimit (0, 11, e.rootIdx);
    detectedAutoScale = juce::jlimit (0, 2, e.scaleIdx);
    keyMapCurrentSegment.store (idx);
}

SattariAutoPitchAudioProcessor::PitchFrame SattariAutoPitchAudioProcessor::buildCorrectionFrame (PitchFrame detected) const
{
    if (! detected.voiced)
        return detected;

    const int rootIndex = currentCorrectionRootIndex();
    const int scaleIndex = effectiveScaleIndex();
    const auto scaleName = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    int targetMidi = nearestScaleMidi (detected.midi, rootIndex, scaleName);

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

float SattariAutoPitchAudioProcessor::styleCorrectionStrength() const
{
    const auto style = currentStyleChoice();
    const float amount = apvts.getRawParameterValue ("amount")->load();
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
    const auto style = currentStyleChoice();
    const float speed = apvts.getRawParameterValue ("speed")->load();
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

    double strength = (double) styleCorrectionStrength();

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

    const double limitedCents = juce::jlimit (-700.0, 700.0, frame.correctionCents * strength);
    return std::pow (2.0, limitedCents / 1200.0);
}

void SattariAutoPitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
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
    if (psolaSwapReady.exchange (false, std::memory_order_acquire))
    {
        psola.swap (pendingPsola);
        for (auto& p : psola) p.reset();
        psolaLiveLowLat.store (pendingLowLat, std::memory_order_release);
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

    const bool autoKeyOn = apvts.getRawParameterValue ("autoKey")->load() > 0.5f;
    const bool useKeyMap = apvts.getRawParameterValue ("useKeyMap")->load() > 0.5f;

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
                // Always publish the playhead so the editor's Key Map timeline can
                // draw a position marker even when Use Key Map is off.
                keyMapPlayheadSec.store (*t, std::memory_order_relaxed);
                if (useKeyMap)
                    applyKeyMapAtSeconds (*t);
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
                    sidechainSeenAudio = true;
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
        && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled();
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
            sidechainSeenAudio = true;   // prefer the chroma/neural key over the mono histogram
        pushSidechainBlock (scMonoScratch.data(), n);
    }

    // Computed once per block: neural pitch feeds BOTH the correction override
    // (below) and the AutoKey histogram (in the loop) when a CREPE model is active.
    const bool neuralOn   = apvts.getRawParameterValue ("neuralPitch")->load() > 0.5f;
    const bool neuralHist = neuralOn && crepeModelPresent;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            mono += mainBuffer.getSample (channel, i);
        pushPitchSample (mono / (float) numChannels);

        if (--pitchSamplesUntilAnalysis <= 0)
        {
            auto detected = analyseCurrentPitchFrame();
            // The mono histogram and the Key Map both write detectedAutoRoot, and this
            // runs AFTER applyKeyMapAtSeconds in the block — so while the Key Map is
            // driving, updating the histogram here would clobber the map's key with the
            // singer's own, retuning to the wrong key while the UI still says "Key Map".
            // Skip it when the map drives; AutoKey and manual (display) modes are
            // unaffected. The sidechain path uses the neural chroma detector instead.
            if (! keyMapDriving() && ! sidechainSeenAudio)
            {
                // Neural-assisted AutoKey: when CREPE is active + confident, feed
                // its more-accurate pitch class into the histogram instead of YIN's,
                // so key detection is cleaner on breathy/noisy vocals.
                PitchFrame forHist = detected;
                if (neuralHist)
                {
                    const float chz = crepeResultHz.load (std::memory_order_relaxed);
                    if (crepeResultVoiced.load (std::memory_order_relaxed) && chz > 0.0f)
                    {
                        forHist.frequencyHz = chz;
                        forHist.midi        = midiFromHz (chz);
                        forHist.confidence  = juce::jmax (forHist.confidence,
                                                          (double) crepeResultConf.load (std::memory_order_relaxed));
                        forHist.voiced      = true;
                    }
                }
                updateAutoKeyHistogram (forHist);
            }
            lastPitchFrame = buildCorrectionFrame (detected);
            pitchSamplesUntilAnalysis = pitchHopSize;
        }
    }

    // Neural pitch (CREPE) override: when a model is installed and enabled, the
    // CNN estimate replaces YIN's for correction too — it does not octave-jump on
    // breathy/noisy vocals. The heavy ONNX inference runs on crepeThread; here we
    // only feed the ring and read the worker's result atomics.
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
        if (crVoiced && crHz > 0.0f && lastPitchFrame.voiced)
        {
            // Range-gate the CREPE estimate to the selected Sound Source, exactly
            // like the YIN gate (analyseCurrentPitchFrame -> result.voiced). An
            // out-of-range neural estimate (an octave error, sub-bass rumble, a
            // high harmonic) is NOT a valid correction basis: treat it as unvoiced
            // so the signal passes through untouched rather than being retuned to a
            // wrong note. Without this the CREPE override bypassed the voiced-range
            // gate that YIN honours.
            double rMinHz = minVoiceHz, rMaxHz = maxVoiceHz;
            voicedRangeHzForSource (rMinHz, rMaxHz);
            if ((double) crHz >= rMinHz && (double) crHz <= rMaxHz)
            {
                PitchFrame nf = lastPitchFrame;
                nf.frequencyHz = crHz;
                nf.midi        = midiFromHz (crHz);
                nf.confidence  = juce::jmax (nf.confidence, (double) crConf);
                lastPitchFrame = buildCorrectionFrame (nf);
            }
            else
            {
                lastPitchFrame.voiced = false;   // out-of-range neural pitch -> no correction
            }
        }
    }

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
    const double transition = (double) apvts.getRawParameterValue ("transition")->load();

    if (lastPitchFrame.voiced)
    {
        const int tgt = (int) std::llround (lastPitchFrame.targetMidi);
        if (tgt != lastTargetMidiForTransition)
        {
            transitionBoost = 1.0;                 // fresh note: snap fast
            lastTargetMidiForTransition = tgt;
            sessionTransientId.fetch_add (1);      // pulse for the cross-plugin bus
            addVibratoRampSamples = 0.0;           // synthetic vibrato blooms afresh on each note
        }
    }
    // Decay the boost once per block (~time constant 60 ms).
    {
        const double blockTime = (double) numSamples / currentSampleRate;
        transitionBoost *= std::exp (-blockTime / 0.060);
    }

    // --- Vibrato preservation -------------------------------------------------
    // Correct the note CENTRE to the scale, but re-inject a fraction of the
    // singer's fast pitch oscillation so hard tuning doesn't flatten expressive
    // vibrato. vibratoCenterMidi is a slow low-pass of the detected pitch (tracks
    // the note centre, lags 5-7 Hz vibrato); the residual (detected - centre) is
    // the vibrato we keep. Reset on note onset so new notes still snap; disabled
    // in Classic Mode (the deliberate dead-flat sound).
    const double vibRetain = (double) apvts.getRawParameterValue ("vibrato")->load();
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
        const double keep = vibRetain * (1.0 - transitionBoost) * vibSemis;
        targetPitchScale *= std::pow (2.0, keep / 12.0);
        targetPitchScale = juce::jlimit (0.5, 2.0, targetPitchScale);
    }
    else
    {
        vibratoCenterMidi = -1000.0;   // disarmed when off
    }

    // Sustain retune from the Style/Speed control; onset retune is much faster.
    const double sustainRetune = (double) styleRetuneCoefficient();
    const double onsetRetune   = juce::jlimit (0.0, 1.0, sustainRetune + 0.55);
    // Blend by the boost, scaled by the Transition amount. Classic Mode forces
    // near-instant retune regardless (the hard snap).
    double retune = classicMode
        ? 0.95
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
    const float humanize = apvts.getRawParameterValue ("humanize")->load();
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
    const double addVibDepth = (double) apvts.getRawParameterValue ("addVibrato")->load();   // cents
    if (addVibDepth > 0.0001 && lastPitchFrame.voiced && ! polyOn)
    {
        const double rate  = (double) apvts.getRawParameterValue ("vibratoRate")->load();    // Hz
        const double onset = (double) apvts.getRawParameterValue ("vibratoOnset")->load();   // ms
        const double onsetSamples = juce::jmax (1.0, onset * 0.001 * currentSampleRate);
        const double ramp  = juce::jlimit (0.0, 1.0, addVibratoRampSamples / onsetSamples);
        const double cents = addVibDepth * ramp * std::sin (addVibratoPhase);
        engineScale *= std::pow (2.0, cents / 1200.0);
        engineScale  = juce::jlimit (0.5, 2.0, engineScale);

        // Advance the LFO phase + onset ramp by exactly this block.
        addVibratoPhase += juce::MathConstants<double>::twoPi * rate * (double) numSamples / currentSampleRate;
        if (addVibratoPhase > juce::MathConstants<double>::twoPi)
            addVibratoPhase -= juce::MathConstants<double>::twoPi;
        addVibratoRampSamples += (double) numSamples;
    }

    // Input drive and output trim are kept SEPARATE. Input gain feeds the pitch
    // engine and the Style stage — turning it up is meant to push the Classic/Robot
    // saturators and the harmony blend harder (it is a drive control). Output gain
    // is a clean level trim applied AFTER the Style stage (below), so raising output
    // level never drives the saturators/harmony into extra distortion — it just sets
    // how hot the block leaves the plugin, bounded by the final soft ceiling.
    const float inputDb   = apvts.getRawParameterValue ("inputGain")->load();
    const float outputDb  = apvts.getRawParameterValue ("outputGain")->load();
    const float inputGain  = juce::Decibels::decibelsToGain (inputDb);
    const float outputGain = juce::Decibels::decibelsToGain (outputDb);

    // Engine select. Live Engine (PSOLA) is the near-zero-latency monitor path;
    // Rubber Band is the default higher-fidelity "print" engine. Low Latency is an
    // alias for the same low-latency PSOLA path — turning EITHER on swaps to PSOLA
    // at runtime, which is the honest runtime effect that makes Low Latency more
    // than a dead prepare-only toggle. The reported latency (prepareToPlay) tracks
    // the active engine, so the delay-comp figure matches the audible latency.
    const bool liveEngine = apvts.getRawParameterValue ("liveEngine")->load() > 0.5f;
    const bool lowLatency = apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
    const bool useLiveEngine = liveEngine || lowLatency;

    // Engine swapped since last block: flush BOTH paths so the switch is clean. The
    // OUTGOING engine still holds ~one pipeline of already-processed audio (the RB
    // FIFO ~= the start-delay + cushion, or the PSOLA output ring) — without a flush
    // that stale audio would either be replayed on a later switch back or leave the
    // INCOMING engine to re-prime while the block seam splices across a phase
    // discontinuity (an audible ~28 ms dropout / click). Resetting both makes the
    // incoming engine re-prime cleanly from silence (its reported start-up latency)
    // and guarantees the outgoing engine is empty for its next turn. Allocation-free.
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

    if (xposeActive)
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
    else if (useLiveEngine)
    {
        processWithPsola (mainBuffer, engineScale, inputGain);
    }
    else
    {
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        processWithRubberBand (mainBuffer, engineScale, inputGain);
#else
        processFallbackFormantGuide (mainBuffer, engineScale, inputGain);
#endif
    }
    applyModeCharacter (mainBuffer);

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

void SattariAutoPitchAudioProcessor::processWithRubberBand (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain,
                                                            bool straightTranspose)
{
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Oversized-block guard. Rubber Band was configured with
    // setMaxProcessSize(preparedBlockSize) and rubberBandOutput / rbFifo are sized
    // for that. If a host ever delivers a block larger than it declared in
    // prepareToPlay, feeding it whole would (a) overrun Rubber Band's max process
    // size (contract violation) and (b) reallocate rubberBandOutput mid-callback.
    // Instead, process the block in <=preparedBlockSize sub-chunks that alias the
    // buffer's channels (no allocation — AudioBuffer's referencing ctor uses inline
    // channel-pointer storage for <=32 channels). Each sub-chunk recurses exactly
    // once (its length is already within the cap).
    const int maxChunk = juce::jmax (1, preparedBlockSize);
    if (numSamples > maxChunk)
    {
        for (int offset = 0; offset < numSamples; offset += maxChunk)
        {
            const int chunk = juce::jmin (maxChunk, numSamples - offset);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numChannels, offset, chunk);
            processWithRubberBand (sub, pitchScale, linearGain, straightTranspose);
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
    const float formantShiftSemis = apvts.getRawParameterValue ("formantShift")->load();
    // Throat: vocal-tract-length ratio (1.0 = natural). Combines multiplicatively
    // with the semitone Formant Shift so both controls move the formant envelope
    // together. A throat > 1 lengthens the tract (darker/bigger); < 1 shortens it.
    const double throat = (double) apvts.getRawParameterValue ("throat")->load();
    const double shiftRatio = std::pow (2.0, (double) formantShiftSemis / 12.0);
    const double combinedFormant = throat * shiftRatio;
    // 0.0 tells Rubber Band to auto-preserve (default). We send an explicit scale
    // whenever Throat or Formant Shift departs from neutral.
    const bool neutralFormant = (std::abs (combinedFormant - 1.0) < 1.0e-4);
    const double formantScale = neutralFormant ? 0.0 : combinedFormant;

    // If for any reason a channel has no stretcher, fall back to dry passthrough
    // for the whole block (keeps channels phase-locked; never mid-block spliced).
    const bool haveAllStretchers =
        (int) rubberBand.size() >= numChannels
        && std::all_of (rubberBand.begin(), rubberBand.begin() + numChannels,
                        [] (const auto& s) { return s != nullptr; });
    if (! haveAllStretchers)
    {
        buffer.applyGain (linearGain);
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
        rbPrimed = true;

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
    if (style == "Natural" || style == "Classic")
    {
        if (style == "Classic")
        {
            // Gentle vintage warmth: near-unity low-level gain (0.65 * 1.5), soft peak
            // compression. ~1% THD at -6 dBFS, ~3% at 0 dBFS — musical, not the 8% the old
            // 1.12/0.96 drive produced. Character you'd reach for, not a distortion pedal.
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (channel, i, std::tanh (buffer.getSample (channel, i) * 0.65f) * 1.5f);
        }
        return;
    }

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

    if (style == "Modern Rap")
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            float previous = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto sample = buffer.getSample (channel, i);
                const auto edge = sample - previous;
                previous = sample;
                buffer.setSample (channel, i, softCeiling (sample * 1.02f + edge * 0.08f));
            }
        }
        return;
    }

    if (style == "Big Harmony")
    {
        auto& base = harmonyScratch;
        const int baseChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();
        base.setSize (baseChannels, numSamples, false, false, true); // preallocated: no realloc
        for (int channel = 0; channel < baseChannels; ++channel)
            base.copyFrom (channel, 0, buffer, channel, 0, numSamples);
        const double centre = (double) (numSamples - 1) * 0.5;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const double upPosition = centre + ((double) i - centre) / 1.25992104989;   // +4 semitones flavour layer
                const double downPosition = centre + ((double) i - centre) / 0.84089641525; // -3 semitones flavour layer
                const auto dry = base.getSample (channel, i);
                const auto up = readLinearInterpolated (base, channel, upPosition);
                const auto down = readLinearInterpolated (base, channel, downPosition);
                buffer.setSample (channel, i, softCeiling (dry * 0.68f + up * 0.2f + down * 0.12f));
            }
        }
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
    const auto origin = currentKeyOrigin();
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, origin.root)];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, origin.scale)];
    const bool liveEngine = apvts.getRawParameterValue ("liveEngine")->load() > 0.5f
                         || apvts.getRawParameterValue ("lowLatency")->load() > 0.5f;
    const juce::String engine =
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        liveEngine ? "PSOLA" : "Rubber Band";
#else
        liveEngine ? "PSOLA" : "Guide";
#endif
    const juce::String dot = juce::String::fromUTF8 ("   \xc2\xb7   ");   // "  ·  "

    if (! lastPitchFrame.voiced)
        return engine + dot + root + " " + scale + dot + "waiting for input";

    return engine + dot + root + " " + scale + dot + currentStyleChoice() + dot
        + midiNoteName (lastPitchFrame.midi) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + midiNoteName (lastPitchFrame.targetMidi)
        + dot + juce::String (lastPitchFrame.correctionCents, 1) + " ct"
        + dot + juce::String (lastPitchFrame.confidence * 100.0, 0) + "%";
}

sattari::audio_core::KeySignature SattariAutoPitchAudioProcessor::currentKeySignature() const
{
    const int scaleIndex = effectiveScaleIndex();
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, scaleIndex)];
    sattari::audio_core::MusicalMode mode = sattari::audio_core::MusicalMode::Unknown;
    if (scale == "major")
        mode = sattari::audio_core::MusicalMode::Major;
    else if (scale == "minor")
        mode = sattari::audio_core::MusicalMode::Minor;

    const double traceConfidence = (double) apvts.getRawParameterValue ("traceConfidence")->load();
    const double liveConfidence = lastPitchFrame.voiced ? lastPitchFrame.confidence : 0.0;
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
    // Minimal: just the key and how sure we are. (The harmonic-wheel / compatibility /
    // bus-sharing detail used to live here but read as insider jargon in the UI.)
    const auto key = currentKeySignature();
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, key.tonicPitchClass)];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, effectiveScaleIndex())];
    const int confidence = juce::roundToInt (key.confidence * 100.0);
    return "Key   " + root + " " + scale
        + juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String (confidence) + "% confidence";
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
    setParameterValue ("scale", (float) scaleToChoiceIndex (scale));
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

    midiLearn.saveTo (state);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SattariAutoPitchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto restored = juce::ValueTree::fromXml (*xml);
        lastTraceSummary = restored.getProperty ("lastTraceSummary", "No trace loaded").toString();

        // A background key-map scan may already be running (started before the host
        // pushed this state). Cancel + join it BEFORE we overwrite keyMap, otherwise
        // it would finish later and clobber the timeline we're about to restore. The
        // scan loop polls keyMapScanCancel every window, so the join returns almost
        // immediately — setStateInformation stays effectively non-blocking.
        keyMapScanCancel.store (true);
        if (keyMapScanThread && keyMapScanThread->joinable())
            keyMapScanThread->join();
        keyMapScanThread.reset();
        keyMapScanCancel.store (false);

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
            const juce::ScopedLock l (keyMapLock);
            keyMap = std::move (rebuilt);
            keyMapFileName = km.getProperty ("file", "").toString();
            keyMapProgress.store (keyMap.empty() ? -1 : 100);
        }

        midiLearn.loadFrom (restored);

        apvts.replaceState (restored);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariAutoPitchAudioProcessor();
}
