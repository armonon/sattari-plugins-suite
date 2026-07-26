#include "SongKeyProcessor.h"
#include "SongKeyEditor.h"
#include <chrono>

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

    for (int i = 0; i < fftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
    writePos.store (0);
    std::fill (running, running + 12, 0.0);

    // HPCP accumulator: warm its whitening buffer here so accumulate() never
    // allocates, and clear any state carried over from the previous sample rate.
    hpcp.reserveBins (fftSize / 2 + 1);
    hpcp.reset();

    // Long ring for the neural model (~30 s of mono at the host rate). Allocated
    // once per prepare; the audio thread only ever stores into it.
    specRingSize = juce::jmax (1, (int) (currentSampleRate * kSpecSeconds));
    specRing = std::make_unique<std::atomic<float>[]> ((size_t) specRingSize);
    for (int i = 0; i < specRingSize; ++i)
        specRing[(size_t) i].store (0.0f, std::memory_order_relaxed);
    specWritePos.store (0);
    specFilled.store (0);
    specMono.assign ((size_t) specRingSize, 0.0f);              // worker scratch, sized once
    specFeat.assign ((size_t) (sattari::dsp::LogSpec::kBins
                             * sattari::dsp::LogSpec::kFrames), 0.0f);
    specLastRunSamples = -1;

    // Load the spectrogram key model (72.1 MIREX vs 17.4 for the old linear chroma).
    // Optional: absent or ONNX-less builds fall back to the HPCP chroma path, which
    // is still ~5x the old exact-match rate.
    if (! keyNetSpecPresent)
    {
        auto specFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("keynet_spec.onnx");
        if (specFile.existsAsFile())
            keyNetSpecPresent = keyNetSpec.loadModel (specFile.getFullPathName().toStdString());
    }

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
    int sp = specWritePos.load (std::memory_order_relaxed);
    const bool haveSpecRing = (specRing != nullptr && specRingSize > 0);
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) m += buffer.getSample (ch, i);
        m /= (float) nc;
        ring[(size_t) wp].store (m, std::memory_order_relaxed);
        wp = (wp + 1) % fftSize;
        // Same mono sum also feeds the long (~30 s) ring the neural key model reads.
        if (haveSpecRing)
        {
            specRing[(size_t) sp].store (m, std::memory_order_relaxed);
            sp = (sp + 1) % specRingSize;
        }
    }
    writePos.store (wp, std::memory_order_release);
    if (haveSpecRing)
    {
        specWritePos.store (sp, std::memory_order_release);
        specFilled.fetch_add (n, std::memory_order_relaxed);
    }
}

void SattariSongKeyAudioProcessor::analyseNow()
{
    if (! fft) return;
    if (rescan.exchange (false))
    {
        std::fill (running, running + 12, 0.0);
        hpcp.reset();
        runningDetune = 0.0;
        specFilled.store (0);          // make the net wait for a fresh 30 s window
        specLastRunSamples = -1;
        specHaveResult = false;
        pubRoot = -1; candCount = 0;   // let a fresh scan re-commit the key
    }

    const int wp = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((wp + i) % fftSize)].load (std::memory_order_relaxed) * hann[(size_t) i];
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

    auto k = sattari::dsp::detectKey (running);

    // Neural override. The spectrogram model is ~16 MIREX points better than the HPCP
    // chroma above, but it wants a 30 s excerpt (what it was trained on) and a TTA
    // pass is 12 forward passes — so it runs only once we have a full window, and no
    // more than every kSpecIntervalSec. Between runs the chroma result stands, which
    // is also what ships when no model is installed.
    const long seen = specFilled.load (std::memory_order_relaxed);
    const long intervalSamples = (long) (kSpecIntervalSec * currentSampleRate);
    const bool haveWindow = seen >= (long) specRingSize;
    const bool intervalElapsed = (specLastRunSamples < 0) || (seen - specLastRunSamples >= intervalSamples);
    if (keyNetSpecPresent && haveWindow && intervalElapsed
        && specRing != nullptr && (int) specMono.size() >= specRingSize)
    {
        specLastRunSamples = seen;
        const int sp = specWritePos.load (std::memory_order_acquire);
        for (int i = 0; i < specRingSize; ++i)   // oldest-first linearisation of the ring
            specMono[(size_t) i] = specRing[(size_t) ((sp + i) % specRingSize)].load (std::memory_order_relaxed);

        if (logSpec.compute (specMono.data(), specRingSize, currentSampleRate, specFeat))
        {
            const auto kn = keyNetSpec.detectTTA (specFeat.data());
            if (kn.valid)
            {
                specHaveResult = true;
                specRoot = kn.root;
                specMinor = kn.minor;
                specConfidence = kn.confidence;
            }
        }
    }

    // The net's verdict wins whenever we have one — it is ~16 MIREX points ahead of
    // the chroma path, and it stays latched between runs so the cheaper detector
    // can't overwrite it on the cycles in between.
    if (specHaveResult)
    {
        k.valid = true;
        k.root = specRoot;
        k.minor = specMinor;
        k.confidence = specConfidence;
    }

    detNeural.store (specHaveResult);
    if (k.valid)
    {
        detRoot.store (k.root);
        detMinor.store (k.minor);
        detConf.store (k.confidence);
        detValid.store (true);
    }
}

void SattariSongKeyAudioProcessor::publishCurrent()
{
    const bool pub = param ("publish") > 0.5f;
    publishingNow.store (pub && detValid.load());
    if (! pub) return;

    int root, scale;
    if (param ("manual") > 0.5f)
    {
        root  = (int) std::round (param ("manualRoot"));
        scale = (int) std::round (param ("manualScale"));
    }
    else
    {
        if (! detValid.load()) return;
        const int    dRoot  = detRoot.load();
        const int    dScale = detMinor.load() ? 1 : 0;
        const double conf   = detConf.load();

        // Confidence gate: an ambiguous detection (tiny winner-vs-runner-up margin,
        // e.g. flat/atonal material) neither commits nor disturbs the held key —
        // this is what stops "confidently publish a wrong key".
        if (conf >= kMinPublishConfidence)
        {
            if (pubRoot < 0)
            {
                pubRoot = dRoot; pubScale = dScale;              // bootstrap: first confident reading
                candRoot = dRoot; candScale = dScale; candCount = 1;
            }
            else if (dRoot == pubRoot && dScale == pubScale)
            {
                candRoot = dRoot; candScale = dScale; candCount = 1;   // agrees with held key
            }
            else
            {
                // Hysteresis: a different candidate must win kStableAnalyses cycles
                // in a row before it can replace the held key, so one odd frame
                // can't flip the whole suite.
                if (dRoot == candRoot && dScale == candScale) ++candCount;
                else { candRoot = dRoot; candScale = dScale; candCount = 1; }
                if (candCount >= kStableAnalyses) { pubRoot = dRoot; pubScale = dScale; }
            }
        }

        if (pubRoot < 0) return;   // nothing confident committed yet — hold silence
        root  = pubRoot;
        scale = pubScale;
    }
    // Publish the full SessionState (Brain foundation): key + host tempo + the
    // reference tuning we already estimate from the spectrum. referenceHz rides the
    // bus so any plugin can follow the song's actual A, not just its key. The other
    // SessionState fields stay at safe defaults until a dedicated Brain sets them.
    sattari::xplugin::SessionMessage msg;
    msg.root        = root;
    msg.scale       = scale;
    msg.bpm         = hostBpm.load();
    msg.transientId = ++transientId;
    msg.referenceHz = 440.0 * std::pow (2.0, detDetuneCents.load() / 1200.0);
    sattari::xplugin::publishSessionState ("song-key", msg);
}

void SattariSongKeyAudioProcessor::stopWorker()
{
    workerRunning.store (false, std::memory_order_release);
    if (worker.joinable())
        worker.join();
}

void SattariSongKeyAudioProcessor::workerLoop()
{
    while (workerRunning.load (std::memory_order_acquire))
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (700));
        if (! workerRunning.load (std::memory_order_acquire)) break;
        analyseNow();
        publishCurrent();
    }
}

SattariSongKeyAudioProcessor::Detected SattariSongKeyAudioProcessor::detected() const noexcept
{
    Detected d;
    d.valid = detValid.load();
    d.root = detRoot.load();
    d.minor = detMinor.load();
    d.confidence = detConf.load();
    d.publishing = publishingNow.load();
    d.detuneCents = detDetuneCents.load();
    d.neural = detNeural.load();
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
