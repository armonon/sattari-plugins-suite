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
    workerRunning.store (false);
    if (worker.joinable()) worker.join();
}

void SattariSongKeyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    ring.assign ((size_t) fftSize, 0.0f);
    scratch.assign ((size_t) fftSize * 2, 0.0f);
    hann.assign ((size_t) fftSize, 0.0f);
    for (int i = 0; i < fftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
    writePos.store (0);
    std::fill (running, running + 12, 0.0);

    if (! workerRunning.exchange (true))
        worker = std::thread ([this] { workerLoop(); });
}

void SattariSongKeyAudioProcessor::releaseResources()
{
    workerRunning.store (false);
    if (worker.joinable()) worker.join();
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
        ring[(size_t) wp] = m / (float) nc;
        wp = (wp + 1) % fftSize;
    }
    writePos.store (wp, std::memory_order_relaxed);
}

void SattariSongKeyAudioProcessor::analyseNow()
{
    if (! fft || ring.empty()) return;
    if (rescan.exchange (false)) std::fill (running, running + 12, 0.0);

    const int wp = writePos.load (std::memory_order_relaxed);
    for (int i = 0; i < fftSize; ++i)
        scratch[(size_t) i] = ring[(size_t) ((wp + i) % fftSize)] * hann[(size_t) i];
    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (scratch.data());

    double frame[12] = { 0 };
    sattari::dsp::accumulateChroma (scratch.data(), fftSize / 2 + 1, currentSampleRate / (double) fftSize, frame);
    sattari::dsp::blendChroma (running, frame, 0.4);

    const auto k = sattari::dsp::detectKey (running);
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
        root  = detRoot.load();
        scale = detMinor.load() ? 1 : 0;
    }
    sattari::xplugin::publishSession ("song-key", root, scale, hostBpm.load(), ++transientId);
}

void SattariSongKeyAudioProcessor::workerLoop()
{
    while (workerRunning.load())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (700));
        if (! workerRunning.load()) break;
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
