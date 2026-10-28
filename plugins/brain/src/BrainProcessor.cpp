#include "BrainProcessor.h"
#include "BrainEditor.h"
#include <chrono>
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

// --- Param choice vocabularies ----------------------------------------------
juce::StringArray SattariBrainAudioProcessor::rootChoices()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}
juce::StringArray SattariBrainAudioProcessor::scaleChoices() { return { "Major", "Minor" }; }

juce::StringArray SattariBrainAudioProcessor::chordRootChoices()
{
    return { "None", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}
juce::StringArray SattariBrainAudioProcessor::chordQualityChoices()
{
    // Order matches SessionMessage::chordQuality (0=none..8=sus).
    return { "none", "maj", "min", "dom7", "min7", "maj7", "dim", "aug", "sus" };
}
juce::StringArray SattariBrainAudioProcessor::timeSigDenChoices() { return { "1", "2", "4", "8", "16" }; }
juce::StringArray SattariBrainAudioProcessor::sectionChoices()
{
    // Order matches SessionMessage::sectionId (0=none..6=outro).
    return { "None", "Intro", "Verse", "Prechorus", "Chorus", "Bridge", "Outro" };
}
int SattariBrainAudioProcessor::timeSigDenValue (int choiceIndex) noexcept
{
    static const int den[] = { 1, 2, 4, 8, 16 };
    return den[juce::jlimit (0, 4, choiceIndex)];
}

APVTS::ParameterLayout SattariBrainAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Publish master switch.
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "brainPublish", 1 }, "Publish to Suite", true));

    // Manual key override (detected key otherwise wins).
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "brainManual",      1 }, "Manual Key Override", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainManualRoot",  1 }, "Manual Root",  rootChoices(),  0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainManualScale", 1 }, "Manual Scale", scaleChoices(), 0));

    // Reference tuning override (detected detune otherwise drives referenceHz).
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "brainRefOverride", 1 }, "Reference Override", false));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "brainReferenceHz", 1 }, "Reference A (Hz)",
        juce::NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f));

    // Editable SessionState fields.
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainChordRoot",    1 }, "Chord Root",    chordRootChoices(),    0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainChordQuality", 1 }, "Chord Quality", chordQualityChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterInt>    (juce::ParameterID { "brainTimeSigNum",   1 }, "Time Sig Num", 1, 16, 4));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainTimeSigDen",   1 }, "Time Sig Den", timeSigDenChoices(), 2 /* -> 4 */));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "brainSwing",  1 }, "Swing",  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "brainSection",1 }, "Section", sectionChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterFloat>  (juce::ParameterID { "brainEnergy", 1 }, "Energy", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterInt>    (juce::ParameterID { "brainMaqam",  1 }, "Maqam Id", 0, 12, 0));

    // Appended to preserve every existing host parameter index. Automatic
    // energy can be locally overridden without weakening the detected context.
    p.push_back (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { "brainEnergyAuto", 1 }, "Automatic Energy", true));

    return { p.begin(), p.end() };
}

SattariBrainAudioProcessor::SattariBrainAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

SattariBrainAudioProcessor::~SattariBrainAudioProcessor()
{
    stopWorker();
}

void SattariBrainAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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
    hpcp.reserveBins (fftSize / 2 + 1);
    hpcp.reset();

    //  Four seconds of feed against a ~700 ms worker cadence: room for the worker
    //  to be late without dropping audio, and small enough to stay trivial
    //  (~768 kB at 48 kHz). Allocated here, never on the audio thread.
    {
        const int feedSamples = juce::jmax (samplesPerBlock * 8,
                                            (int) std::ceil (sampleRate * 4.0));
        keyFeed = std::vector<std::atomic<float>> ((size_t) feedSamples);
        for (auto& sample : keyFeed) sample.store (0.0f, std::memory_order_relaxed);
        keyDrain.assign ((size_t) feedSamples, 0.0f);
        keyFeedWrite.store (0, std::memory_order_relaxed);
        keyFeedRead = 0;
        keyFeedDropped.store (0, std::memory_order_relaxed);
        songKeyFrames.store (0, std::memory_order_relaxed);
        songKey.prepare (sampleRate, 0);   // 0 = open-ended live stream
        songKey.reset();
    }
    runningDetune = 0.0;
    pubRoot = -1; pubScale = 0; candRoot = -1; candScale = 0; candCount = 0;
    detValid.store (false);
    detChordValid.store (false);
    detChordRoot.store (-1);
    detChordQuality.store (0);
    detChordConfidence.store (0.0);
    detEnergy.store (0.0f);
    transientId.store (0);
    onsetEnvelope = 0.0f;
    onsetRefractorySamples = 0;
    publishingNow.store (false);
    publicationResult.store (sattari::xplugin::SessionPublishResult::notAttempted);
    publicationAttempts.store (0);
    refreshDetectedFrame();

    workerRunning.store (true, std::memory_order_release);
    worker = std::thread ([this] { workerLoop(); });
}

void SattariBrainAudioProcessor::releaseResources()
{
    stopWorker();
}

bool SattariBrainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    if (in != layouts.getMainOutputChannelSet()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SattariBrainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int n  = buffer.getNumSamples();
    const int nc = juce::jmax (1, buffer.getNumChannels());

    // Host transport: BPM (shared reader) + time signature (playhead). RT-safe —
    // Optionals only, no allocation. These feed the readout; the published time
    // signature is the editable param (edited value wins over detected).
    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm) hostBpm.store (t.bpm);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto ts = pos->getTimeSignature())
            {
                hostTsNum.store (ts->numerator, std::memory_order_relaxed);
                hostTsDen.store (ts->denominator, std::memory_order_relaxed);
            }

    // Feed the analysis ring with a mono sum. Audio is NOT modified — Compass is a
    // transparent analyser that only listens (bit-exact pass-through).
    int wp = writePos.load (std::memory_order_relaxed);
    double sumSquares = 0.0;
    const float onsetRelease = std::exp (-1.0f / (float) (currentSampleRate * 0.080));
    const int refractory = juce::jmax (1, (int) std::round (currentSampleRate * 0.055));
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) m += buffer.getSample (ch, i);
        m /= (float) nc;
        ring[(size_t) wp].store (m, std::memory_order_relaxed);
        if (! keyFeed.empty())
        {
            const std::int64_t w = keyFeedWrite.load (std::memory_order_relaxed);
            keyFeed[(size_t) (w % (std::int64_t) keyFeed.size())]
                .store (m, std::memory_order_relaxed);
            keyFeedWrite.store (w + 1, std::memory_order_release);
        }
        sumSquares += (double) m * (double) m;

        const float level = std::abs (m);
        if (onsetRefractorySamples > 0) --onsetRefractorySamples;
        if (onsetRefractorySamples == 0
            && level > 0.035f
            && level > onsetEnvelope * 1.85f)
        {
            transientId.fetch_add (1, std::memory_order_relaxed);
            onsetRefractorySamples = refractory;
        }
        onsetEnvelope = level > onsetEnvelope ? level : onsetEnvelope * onsetRelease;
        wp = (wp + 1) % fftSize;
    }
    writePos.store (wp, std::memory_order_release);

    const double rms = n > 0 ? std::sqrt (sumSquares / (double) n) : 0.0;
    const double db = juce::Decibels::gainToDecibels (rms, -60.0);
    const float measuredEnergy = (float) juce::jlimit (0.0, 1.0, (db + 60.0) / 60.0);
    const float previousEnergy = detEnergy.load (std::memory_order_relaxed);
    const float energyAlpha = 1.0f - std::exp (-(float) n / (float) (currentSampleRate * 0.20));
    detEnergy.store (previousEnergy + energyAlpha * (measuredEnergy - previousEnergy),
                     std::memory_order_relaxed);
}

void SattariBrainAudioProcessor::analyseNow()
{
    if (! fft) return;
    if (rescan.exchange (false))
    {
        hpcp.reset();
        runningDetune = 0.0;
        pubRoot = -1; candCount = 0;   // let a fresh scan re-commit the key
        //  The estimator accumulates over the whole session, so a rescan has to
        //  clear it as well — otherwise "rescan" would keep answering with the
        //  chroma of everything played before it.
        songKey.reset();
        songKeyFrames.store (0, std::memory_order_relaxed);
        keyFeedRead = keyFeedWrite.load (std::memory_order_acquire);
    }

    //  Drain whatever the audio thread has produced since the last pass.
    if (! keyFeed.empty())
    {
        const std::int64_t w = keyFeedWrite.load (std::memory_order_acquire);
        const std::int64_t capacity = (std::int64_t) keyFeed.size();
        std::int64_t available = w - keyFeedRead;
        if (available > capacity)
        {
            //  The worker fell far enough behind that the audio thread lapped the
            //  buffer. Count it rather than silently analysing a scrambled ring:
            //  a key built from torn audio is worse than one built from less of it.
            keyFeedDropped.fetch_add (available - capacity, std::memory_order_relaxed);
            keyFeedRead = w - capacity;
            available = capacity;
        }
        if (available > 0)
        {
            const int count = (int) juce::jmin (available, (std::int64_t) keyDrain.size());
            for (int i = 0; i < count; ++i)
                keyDrain[(size_t) i] = keyFeed[(size_t) ((keyFeedRead + i) % capacity)]
                                           .load (std::memory_order_relaxed);
            keyFeedRead += count;
            songKey.addSamples (keyDrain.data(), count);
            songKeyFrames.store (songKey.framesAnalysed(), std::memory_order_relaxed);
        }
    }

    const int wp = writePos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((wp + i) % fftSize)].load (std::memory_order_relaxed) * hann[(size_t) i];
    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (scratch.data());

    hpcp.accumulate (scratch.data(), fftSize / 2 + 1,
                     currentSampleRate / (double) fftSize);
    double frame[12] = {};
    hpcp.finish (frame);
    runningDetune += 0.2 * (hpcp.tuningOffsetSemitones() * 100.0 - runningDetune);
    detDetuneCents.store (runningDetune);

    //  Key: prefer the SongKeyEstimator once it has heard enough to have an
    //  opinion, and fall back to the HPCP/Aarden path until then. The fallback is
    //  not an error path — it is what answers in the first seconds of a session,
    //  and it is why removing the model-shaped dependency mattered: the estimator
    //  is plain DSP, so there is nothing to install and nothing to be missing.
    //
    //  Both confidences are winner-minus-runner-up margins, so kMinPublishConfidence
    //  and the stability hysteresis downstream keep their meaning. They are not the
    //  SAME margin though, computed over different features, so the threshold is
    //  inherited rather than re-derived and should be recalibrated against measured
    //  data before anyone leans on its exact value.
    constexpr int kMinSongKeyFrames = 120;         // ~30 s of 16 kHz frames
    const auto songKeyResult = songKey.result();
    const bool useSongKey = songKeyResult.valid
                         && songKey.framesAnalysed() >= kMinSongKeyFrames;

    const auto k = sattari::dsp::detectKey (frame);
    if (useSongKey)
    {
        detRoot.store (songKeyResult.root);
        detMinor.store (songKeyResult.minor);
        detConf.store (songKeyResult.confidence);
        detValid.store (true);
    }
    else if (k.valid)
    {
        detRoot.store (k.root);
        detMinor.store (k.minor);
        detConf.store (k.confidence);
        detValid.store (true);
    }

    // Lightweight triad evidence. This is deliberately limited to major/minor
    // rather than pretending to identify extended harmony from one short frame.
    // An explicit chord in the UI always wins at publish time.
    double best = -1.0, second = -1.0;
    int bestRoot = -1, bestQuality = 0;
    for (int root = 0; root < 12; ++root)
    {
        const double major = frame[root] + 0.82 * frame[(root + 4) % 12]
                           + 0.90 * frame[(root + 7) % 12];
        const double minor = frame[root] + 0.82 * frame[(root + 3) % 12]
                           + 0.90 * frame[(root + 7) % 12];
        for (const auto candidate : { std::pair<double, int> { major, 1 },
                                      std::pair<double, int> { minor, 2 } })
        {
            if (candidate.first > best)
            {
                second = best;
                best = candidate.first;
                bestRoot = root;
                bestQuality = candidate.second;
            }
            else if (candidate.first > second)
            {
                second = candidate.first;
            }
        }
    }
    const double chordConfidence = best > 1.0e-9 ? (best - second) / best : 0.0;
    const bool chordValid = bestRoot >= 0 && best > 0.16 && chordConfidence > 0.025;
    detChordValid.store (chordValid);
    detChordRoot.store (chordValid ? bestRoot : -1);
    detChordQuality.store (chordValid ? bestQuality : 0);
    detChordConfidence.store (chordConfidence);
}

void SattariBrainAudioProcessor::publishCurrent()
{
    const bool pub = param ("brainPublish") > 0.5f;
    publishingNow.store (false);
    if (! pub)
    {
        refreshDetectedFrame();
        return;
    }

    // --- Key: manual override always wins; else the confidence-gated + hysteresis
    //     detected key (never flickers). Compass stays silent until a confident
    //     key commits; publishing an invented C major would corrupt the suite.
    int root = 0, scale = 0;
    const bool manual = param ("brainManual") > 0.5f;
    if (manual)
    {
        root  = (int) std::round (param ("brainManualRoot"));
        scale = (int) std::round (param ("brainManualScale"));
    }
    else if (detValid.load())
    {
        const int    dRoot  = detRoot.load();
        const int    dScale = detMinor.load() ? 1 : 0;
        const double conf   = detConf.load();

        if (conf >= kMinPublishConfidence)
        {
            if (pubRoot < 0)
            {
                pubRoot = dRoot; pubScale = dScale;
                candRoot = dRoot; candScale = dScale; candCount = 1;
            }
            else if (dRoot == pubRoot && dScale == pubScale)
            {
                candRoot = dRoot; candScale = dScale; candCount = 1;
            }
            else
            {
                if (dRoot == candRoot && dScale == candScale) ++candCount;
                else { candRoot = dRoot; candScale = dScale; candCount = 1; }
                if (candCount >= kStableAnalyses) { pubRoot = dRoot; pubScale = dScale; }
            }
        }
        if (pubRoot >= 0) { root = pubRoot; scale = pubScale; }
    }
    if (! manual && pubRoot < 0)
    {
        refreshDetectedFrame();
        return;
    }

    // --- Editable SessionState fields (edited values are authoritative). --------
    const int chordRootChoice = (int) std::round (param ("brainChordRoot"));   // 0=None, 1..12 -> 0..11
    const int refOverride     = param ("brainRefOverride") > 0.5f;

    sattari::xplugin::SessionMessage msg;
    msg.root         = root;
    msg.scale        = scale;
    msg.bpm          = hostBpm.load();
    msg.transientId  = transientId.load (std::memory_order_relaxed);
    msg.chordRoot    = chordRootChoice <= 0
        ? (detChordValid.load() ? detChordRoot.load() : -1)
        : chordRootChoice - 1;
    msg.chordQuality = chordRootChoice <= 0
        ? (detChordValid.load() ? detChordQuality.load() : 0)
        : (int) std::round (param ("brainChordQuality"));                     // 0..8
    msg.timeSigNum   = (int) std::round (param ("brainTimeSigNum"));
    msg.timeSigDen   = timeSigDenValue ((int) std::round (param ("brainTimeSigDen")));
    msg.swing        = param ("brainSwing");
    msg.sectionId    = (int) std::round (param ("brainSection"));              // 0..6
    msg.energy       = param ("brainEnergyAuto") > 0.5f
        ? detEnergy.load (std::memory_order_relaxed)
        : param ("brainEnergy");
    msg.maqamId      = (int) std::round (param ("brainMaqam"));
    msg.referenceHz  = refOverride ? (double) param ("brainReferenceHz")
                                   : 440.0 * std::pow (2.0, detDetuneCents.load() / 1200.0);

    const auto result = sattari::xplugin::publishSessionStateDetailed ("brain", msg);
    publicationResult.store (result, std::memory_order_relaxed);
    publicationAttempts.fetch_add (1, std::memory_order_relaxed);
    publishingNow.store (result == sattari::xplugin::SessionPublishResult::published);
    refreshDetectedFrame();
}

void SattariBrainAudioProcessor::stopWorker()
{
    workerRunning.store (false, std::memory_order_release);
    if (worker.joinable())
        worker.join();
}

void SattariBrainAudioProcessor::workerLoop()
{
    while (workerRunning.load (std::memory_order_acquire))
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (700));
        if (! workerRunning.load (std::memory_order_acquire)) break;
        analyseNow();
        publishCurrent();
    }
}

SattariBrainAudioProcessor::Detected SattariBrainAudioProcessor::detected() const noexcept
{
    return detectedFrame.load();
}

void SattariBrainAudioProcessor::refreshDetectedFrame() noexcept
{
    Detected d;
    d.valid       = detValid.load();
    d.root        = detRoot.load();
    d.minor       = detMinor.load();
    d.confidence  = detConf.load();
    d.chordValid  = detChordValid.load();
    d.chordRoot   = detChordRoot.load();
    d.chordQuality = detChordQuality.load();
    d.chordConfidence = detChordConfidence.load();
    d.energy      = detEnergy.load (std::memory_order_relaxed);
    d.transientId = transientId.load (std::memory_order_relaxed);
    d.publishing  = publishingNow.load();
    d.detuneCents = detDetuneCents.load();
    d.bpm         = hostBpm.load();
    d.tsNum       = hostTsNum.load (std::memory_order_relaxed);
    d.tsDen       = hostTsDen.load (std::memory_order_relaxed);
    detectedFrame.store (d);
}

void SattariBrainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariBrainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariBrainAudioProcessor::createEditor()
{
    return new SattariBrainAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariBrainAudioProcessor();
}
