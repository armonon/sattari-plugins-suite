#include "PluginProcessor.h"
#include "PluginEditor.h"

using sattari::dsp::ScaleId;

juce::StringArray SattariScalerAudioProcessor::rootChoices()
{ return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
juce::StringArray SattariScalerAudioProcessor::scaleChoices()
{ return { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" }; }
juce::StringArray SattariScalerAudioProcessor::chordChoices()
{ return { "Off (snap)", "Triad", "Seventh", "Power", "Octave" }; }

SattariScalerAudioProcessor::SattariScalerAudioProcessor()
    : juce::AudioProcessor (BusesProperties()),   // MIDI effect: no audio buses
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}
SattariScalerAudioProcessor::~SattariScalerAudioProcessor() { sessionReader.stop(); }

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Presets pick a scale mode + chord type; Root and
// Follow Song Key are left untouched so the generator keeps the user's key.
void SattariScalerAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // chord: 0=Off(snap) 1=Triad 2=Seventh 3=Power 4=Octave
    // scale: 0=Chromatic 1=Major 2=Minor 3=Penta Maj 4=Penta Min 5=Dorian 6=Mixolydian 7=Harm Minor
    presetBank.addFactoryPreset ("Major Triads",    PV{ {"chord", 1}, {"scale", 1}, {"octave", 0} });
    presetBank.addFactoryPreset ("Minor Triads",    PV{ {"chord", 1}, {"scale", 2}, {"octave", 0} });
    presetBank.addFactoryPreset ("Major 7ths",      PV{ {"chord", 2}, {"scale", 1}, {"octave", 0} });
    presetBank.addFactoryPreset ("Minor 7ths",      PV{ {"chord", 2}, {"scale", 2}, {"octave", 0} });
    presetBank.addFactoryPreset ("Power Chords",    PV{ {"chord", 3}, {"scale", 2}, {"octave", 0} });
    presetBank.addFactoryPreset ("Octave Doubler",  PV{ {"chord", 4}, {"scale", 1}, {"octave", 0} });
    presetBank.addFactoryPreset ("Pentatonic Snap", PV{ {"chord", 0}, {"scale", 3}, {"octave", 0} });
    presetBank.addFactoryPreset ("Dorian 7ths",     PV{ {"chord", 2}, {"scale", 5}, {"octave", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariScalerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("chord", "Chord", chordChoices(), 1));
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("followKey", "Follow Song Key", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("root", "Root", rootChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", scaleChoices(), 1));
    p.push_back (std::make_unique<juce::AudioParameterInt>    ("octave", "Octave", -2, 2, 0));
    return { p.begin(), p.end() };
}

void SattariScalerAudioProcessor::prepareToPlay (double, int)
{
    sessionReader.start();
    for (auto& h : held) { h.count = 0; h.notes.fill (-1); }
}
void SattariScalerAudioProcessor::releaseResources() { sessionReader.stop(); }

bool SattariScalerAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const { return true; }

void SattariScalerAudioProcessor::resolveKey (int& root, ScaleId& id) const
{
    root = (int) std::round (param ("root"));
    id   = (ScaleId) (int) std::round (param ("scale"));
    if (param ("followKey") > 0.5f)
        if (auto s = sessionReader.current(); s.valid)
        {
            root = juce::jlimit (0, 11, s.root);
            id   = (s.scale == 1) ? ScaleId::Minor : ScaleId::Major;   // bus: 0=maj,1=min
        }
}

SattariScalerAudioProcessor::KeyState SattariScalerAudioProcessor::currentKeyState() const
{
    KeyState k;
    int root; ScaleId id; resolveKey (root, id);
    k.root = root; k.scale = (int) id;
    k.following = param ("followKey") > 0.5f;
    return k;
}

// Return the MIDI note `steps` scale-degrees above (or below) a scale note.
static int nthDegree (int fromNote, int keyRoot, ScaleId id, int steps)
{
    int n = 0; const int* iv = sattari::dsp::scaleIntervals (id, n);
    if (iv == nullptr || n <= 0) return fromNote + steps;  // chromatic fallback
    // Find the degree index of fromNote within the scale (snap to nearest).
    const int snapped = sattari::dsp::snapMidiToScale (fromNote, keyRoot, id);
    const int pc = ((snapped - keyRoot) % 12 + 12) % 12;
    int idx = 0; for (int i = 0; i < n; ++i) if (iv[i] == pc) { idx = i; break; }
    int deg = idx + steps;
    int octShift = 0;
    while (deg >= n) { deg -= n; ++octShift; }
    while (deg < 0)  { deg += n; --octShift; }
    const int baseOct = (snapped - keyRoot - pc) / 12;   // octave of the snapped note
    return keyRoot + (baseOct + octShift) * 12 + iv[deg];
}

int SattariScalerAudioProcessor::buildChord (int inputNote, int keyRoot, ScaleId id,
                                             int chordType, int* out, int maxOut) const
{
    const int rootNote = sattari::dsp::snapMidiToScale (inputNote, keyRoot, id);
    int c = 0;
    auto add = [&] (int note) { if (c < maxOut && note >= 0 && note <= 127) out[c++] = note; };
    switch (chordType)
    {
        case 0: add (rootNote); break;                                    // Off / snap
        case 1: add (rootNote); add (nthDegree (rootNote, keyRoot, id, 2));
                add (nthDegree (rootNote, keyRoot, id, 4)); break;        // Triad
        case 2: add (rootNote); add (nthDegree (rootNote, keyRoot, id, 2));
                add (nthDegree (rootNote, keyRoot, id, 4));
                add (nthDegree (rootNote, keyRoot, id, 6)); break;        // Seventh
        case 3: add (rootNote); add (rootNote + 7); add (rootNote + 12); break;  // Power
        case 4: add (rootNote); add (rootNote + 12); break;               // Octave
        default: add (rootNote); break;
    }
    return c;
}

void SattariScalerAudioProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer& midi)
{
    int root; ScaleId id; resolveKey (root, id);
    const int chordType = (int) std::round (param ("chord"));
    const int octaveShift = (int) std::round (param ("octave")) * 12;

    juce::MidiBuffer out;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int t = meta.samplePosition;

        if (m.isNoteOn())
        {
            const int in = juce::jlimit (0, 127, m.getNoteNumber());
            const int ch = m.getChannel();
            const float vel = m.getFloatVelocity();
            int notes[5]; const int cnt = buildChord (in, root, id, chordType, notes, 5);
            auto& h = held[(size_t) in]; h.count = 0; h.channel = ch;
            for (int i = 0; i < cnt; ++i)
            {
                const int nn = juce::jlimit (0, 127, notes[i] + octaveShift);
                out.addEvent (juce::MidiMessage::noteOn (ch, nn, vel), t);
                if (h.count < (int) h.notes.size()) h.notes[(size_t) h.count++] = nn;
            }
        }
        else if (m.isNoteOff())
        {
            const int in = juce::jlimit (0, 127, m.getNoteNumber());
            auto& h = held[(size_t) in];
            for (int i = 0; i < h.count; ++i)
                out.addEvent (juce::MidiMessage::noteOff (h.channel, h.notes[(size_t) i]), t);
            h.count = 0;
        }
        else
        {
            out.addEvent (m, t);   // pass CC / pitchbend / etc. through
        }
    }
    midi.swapWith (out);
}

void SattariScalerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariScalerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariScalerAudioProcessor::createEditor()
{
    return new SattariScalerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariScalerAudioProcessor();
}
