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
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::ChopModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

SattariChopAudioProcessor::SattariChopAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
}

void SattariChopAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumOutputChannels());
}

bool SattariChopAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// --- Source loading + slicing (message thread) ---------------------------------
SattariChopAudioProcessor::Source::Ptr SattariChopAudioProcessor::snapshot() const
{
    return module.snapshot();
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
    // The module owns the audio-thread reference and the session-long retain, so
    // publishing is a single hand-off. We keep our own copy for the editor.
    current = s;
    module.setSource (std::move (s));
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

// Rebuild the published slices from the current table (after a hand edit), keeping the
// audio. One deep copy per edit — edits are user gestures, not per-frame, so this is fine.
void SattariChopAudioProcessor::republishFromTable()
{
    Source::Ptr s = snapshot();
    if (s == nullptr) return;
    Source::Ptr next = new Source();
    next->audio.makeCopyOf (s->audio);
    next->sr = s->sr;
    next->name = s->name;
    rebuildSlicesFromTable (*next);
    publish (next);
}

// Nearest detected onset to `sample` within `window`, else `sample`. Snaps to the actual
// transients (candidates at a permissive threshold), not the currently-thresholded set,
// so magnetism works regardless of the Sensitivity setting (§3.2 priority: onsets first).
std::int64_t SattariChopAudioProcessor::onsetSnap (std::int64_t sample, std::int64_t window) const
{
    if (! onset.analysed()) return sample;
    const auto cands = onset.candidates (0.05f);
    std::int64_t best = sample, bestD = window + 1;
    for (const auto& c : cands)
    {
        const std::int64_t d = std::llabs (c.samplePos - sample);
        if (d <= window && d < bestD) { bestD = d; best = c.samplePos; }
    }
    return best;
}

// --- §3 gestures ----------------------------------------------------------------
void SattariChopAudioProcessor::addSliceAtSample (std::int64_t sample, bool snap)
{
    Source::Ptr s = snapshot();
    if (s == nullptr) return;
    const std::int64_t win = (std::int64_t) (s->sr * 0.015);   // +/-15 ms magnetism at default zoom
    std::int64_t pos = snap ? onsetSnap (sample, win) : sample;
    pos = snapToZeroCrossing (s->audio, pos);                  // always: no clicks (§3.2)
    table.addMarker (pos);
    republishFromTable();
}

void SattariChopAudioProcessor::moveSliceNear (std::int64_t fromSample, std::int64_t toSample, bool snap)
{
    Source::Ptr s = snapshot();
    if (s == nullptr) return;
    const std::int64_t win = (std::int64_t) (s->sr * 0.015);
    std::int64_t pos = snap ? onsetSnap (toSample, win) : toSample;
    pos = snapToZeroCrossing (s->audio, pos);
    table.moveMarkerNear (fromSample, pos);
    republishFromTable();
}

void SattariChopAudioProcessor::deleteSliceNear (std::int64_t sample)
{
    table.deleteMarkerNear (sample);
    republishFromTable();
}

void SattariChopAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    module.process (buffer, midi);
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
