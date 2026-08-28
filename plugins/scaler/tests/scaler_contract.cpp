// Sattari Scaler — functional contract.
// Proves (1) buildChord stacks the correct diatonic chord in the key, (2) a
// played note becomes an in-key chord out of processBlock, (3) out-of-scale
// notes are snapped, and (4) note-offs release exactly the chord that was sent.
#include "PluginProcessor.h"
#include <cstdio>
#include <set>

using sattari::dsp::ScaleId;

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

std::set<int> noteOns (const juce::MidiBuffer& b)
{
    std::set<int> s; for (auto m : b) if (m.getMessage().isNoteOn()) s.insert (m.getMessage().getNoteNumber());
    return s;
}
std::set<int> noteOffs (const juce::MidiBuffer& b)
{
    std::set<int> s; for (auto m : b) if (m.getMessage().isNoteOff()) s.insert (m.getMessage().getNoteNumber());
    return s;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;
    SattariScalerAudioProcessor p; auto& a = p.parameters();
    setParam (a, "followKey", 0.0f);
    setParam (a, "root", 0.0f);          // C
    setParam (a, "scale", 1.0f);         // Major
    setParam (a, "octave", 0.0f);

    // 1) buildChord logic: C major triad on C4 -> C E G (60,64,67).
    int out[5];
    int c = p.buildChord (60, 0, ScaleId::Major, 1, out, 5);
    std::printf ("triad C4: "); for (int i = 0; i < c; ++i) std::printf ("%d ", out[i]); std::printf ("\n");
    if (! (c == 3 && out[0] == 60 && out[1] == 64 && out[2] == 67)) { std::printf ("  FAIL triad != C E G\n"); ++fails; }

    int c7 = p.buildChord (60, 0, ScaleId::Major, 2, out, 5);      // seventh
    if (! (c7 == 4 && out[3] == 71)) { std::printf ("  FAIL seventh != C E G B\n"); ++fails; }

    // Out-of-scale note snaps (C# with C major -> C or D, still in scale).
    int cs = p.buildChord (61, 0, ScaleId::Major, 0, out, 5);
    const bool snapped = (cs == 1 && (out[0] == 60 || out[0] == 62));
    if (! snapped) { std::printf ("  FAIL C# not snapped into C major\n"); ++fails; }

    // 2) processBlock: note-on 60 -> chord out; note-off 60 -> releases them.
    setParam (a, "chord", 1.0f);         // Triad
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (0, 512);

    juce::MidiBuffer on; on.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
    p.processBlock (audio, on);
    auto ons = noteOns (on);
    std::printf ("processed note-ons: "); for (int nn : ons) std::printf ("%d ", nn); std::printf ("\n");
    if (! (ons.count (60) && ons.count (64) && ons.count (67) && ons.size() == 3))
        { std::printf ("  FAIL processBlock chord != {60,64,67}\n"); ++fails; }

    juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    p.processBlock (audio, off);
    auto offs = noteOffs (off);
    if (! (offs.count (60) && offs.count (64) && offs.count (67) && offs.size() == 3))
        { std::printf ("  FAIL note-off did not release the whole chord\n"); ++fails; }

    std::printf (fails == 0 ? "Scaler contract ok\n" : "Scaler contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
