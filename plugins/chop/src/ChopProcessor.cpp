#include "ChopProcessor.h"
#include "ChopEditor.h"

#include <algorithm>

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
constexpr float kEnvRate = 0.002f;    // ~1 ms at 48k: short attack/release, no clicks
constexpr int   kZeroSearch = 256;    // samples either side to find a zero crossing
}

APVTS::ParameterLayout SattariChopAudioProcessor::createParameterLayout()
{
    using FloatP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    // The load-bearing control (§2.2): re-thresholds the stored onset curve live.
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "sensitivity", 1 }, "Sensitivity", unit(), 0.5f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "output",      1 }, "Output",      unit(), 0.8f));

    return { p.begin(), p.end() };
}

SattariChopAudioProcessor::SattariChopAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
}

void SattariChopAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices) { v.active = false; v.releasing = false; v.env = 0.0f; v.pos = 0.0; }
}

bool SattariChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// --- Source loading + slicing (message thread) ---------------------------------
SattariChopAudioProcessor::Source::Ptr SattariChopAudioProcessor::snapshot() const
{
    const juce::SpinLock::ScopedLockType sl (sourceLock);
    return current;
}

bool SattariChopAudioProcessor::hasSource() const noexcept { return current != nullptr; }
juce::String SattariChopAudioProcessor::sourceName() const { return current != nullptr ? current->name : juce::String(); }
int SattariChopAudioProcessor::numSlices() const noexcept { return current != nullptr ? (int) current->slices.size() : 0; }
double SattariChopAudioProcessor::sourceLengthSeconds() const noexcept
{
    return current != nullptr && current->sr > 0.0 ? (double) current->audio.getNumSamples() / current->sr : 0.0;
}

void SattariChopAudioProcessor::publish (Source::Ptr s)
{
    retained.add (s);                                   // never freed on the audio thread
    const juce::SpinLock::ScopedLockType sl (sourceLock);
    current = s;
}

// §3.2: slice boundaries always resolve to a nearby zero crossing. Non-negotiable —
// without it every slice clicks.
std::int64_t SattariChopAudioProcessor::snapToZeroCrossing (const juce::AudioBuffer<float>& b, std::int64_t pos) const
{
    const std::int64_t n = b.getNumSamples();
    if (n <= 1) return 0;
    pos = juce::jlimit<std::int64_t> (0, n - 1, pos);
    const float* d = b.getReadPointer (0);

    for (int off = 0; off < kZeroSearch; ++off)
    {
        const std::int64_t a = pos - off, c = pos + off;
        if (a > 0 && ((d[a - 1] <= 0.0f && d[a] >= 0.0f) || (d[a - 1] >= 0.0f && d[a] <= 0.0f))) return a;
        if (c + 1 < n && ((d[c] <= 0.0f && d[c + 1] >= 0.0f) || (d[c] >= 0.0f && d[c + 1] <= 0.0f))) return c;
    }
    return pos;
}

void SattariChopAudioProcessor::rebuildSlicesFromTable (Source& s) const
{
    s.slices.clear();
    for (const auto& b : table.boundaries ((std::int64_t) s.audio.getNumSamples()))
    {
        const std::int64_t a = snapToZeroCrossing (s.audio, b.first);
        const std::int64_t e = snapToZeroCrossing (s.audio, b.second);
        if (e > a) s.slices.push_back ({ a, e });
    }
}

bool SattariChopAudioProcessor::loadFile (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;

    const std::int64_t len = (std::int64_t) reader->lengthInSamples;
    if (len <= 0) return false;

    Source::Ptr s = new Source();
    s->sr = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    s->name = file.getFileName();
    s->audio.setSize ((int) juce::jmax (1u, reader->numChannels), (int) len);
    reader->read (&s->audio, 0, (int) len, 0, true, true);

    // Mono sum drives detection — chopping follows what you hear.
    monoScratch.assign ((std::size_t) len, 0.0f);
    for (int ch = 0; ch < s->audio.getNumChannels(); ++ch)
    {
        const float* p = s->audio.getReadPointer (ch);
        for (std::int64_t i = 0; i < len; ++i) monoScratch[(std::size_t) i] += p[i];
    }
    const float inv = 1.0f / (float) juce::jmax (1, s->audio.getNumChannels());
    for (auto& v : monoScratch) v *= inv;

    onset.analyze (monoScratch.data(), len, s->sr);       // the one expensive pass
    table.clear();
    table.setMergeTolerance ((std::int64_t) (s->sr * 0.010));
    table.applyDetection (onset.candidates (apvts.getRawParameterValue ("sensitivity")->load()),
                          0.0f);                          // curve already thresholded
    rebuildSlicesFromTable (*s);

    loadedPath = file.getFullPathName();
    publish (s);
    return true;
}

// Sensitivity moved: re-threshold the STORED curve. No FFT, no file I/O (§2.2).
// Locked/deleted markers survive, because SliceTable enforces that.
void SattariChopAudioProcessor::reslice()
{
    Source::Ptr s = snapshot();
    if (s == nullptr || ! onset.analysed()) return;

    table.applyDetection (onset.candidates (apvts.getRawParameterValue ("sensitivity")->load()), 0.0f);

    Source::Ptr next = new Source();
    next->audio.makeCopyOf (s->audio);
    next->sr = s->sr;
    next->name = s->name;
    rebuildSlicesFromTable (*next);
    publish (next);
}

// --- Voices --------------------------------------------------------------------
void SattariChopAudioProcessor::startVoice (int sliceIndex, int note, float velocity)
{
    Source::Ptr s;
    {
        const juce::SpinLock::ScopedTryLockType sl (sourceLock);
        if (! sl.isLocked() || current == nullptr) return;
        s = current;
    }
    if (sliceIndex < 0 || sliceIndex >= (int) s->slices.size()) return;

    // Free voice, else steal the oldest.
    int idx = -1;
    for (int i = 0; i < kMaxVoices; ++i) if (! voices[(size_t) i].active) { idx = i; break; }
    if (idx < 0) { idx = voiceCounter % kMaxVoices; }
    voiceCounter = (voiceCounter + 1) % (kMaxVoices * 4);

    auto& v = voices[(size_t) idx];
    v.src = s;
    v.slice = sliceIndex;
    v.pos = (double) s->slices[(std::size_t) sliceIndex].first;
    v.gain = juce::jlimit (0.0f, 1.0f, velocity);
    v.env = 0.0f;
    v.releasing = false;
    v.active = true;
    v.note = note;
}

void SattariChopAudioProcessor::stopVoicesForNote (int note)
{
    for (auto& v : voices) if (v.active && v.note == note) v.releasing = true;
}

void SattariChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       startVoice (m.getNoteNumber() - kBaseNote, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) stopVoicesForNote (m.getNoteNumber());
    }

    const float outGain = 0.2f + 1.0f * juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("output")->load());
    const int outCh = buffer.getNumChannels();

    for (auto& v : voices)
    {
        if (! v.active || v.src == nullptr) continue;
        const auto& src = v.src->audio;
        const auto bounds = v.src->slices[(std::size_t) v.slice];
        const int srcCh = src.getNumChannels();
        // Resample if the file's rate differs from the session's.
        const double rate = v.src->sr / (currentSampleRate > 0.0 ? currentSampleRate : 44100.0);

        for (int i = 0; i < numSamples; ++i)
        {
            const std::int64_t idx = (std::int64_t) v.pos;
            if (idx + 1 >= bounds.second) { v.active = false; break; }

            v.env += v.releasing ? -kEnvRate : kEnvRate;
            v.env = juce::jlimit (0.0f, 1.0f, v.env);
            if (v.releasing && v.env <= 0.0f) { v.active = false; break; }

            const float frac = (float) (v.pos - (double) idx);
            const float amp = v.gain * v.env * outGain;
            for (int ch = 0; ch < outCh; ++ch)
            {
                const float* d = src.getReadPointer (juce::jmin (ch, srcCh - 1));
                const float s0 = d[idx], s1 = d[idx + 1];
                buffer.getWritePointer (ch)[i] += (s0 + frac * (s1 - s0)) * amp;
            }
            v.pos += rate;
        }
    }
}

void SattariChopAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        if (loadedPath.isNotEmpty()) xml->setAttribute ("sourcePath", loadedPath);
        copyXmlToBinary (*xml, destData);
    }
}

void SattariChopAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const juce::String path = xml->getStringAttribute ("sourcePath");
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        if (path.isNotEmpty()) loadFile (juce::File (path));   // re-analyse on restore
    }
}

juce::AudioProcessorEditor* SattariChopAudioProcessor::createEditor()
{
    return new SattariChopAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariChopAudioProcessor();
}
