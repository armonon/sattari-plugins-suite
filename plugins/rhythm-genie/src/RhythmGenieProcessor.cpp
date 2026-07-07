#include "RhythmGenieProcessor.h"
#include "RhythmGenieEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace sattari::rhythm;

namespace { constexpr int kDrumChannel = 10; }   // GM percussion channel

juce::StringArray SattariRhythmGenieAudioProcessor::styleChoices()
{
    int n = 0; const auto* b = patterns (n);
    juce::StringArray out;
    for (int i = 0; i < n; ++i) out.add (b[i].name);
    return out;
}

APVTS::ParameterLayout SattariRhythmGenieAudioProcessor::createParameterLayout()
{
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "style", 1 }, "Style", styleChoices(), 0));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "density",  1 }, "Density",  unit(), 0.5f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "swing",    1 }, "Swing",    unit(), 0.0f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "velocity", 1 }, "Velocity", unit(), 0.8f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "gate",     1 }, "Gate",     unit(), 0.5f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "accent",   1 }, "Accent",   unit(), 0.35f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "humanize", 1 }, "Humanize", unit(), 0.2f));

    static const char* laneIds[kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
    for (int l = 0; l < kNumLanes; ++l)
        p.push_back (std::make_unique<BoolP> (juce::ParameterID { laneIds[l], 1 },
                                              juce::String (laneName (l)) + " On", true));

    return { p.begin(), p.end() };
}

SattariRhythmGenieAudioProcessor::SattariRhythmGenieAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    pendingOffs.reserve (64);
}

void SattariRhythmGenieAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    internalPpq = 0.0;
    pendingOffs.clear();
    wasGenerating = false;
    playStep.store (-1);
}

bool SattariRhythmGenieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

bool SattariRhythmGenieAudioProcessor::laneEnabled (int lane) const noexcept
{
    static const char* ids[kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
    if (lane < 0 || lane >= kNumLanes) return false;
    return apvts.getRawParameterValue (ids[lane])->load() > 0.5f;
}

void SattariRhythmGenieAudioProcessor::allNotesOff (juce::MidiBuffer& out)
{
    for (const auto& po : pendingOffs)
        out.addEvent (juce::MidiMessage::noteOff (kDrumChannel, po.note), 0);
    pendingOffs.clear();
}

void SattariRhythmGenieAudioProcessor::emitStep (int stepIdx, int sampleOffset,
                                                 juce::MidiBuffer& out, double samplesPerStep)
{
    const auto& pat   = pattern ((int) std::round (param ("style")));
    const float dens  = param ("density");
    const float vBase = param ("velocity");
    const float acc   = param ("accent");
    const float hum   = param ("humanize");
    const int   gateS = juce::jmax (1, (int) (param ("gate") * samplesPerStep));

    for (int lane = 0; lane < kNumLanes; ++lane)
    {
        if (! laneEnabled (lane) || ! stepActive (pat, lane, stepIdx, dens))
            continue;

        float v = vBase;
        if (stepIdx % 4 == 0) v += acc * 0.3f;                       // downbeat accent
        v += (rng.nextFloat() * 2.0f - 1.0f) * 0.15f * hum;          // humanise
        const int vel = juce::jlimit (1, 127, (int) std::round (v * 127.0f));
        const int note = laneNote (lane);

        out.addEvent (juce::MidiMessage::noteOn (kDrumChannel, note, (juce::uint8) vel), sampleOffset);
        pendingOffs.push_back ({ note, sampleOffset + gateS });
        pubPulse.fetch_add (1, std::memory_order_relaxed);   // transient pulse for the bus
    }
}

void SattariRhythmGenieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();
    buffer.clear();                                  // generator: no audio output

    juce::MidiBuffer generated;

    const auto t = sattari::host::read (*this);
    const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
    pubBpm.store (bpm, std::memory_order_relaxed);   // shared to the cross-plugin tempo bus
    const double samplesPerBeat = currentSampleRate * 60.0 / bpm;
    const double samplesPerStep = samplesPerBeat / 4.0;
    const bool standalone = (wrapperType == wrapperType_Standalone);

    double startPpq = 0.0;
    bool generate = false;
    if (t.hasPpq) { startPpq = t.ppqPosition;  generate = t.isPlaying; }
    else          { startPpq = internalPpq;    generate = t.isPlaying || standalone; }

    if (! generate)
    {
        if (wasGenerating) allNotesOff (generated);  // release hanging notes on stop
        wasGenerating = false;
        playStep.store (-1);
        midi.swapWith (generated);
        return;
    }
    wasGenerating = true;

    const double blockBeats = (double) numSamples / samplesPerBeat;
    const double endPpq     = startPpq + blockBeats;
    const double swingPpq   = (double) param ("swing") * 0.125;  // delay odd 16ths up to half a step

    // Emit every step whose (swung) onset lands inside this block.
    for (int k = (int) std::floor (startPpq * 4.0) - 1; ; ++k)
    {
        const double onset = (double) k * 0.25 + ((k & 1) ? swingPpq : 0.0);
        if (onset >= endPpq) break;
        if (onset < startPpq) continue;
        const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                         (int) std::lround ((onset - startPpq) * samplesPerBeat));
        emitStep (((k % kSteps) + kSteps) % kSteps, offset, generated, samplesPerStep);
    }

    // Resolve note-offs (existing + the ones just scheduled) against this block.
    std::vector<PendingOff> keep;
    keep.reserve (pendingOffs.size());
    for (auto& po : pendingOffs)
    {
        if (po.samplesRemaining < numSamples)
            generated.addEvent (juce::MidiMessage::noteOff (kDrumChannel, po.note), po.samplesRemaining);
        else
        {
            po.samplesRemaining -= numSamples;
            keep.push_back (po);
        }
    }
    pendingOffs.swap (keep);

    midi.swapWith (generated);

    if (! t.hasPpq) internalPpq += blockBeats;
    playStep.store ((((int) std::floor (startPpq * 4.0)) % kSteps + kSteps) % kSteps);
}

void SattariRhythmGenieAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariRhythmGenieAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void SattariRhythmGenieAudioProcessor::publishTempoToBus()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    if (now - lastPublishMs < 250) return;   // throttle to ~4 Hz
    lastPublishMs = now;
    sattari::xplugin::publishTempo ("rhythm-genie",
                                    pubBpm.load (std::memory_order_relaxed),
                                    pubPulse.load (std::memory_order_relaxed));
}

juce::AudioProcessorEditor* SattariRhythmGenieAudioProcessor::createEditor()
{
    return new SattariRhythmGenieAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariRhythmGenieAudioProcessor();
}
