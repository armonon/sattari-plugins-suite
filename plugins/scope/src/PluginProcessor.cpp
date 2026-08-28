#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <chrono>
#include <cmath>

SattariScopeAudioProcessor::SattariScopeAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

SattariScopeAudioProcessor::~SattariScopeAudioProcessor()
{
    workerRunning.store (false);
    if (worker.joinable()) worker.join();
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariScopeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    // Display-only controls (never touch the audio path).
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tilt", 1 }, "Spectrum Tilt",
        juce::NormalisableRange<float> (0.0f, 6.0f, 0.1f), 3.0f));   // dB/oct pink-ish tilt
    p.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "chroma", 1 }, "Pitch-Class Overlay", true));
    return { p.begin(), p.end() };
}

void SattariScopeAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    fft = std::make_unique<juce::dsp::FFT> (kFftOrder);
    ring.assign ((size_t) kFftSize, 0.0f);
    fftScratch.assign ((size_t) kFftSize * 2, 0.0f);
    window.assign ((size_t) kFftSize, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) i / (float) (kFftSize - 1));
    writePos.store (0, std::memory_order_relaxed);
    std::fill (runningChroma, runningChroma + 12, 0.0);

    // Build log-spaced band edges spanning ~20 Hz .. Nyquist over the FFT bins.
    const double binHz   = sampleRate / (double) kFftSize;
    const int    maxBin  = kFftSize / 2;
    const double loHz    = juce::jmax (20.0, binHz);
    const double hiHz    = juce::jmax (loHz * 2.0, sampleRate * 0.5);
    const double logLo   = std::log (loHz), logHi = std::log (hiHz);
    for (int b = 0; b <= kNumBands; ++b)
    {
        const double f   = std::exp (logLo + (logHi - logLo) * (double) b / (double) kNumBands);
        const int    bin = juce::jlimit (1, maxBin, (int) std::round (f / binHz));
        bandEdges[(size_t) b] = bin;
    }

    // Goniometer: decimate the incoming stream so ~kNumGonio points cover the
    // most recent ~50 ms slice regardless of sample rate.
    gonioDecim = juce::jmax (1, (int) std::round ((sampleRate * 0.05) / (double) kNumGonio));
    gonioPhase = 0; gonioWrite = 0;
    gonioX.fill (0.0f); gonioY.fill (0.0f);

    corrLL = corrRR = corrLR = 0.0; corrCount = 0; lastCorr = 0.0f;
    corrWindow = juce::jmax (1, (int) std::round (sampleRate * 0.10)); // 100 ms

    spectrumSnap.store (Spectrum {});
    vectorSnap.store (Vector {});

    if (! workerRunning.exchange (true))
        worker = std::thread ([this] { workerLoop(); });
}

void SattariScopeAudioProcessor::releaseResources()
{
    workerRunning.store (false);
    if (worker.joinable()) worker.join();
}

bool SattariScopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariScopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // ---- PURE ANALYSER: audio is never modified. We only READ the buffer. ----
    const int numCh = juce::jmax (1, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (n <= 0 || ring.empty()) return;

    const int chL = 0;
    const int chR = numCh > 1 ? 1 : 0;
    const auto* pL = buffer.getReadPointer (chL);
    const auto* pR = buffer.getReadPointer (chR);

    // 1) Feed the analysis ring with a mono sum (worker thread reads this).
    int wp = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const float mono = 0.5f * (pL[i] + pR[i]);
        ring[(size_t) wp] = mono;
        if (++wp >= kFftSize) wp = 0;
    }
    writePos.store (wp, std::memory_order_relaxed);

    // 2) Goniometer point cloud + running correlation (cheap, on this thread).
    for (int i = 0; i < n; ++i)
    {
        const float l = pL[i], r = pR[i];

        // Correlation accumulators over a sliding ~100 ms window.
        corrLL += (double) l * l;
        corrRR += (double) r * r;
        corrLR += (double) l * r;
        if (++corrCount >= corrWindow)
        {
            const double den = std::sqrt (corrLL * corrRR);
            lastCorr = den > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR / den) : 0.0f;
            corrLL = corrRR = corrLR = 0.0; corrCount = 0;
        }

        // Decimated goniometer: rotate L/R by 45deg into mid/side axes.
        if (++gonioPhase >= gonioDecim)
        {
            gonioPhase = 0;
            const float mid  = 0.70710678f * (l + r);   // vertical
            const float side = 0.70710678f * (l - r);   // horizontal
            gonioX[(size_t) gonioWrite] = side;
            gonioY[(size_t) gonioWrite] = mid;
            if (++gonioWrite >= kNumGonio) gonioWrite = 0;
        }
    }

    // 3) Publish the vectorscope snapshot (lock-free, trivially-copyable POD).
    Vector v;
    for (int i = 0; i < kNumGonio; ++i)
    {
        v.x[i] = gonioX[(size_t) i];
        v.y[i] = gonioY[(size_t) i];
    }
    v.gonioCount  = kNumGonio;
    v.correlation = lastCorr;
    vectorSnap.store (v);
}

void SattariScopeAudioProcessor::analyseNow()
{
    if (! fft || ring.empty()) return;

    // Snapshot the most recent kFftSize samples, oldest-first, into the scratch.
    const int wp = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < kFftSize; ++i)
        fftScratch[(size_t) i] = ring[(size_t) ((wp + i) % kFftSize)] * window[(size_t) i];
    std::fill (fftScratch.begin() + kFftSize, fftScratch.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (fftScratch.data());
    const int numBins = kFftSize / 2 + 1;

    // --- Chroma (pitch-class) fold for the key overlay -----------------------
    const double binHz = sampleRate / (double) kFftSize;
    double frame[12] = { 0.0 };
    sattari::dsp::accumulateChroma (fftScratch.data(), numBins, binHz, frame);
    sattari::dsp::blendChroma (runningChroma, frame, 0.4);

    Spectrum out;
    out.hasData = true;

    // Normalise chroma to its own peak for the overlay bars.
    double chromaMax = 1.0e-12;
    for (int i = 0; i < 12; ++i) chromaMax = juce::jmax (chromaMax, runningChroma[i]);
    for (int i = 0; i < 12; ++i) out.chroma[i] = (float) (runningChroma[i] / chromaMax);

    // --- Log-spaced spectrum in dB, with an optional tilt for the display ----
    const float tilt      = param ("tilt");                    // dB/octave
    const double refHz    = 1000.0;
    const float floorDb   = -100.0f;
    const float normScale = 2.0f / (float) kFftSize;           // window/size norm

    for (int b = 0; b < kNumBands; ++b)
    {
        const int lo = bandEdges[(size_t) b];
        const int hi = juce::jmax (lo + 1, bandEdges[(size_t) (b + 1)]);
        float peak = 0.0f;
        for (int bin = lo; bin < hi && bin < numBins; ++bin)
            peak = juce::jmax (peak, fftScratch[(size_t) bin]);

        float db = floorDb;
        if (peak > 0.0f && std::isfinite (peak))
        {
            db = juce::Decibels::gainToDecibels (peak * normScale, floorDb);
            const double centreHz = (double) ((lo + hi) * 0.5) * binHz;
            if (centreHz > 0.0)
                db += tilt * (float) std::log2 (centreHz / refHz);  // tilt display only
        }
        out.bandDb[b] = juce::jlimit (floorDb, 12.0f, std::isfinite (db) ? db : floorDb);
    }

    // --- Key from the cross-plugin bus (published by Sattari Key etc.) --------
    if (auto s = sattari::xplugin::readSession())
    {
        out.keyValid = true;
        out.keyRoot  = juce::jlimit (0, 11, s->root);
        out.keyScale = s->scale == 1 ? 1 : 0;
    }
    else
    {
        // No publisher: fall back to our own local key estimate off the chroma.
        const auto k = sattari::dsp::detectKey (runningChroma);
        if (k.valid)
        {
            out.keyValid = true;
            out.keyRoot  = juce::jlimit (0, 11, k.root);
            out.keyScale = k.minor ? 1 : 0;
        }
    }

    spectrumSnap.store (out);
}

void SattariScopeAudioProcessor::workerLoop()
{
    while (workerRunning.load())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (40)); // ~25 Hz refresh
        if (! workerRunning.load()) break;
        analyseNow();
    }
}

void SattariScopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariScopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariScopeAudioProcessor::createEditor()
{
    return new SattariScopeAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariScopeAudioProcessor();
}
