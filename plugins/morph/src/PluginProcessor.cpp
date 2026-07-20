#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace { const juce::StringArray kModes { "Shift", "Quantize", "Robot" };
           const juce::StringArray kNotes { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }

SattariMorphAudioProcessor::SattariMorphAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MORPH", createLayout())
{
    for (auto& b : busScale_) b = true;
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariMorphAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> ("mode", "Mode", kModes, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  ("pitch", "Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("formant", "Formant",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterChoice> ("robotnote", "Robot Note", kNotes, 9));
    p.push_back (std::make_unique<AudioParameterFloat>  ("drive", "Drive",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("mix", "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    return { p.begin(), p.end() };
}

void SattariMorphAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    pitch_.prepare (sampleRate, 60.0f, 1000.0f, 0.045f);
    psola_.prepare (sampleRate, 60.0, 1000.0);
    formant_.prepare (sampleRate, 10, 4);
    const int n = juce::jmax (32, samplesPerBlock);
    dryMono_.assign ((size_t) n, 0.0f);
    wet_.assign ((size_t) n, 0.0f);
    totalLatency_ = psola_.getLatencySamples() + formant_.latencySamples();
    dryDelay_.assign ((size_t) (totalLatency_ + n + 4), 0.0f);
    dryPos_ = 0; lastPeriod_ = sampleRate / 220.0; lastScale_ = 1.0f;
    setLatencySamples (totalLatency_);
}

bool SattariMorphAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto o = layouts.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void SattariMorphAudioProcessor::pullBusScale()
{
    if (auto m = sattari::xplugin::readSession (30))
    {
        static const int MAJ[] = { 0,2,4,5,7,9,11 }, MIN[] = { 0,2,3,5,7,8,10 };
        for (auto& b : busScale_) b = false;
        if (m->scale == 2) { for (auto& b : busScale_) b = true; }
        else { const int* s = (m->scale == 1) ? MIN : MAJ; for (int i = 0; i < 7; ++i) busScale_[(size_t) ((m->root + s[i]) % 12)] = true; }
    }
}

void SattariMorphAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    auto main = getBusBuffer (buffer, true, 0);
    const int nch = main.getNumChannels();
    if ((int) dryMono_.size() < n) { dryMono_.assign ((size_t) n, 0.0f); wet_.assign ((size_t) n, 0.0f); }

    const int mode = (int) apvts.getRawParameterValue ("mode")->load();   // 0 Shift 1 Quantize 2 Robot
    const float pitchKnob = apvts.getRawParameterValue ("pitch")->load();
    const float formantSt = apvts.getRawParameterValue ("formant")->load();
    const int   robotNote = (int) apvts.getRawParameterValue ("robotnote")->load();
    const float drive = apvts.getRawParameterValue ("drive")->load();
    const float mix   = apvts.getRawParameterValue ("mix")->load();

    if (mode == 1)   // Quantize needs the bus key (poll off the fast path)
    {
        const auto now = juce::Time::currentTimeMillis();
        if (now - lastBusPollMs_ > 500) { lastBusPollMs_ = now; pullBusScale(); }
    }

    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f; for (int c = 0; c < nch; ++c) s += main.getSample (c, i);
        dryMono_[(size_t) i] = nch > 0 ? s / nch : 0.0f;
    }

    // Detect pitch for Quantize/Robot + the PSOLA period.
    pitch_.pushBlock (dryMono_.data(), n);
    const auto pr = pitch_.analyse (0.2f);
    double period = lastPeriod_;
    if (pr.voiced && pr.frequencyHz > 0.0f) { period = sr_ / pr.frequencyHz; lastPeriod_ = period; }

    // Pitch-shift ratio by mode. The Pitch knob transposes in every mode.
    float scale = std::pow (2.0f, pitchKnob / 12.0f);
    if ((mode == 1 || mode == 2) && pr.voiced && pr.frequencyHz > 0.0f)
    {
        const float detMidi = 69.0f + 12.0f * std::log2 (pr.frequencyHz / 440.0f);
        float tgtMidi;
        if (mode == 1)
        {
            int best = (int) std::lround (detMidi); float bd = 1e9f;
            for (int d = -7; d <= 7; ++d) { const int mm = (int) std::lround (detMidi) + d; if (! busScale_[(size_t) (((mm % 12) + 12) % 12)]) continue; const float dist = std::abs (detMidi - mm); if (dist < bd) { bd = dist; best = mm; } }
            tgtMidi = (float) best;
        }
        else   // Robot: nearest octave of the fixed note
            tgtMidi = std::round ((detMidi - robotNote) / 12.0f) * 12.0f + robotNote;
        scale *= std::pow (2.0f, (tgtMidi - detMidi) / 12.0f);
    }
    scale = juce::jlimit (0.25f, 4.0f, scale);
    lastScale_ = scale;

    // 1) pitch shift (TD-PSOLA), 2) formant warp, 3) drive.
    psola_.process (dryMono_.data(), wet_.data(), n, scale, period);
    sattari::dsp::FormantShifter::Params fp; fp.shiftSemitones = formantSt; fp.amount = 1.0f;
    formant_.process (wet_.data(), wet_.data(), n, fp);
    if (drive > 0.0001f)
    {
        const float g = 1.0f + drive * 6.0f, mk = 1.0f / std::tanh (g);
        for (int i = 0; i < n; ++i) wet_[(size_t) i] = std::tanh (wet_[(size_t) i] * g) * mk;
    }

    // Delay dry to the wet latency, then mix.
    for (int i = 0; i < n; ++i)
    {
        dryDelay_[(size_t) dryPos_] = dryMono_[(size_t) i];
        const int rd = (dryPos_ - totalLatency_ + (int) dryDelay_.size()) % (int) dryDelay_.size();
        const float dry = dryDelay_[(size_t) rd];
        dryPos_ = (dryPos_ + 1) % (int) dryDelay_.size();
        const float o = mix * wet_[(size_t) i] + (1.0f - mix) * dry;
        for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.setSample (c, i, o);
    }
}

void SattariMorphAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void SattariMorphAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessorEditor* SattariMorphAudioProcessor::createEditor() { return new SattariMorphAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMorphAudioProcessor(); }
