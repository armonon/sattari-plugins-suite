#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <cmath>

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
}

SattariAutoPitchAudioProcessor::~SattariAutoPitchAudioProcessor()
{
    // Stop and join the offline scan thread cleanly so we never leak a worker
    // across plugin unloads (the worker holds a juce::AudioFormatReader that
    // owns a file handle).
    if (keyMapScanThread && keyMapScanThread->joinable())
        keyMapScanThread->join();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariAutoPitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("keyRoot", "Key Root", rootChoices, 9));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", scaleChoices, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("style", "Style", styleChoices, 1));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("enableCorrection", "Enable Correction", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("autoKey", "AutoKey Follow", false));
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
    params.push_back (std::make_unique<juce::AudioParameterBool> ("lowLatency", "Low Latency", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("useKeyMap", "Use Key Map", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("humanize", "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    // 12-bit mask of enabled pitch classes (bit 0 = C, bit 11 = B). Default
    // 4095 = all 12 notes valid. The Edit Notes pill row clicks toggle bits
    // (shift-click sets root, plain click blocks/unblocks the note). When a
    // note is blocked, correction snaps to the next nearest enabled note.
    params.push_back (std::make_unique<juce::AudioParameterInt> ("enabledNotes", "Enabled Notes",
        0, 4095, 4095));
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
    resetPitchState (preparedBlockSize);

    const int scratchChannels = juce::jmax (2, getTotalNumOutputChannels());
    formantScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    harmonyScratch.setSize (scratchChannels, preparedBlockSize, false, false, true);
    formantScratch.clear();
    harmonyScratch.clear();

    // Chroma FFT setup (allocation-free after this). Hann-windowed magnitude
    // spectrum is mapped to 12 pitch classes for polyphonic AutoKey on the SC.
    chromaFft = std::make_unique<juce::dsp::FFT> (chromaFftOrder);
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

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    rubberBand.clear();
    const int numChannels = juce::jmax (1, getTotalNumOutputChannels());
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto stretcher = std::make_unique<RubberBand::RubberBandStretcher> (
            currentSampleRate,
            1,
            RubberBand::RubberBandStretcher::OptionProcessRealTime
                | RubberBand::RubberBandStretcher::OptionPitchHighQuality
                | RubberBand::RubberBandStretcher::OptionTransientsMixed
                | RubberBand::RubberBandStretcher::OptionDetectorSoft,
            1.0,
            1.0);
        stretcher->setMaxProcessSize ((size_t) preparedBlockSize);
        stretcher->setPitchScale (1.0);
        rubberBand.push_back (std::move (stretcher));
    }
    rubberBandOutput.setSize (numChannels, preparedBlockSize);
    rubberBandScratch.resize ((size_t) preparedBlockSize, 0.0f);
#endif
}

void SattariAutoPitchAudioProcessor::releaseResources()
{
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

void SattariAutoPitchAudioProcessor::resetPitchState (int)
{
    pitchFrameBuffer.assign ((size_t) pitchFrameSize, 0.0f);
    pitchWriteIndex = 0;
    pitchSamplesUntilAnalysis = pitchHopSize;
    lastPitchFrame = {};
    smoothedPitchScale = 1.0;
    autoKeyHistogram.fill (0.0);
    detectedAutoRoot = 9;
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

// Auto Pitch's pitch follower is deliberately NOT migrated to the shared
// sattari::dsp::PitchDetector. The shared detector targets general suite use
// (Sub Conjurer, VoxKey, Royal Chain) and: (1) does not Hann-window the analysis
// frame, and (2) picks the SHORTEST qualifying lag to dodge sub-harmonic errors.
// Auto Pitch instead Hann-windows the frame (cleaner spectral peak for retune)
// and picks the highest-score lag. Both behaviours are baked into the
// AutoPitchCoreContract test (precise -33.5 cents correction), so this is kept
// bespoke. The shared module remains the right choice everywhere else.
SattariAutoPitchAudioProcessor::PitchFrame SattariAutoPitchAudioProcessor::analyseCurrentPitchFrame() const
{
    PitchFrame result;
    if (currentSampleRate <= 0.0 || pitchFrameBuffer.empty())
        return result;

    std::array<float, pitchFrameSize> frame {};
    double rms = 0.0;
    for (int i = 0; i < pitchFrameSize; ++i)
    {
        const int index = (pitchWriteIndex + i) % pitchFrameSize;
        const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (pitchFrameSize - 1));
        const float sample = pitchFrameBuffer[(size_t) index] * window;
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
    const int sourceIdx = juce::roundToInt (apvts.getRawParameterValue ("vocalRange")->load());
    double rangeMinHz = minVoiceHz, rangeMaxHz = maxVoiceHz;
    switch (sourceIdx)
    {
        case 7: rangeMinHz =  40.0; rangeMaxHz =  500.0; break;   // Instrument: Bass
        case 8: rangeMinHz =  60.0; rangeMaxHz = 1500.0; break;   // Instrument: Mid
        case 9: rangeMinHz = 110.0; rangeMaxHz = 2200.0; break;   // Instrument: High
        default: break;                                            // voice ranges -> unchanged
    }
    const int minLag = juce::jlimit (1, pitchFrameSize - 2, (int) std::floor (currentSampleRate / rangeMaxHz));
    const int maxLag = juce::jlimit (minLag + 1, pitchFrameSize - 2, (int) std::ceil (currentSampleRate / rangeMinHz));

    double bestScore = 0.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int i = 0; i < pitchFrameSize - lag; ++i)
        {
            const double a = frame[(size_t) i];
            const double b = frame[(size_t) (i + lag)];
            corr += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denom = std::sqrt (energyA * energyB) + 1.0e-12;
        const double score = corr / denom;
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestScore < minConfidence)
    {
        result.confidence = bestScore;
        return result;
    }

    const double frequencyHz = currentSampleRate / (double) bestLag;
    result.frequencyHz = frequencyHz;
    result.midi = midiFromHz (frequencyHz);
    result.confidence = juce::jlimit (0.0, 1.0, bestScore);
    result.voiced = frequencyHz >= rangeMinHz && frequencyHz <= rangeMaxHz;
    return result;
}

int SattariAutoPitchAudioProcessor::currentCorrectionRootIndex() const
{
    if (apvts.getRawParameterValue ("autoKey")->load() > 0.5f)
        return juce::jlimit (0, 11, detectedAutoRoot);

    return juce::jlimit (0, 11, juce::roundToInt (apvts.getRawParameterValue ("keyRoot")->load()));
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

    // Krumhansl-Schmuckler key-profile correlation for major/minor (the textbook
    // music-IR algorithm — pitch-classes are weighted by their perceptual key-
    // definition strength, not just in/out of scale). Highest Pearson correlation
    // between the rolling histogram and a candidate key's profile wins.
    static constexpr double kProfileMajor[12] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
    };
    static constexpr double kProfileMinor[12] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
    };

    auto pearson = [this] (const double* profile, int root) -> double
    {
        double meanH = 0.0, meanP = 0.0;
        for (int i = 0; i < 12; ++i) { meanH += autoKeyHistogram[(size_t) i]; meanP += profile[i]; }
        meanH /= 12.0; meanP /= 12.0;
        double num = 0.0, dH = 0.0, dP = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double h = autoKeyHistogram[(size_t) ((i + root) % 12)] - meanH;
            const double p = profile[i] - meanP;
            num += h * p; dH += h * h; dP += p * p;
        }
        const double denom = std::sqrt (dH * dP);
        return denom > 1.0e-9 ? num / denom : -1.0;
    };

    double bestScore = -2.0;
    int bestRoot = detectedAutoRoot;

    if (scaleName == "major" || scaleName == "minor")
    {
        // Correlate against the user-selected mode's profile across all 12 roots.
        const double* profile = (scaleName == "major") ? kProfileMajor : kProfileMinor;
        for (int root = 0; root < 12; ++root)
        {
            const double s = pearson (profile, root);
            if (s > bestScore) { bestScore = s; bestRoot = root; }
        }
    }
    else
    {
        // Chromatic / non-modal: fall back to "which root maximises in-scale energy",
        // same shape as before but using the shared scaleAllowsDegree() helper.
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
    // Throttle to once a second so we never thrash the bus file.
    const auto now = juce::Time::currentTimeMillis();
    static juce::int64 lastPublishMs = 0;
    if (now - lastPublishMs < 1000) return;
    lastPublishMs = now;

    const int root  = currentCorrectionRootIndex();
    const int scale = juce::jlimit (0, 11,
        juce::roundToInt (apvts.getRawParameterValue ("scale")->load()));
    sattari::xplugin::publishKey ("auto-pitch", root, scale);
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
        chromaSamplesSinceFft = 0;
        runChromaAnalysis();
        correlateAutoKeyHistogram();
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

    // Map FFT magnitude bins to pitch classes (sum power per pc). Bins below ~A1
    // (55 Hz) and above ~C8 (4186 Hz) are out of musical range -> skip.
    std::array<double, 12> pcAccum {};
    const double binHz = currentSampleRate / (double) chromaFftSize;
    const int firstBin = juce::jmax (1, (int) std::ceil (55.0  / binHz));
    const int lastBin  = juce::jmin (chromaFftSize / 2, (int) std::floor (4186.0 / binHz));
    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const double f = bin * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const int pc = ((int) std::round (midi) % 12 + 12) % 12;
        const float mag = chromaFftScratch[(size_t) bin];
        pcAccum[(size_t) pc] += (double) mag * mag;   // power, not amplitude
    }

    // Normalise -> chroma vector summing to 1. Then blend it into the histogram
    // with a moderate weight so the rolling estimate stays smooth.
    double total = 0.0;
    for (double v : pcAccum) total += v;
    if (total < 1.0e-9) return;
    for (auto& v : pcAccum) v /= total;

    constexpr double blend = 0.35;
    for (size_t i = 0; i < autoKeyHistogram.size(); ++i)
        autoKeyHistogram[i] = (1.0 - blend) * autoKeyHistogram[i] + blend * pcAccum[i];
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

    keyMapScanning.store (true);
    keyMapProgress.store (0);
    keyMapLastError.clear();
    keyMapScanThread = std::make_unique<std::thread> ([this, audioFile] { runKeyMapScan (audioFile); });
    return true;
}

void SattariAutoPitchAudioProcessor::clearKeyMap()
{
    if (keyMapScanThread && keyMapScanThread->joinable())
        keyMapScanThread->join();
    keyMapScanThread.reset();
    const juce::ScopedLock l (keyMapLock);
    keyMap.clear();
    keyMapFileName.clear();
    keyMapProgress.store (-1);
    keyMapCurrentSegment.store (-1);
    keyMapScanning.store (false);
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
    const double durationSec = (double) totalLen / sr;

    // Window the scan: ~3.5 second analysis windows hop every ~1 second so the
    // map captures mid-song key changes without spamming segments.
    const int windowSamples = juce::jmax (8192, (int) (sr * 3.5));
    const int hopSamples    = juce::jmax (1024, (int) (sr * 1.0));

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

    static constexpr double kProfileMajor[12] = { 6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88 };
    static constexpr double kProfileMinor[12] = { 6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17 };

    auto pearson = [] (const std::array<double,12>& h, const double* profile, int root) -> double
    {
        double meanH = 0.0, meanP = 0.0;
        for (int i = 0; i < 12; ++i) { meanH += h[(size_t) i]; meanP += profile[i]; }
        meanH /= 12.0; meanP /= 12.0;
        double num = 0.0, dH = 0.0, dP = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double dh = h[(size_t) ((i + root) % 12)] - meanH;
            const double dp = profile[i] - meanP;
            num += dh * dp; dH += dh * dh; dP += dp * dp;
        }
        const double d = std::sqrt (dH * dP);
        return d > 1.0e-9 ? num / d : -1.0;
    };

    std::vector<KeyMapEntry> built;
    juce::int64 pos = 0;
    int prevRoot = -1, prevScale = -1;

    while (pos + windowSamples <= totalLen)
    {
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

        // Chroma over the centred FFT-sized portion of the window.
        const int start = (windowSamples - fftSize) / 2;
        for (int i = 0; i < fftSize; ++i)
            fftBuf[(size_t) i] = windowMono[(size_t) (start + i)] * hann[(size_t) i];
        std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());

        std::array<double, 12> chroma {};
        const double binHz = sr / (double) fftSize;
        const int firstBin = juce::jmax (1, (int) std::ceil (55.0  / binHz));
        const int lastBin  = juce::jmin (fftSize / 2, (int) std::floor (4186.0 / binHz));
        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const double f = bin * binHz;
            const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
            const int pc = ((int) std::round (midi) % 12 + 12) % 12;
            const float mag = fftBuf[(size_t) bin];
            chroma[(size_t) pc] += (double) mag * mag;
        }

        double total = 0.0;
        for (double v : chroma) total += v;
        if (total > 1.0e-9)
            for (auto& v : chroma) v /= total;

        // Pick best (root, mode) by Krumhansl correlation.
        double bestScore = -2.0;
        int bestRoot = 0, bestMode = 0;
        for (int mode = 0; mode < 2; ++mode)
        {
            const double* prof = (mode == 0) ? kProfileMajor : kProfileMinor;
            for (int root = 0; root < 12; ++root)
            {
                const double s = pearson (chroma, prof, root);
                if (s > bestScore) { bestScore = s; bestRoot = root; bestMode = mode; }
            }
        }

        const double tSec = (double) pos / sr;
        // Only emit a new entry when the (root, mode) actually changes -> compact map.
        if (bestRoot != prevRoot || bestMode != prevScale)
        {
            built.push_back ({ tSec, bestRoot, bestMode, (float) bestScore });
            prevRoot = bestRoot; prevScale = bestMode;
        }

        pos += hopSamples;
        const int pct = totalLen > 0 ? (int) (100.0 * (double) pos / (double) totalLen) : 0;
        keyMapProgress.store (juce::jlimit (0, 100, pct));
    }

    // Publish.
    {
        const juce::ScopedLock l (keyMapLock);
        keyMap = std::move (built);
        keyMapFileName = audioFile.getFileName();
        keyMapCurrentSegment.store (-1);
    }
    keyMapProgress.store (100);
    keyMapScanning.store (false);
}

// Called from processBlock when useKeyMap is on and the playhead has a position.
// Looks up the segment covering `seconds` and writes the result to
// `detectedAutoRoot` + (best-effort) the user's scale param.
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
    keyMapCurrentSegment.store (idx);
}

SattariAutoPitchAudioProcessor::PitchFrame SattariAutoPitchAudioProcessor::buildCorrectionFrame (PitchFrame detected) const
{
    if (! detected.voiced)
        return detected;

    const int rootIndex = currentCorrectionRootIndex();
    const int scaleIndex = juce::roundToInt (apvts.getRawParameterValue ("scale")->load());
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
    if (style == "Natural")
        return amount * 0.52f;
    if (style == "Classic")
        return amount * 0.76f;
    if (style == "Modern Rap")
        return juce::jmin (1.0f, amount * 1.08f);
    if (style == "Robot")
        return juce::jmin (1.0f, amount * 1.25f);
    return amount * 0.82f;
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

    if (apvts.getRawParameterValue ("doNoHarm")->load() > 0.5f && (frame.confidence < minConfidence || frame.rms < silenceRms))
        return 1.0;

    const double limitedCents = juce::jlimit (-700.0, 700.0, frame.correctionCents * (double) styleCorrectionStrength());
    return std::pow (2.0, limitedCents / 1200.0);
}

void SattariAutoPitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

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
    if (useKeyMap)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto t = pos->getTimeInSeconds())
                    applyKeyMapAtSeconds (*t);

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
                if ((int) scMonoScratch.size() < numSamples)
                    scMonoScratch.assign ((size_t) numSamples, 0.0f);
                float energy = 0.0f;
                for (int i = 0; i < numSamples; ++i)
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
                pushSidechainBlock (scMonoScratch.data(), numSamples);
            }
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            mono += mainBuffer.getSample (channel, i);
        pushPitchSample (mono / (float) numChannels);

        if (--pitchSamplesUntilAnalysis <= 0)
        {
            auto detected = analyseCurrentPitchFrame();
            // Only fall back to the vocal-driven AutoKey when no sidechain is
            // actively training the histogram. Otherwise the polyphonic SC path
            // (much more accurate for a full mix) owns the key estimate.
            if (! sidechainSeenAudio)
                updateAutoKeyHistogram (detected);
            lastPitchFrame = buildCorrectionFrame (detected);
            pitchSamplesUntilAnalysis = pitchHopSize;
        }
    }

    const double targetPitchScale = pitchScaleForFrame (lastPitchFrame);
    const double retune = (double) styleRetuneCoefficient();
    smoothedPitchScale += (targetPitchScale - smoothedPitchScale) * retune;
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
        const double blockTime = (double) numSamples / currentSampleRate;
        const double alpha = 1.0 - std::exp (-blockTime / 0.080);
        humanizeSmoothedCents += alpha * (humanizeTargetCents - humanizeSmoothedCents);
        const double centsApplied = humanizeSmoothedCents * (double) humanize;
        smoothedPitchScale *= std::pow (2.0, centsApplied / 1200.0);
        smoothedPitchScale = juce::jlimit (0.5, 2.0, smoothedPitchScale);
    }
    else
    {
        humanizeSmoothedCents *= 0.95;   // drift back to zero when off
    }

    const float inputDb = apvts.getRawParameterValue ("inputGain")->load();
    const float outputDb = apvts.getRawParameterValue ("outputGain")->load();
    const float linearGain = juce::Decibels::decibelsToGain (inputDb + outputDb);

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    processWithRubberBand (mainBuffer, smoothedPitchScale, linearGain);
#else
    processFallbackFormantGuide (mainBuffer, smoothedPitchScale, linearGain);
#endif
    applyModeCharacter (mainBuffer);
}

void SattariAutoPitchAudioProcessor::processWithRubberBand (juce::AudioBuffer<float>& buffer, double pitchScale, float linearGain)
{
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    rubberBandOutput.setSize (numChannels, numSamples, false, false, true);
    rubberBandOutput.clear();

    // Formant handling: preserving formants keeps the voice natural under heavy
    // correction (no "chipmunk" colour); the shift control deliberately moves the
    // vocal-tract resonances for a throat/gender effect (semitones -> scale).
    const bool formantPreserve = apvts.getRawParameterValue ("formantPreserve")->load() > 0.5f;
    const float formantShiftSemis = apvts.getRawParameterValue ("formantShift")->load();
    // 0.0 tells Rubber Band to auto-preserve (default); a non-zero shift sets an
    // explicit formant scale for a deliberate throat/gender move.
    const double formantScale = (formantShiftSemis == 0.0f)
                                  ? 0.0 : std::pow (2.0, (double) formantShiftSemis / 12.0);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (channel >= (int) rubberBand.size() || rubberBand[(size_t) channel] == nullptr)
        {
            rubberBandOutput.copyFrom (channel, 0, buffer, channel, 0, numSamples);
            continue;
        }

        auto& stretcher = *rubberBand[(size_t) channel];
        stretcher.setFormantOption (formantPreserve
            ? RubberBand::RubberBandStretcher::OptionFormantPreserved
            : RubberBand::RubberBandStretcher::OptionFormantShifted);
        // 1.0 (shift 0) keeps the natural envelope; other values move formants.
        stretcher.setFormantScale (formantPreserve ? formantScale : 0.0);
        stretcher.setPitchScale (pitchScale);
        const float* input = buffer.getReadPointer (channel);
        stretcher.process (&input, (size_t) numSamples, false);

        int written = 0;
        while (written < numSamples && stretcher.available() > 0)
        {
            const int count = juce::jmin (numSamples - written, (int) stretcher.available());
            float* output = rubberBandOutput.getWritePointer (channel, written);
            stretcher.retrieve (&output, (size_t) count);
            written += count;
        }

        if (written < numSamples)
            rubberBandOutput.copyFrom (channel, written, buffer, channel, written, numSamples - written);
    }

    buffer.makeCopyOf (rubberBandOutput, true);
    buffer.applyGain (linearGain);
#else
    juce::ignoreUnused (buffer, pitchScale, linearGain);
#endif
}

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
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (channel, i, std::tanh (buffer.getSample (channel, i) * 1.12f) * 0.96f);
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
                buffer.setSample (channel, i, juce::jlimit (-0.98f, 0.98f, sample * 1.02f + edge * 0.08f));
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
                buffer.setSample (channel, i, juce::jlimit (-0.98f, 0.98f, dry * 0.68f + up * 0.2f + down * 0.12f));
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
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, currentCorrectionRootIndex())];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, juce::roundToInt (apvts.getRawParameterValue ("scale")->load()))];
    const bool autoKeyOn = apvts.getRawParameterValue ("autoKey")->load() > 0.5f;
    const juce::String engine =
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
        "Rubber Band realtime";
#else
        "fallback guide";
#endif

    if (! lastPitchFrame.voiced)
        return engine + " engine, " + (autoKeyOn ? "AutoKey " : "key ") + root + " " + scale + " - waiting for voiced input";

    return engine + (autoKeyOn ? " - AutoKey " : " - key ") + root + " " + scale
        + ", mode " + currentStyleChoice()
        + ", in " + midiNoteName (lastPitchFrame.midi)
        + " -> " + midiNoteName (lastPitchFrame.targetMidi)
        + ", correction " + juce::String (lastPitchFrame.correctionCents, 1) + " cents"
        + ", confidence " + juce::String (lastPitchFrame.confidence * 100.0, 0) + "%";
}

sattari::audio_core::KeySignature SattariAutoPitchAudioProcessor::currentKeySignature() const
{
    const int scaleIndex = juce::roundToInt (apvts.getRawParameterValue ("scale")->load());
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
    const auto key = currentKeySignature();
    const auto root = rootChoices[juce::jlimit (0, rootChoices.size() - 1, key.tonicPitchClass)];
    const auto scale = scaleChoices[juce::jlimit (0, scaleChoices.size() - 1, juce::roundToInt (apvts.getRawParameterValue ("scale")->load()))];
    const auto wheelCode = currentSattariWheelCode();
    const int confidence = juce::roundToInt (key.confidence * 100.0);

    if (wheelCode == "unknown")
    {
        return "Key Detection tab: " + root + " " + scale
            + ", confidence " + juce::String (confidence) + "%"
            + ", Sattari Wheel needs major/minor mode; Send to Tune keeps selected chromatic root.";
    }

    const auto same = sattari::audio_core::classifyHarmonicMove (key, key);
    auto sideStepUp = key;
    sideStepUp.tonicPitchClass = sattari::audio_core::positiveModulo (key.tonicPitchClass + 7, 12);
    const auto sideStepMove = sattari::audio_core::classifyHarmonicMove (key, sideStepUp);
    auto relative = key;
    relative.mode = key.mode == sattari::audio_core::MusicalMode::Major
        ? sattari::audio_core::MusicalMode::Minor
        : sattari::audio_core::MusicalMode::Major;
    if (key.mode == sattari::audio_core::MusicalMode::Major)
        relative.tonicPitchClass = sattari::audio_core::positiveModulo (key.tonicPitchClass - 3, 12);
    else if (key.mode == sattari::audio_core::MusicalMode::Minor)
        relative.tonicPitchClass = sattari::audio_core::positiveModulo (key.tonicPitchClass + 3, 12);

    return "Key Detection tab: " + root + " " + scale
        + " / Sattari Wheel " + wheelCode
        + " / confidence " + juce::String (confidence) + "%"
        + " / compatibility: " + juce::String (sattari::audio_core::toString (same))
        + ", " + juce::String (sattari::audio_core::toString (sideStepMove))
        + ", " + juce::String (sattari::audio_core::toString (sattari::audio_core::classifyHarmonicMove (key, relative)))
        + " / Send to Tune shares this root+mode with correction and Adapt Map.";
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

        apvts.replaceState (restored);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariAutoPitchAudioProcessor();
}
