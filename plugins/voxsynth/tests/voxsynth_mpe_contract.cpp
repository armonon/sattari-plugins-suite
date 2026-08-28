// Sattari VoxSynth — MPE output contract.
//
//  VoxSynth scales its per-note bends by a bend range that the receiving
//  instrument has to agree on. MPE defaults to +/-48 semitones; a plain synth
//  defaults to +/-2. Shipping 48-scaled bends without saying so made every
//  vibrato 24x too wide on anything that was not already in MPE mode, which is
//  most instruments. This contract pins the fix:
//
//   1. the RPN announcement is emitted BEFORE the first pitch bend, not after;
//   2. the announced sensitivity equals the bendRange parameter;
//   3. changing bendRange re-announces;
//   4. bendRange actually scales the bends — it is not decorative.
//
//  (4) is the one that would catch a regression where the announcement is right
//  but the maths still uses a hardcoded constant.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;

int fails = 0;
void check (bool ok, const char* what)
{
    if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; }
}

void setParam (juce::AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// One captured MIDI event, flattened so we can assert on ORDER across blocks.
struct Event
{
    int  seq;        // global ordering index
    int  channel;    // 1-based
    bool isCC, isBend;
    int  cc, value;  // CC number / CC value
    int  bend;       // 14-bit pitch wheel
};

// A voiced tone: fundamental plus a couple of harmonics so the analyzer treats it
// as voice rather than a bare sine. `amp` of 0 feeds silence — the analyzer fires a
// note-on off a spectral-flux ATTACK, so a note needs silence in front of it. A
// continuously running tone produces no onset and therefore no MIDI at all.
double feed (SattariVoxSynthAudioProcessor& p, std::vector<Event>& out, int& seq,
             double phase, double freq, float amp, int numBlocks)
{
    juce::AudioBuffer<float> buf (2, kBlock);
    const double inc = freq / kSr;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const double ph = phase + inc * i;
            const float s = amp * (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * ph)
                                         + 0.2 * std::sin (4.0 * juce::MathConstants<double>::pi * ph)
                                         + 0.1 * std::sin (6.0 * juce::MathConstants<double>::pi * ph));
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        phase += inc * kBlock;

        juce::MidiBuffer midi;
        p.processBlock (buf, midi);

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            Event e {};
            e.seq     = seq++;
            e.channel = m.getChannel();
            e.isCC    = m.isController();
            e.isBend  = m.isPitchWheel();
            if (e.isCC)   { e.cc = m.getControllerNumber(); e.value = m.getControllerValue(); }
            if (e.isBend) { e.bend = m.getPitchWheelValue(); }
            if (e.isCC || e.isBend) out.push_back (e);
        }
    }
    return phase;
}

// Index of the first RPN 0 (pitch bend sensitivity) data-entry on `channel`, or -1.
// The sequence is CC101=0, CC100=0, CC6=<semitones>; we return the CC6 event.
int findBendSensitivity (const std::vector<Event>& ev, int channel, int from = 0)
{
    for (int i = from; i + 2 < (int) ev.size(); ++i)
    {
        if (ev[(size_t) i].isCC && ev[(size_t) i].channel == channel
            && ev[(size_t) i].cc == 101 && ev[(size_t) i].value == 0
            && ev[(size_t) i + 1].isCC && ev[(size_t) i + 1].cc == 100 && ev[(size_t) i + 1].value == 0
            && ev[(size_t) i + 2].isCC && ev[(size_t) i + 2].cc == 6)
            return i + 2;
    }
    return -1;
}

int findFirstBend (const std::vector<Event>& ev, int from = 0)
{
    for (int i = from; i < (int) ev.size(); ++i)
        if (ev[(size_t) i].isBend) return i;
    return -1;
}

// Largest distance from centre across the bends in [from, end).
int maxBendDeviation (const std::vector<Event>& ev, int from)
{
    int worst = 0;
    for (int i = from; i < (int) ev.size(); ++i)
        if (ev[(size_t) i].isBend)
            worst = juce::jmax (worst, std::abs (ev[(size_t) i].bend - 8192));
    return worst;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("VoxSynth MPE contract\n");

    // A3 = 220 Hz, sung 40 cents sharp. The nearest note is still A3, so every
    // frame carries a constant ~40 cent bend — small at a 48-semitone range,
    // large at 2. That is what makes assertion (4) deterministic.
    const double kFreq = 220.0 * std::pow (2.0, 40.0 / 1200.0);

    SattariVoxSynthAudioProcessor p;
    p.prepareToPlay (kSr, kBlock);
    setParam (p.parameters(), "mode", 1.0f);          // MIDI mode
    setParam (p.parameters(), "percussion", 0.0f);
    setParam (p.parameters(), "quantize", 0.0f);      // no key snap: keep the bend honest

    // Silence, then the note: the onset detector needs an attack to fire on. The
    // first cycle is a warm-up — the flux and amplitude baselines start at zero, so
    // the detector will not arm until it has seen some real signal history.
    std::vector<Event> ev;
    int seq = 0;
    double ph = feed (p, ev, seq, 0.0, kFreq, 0.0f, 8);
    ph = feed (p, ev, seq, ph, kFreq, 1.0f, 30);    // warm-up note
    ph = feed (p, ev, seq, ph, kFreq, 0.0f, 12);    // release
    ph = feed (p, ev, seq, ph, kFreq, 1.0f, 40);    // the note we measure

    // --- 1 + 2: announced before any bend, and at the parameter's value ---------
    const int sens48 = findBendSensitivity (ev, 2);
    const int bend1  = findFirstBend (ev);

    check (sens48 >= 0, "no pitch-bend-sensitivity RPN was ever emitted");
    check (bend1  >= 0, "no pitch bend was emitted - the harness never triggered a note");
    if (sens48 >= 0 && bend1 >= 0)
        check (sens48 < bend1, "bend sensitivity announced AFTER the first bend");
    if (sens48 >= 0)
        check (ev[(size_t) sens48].value == 48, "announced range is not the default 48");

    // The MPE Configuration Message must precede it, on the zone master.
    const int mcm = [&]
    {
        for (int i = 0; i + 2 < (int) ev.size(); ++i)
            if (ev[(size_t) i].isCC && ev[(size_t) i].channel == 1
                && ev[(size_t) i].cc == 101 && ev[(size_t) i].value == 0
                && ev[(size_t) i + 1].cc == 100 && ev[(size_t) i + 1].value == 6
                && ev[(size_t) i + 2].cc == 6)
                return i + 2;
        return -1;
    }();
    check (mcm >= 0, "no MPE Configuration Message (RPN 6) on the zone master");
    if (mcm >= 0 && sens48 >= 0) check (mcm < sens48, "MCM must precede bend sensitivity");

    const int dev48 = maxBendDeviation (ev, bend1 >= 0 ? bend1 : 0);
    check (dev48 > 0, "bends never left centre - cannot verify scaling");

    // --- 3 + 4: change the range; it re-announces AND rescales ------------------
    const int mark = (int) ev.size();
    setParam (p.parameters(), "bendRange", 2.0f);
    ph = feed (p, ev, seq, ph, kFreq, 0.0f, 12);   // release, so the next note re-onsets
    ph = feed (p, ev, seq, ph, kFreq, 1.0f, 40);
    (void) ph;

    const int sens2 = findBendSensitivity (ev, 2, mark);
    check (sens2 >= 0, "changing bendRange did not re-announce");
    if (sens2 >= 0) check (ev[(size_t) sens2].value == 2, "re-announced the wrong range");

    const int dev2 = maxBendDeviation (ev, mark);
    // Same sung pitch, range 48 -> 2, so the wheel deviation must grow ~24x. A 5x
    // floor keeps this robust against detector jitter while still failing hard if
    // the maths ignores the parameter.
    check (dev2 > dev48 * 5,
           "bendRange does not scale the bends - the maths is still hardcoded");

    std::printf ("  range 48 -> max |bend-8192| = %d\n", dev48);
    std::printf ("  range  2 -> max |bend-8192| = %d\n", dev2);

    if (fails == 0) { std::printf ("VoxSynth MPE contract ok\n"); return 0; }
    std::printf ("VoxSynth MPE contract FAILED (%d)\n", fails);
    return 1;
}
