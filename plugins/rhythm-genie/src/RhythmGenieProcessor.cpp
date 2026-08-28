#include "RhythmGenieProcessor.h"
#include "RhythmGenieEditor.h"

#include <algorithm>

using APVTS = juce::AudioProcessorValueTreeState;
using namespace sattari::rhythm;

APVTS::ParameterLayout SattariRhythmGenieAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::RhythmGenieModule().addParameters (p, "");
    return { p.begin(), p.end() };
}

SattariRhythmGenieAudioProcessor::SattariRhythmGenieAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // The grid and the default groove live in the module now; seeding them still
    // belongs here, because it happens once at construction on the message thread.
    module.link (apvts, "");
    module.editableGrid().setLanes (kGridLanes);
    module.editableGrid().setSteps (sattari::rhythm::kSteps);
    registerFactoryPresets();
    module.regenerateGrid();   // seed an editable groove from the default style
    formatManager.registerBasicFormats();
}

void SattariRhythmGenieAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("House",       PV{ {"style",0}, {"density",0.70f}, {"swing",0.08f}, {"velocity",0.85f}, {"accent",0.40f}, {"humanize",0.15f} });
    presetBank.addFactoryPreset ("Techno Drive",PV{ {"style",0}, {"density",0.88f}, {"swing",0.00f}, {"velocity",0.92f}, {"accent",0.35f}, {"humanize",0.10f}, {"chatDecay",0.60f} });
    presetBank.addFactoryPreset ("Boom Bap",    PV{ {"style",1}, {"density",0.50f}, {"swing",0.18f}, {"velocity",0.80f}, {"accent",0.45f}, {"humanize",0.35f} });
    presetBank.addFactoryPreset ("Lo-Fi Swing", PV{ {"style",1}, {"density",0.45f}, {"swing",0.26f}, {"velocity",0.72f}, {"accent",0.30f}, {"humanize",0.50f}, {"kitLevel",0.72f} });
    presetBank.addFactoryPreset ("Trap",        PV{ {"style",2}, {"density",0.75f}, {"swing",0.00f}, {"velocity",0.85f}, {"accent",0.55f}, {"humanize",0.12f}, {"kickDecay",0.70f} });
    presetBank.addFactoryPreset ("Pop",         PV{ {"style",3}, {"density",0.55f}, {"swing",0.10f}, {"velocity",0.82f}, {"accent",0.40f}, {"humanize",0.20f} });
    presetBank.addFactoryPreset ("Breakbeat",   PV{ {"style",4}, {"density",0.72f}, {"swing",0.05f}, {"velocity",0.85f}, {"accent",0.45f}, {"humanize",0.30f} });
    presetBank.addFactoryPreset ("Half-Time",   PV{ {"style",5}, {"density",0.50f}, {"swing",0.00f}, {"velocity",0.80f}, {"accent",0.50f}, {"humanize",0.18f} });
}


juce::StringArray SattariRhythmGenieAudioProcessor::styleChoices()
{
    return sattari::dsp::RhythmGenieModule::styleChoices();
}

int SattariRhythmGenieAudioProcessor::kitLaneNote (int lane) noexcept
{
    return sattari::dsp::RhythmGenieModule::kitLaneNote (lane);
}

bool SattariRhythmGenieAudioProcessor::laneEnabled (int lane) const noexcept
{
    return module.laneEnabled (lane);
}

void SattariRhythmGenieAudioProcessor::regenerateGrid()
{
    module.regenerateGrid();
}

void SattariRhythmGenieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumOutputChannels());
}

bool SattariRhythmGenieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SattariRhythmGenieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);
    module.setTransport (sattari::host::read (*this));
    module.process (buffer, midi);
}

void SattariRhythmGenieAudioProcessor::publishTempoToBus()
{
    // Message thread: throttled publish of the engine's live tempo + pulse count.
    const auto now = juce::Time::currentTimeMillis();
    if (now - lastPublishMs < 250) return;
    lastPublishMs = now;
    sattari::xplugin::publishTempo ("rhythm-genie", module.publishedBpm(), module.publishedPulse());
}

void SattariRhythmGenieAudioProcessor::pushLiveNote (int note, float velocity) noexcept { module.pushLiveNote (note, velocity); }
void SattariRhythmGenieAudioProcessor::pushLiveKey  (int keyCode, float velocity) noexcept { module.pushLiveKey (keyCode, velocity); }
void SattariRhythmGenieAudioProcessor::pushLiveLane (int lane, float velocity) noexcept { module.pushLiveLane (lane, velocity); }


// --- Per-lane sample slots (MESSAGE THREAD) ------------------------------------
// File decoding and the session-long retain stay here; the module only ever
// receives a finished, immutable slot. It must never release the last reference,
// which is why `retainedSamples` lives in the shell.
void SattariRhythmGenieAudioProcessor::setLaneSampleBuffer (int lane, juce::AudioBuffer<float> buf,
                                                            double sourceSr, const juce::String& name)
{
    if (lane < 0 || lane >= kGridLanes) return;
    using Slot = sattari::dsp::RhythmGenieModule::SampleSlot;
    Slot::Ptr s = new Slot();
    s->data = std::move (buf);
    s->sourceSr = sourceSr > 0.0 ? sourceSr : 44100.0;
    s->name = name;
    retainedSamples.add (s);
    module.setLaneSampleSlot (lane, s);
}

bool SattariRhythmGenieAudioProcessor::loadSampleForLane (int lane, const juce::File& file)
{
    if (lane < 0 || lane >= kGridLanes || ! file.existsAsFile()) return false;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;
    const int len = (int) juce::jmin ((juce::int64) (reader->sampleRate * 12.0), reader->lengthInSamples);  // cap 12 s
    if (len <= 0) return false;
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    setLaneSampleBuffer (lane, std::move (buf), reader->sampleRate, file.getFileName());
    laneSamplePath[lane] = file.getFullPathName();
    return true;
}

void SattariRhythmGenieAudioProcessor::clearSampleForLane (int lane) noexcept
{
    if (lane < 0 || lane >= kGridLanes) return;
    laneSamplePath[lane].clear();
    module.setLaneSampleSlot (lane, nullptr);   // falls back to the synth voice
}

bool SattariRhythmGenieAudioProcessor::laneHasSample (int lane) const noexcept { return module.laneHasSample (lane); }
juce::String SattariRhythmGenieAudioProcessor::laneSampleName (int lane) const { return module.laneSampleName (lane); }

void SattariRhythmGenieAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // Persist the pad bindings (learned MIDI notes / keys) alongside the params.
        auto* pads = xml->createNewChildElement ("PADMAP");
        const auto v = module.pads().toState();
        pads->setAttribute ("n", (int) v.size());
        for (int i = 0; i < (int) v.size(); ++i)
            pads->setAttribute ("b" + juce::String (i), v[(size_t) i]);

        // Persist the editable grid (only the ON cells, so it stays compact).
        auto* g = xml->createNewChildElement ("GRID");
        g->setAttribute ("lanes", module.editableGrid().lanes());
        g->setAttribute ("steps", module.editableGrid().steps());
        for (int l = 0; l < module.editableGrid().lanes(); ++l)
            for (int s = 0; s < module.editableGrid().steps(); ++s)
                if (module.editableGrid().on (l, s))
                {
                    auto* c = g->createNewChildElement ("c");
                    c->setAttribute ("l", l);
                    c->setAttribute ("s", s);
                    c->setAttribute ("v", module.editableGrid().velocity (l, s));
                    c->setAttribute ("p", module.editableGrid().probability (l, s));
                }

        // Persist per-lane sample file paths (reloaded on restore).
        auto* smp = xml->createNewChildElement ("SAMPLES");
        for (int l = 0; l < kGridLanes; ++l)
            if (laneSamplePath[l].isNotEmpty())
            {
                auto* e = smp->createNewChildElement ("s");
                e->setAttribute ("l", l);
                e->setAttribute ("path", laneSamplePath[l]);
            }
        copyXmlToBinary (*xml, destData);
    }
}

void SattariRhythmGenieAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (auto* pads = xml->getChildByName ("PADMAP"))
        {
            const int n = juce::jlimit (0, 512, pads->getIntAttribute ("n", 0));
            std::vector<int> v ((size_t) n, -1);
            for (int i = 0; i < n; ++i)
                v[(size_t) i] = pads->getIntAttribute ("b" + juce::String (i), -1);
            module.pads().fromState (v);
        }
        if (auto* g = xml->getChildByName ("GRID"))
        {
            module.editableGrid().clear();
            module.editableGrid().setLanes (g->getIntAttribute ("lanes", kGridLanes));
            module.editableGrid().setSteps (g->getIntAttribute ("steps", sattari::rhythm::kSteps));
            for (auto* c : g->getChildWithTagNameIterator ("c"))
                module.editableGrid().set (c->getIntAttribute ("l", -1), c->getIntAttribute ("s", -1), true,
                          (float) c->getDoubleAttribute ("v", 0.8),
                          (float) c->getDoubleAttribute ("p", 1.0));
        }
        if (auto* smp = xml->getChildByName ("SAMPLES"))
            for (auto* e : smp->getChildWithTagNameIterator ("s"))
            {
                const int l = e->getIntAttribute ("l", -1);
                const juce::File f (e->getStringAttribute ("path"));
                if (l >= 0 && l < kGridLanes && f.existsAsFile()) loadSampleForLane (l, f);
            }
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}


juce::AudioProcessorEditor* SattariRhythmGenieAudioProcessor::createEditor()
{
    return new SattariRhythmGenieAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariRhythmGenieAudioProcessor();
}
