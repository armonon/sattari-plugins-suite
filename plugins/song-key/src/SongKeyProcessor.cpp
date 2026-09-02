#include "SongKeyProcessor.h"
#include "SongKeyEditor.h"
#include <chrono>
#include <cstring>

using APVTS = juce::AudioProcessorValueTreeState;

juce::StringArray SattariSongKeyAudioProcessor::rootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}
juce::StringArray SattariSongKeyAudioProcessor::scaleChoices() { return { "Major", "Minor" }; }

APVTS::ParameterLayout SattariSongKeyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "publish", 1 }, "Publish to Suite", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "lockSession", 1 }, "Lock Session", false));
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "dynamicFollow", 1 }, "Dynamic Key Follow", false));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "dynamicInterval", 1 }, "Dynamic Scan Interval",
        juce::NormalisableRange<float> (1.0f, 5.0f, 0.5f), 3.0f));
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "manual",  1 }, "Manual Override", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "manualRoot",  1 }, "Manual Root",  rootChoices(),  0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "manualScale", 1 }, "Manual Scale", scaleChoices(), 0));
    return { p.begin(), p.end() };
}

SattariSongKeyAudioProcessor::SattariSongKeyAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

SattariSongKeyAudioProcessor::~SattariSongKeyAudioProcessor()
{
    stopFileWorker();
    stopWorker();
}

void SattariSongKeyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    stopWorker();

    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    if (! fft)
        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    for (auto& sample : ring)
        sample.store (0.0f, std::memory_order_relaxed);
    scratch.fill (0.0f);
    liveKeyInput.fill (0.0f);

    for (int i = 0; i < fftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
    writePos.store (0);
    totalSamplesWritten.store (0);
    std::fill (running, running + 12, 0.0);

    // HPCP accumulator: warm its whitening buffer here so accumulate() never
    // allocates, and clear any state carried over from the previous sample rate.
    hpcp.reserveBins (fftSize / 2 + 1);
    hpcp.reset();
    liveSongKey.prepare (currentSampleRate);
    livePreviousSpectrum.assign ((size_t) fftSize / 2 + 1, 0.0f);
    liveOnsetEnvelope.clear();
    liveOnsetEnvelope.reserve ((size_t) std::ceil (
        currentSampleRate / (double) (fftSize / 2) * 120.0));
    resetDynamicDecision();

    workerRunning.store (true, std::memory_order_release);
    worker = std::thread ([this] { workerLoop(); });
}

void SattariSongKeyAudioProcessor::releaseResources()
{
    stopWorker();
}

bool SattariSongKeyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    if (in != layouts.getMainOutputChannelSet()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SattariSongKeyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int n  = buffer.getNumSamples();
    const int nc = juce::jmax (1, buffer.getNumChannels());

    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm) hostBpm.store (t.bpm);

    // Feed the analysis ring with a mono sum. Audio is NOT modified — Sattari Key
    // is a transparent analyser that just listens to the master.
    int wp = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) m += buffer.getSample (ch, i);
        m /= (float) nc;
        ring[(size_t) wp].store (m, std::memory_order_relaxed);
        wp = (wp + 1) % fftSize;
    }
    writePos.store (wp, std::memory_order_release);
    totalSamplesWritten.fetch_add ((juce::int64) n, std::memory_order_release);
}

void SattariSongKeyAudioProcessor::analyseNow()
{
    const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
    if (! fft) return;
    if (rescan.exchange (false))
    {
        resetLiveKeyWindow();
        std::fill (livePreviousSpectrum.begin(), livePreviousSpectrum.end(), 0.0f);
        liveOnsetEnvelope.clear();
        runningDetune = 0.0;
        clearPublishedDecision();
        detValid.store (false);
        detAlternativeValid.store (false);
        detTempoValid.store (false);
        detBpm.store (0.0);
        detTempoConfidence.store (0.0);
        detTempoOctaveAmbiguous.store (false);
        detSegmentAgreement.store (0.0);
        detSegments.store (0);
        detPossibleModulation.store (false);
        resetDynamicDecision();
    }

    const int wp = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
    {
        const float sample = ring[(size_t) ((wp + i) % fftSize)].load (
            std::memory_order_relaxed);
        liveKeyInput[(size_t) i] = sample;
        scratch[(size_t) i] = sample * hann[(size_t) i];
    }
    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (scratch.data());

    // HPCP chroma: sub-bin peak interpolation + harmonic summation + tuning
    // correction. Replaces accumulateChroma()'s raw nearest-class fold, which sat at
    // 9% exact on real full mixes because low-frequency bins are wider than a
    // semitone. The accumulator keeps its own rolling state, so `running` is just a
    // snapshot of it for the detector (no second smoothing pass).
    hpcp.accumulate (scratch.data(), fftSize / 2 + 1, currentSampleRate / (double) fftSize);
    hpcp.finish (running);

    // Tuning-reference estimate (detune vs A440). For each strong spectral peak in
    // the musical range, measure how far it sits from the nearest equal-tempered
    // semitone; combine them as a magnitude-weighted circular mean (circular so a
    // detune near +/-50 cents doesn't cancel), then exponentially blend for a stable
    // readout. referenceA = 440 * 2^(cents/1200).
    {
        const double binHz = currentSampleRate / (double) fftSize;
        const int loBin = (int) std::max (2.0, 80.0 / binHz);
        const int hiBin = (int) std::min ((double) (fftSize / 2 - 2), 2000.0 / binHz);
        const double toRad = juce::MathConstants<double>::pi / 50.0;
        double sx = 0.0, sy = 0.0, wsum = 0.0;
        for (int b = loBin; b <= hiBin; ++b)
        {
            const double m0 = scratch[(size_t) b];
            const double a = scratch[(size_t) (b - 1)], c = scratch[(size_t) (b + 1)];
            if (m0 <= a || m0 < c || m0 < 1.0e-6) continue;             // local maxima only
            const double den = a - 2.0 * m0 + c;
            const double delta = den != 0.0 ? 0.5 * (a - c) / den : 0.0; // sub-bin peak
            const double freq = ((double) b + delta) * binHz;
            if (freq <= 0.0) continue;
            const double semis = 12.0 * std::log2 (freq / 440.0);
            const double cents = 100.0 * (semis - std::round (semis));  // [-50, 50]
            sx += m0 * std::cos (cents * toRad);
            sy += m0 * std::sin (cents * toRad);
            wsum += m0;
        }
        if (wsum > 1.0e-9 && (sx * sx + sy * sy) > 1.0e-12)
        {
            const double est = std::atan2 (sy, sx) / toRad;             // back to cents
            runningDetune += 0.2 * (est - runningDetune);
            detDetuneCents.store (runningDetune);
        }
    }

    // Feed the same fixed 16 kHz/4096-frame path used for dropped song files.
    // The worker snapshots overlap, which preserves continuity and improves the
    // live warm-up time without ever touching the realtime audio callback.
    liveSongKey.addSamples (liveKeyInput.data(), fftSize);
    auto k = liveSongKey.result();
    auto alternative = liveSongKey.alternative();
    const auto cleanTonal = keyAnalysis.analyseChroma (running);
    if (! k.valid || (cleanTonal.valid && cleanTonal.confidence >= 0.25
                      && (cleanTonal.root != k.root
                          || cleanTonal.minor != k.minor)))
    {
        // A sustained exposed chord has much less harmonic context than a full
        // song. In that high-margin special case, the direct tuning-corrected
        // chroma verdict is the stronger observation (and prevents the
        // whitening path from treating a partial as a new tonic).
        k = cleanTonal;
        alternative = keyAnalysis.analyseAlternativeChroma (running, k);
    }
    if (k.valid)
    {
        detRoot.store (k.root);
        detMinor.store (k.minor);
        detConf.store (k.confidence);
        detValid.store (true);
        liveWindowHasValidDecision = true;
        detAlternativeValid.store (alternative.valid);
        if (alternative.valid)
        {
            detAlternativeRoot.store (alternative.root);
            detAlternativeMinor.store (alternative.minor);
        }
    }

    liveOnsetEnvelope.push_back (onsetStrength (
        scratch.data(), fftSize / 2 + 1, livePreviousSpectrum));
    const auto maxLiveFrames = (size_t) std::ceil (
        currentSampleRate / (double) (fftSize / 2) * 120.0);
    if (liveOnsetEnvelope.size() > maxLiveFrames + 256)
        liveOnsetEnvelope.erase (
            liveOnsetEnvelope.begin(),
            liveOnsetEnvelope.begin() + (std::ptrdiff_t) 256);
    const auto tempo = sattari::dsp::TempoEstimator::analyse (
        liveOnsetEnvelope, currentSampleRate / (double) (fftSize / 2));
    detTempoValid.store (tempo.valid);
    detBpm.store (tempo.valid ? tempo.bpm : 0.0);
    detTempoConfidence.store (tempo.confidence);
    detTempoOctaveAmbiguous.store (tempo.octaveAmbiguous);
}

void SattariSongKeyAudioProcessor::serviceLiveAnalysis()
{
    analyseNow();
    publishCurrent();
}

float SattariSongKeyAudioProcessor::onsetStrength (
    const float* magnitude, int numBins, std::vector<float>& previous)
{
    if (magnitude == nullptr || numBins < 4) return 0.0f;
    if ((int) previous.size() != numBins)
        previous.assign ((size_t) numBins, 0.0f);

    double flux = 0.0;
    for (int bin = 2; bin < numBins; ++bin)
    {
        const float value = std::log1p (juce::jmax (0.0f, magnitude[bin]));
        flux += juce::jmax (0.0f, value - previous[(size_t) bin]);
        previous[(size_t) bin] = value;
    }
    return (float) (flux / (double) (numBins - 2));
}

bool SattariSongKeyAudioProcessor::isSupportedAudioFile (const juce::File& file)
{
    return file.existsAsFile()
        && file.hasFileExtension ("wav;aif;aiff;flac;mp3;m4a;aac;ogg");
}

void SattariSongKeyAudioProcessor::clearPublishedDecision()
{
    lockSnapshotValid = false;
    publishingNow.store (false);
}

void SattariSongKeyAudioProcessor::resetLiveKeyWindow()
{
    std::fill (running, running + 12, 0.0);
    hpcp.reset();
    liveSongKey.prepare (currentSampleRate);
    liveWindowHasValidDecision = false;
}

void SattariSongKeyAudioProcessor::resetDynamicDecision()
{
    dynamicWasEnabled = false;
    dynamicDecisionValid = false;
    dynamicRoot = dynamicPendingRoot = 0;
    dynamicMinor = dynamicPendingMinor = false;
    dynamicConfidence = 0.0;
    dynamicPendingConfirmations = 0;
    dynamicRevision = 0;
    nextDynamicScanSample = 0;
    lastDynamicPublishAttemptMs = 0;
    liveWindowHasValidDecision = false;
}

void SattariSongKeyAudioProcessor::setSessionLocked (bool shouldLock)
{
    if (auto* parameter = apvts.getParameter ("lockSession"))
        parameter->setValueNotifyingHost (shouldLock ? 1.0f : 0.0f);
    if (! shouldLock)
    {
        {
            const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
            clearPublishedDecision();
        }
        sattari::xplugin::withdrawSessionState ("song-key");
        return;
    }
    publishCurrent();
}

void SattariSongKeyAudioProcessor::setDynamicFollowEnabled (bool enabled)
{
    if (auto* parameter = apvts.getParameter ("dynamicFollow"))
        parameter->setValueNotifyingHost (enabled ? 1.0f : 0.0f);
    if (enabled)
    {
        if (auto* parameter = apvts.getParameter ("lockSession"))
            parameter->setValueNotifyingHost (0.0f);
        if (auto* parameter = apvts.getParameter ("manual"))
            parameter->setValueNotifyingHost (0.0f);
        listenLive();
    }
    else
    {
        {
            const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
            resetDynamicDecision();
            publishingNow.store (false);
        }
        sattari::xplugin::withdrawSessionState ("song-key");
    }
}

void SattariSongKeyAudioProcessor::unlockSession()
{
    setSessionLocked (false);
}

void SattariSongKeyAudioProcessor::requestRescan()
{
    unlockSession();
    rescan.store (true, std::memory_order_release);
}

void SattariSongKeyAudioProcessor::stopFileWorker()
{
    fileCancel.store (true, std::memory_order_release);
    if (fileWorker.joinable())
        fileWorker.join();
    fileAnalysing.store (false, std::memory_order_release);
}

void SattariSongKeyAudioProcessor::listenLive()
{
    stopFileWorker();
    unlockSession();
    analysisSource.store (AnalysisSource::Live, std::memory_order_release);
    {
        const juce::ScopedLock state (fileStateLock);
        fileSourceName.clear();
        fileError.clear();
    }
    fileProgress.store (0.0);
    {
        const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
        clearPublishedDecision();
        resetDynamicDecision();
        detValid.store (false);
        detSegmentAgreement.store (0.0);
        detSegments.store (0);
        detPossibleModulation.store (false);
    }
    rescan.store (true, std::memory_order_release);
}

void SattariSongKeyAudioProcessor::reanalyseCurrentSource()
{
    if (analysisSource.load (std::memory_order_acquire) == AnalysisSource::File)
    {
        juce::File file;
        {
            const juce::ScopedLock state (fileStateLock);
            file = lastFile;
        }
        if (file.existsAsFile())
        {
            analyseFileAsync (file);
            return;
        }
    }
    requestRescan();
}

bool SattariSongKeyAudioProcessor::analyseFileAsync (const juce::File& file)
{
    stopFileWorker();
    if (auto* parameter = apvts.getParameter ("dynamicFollow"))
        parameter->setValueNotifyingHost (0.0f);
    unlockSession();
    analysisSource.store (AnalysisSource::File, std::memory_order_release);
    fileProgress.store (0.0);

    {
        const juce::ScopedLock state (fileStateLock);
        lastFile = file;
        fileSourceName = file.getFileNameWithoutExtension();
        fileError.clear();
    }
    {
        const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
        clearPublishedDecision();
        resetDynamicDecision();
        detValid.store (false);
        detAlternativeValid.store (false);
        detTempoValid.store (false);
        detBpm.store (0.0);
        detTempoConfidence.store (0.0);
        detTempoOctaveAmbiguous.store (false);
        detSegmentAgreement.store (0.0);
        detSegments.store (0);
        detPossibleModulation.store (false);
    }

    if (! isSupportedAudioFile (file))
    {
        const juce::ScopedLock state (fileStateLock);
        fileError = file.existsAsFile()
            ? "Unsupported audio format"
            : "Song file not found";
        return false;
    }

    fileCancel.store (false, std::memory_order_release);
    fileAnalysing.store (true, std::memory_order_release);
    fileWorker = std::thread ([this, file]
    {
        OfflineResult result;
        juce::String error;
        const bool ok = analyseFile (file, result, error);

        if (fileCancel.load (std::memory_order_acquire))
            return;

        if (! ok || ! result.key.valid)
        {
            const juce::ScopedLock state (fileStateLock);
            fileError = error.isNotEmpty() ? error : "No stable musical key found";
            fileAnalysing.store (false, std::memory_order_release);
            return;
        }

        {
            const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
            if (analysisSource.load (std::memory_order_acquire) != AnalysisSource::File)
                return;
            detRoot.store (result.key.root);
            detMinor.store (result.key.minor);
            detConf.store (result.key.confidence);
            detAlternativeValid.store (result.alternative.valid);
            if (result.alternative.valid)
            {
                detAlternativeRoot.store (result.alternative.root);
                detAlternativeMinor.store (result.alternative.minor);
            }
            detDetuneCents.store (result.detuneCents);
            detTempoValid.store (result.tempo.valid);
            detBpm.store (result.tempo.valid ? result.tempo.bpm : 0.0);
            detTempoConfidence.store (result.tempo.confidence);
            detTempoOctaveAmbiguous.store (result.tempo.octaveAmbiguous);
            detSegmentAgreement.store (result.segmentAgreement);
            detSegments.store (result.segments);
            detPossibleModulation.store (result.possibleModulation);
            detValid.store (true);
            clearPublishedDecision();
        }
        fileProgress.store (1.0, std::memory_order_release);
        fileAnalysing.store (false, std::memory_order_release);
        publishCurrent();
    });
    return true;
}

bool SattariSongKeyAudioProcessor::analyseFile (const juce::File& file,
                                                 OfflineResult& result,
                                                 juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "This build cannot decode that audio file";
        return false;
    }
    if (reader->lengthInSamples <= 0 || reader->numChannels == 0
        || reader->sampleRate <= 0.0)
    {
        error = "The song file contains no readable audio";
        return false;
    }

    constexpr int hop = fftSize / 2;
    constexpr int chunkSamples = 65536;
    const int channels = juce::jmax (1, (int) reader->numChannels);

    juce::AudioBuffer<float> decoded (channels, chunkSamples);
    std::vector<float> mono ((size_t) chunkSamples + fftSize, 0.0f);
    std::array<float, fftSize * 2> spectrum {};
    std::array<float, fftSize> window {};
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (
            juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));

    juce::dsp::FFT fileFft (fftOrder);
    sattari::dsp::HpcpChromagram globalHpcp;
    sattari::dsp::SongKeyEstimator songKey;
    globalHpcp.reserveBins (fftSize / 2 + 1);
    songKey.prepare (reader->sampleRate, (std::int64_t) reader->lengthInSamples);

    std::vector<float> previousSpectrum ((size_t) fftSize / 2 + 1, 0.0f);
    std::vector<float> onsetEnvelope;
    onsetEnvelope.reserve ((size_t) std::ceil (
        (double) reader->lengthInSamples / (double) hop));
    int frames = 0;
    auto analyseFrame = [&] (const float* samples)
    {
        for (int i = 0; i < fftSize; ++i)
            spectrum[(size_t) i] = samples[i] * window[(size_t) i];
        std::fill (spectrum.begin() + fftSize, spectrum.end(), 0.0f);
        fileFft.performFrequencyOnlyForwardTransform (spectrum.data());
        const double binHz = reader->sampleRate / (double) fftSize;
        globalHpcp.accumulate (spectrum.data(), fftSize / 2 + 1, binHz);
        onsetEnvelope.push_back (onsetStrength (
            spectrum.data(), fftSize / 2 + 1, previousSpectrum));
        ++frames;
    };

    int carried = 0;
    juce::int64 readPosition = 0;
    while (readPosition < reader->lengthInSamples)
    {
        if (fileCancel.load (std::memory_order_acquire))
            return false;

        const int count = (int) juce::jmin ((juce::int64) chunkSamples,
                                            reader->lengthInSamples - readPosition);
        decoded.clear();
        if (! reader->read (&decoded, 0, count, readPosition, true, true))
        {
            error = "The song could not be decoded completely";
            return false;
        }
        for (int i = 0; i < count; ++i)
        {
            float sum = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                sum += decoded.getSample (channel, i);
            mono[(size_t) carried + (size_t) i] = sum / (float) channels;
        }
        songKey.addSamples (mono.data() + carried, count);

        const int available = carried + count;
        int cursor = 0;
        while (cursor + fftSize <= available)
        {
            analyseFrame (mono.data() + cursor);
            cursor += hop;
        }
        carried = available - cursor;
        if (carried > 0)
            std::memmove (mono.data(), mono.data() + cursor,
                          (size_t) carried * sizeof (float));

        readPosition += count;
        fileProgress.store (0.98 * (double) readPosition
                                  / (double) reader->lengthInSamples,
                            std::memory_order_release);
    }

    // Retain the musical tail and make even a short clip analyzable.
    if (carried > 0 && (frames == 0 || carried >= hop / 2))
    {
        std::fill (mono.begin() + carried, mono.begin() + fftSize, 0.0f);
        analyseFrame (mono.data());
    }
    songKey.finalise();

    if (frames == 0)
    {
        error = "The song is too short to analyse";
        return false;
    }

    result.key = songKey.result();
    if (! result.key.valid)
    {
        error = "No pitched musical content was found";
        return false;
    }
    result.alternative = songKey.alternative();
    result.tempo = sattari::dsp::TempoEstimator::analyse (
        onsetEnvelope, reader->sampleRate / (double) hop);
    result.detuneCents = globalHpcp.tuningOffsetSemitones() * 100.0;
    const auto& sectionKeys = songKey.sections();
    result.segments = (int) sectionKeys.size();

    double agreementWeight = 0.0;
    double totalWeight = 0.0;
    std::array<double, 24> vote {};
    for (const auto& section : sectionKeys)
    {
        const double weight = juce::jmax (0.01, section.confidence);
        totalWeight += weight;
        vote[(size_t) (section.root * 2 + (section.minor ? 1 : 0))] += weight;
        if (section.root == result.key.root && section.minor == result.key.minor)
            agreementWeight += weight;
    }
    result.segmentAgreement = totalWeight > 0.0
        ? agreementWeight / totalWeight
        : 1.0;

    double strongestAlternative = 0.0;
    for (int i = 0; i < 24; ++i)
        if (i != result.key.root * 2 + (result.key.minor ? 1 : 0))
            strongestAlternative = juce::jmax (strongestAlternative, vote[(size_t) i]);
    result.possibleModulation = result.segments >= 3
        && totalWeight > 0.0
        && result.segmentAgreement < 0.65
        && strongestAlternative / totalWeight >= 0.22;
    return true;
}

void SattariSongKeyAudioProcessor::publishCurrent()
{
    const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
    const bool pub = param ("publish") > 0.5f;
    const bool dynamicRequested = param ("dynamicFollow") > 0.5f
        && analysisSource.load (std::memory_order_acquire) == AnalysisSource::Live;
    const bool lockRequested = param ("lockSession") > 0.5f;
    const bool wasPublishing = publishingNow.exchange (false);

    if (dynamicRequested)
    {
        lockSnapshotValid = false;
        const auto written = totalSamplesWritten.load (std::memory_order_acquire);
        const double interval = juce::jlimit (
            1.0, 5.0, (double) param ("dynamicInterval"));
        const auto intervalSamples = juce::jmax<juce::int64> (
            1, (juce::int64) std::llround (interval * currentSampleRate));
        if (! dynamicWasEnabled)
        {
            dynamicWasEnabled = true;
            nextDynamicScanSample = written + intervalSamples;
            dynamicPendingConfirmations = 0;
            resetLiveKeyWindow();
        }

        bool acceptedNewKey = false;
        if (written >= nextDynamicScanSample)
        {
            if (liveWindowHasValidDecision && detValid.load()
                && detConf.load() >= kMinPublishConfidence)
            {
                const int candidateRoot = juce::jlimit (0, 11, detRoot.load());
                const bool candidateMinor = detMinor.load();
                const double candidateConfidence = detConf.load();
                const bool sameAsActive = dynamicDecisionValid
                    && candidateRoot == dynamicRoot
                    && candidateMinor == dynamicMinor;
                if (! dynamicDecisionValid || sameAsActive)
                {
                    if (! dynamicDecisionValid)
                    {
                        dynamicRoot = candidateRoot;
                        dynamicMinor = candidateMinor;
                        dynamicDecisionValid = true;
                        ++dynamicRevision;
                        acceptedNewKey = true;
                    }
                    dynamicConfidence = candidateConfidence;
                    dynamicPendingConfirmations = 0;
                }
                else
                {
                    const bool sameAsPending = dynamicPendingConfirmations > 0
                        && candidateRoot == dynamicPendingRoot
                        && candidateMinor == dynamicPendingMinor;
                    if (sameAsPending)
                        ++dynamicPendingConfirmations;
                    else
                    {
                        dynamicPendingRoot = candidateRoot;
                        dynamicPendingMinor = candidateMinor;
                        dynamicPendingConfirmations = 1;
                    }

                    // Strong evidence can follow one scan window. A marginal
                    // but releasable change must repeat in two consecutive,
                    // non-overlapping windows before it can move a singer.
                    if (candidateConfidence >= kStrongDynamicConfidence
                        || dynamicPendingConfirmations >= 2)
                    {
                        dynamicRoot = candidateRoot;
                        dynamicMinor = candidateMinor;
                        dynamicConfidence = candidateConfidence;
                        dynamicDecisionValid = true;
                        dynamicPendingConfirmations = 0;
                        ++dynamicRevision;
                        acceptedNewKey = true;
                    }
                }
            }
            else
            {
                // Silence, drums and ambiguous harmony are not new keys. Keep
                // the last trusted result and discard partial switch evidence.
                dynamicPendingConfirmations = 0;
            }

            nextDynamicScanSample = written + intervalSamples;
            resetLiveKeyWindow();
        }

        if (! pub || ! dynamicDecisionValid)
        {
            if (! pub && wasPublishing)
                sattari::xplugin::withdrawSessionState ("song-key");
            return;
        }

        const auto now = sattari::xplugin::currentTimeMs();
        const bool heartbeatDue = lastDynamicPublishAttemptMs <= 0
            || now - lastDynamicPublishAttemptMs >= 650;
        if (! acceptedNewKey && ! heartbeatDue)
        {
            publishingNow.store (wasPublishing);
            return;
        }

        const double exactHostTempo = hostBpm.load();
        const double detectedTempo = detTempoValid.load() ? detBpm.load() : 0.0;
        sattari::xplugin::SessionMessage msg;
        msg.root = dynamicRoot;
        msg.scale = dynamicMinor ? 1 : 0;
        msg.bpm = exactHostTempo > 1.0 ? exactHostTempo : detectedTempo;
        msg.transientId = ++transientId;
        msg.referenceHz = 440.0 * std::pow (
            2.0, detDetuneCents.load() / 1200.0);
        msg.dynamicKey = true;
        msg.keyConfidence = (float) juce::jlimit (0.0, 1.0, dynamicConfidence);
        msg.keyRevision = dynamicRevision;
        msg.keyWindowSeconds = interval;
        const bool published = sattari::xplugin::publishSessionState ("song-key", msg);
        publishingNow.store (published);
        lastDynamicPublishAttemptMs = now;
        return;
    }

    if (dynamicWasEnabled)
    {
        resetDynamicDecision();
        if (wasPublishing)
            sattari::xplugin::withdrawSessionState ("song-key");
    }
    if (! pub || ! lockRequested)
    {
        if (wasPublishing)
            sattari::xplugin::withdrawSessionState ("song-key");
        if (! lockRequested) lockSnapshotValid = false;
        return;
    }

    if (! lockSnapshotValid)
    {
        if (param ("manual") > 0.5f)
        {
            lockedRoot = (int) std::round (param ("manualRoot"));
            lockedMinor = param ("manualScale") > 0.5f;
        }
        else
        {
            if (! detValid.load() || detConf.load() < kMinPublishConfidence)
                return;
            lockedRoot = detRoot.load();
            lockedMinor = detMinor.load();
        }

        lockedReferenceHz = 440.0 * std::pow (
            2.0, detDetuneCents.load() / 1200.0);
        const bool isFile = analysisSource.load() == AnalysisSource::File;
        const double detectedTempo = detTempoValid.load() ? detBpm.load() : 0.0;
        const double exactHostTempo = hostBpm.load();
        lockedBpm = isFile
            ? (detectedTempo > 1.0 ? detectedTempo : exactHostTempo)
            : (exactHostTempo > 1.0 ? exactHostTempo : detectedTempo);
        lockSnapshotValid = true;
    }

    // Publish the base SessionState (Compass fallback): key + host tempo + the
    // reference tuning we already estimate from the spectrum. referenceHz rides the
    // bus so any plugin can follow the song's actual A, not just its key. The other
    // SessionState fields stay at safe defaults until Compass sets them.
    sattari::xplugin::SessionMessage msg;
    msg.root        = lockedRoot;
    msg.scale       = lockedMinor ? 1 : 0;
    msg.bpm         = lockedBpm;
    msg.transientId = ++transientId;
    msg.referenceHz = lockedReferenceHz;
    publishingNow.store (
        sattari::xplugin::publishSessionState ("song-key", msg));
}

void SattariSongKeyAudioProcessor::stopWorker()
{
    workerRunning.store (false, std::memory_order_release);
    if (worker.joinable())
        worker.join();
}

void SattariSongKeyAudioProcessor::workerLoop()
{
    juce::int64 lastAnalysed = totalSamplesWritten.load (std::memory_order_acquire);
    auto lastFilePublish = std::chrono::steady_clock::now();
    while (workerRunning.load (std::memory_order_acquire))
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (15));
        if (! workerRunning.load (std::memory_order_acquire)) break;
        if (analysisSource.load (std::memory_order_acquire) != AnalysisSource::Live)
        {
            // File mode has no incoming audio cadence to drive publication. Keep
            // manual/publish parameter changes responsive and renew the suite-bus
            // lease at the historical 700 ms rate after the file result settles.
            const auto now = std::chrono::steady_clock::now();
            if (now - lastFilePublish >= std::chrono::milliseconds (700))
            {
                publishCurrent();
                lastFilePublish = now;
            }
            continue;
        }

        const auto written = totalSamplesWritten.load (std::memory_order_acquire);
        if (written < fftSize || written - lastAnalysed < fftSize / 2)
            continue;
        lastAnalysed = written;
        serviceLiveAnalysis();
    }
}

SattariSongKeyAudioProcessor::Detected SattariSongKeyAudioProcessor::detected() const
{
    Detected d;
    d.valid = detValid.load();
    d.root = detRoot.load();
    d.minor = detMinor.load();
    d.confidence = detConf.load();
    d.alternativeValid = detAlternativeValid.load();
    d.alternativeRoot = detAlternativeRoot.load();
    d.alternativeMinor = detAlternativeMinor.load();
    d.publishing = publishingNow.load();
    d.detuneCents = detDetuneCents.load();
    const bool isFile = analysisSource.load() == AnalysisSource::File;
    const double exactHostTempo = hostBpm.load();
    const bool useHostTempo = ! isFile && exactHostTempo > 1.0;
    d.tempoValid = useHostTempo || detTempoValid.load();
    d.bpm = useHostTempo ? exactHostTempo : detBpm.load();
    d.tempoConfidence = useHostTempo ? 1.0 : detTempoConfidence.load();
    d.tempoFromHost = useHostTempo;
    d.tempoOctaveAmbiguous = ! useHostTempo && detTempoOctaveAmbiguous.load();
    d.lockRequested = param ("lockSession") > 0.5f;
    d.dynamicEnabled = param ("dynamicFollow") > 0.5f;
    d.dynamicIntervalSeconds = juce::jlimit (
        1.0, 5.0, (double) param ("dynamicInterval"));
    const auto written = totalSamplesWritten.load (std::memory_order_acquire);
    {
        const std::lock_guard<std::mutex> stateLock (analysisStateMutex);
        d.sessionLocked = lockSnapshotValid;
        d.lockedRoot = lockedRoot;
        d.lockedMinor = lockedMinor;
        d.dynamicActive = dynamicDecisionValid;
        d.dynamicRoot = dynamicRoot;
        d.dynamicMinor = dynamicMinor;
        d.dynamicConfidence = dynamicConfidence;
        d.dynamicPending = dynamicPendingConfirmations > 0;
        d.dynamicPendingRoot = dynamicPendingRoot;
        d.dynamicPendingMinor = dynamicPendingMinor;
        d.dynamicPendingConfirmations = dynamicPendingConfirmations;
        d.dynamicRevision = dynamicRevision;
        d.dynamicSecondsUntilScan = nextDynamicScanSample > written
            ? (double) (nextDynamicScanSample - written) / currentSampleRate : 0.0;
    }
    d.source = analysisSource.load();
    d.analysing = fileAnalysing.load();
    d.progress = fileProgress.load();
    d.segmentAgreement = detSegmentAgreement.load();
    d.segments = detSegments.load();
    d.possibleModulation = detPossibleModulation.load();
    {
        const juce::ScopedLock state (fileStateLock);
        d.sourceName = fileSourceName;
        d.error = fileError;
    }
    return d;
}

void SattariSongKeyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariSongKeyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariSongKeyAudioProcessor::createEditor()
{
    return new SattariSongKeyAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariSongKeyAudioProcessor();
}
