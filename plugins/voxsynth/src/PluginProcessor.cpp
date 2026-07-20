#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::StringArray kWaves { "Sine", "Saw", "Square", "Triangle" };
const juce::StringArray kModes { "Synth", "MIDI" };
}

SattariVoxSynthAudioProcessor::SattariVoxSynthAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Voice",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOXSYNTH", createLayout()),
      presetBank (apvts, "voxsynth")
{
    for (auto& b : busScale_) b = true;
    registerFactoryPresets();
}

// Starting-point voices spanning the instrument: leads, bass, pad, the talkbox/
// vocoder trick, and the beatbox map. Raw parameter values; choice/int params take
// their index. quantize=1 locks to the session-bus key; percussion=1 is beatbox mode.
void SattariVoxSynthAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // waves: 0 Sine 1 Saw 2 Square 3 Triangle | mode: 0 Synth 1 MIDI
    // params: mode osc1 osc2 osc2oct detune mix glide cutoff filtamt reso quantize percussion vocoder level
    auto P = [] (float mode, float o1, float o2, float oct, float det, float mix, float glide,
                 float cut, float filt, float reso, float quant, float perc, float voc, float lvl) -> PV
    {
        return PV { {"mode",mode}, {"osc1",o1}, {"osc2",o2}, {"osc2oct",oct}, {"detune",det},
                    {"mix",mix}, {"glide",glide}, {"cutoff",cut}, {"filtamt",filt}, {"reso",reso},
                    {"quantize",quant}, {"percussion",perc}, {"vocoder",voc}, {"level",lvl} };
    };
    //                             mode o1 o2 oct  det  mix glide  cut  filt reso quant perc voc  lvl
    presetBank.addFactoryPreset ("Init",          P (0, 1, 1,  0,   7, 0.40f, 12,  400, 1.00f,0.20f, 0,  0,  0.00f, 0.90f));
    presetBank.addFactoryPreset ("Analog Lead",   P (0, 1, 1,  0,  12, 0.50f,  8, 3000, 0.30f,0.35f, 0,  0,  0.00f, 0.90f));
    presetBank.addFactoryPreset ("Fat Supersaw",  P (0, 1, 1,  0,  30, 0.50f,  5, 6000, 0.20f,0.20f, 0,  0,  0.00f, 0.85f));
    presetBank.addFactoryPreset ("Sub Bass",      P (0, 0, 2, -1,   0, 0.30f, 20,  200, 0.20f,0.10f, 1,  0,  0.00f, 0.95f));
    presetBank.addFactoryPreset ("Soft Pad",      P (0, 3, 1,  0,  10, 0.50f, 60, 1200, 0.50f,0.15f, 0,  0,  0.00f, 0.80f));
    presetBank.addFactoryPreset ("Octave Stack",  P (0, 1, 1,  1,   5, 0.50f,  6, 4000, 0.30f,0.20f, 0,  0,  0.00f, 0.85f));
    presetBank.addFactoryPreset ("Glass Bells",   P (0, 0, 0,  1,   3, 0.50f,  0, 8000, 0.10f,0.40f, 0,  0,  0.00f, 0.80f));
    presetBank.addFactoryPreset ("Talkbox",       P (0, 1, 1,  0,   7, 0.45f,  8, 1500, 1.00f,0.30f, 0,  0,  0.80f, 0.90f));
    presetBank.addFactoryPreset ("Robot Vocoder", P (0, 2, 1,  0,   7, 0.50f,  6, 2000, 0.80f,0.25f, 1,  0,  1.00f, 0.90f));
    presetBank.addFactoryPreset ("Key-Locked Lead",P(0, 1, 2,  0,  10, 0.45f,  0, 3500, 0.30f,0.20f, 1,  0,  0.00f, 0.90f));
    presetBank.addFactoryPreset ("Beatbox Kit",   P (0, 1, 1,  0,   7, 0.40f, 12,  400, 1.00f,0.20f, 0,  1,  0.00f, 0.90f));
}

int SattariVoxSynthAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariVoxSynthAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariVoxSynthAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets()) return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariVoxSynthAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariVoxSynthAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> ("mode", "Mode", kModes, 0));
    p.push_back (std::make_unique<AudioParameterChoice> ("osc1", "Osc 1", kWaves, 1));
    p.push_back (std::make_unique<AudioParameterChoice> ("osc2", "Osc 2", kWaves, 1));
    p.push_back (std::make_unique<AudioParameterInt>    ("osc2oct", "Osc 2 Octave", -2, 2, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  ("detune", "Detune",
        NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 7.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("mix", "Osc Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("glide", "Glide",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 12.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("cutoff", "Cutoff",
        NormalisableRange<float> (40.0f, 12000.0f, 1.0f, 0.3f), 400.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("filtamt", "Vowel → Filter",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("reso", "Resonance",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.2f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("quantize", "Quantize to Key",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterBool>   ("percussion", "Percussion (beatbox)", false));
    p.push_back (std::make_unique<AudioParameterFloat>  ("vocoder", "Vocoder (talkbox)",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("level", "Level",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.9f));
    return { p.begin(), p.end() };
}

void SattariVoxSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    sattari::dsp::VoxAnalyzer::Config cfg;   // defaults: 65-1050 Hz, 256 hop
    analyzer_.prepare (sampleRate, cfg);
    engine_.prepare (sampleRate);
    vocoder_.prepare (sampleRate, 20);
    frames_.assign ((size_t) analyzer_.maxFramesForBlock (juce::jmax (32, samplesPerBlock)) + 4, {});
    mono_.assign ((size_t) juce::jmax (32, samplesPerBlock), 0.0f);
    activeNote_ = -1; percHold_ = 0; percNote_ = -1;
    setLatencySamples (analyzer_.latencySamples());
}

bool SattariVoxSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SattariVoxSynthAudioProcessor::pullBusKey()
{
    // Convert the session-bus key (root + major/minor/chromatic) into a 12-bool scale
    // mask for the synth's Quantize and the MIDI note-snapping.
    if (auto m = sattari::xplugin::readSession (30))
    {
        static const int MAJ[] = { 0,2,4,5,7,9,11 }, MIN[] = { 0,2,3,5,7,8,10 };
        for (auto& b : busScale_) b = false;
        if (m->scale == 2) { for (auto& b : busScale_) b = true; }
        else { const int* s = (m->scale == 1) ? MIN : MAJ; for (int i = 0; i < 7; ++i) busScale_[(size_t) ((m->root + s[i]) % 12)] = true; }
        busTonic_ = m->root;
    }
}

void SattariVoxSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    auto main = getBusBuffer (buffer, true, 0);
    const int nch = main.getNumChannels();

    // Poll the session key off the fast path (throttled, cheap file read is fine at UI
    // cadence — but only when Quantize is engaged so idle instances never touch disk).
    const bool quantOn = apvts.getRawParameterValue ("quantize")->load() > 0.001f;
    const auto now = juce::Time::currentTimeMillis();
    if (quantOn && now - lastBusPollMs_ > 500) { lastBusPollMs_ = now; pullBusKey(); }

    // Mono sum of the voice.
    if ((int) mono_.size() < n) mono_.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f; for (int c = 0; c < nch; ++c) s += main.getSample (c, i);
        mono_[(size_t) i] = nch > 0 ? s / nch : 0.0f;
    }

    const bool midiMode = apvts.getRawParameterValue ("mode")->load() > 0.5f;
    const bool percMode = apvts.getRawParameterValue ("percussion")->load() > 0.5f;
    analyzer_.setPercussionMode (percMode);

    // Engine params.
    sattari::dsp::VoxSynthEngine::Params ep;
    ep.osc1Wave   = (int) apvts.getRawParameterValue ("osc1")->load();
    ep.osc2Wave   = (int) apvts.getRawParameterValue ("osc2")->load();
    ep.osc2Octave = (int) apvts.getRawParameterValue ("osc2oct")->load();
    ep.osc2Detune = apvts.getRawParameterValue ("detune")->load();
    ep.mix        = apvts.getRawParameterValue ("mix")->load();
    ep.glideMs    = apvts.getRawParameterValue ("glide")->load();
    ep.filterBaseHz = apvts.getRawParameterValue ("cutoff")->load();
    ep.filterAmount = apvts.getRawParameterValue ("filtamt")->load();
    ep.filterReso   = apvts.getRawParameterValue ("reso")->load();
    ep.quantize     = quantOn ? apvts.getRawParameterValue ("quantize")->load() : 0.0f;
    ep.level        = apvts.getRawParameterValue ("level")->load();
    engine_.setParams (ep);
    engine_.setScale (busScale_.data());

    const int m = analyzer_.process (mono_.data(), n, frames_.data(), (int) frames_.size());

    // Synth output is rendered segment-by-segment between frame boundaries; MIDI is
    // emitted at each frame's sampleOffset. In MIDI mode we pass the dry voice through
    // so the singer can still monitor themselves.
    juce::AudioBuffer<float> synth (1, n); synth.clear();
    float* sp = synth.getWritePointer (0);
    int cursor = 0;
    LiveReading lr;

    auto emitNoteOff = [&] (int at)
    {
        if (activeNote_ >= 0) { midi.addEvent (juce::MidiMessage::noteOff (kMpeChannel, activeNote_), at); activeNote_ = -1; }
    };
    auto bend14 = [&] (float hz, int note)
    {
        const float refNote = 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
        const float cents = 1200.0f * std::log2 (hz / refNote);
        return juce::jlimit (0, 16383, 8192 + (int) (cents / (100.0f * bendRangeSemis_) * 8192.0f));
    };

    for (int fi = 0; fi < m; ++fi)
    {
        const auto& f = frames_[(size_t) fi];
        const int at = juce::jlimit (0, n - 1, f.sampleOffset);
        if (! midiMode) { engine_.render (sp + cursor, juce::jmax (0, at + 1 - cursor)); cursor = at + 1; }

        // drive the synth target for the samples that follow this frame
        engine_.setTarget (f.frequencyHz, f.amplitude, f.centroidHz, f.voiced);

        if (midiMode)
        {
            using E = sattari::dsp::VoxAnalyzer::Event;
            using P = sattari::dsp::VoxAnalyzer::Perc;
            if (percMode)
            {
                if (f.perc != P::None)
                {
                    const int note = f.perc == P::Kick ? 36 : f.perc == P::Snare ? 38 : f.perc == P::HatOpen ? 46 : 42;
                    midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 100), at);
                    percNote_ = note; percHold_ = (int) (0.03 * sr_);
                }
            }
            else if (f.event == E::VoicedOnset || f.event == E::Plosive)
            {
                const float hz = f.onsetPitchHz > 0.0f ? f.onsetPitchHz : f.frequencyHz;
                if (hz > 0.0f)
                {
                    const int note = juce::jlimit (0, 127, (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
                    if (note != activeNote_) emitNoteOff (at);
                    midi.addEvent (juce::MidiMessage::pitchWheel (kMpeChannel, bend14 (hz, note)), at);
                    midi.addEvent (juce::MidiMessage::noteOn (kMpeChannel, note, (juce::uint8) juce::jlimit (1, 127, (int) (f.amplitude * 127.0f))), at);
                    activeNote_ = note;
                }
            }
            else if (f.event == E::Fricative)
            {
                // ignored in pitch/MIDI mode (unvoiced hiss carries no note)
            }

            if (! percMode && activeNote_ >= 0)
            {
                if (f.voiced && f.frequencyHz > 0.0f)
                {
                    midi.addEvent (juce::MidiMessage::pitchWheel (kMpeChannel, bend14 (f.frequencyHz, activeNote_)), at);
                    midi.addEvent (juce::MidiMessage::channelPressureChange (kMpeChannel, juce::jlimit (0, 127, (int) (f.amplitude * 127.0f))), at);
                    midi.addEvent (juce::MidiMessage::controllerEvent (kMpeChannel, 74, juce::jlimit (0, 127, (int) (f.centroidHz / 8000.0f * 127.0f))), at);
                }
                else emitNoteOff (at);
            }
        }

        if (f.voiced) { lr.voiced = true; lr.midiNote = (int) std::lround (f.midi); lr.amplitude = f.amplitude;
                        lr.cents = (float) (f.midi - std::lround (f.midi)) * 100.0f; }
    }
    if (! midiMode) engine_.render (sp + cursor, juce::jmax (0, n - cursor));

    // Vocoder / talkbox: the voice (modulator) shapes the synth (carrier) so it "speaks".
    if (! midiMode)
    {
        const float voc = apvts.getRawParameterValue ("vocoder")->load();
        if (voc > 0.001f) vocoder_.process (sp, mono_.data(), sp, n, voc);
    }

    if (percMode && percHold_ > 0) { percHold_ -= n; if (percHold_ <= 0 && percNote_ >= 0) { midi.addEvent (juce::MidiMessage::noteOff (10, percNote_), n - 1); percNote_ = -1; } }

    // Write output: synth voice in Synth mode, dry monitor in MIDI mode.
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i) d[i] = midiMode ? mono_[(size_t) i] : sp[i];
    }
    live_.store (lr);
}

void SattariVoxSynthAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void SattariVoxSynthAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariVoxSynthAudioProcessor::createEditor()
{
    return new SattariVoxSynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariVoxSynthAudioProcessor(); }
