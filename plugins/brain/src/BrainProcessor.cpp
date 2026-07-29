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

void SattariBrainAudioProcessor::prepareToPlay (double sampleRate, int)
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
    runningDetune = 0.0;
    pubRoot = -1; pubScale = 0; candRoot = -1; candScale = 0; candCount = 0;
    detValid.store (false);
    publishingNow.store (false);
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

    // Feed the analysis ring with a mono sum. Audio is NOT modified — Brain is a
    // transparent analyser that only listens (bit-exact pass-through).
    int wp = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) m += buffer.getSample (ch, i);
        ring[(size_t) wp].store (m / (float) nc, std::memory_order_relaxed);
        wp = (wp + 1) % fftSize;
    }
    writePos.store (wp, std::memory_order_release);
}

void SattariBrainAudioProcessor::analyseNow()
{
    if (! fft) return;
    if (rescan.exchange (false))
    {
        hpcp.reset();
        runningDetune = 0.0;
        pubRoot = -1; candCount = 0;   // let a fresh scan re-commit the key
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

    const auto k = sattari::dsp::detectKey (frame);
    if (k.valid)
    {
        detRoot.store (k.root);
        detMinor.store (k.minor);
        detConf.store (k.confidence);
        detValid.store (true);
    }
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
    //     detected key (never flickers). Brain stays silent until a confident
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
    // Brain currently has no onset detector. Zero is truthful and, unlike the
    // former timer-driven increment, cannot masquerade as a musical transient.
    msg.transientId  = 0;
    msg.chordRoot    = chordRootChoice <= 0 ? -1 : chordRootChoice - 1;
    msg.chordQuality = (int) std::round (param ("brainChordQuality"));         // 0..8
    msg.timeSigNum   = (int) std::round (param ("brainTimeSigNum"));
    msg.timeSigDen   = timeSigDenValue ((int) std::round (param ("brainTimeSigDen")));
    msg.swing        = param ("brainSwing");
    msg.sectionId    = (int) std::round (param ("brainSection"));              // 0..6
    msg.energy       = param ("brainEnergy");
    msg.maqamId      = (int) std::round (param ("brainMaqam"));
    msg.referenceHz  = refOverride ? (double) param ("brainReferenceHz")
                                   : 440.0 * std::pow (2.0, detDetuneCents.load() / 1200.0);

    publishingNow.store (sattari::xplugin::publishSessionState ("brain", msg));
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
