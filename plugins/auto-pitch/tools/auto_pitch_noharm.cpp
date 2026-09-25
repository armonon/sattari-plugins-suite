// Auto Pitch — release gate "no-harm matrix".
//
// Proves that the key engine / AutoKey and state handling never perturb unrelated
// Auto Pitch behaviour. Deterministic synthetic inputs only; one PASS/FAIL line per
// check (plus indented detail lines); exit status is non-zero if any check FAILS.
//
//   1 state round trip        getState -> fresh instance setState -> bit-identical
//   2 preset recall           every factory workflow: reload + state round trip
//   3 AutoKey no-perturb      toggling AutoKey below its gate; AutoKey-param touches
//   4 automation stress       60 s of per-block automation of unrelated parameters
//   5 SR x block matrix       4 rates x 6 blocks + irregular blocks; B3+45c accuracy
//   6 host bypass             processBlockBypassed <-> processBlock toggling
//   7 mono / stereo           both main layouts; L == R; mono == stereo L
//   8 long playback           2 h @ 48 kHz / 512, AutoKey on, modulating melody
//
// Usage: AutoPitchNoHarm [--check N]... [--long-minutes M]
//   --check N          run only check N (repeatable). Default: all eight.
//   --long-minutes M   length of check 8 (default 120; the gate uses 120).
//
// Host emulation: parameters are set from the message thread (this one) and the
// processor's AsyncUpdater is drained before every block, as a host's message loop
// would between callbacks. Every render starts with prepareToPlay.
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>

#include <mach/mach.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace
{
using Proc = SattariAutoPitchAudioProcessor;
using Clock = std::chrono::steady_clock;

// --- reporting ----------------------------------------------------------------
int failures = 0;

double secondsSince (Clock::time_point t0)
{
    return std::chrono::duration<double> (Clock::now() - t0).count();
}

void verdict (int check, const char* name, bool pass, double seconds, const std::string& detail)
{
    if (! pass) ++failures;
    std::printf ("%s  [%d] %-34s (%6.1f s)  %s\n", pass ? "PASS" : "FAIL", check, name, seconds,
                 detail.c_str());
    std::fflush (stdout);
}

void info (const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    std::printf ("      ");
    std::vprintf (fmt, args);
    std::printf ("\n");
    va_end (args);
    std::fflush (stdout);
}

std::string fmt (const char* f, ...)
{
    char buf[1024];
    va_list args;
    va_start (args, f);
    std::vsnprintf (buf, sizeof (buf), f, args);
    va_end (args);
    return buf;
}

// --- parameters -----------------------------------------------------------------
void setParam (Proc& p, const juce::String& id, float value)
{
    auto* parameter = p.parameters().getParameter (id);
    if (parameter == nullptr)
    {
        std::fprintf (stderr, "missing parameter: %s\n", id.toRawUTF8());
        std::exit (2);
    }
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

// The processor defers latency re-reports / PSOLA swaps to its AsyncUpdater (the
// message thread). A host's message loop runs those between audio callbacks; this
// tool has no running loop, so it drains the updater synchronously at the same point.
// (AsyncUpdater is a private base of the processor, hence the C-style cast.)
void drainMessageThread (Proc& p)
{
    ((juce::AsyncUpdater&) p).handleUpdateNowIfNeeded();
}

int numChoices (Proc& p, const juce::String& id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.parameters().getParameter (id)))
        return c->choices.size();
    return 1;
}

// Deterministic PRNG (xorshift64*), identical on every platform.
struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }   // [0,1)
    int    below (int n) { return (int) (uniform() * n); }
    float  bipolar() { return (float) (uniform() * 2.0 - 1.0); }
};

// --- deterministic material -------------------------------------------------------
double midiToHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
double hzToMidi (double hz)   { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

// Vocal-like harmonic voice, one 11.3 s pattern repeated:
//   [0, 2)      C4 +30 c, 5.5 Hz +-25 c vibrato
//   [2, 2.4)    silence (noise floor only)
//   [2.4, 4.4)  glide G3 -> D4 with vibrato
//   [4.4, 4.8)  silence
//   [4.8, 10.8) C-major melody, 20 x 0.3 s notes, each detuned -22..+25 c, delayed vibrato
//   [10.8,11.3) silence
// plus a -72 dBFS white-noise floor throughout. `transpose` shifts everything (used by
// the long run to modulate). Sequential generator so a 2 h run needs no buffer.
struct VocalGen
{
    static constexpr double kPeriod = 11.3;
    double sr;
    int transpose = 0;
    long n = 0;
    double phase = 0.0;
    Rng noise { 0xC0FFEEull };

    explicit VocalGen (double sampleRate) : sr (sampleRate) {}

    static double gate (double t, double dur, double attack = 0.012, double release = 0.03)
    {
        if (t < 0.0 || t >= dur) return 0.0;
        return juce::jlimit (0.0, 1.0, std::min (t / attack, (dur - t) / release));
    }

    float next()
    {
        static const int deg[20]    = { 0, 2, 4, 5, 7, 5, 4, 2, 0, 4, 7, 9, 7, 5, 4, 2, 4, 2, 0, 0 };
        static const int detune[20] = { 18, -12, 25, -20, 8, -5, 15, -22, 10, -15, 20, -8, 5, -18, 12, -10, 22, -6, 14, -3 };
        const double t  = (double) n / sr;
        const double tp = std::fmod (t, kPeriod);
        ++n;
        double midi = 57.0, env = 0.0;
        const double twoPi = juce::MathConstants<double>::twoPi;
        if (tp < 2.0)
        {
            midi = 60.30 + 0.25 * std::sin (twoPi * 5.5 * tp);
            env = gate (tp, 2.0);
        }
        else if (tp >= 2.4 && tp < 4.4)
        {
            const double u = tp - 2.4;
            midi = 55.0 + 7.0 * (u / 2.0) + 0.15 * std::sin (twoPi * 5.0 * u);
            env = gate (u, 2.0);
        }
        else if (tp >= 4.8 && tp < 10.8)
        {
            const double u = tp - 4.8;
            const int k = juce::jlimit (0, 19, (int) (u / 0.3));
            const double v = u - 0.3 * k;
            const double vib = v > 0.12 ? 0.20 * std::sin (twoPi * 5.2 * (v - 0.12)) : 0.0;
            midi = 60.0 + deg[k] + detune[k] / 100.0 + vib;
            env = gate (v, 0.3, 0.010, 0.025);
        }
        midi += transpose;
        phase += twoPi * midiToHz (midi) / sr;
        if (phase > 1.0e6) phase = std::fmod (phase, twoPi);
        const double s = std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)
                       + 0.15 * std::sin (4.0 * phase) + 0.08 * std::sin (5.0 * phase);
        return (float) (0.16 * env * s) + 2.5e-4f * noise.bipolar();
    }
};

juce::AudioBuffer<float> vocalMaterial (double sr, double seconds, int transpose = 0)
{
    VocalGen g (sr);
    g.transpose = transpose;
    juce::AudioBuffer<float> b (1, (int) std::round (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample (0, i, g.next());
    return b;
}

// A steady sine (the contract's fixture): B3 + 45 c at 0.18.
juce::AudioBuffer<float> b3Plus45 (double sr, double seconds)
{
    juce::AudioBuffer<float> b (1, (int) std::round (sr * seconds));
    const double hz = midiToHz (59.45);
    double phase = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        b.setSample (0, i, 0.18f * (float) std::sin (phase));
        phase += juce::MathConstants<double>::twoPi * hz / sr;
    }
    return b;
}

// Tone + noise that carries no key: B3 +35 c harmonic tone, +-20 c vibrato (so it
// touches both B and C), under strong white noise, with gaps. AutoKey must stay below
// its gate on this; the check verifies that at analysis-hop resolution.
juce::AudioBuffer<float> keylessMaterial (double sr, double seconds, double vibratoCents = 20.0)
{
    juce::AudioBuffer<float> b (1, (int) std::round (sr * seconds));
    Rng rng (20260925);
    double phase = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = (double) i / sr;
        const double tp = std::fmod (t, 7.0);
        const double env = VocalGen::gate (tp, 6.0, 0.02, 0.05);
        const double midi = 59.35 + vibratoCents / 100.0 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * t);
        phase += juce::MathConstants<double>::twoPi * midiToHz (midi) / sr;
        const double s = std::sin (phase) + 0.4 * std::sin (2.0 * phase) + 0.2 * std::sin (3.0 * phase);
        b.setSample (0, i, (float) (0.14 * env * s) + 0.03f * rng.bipolar());
    }
    return b;
}

// --- rendering ----------------------------------------------------------------------
struct RenderSpec
{
    double sr = 48000.0;
    int maxBlock = 512;
    std::function<int (long pos)> blockLen;                               // default: maxBlock
    std::function<bool (Proc&, long pos, int len, long blockIndex)> before; // returns "bypassed"
    bool offline = false;
};

struct RenderStats
{
    int latency = -1;          // reported after prepare
    int latencyChanges = 0;    // blocks whose reported latency differed from the first
    std::set<int> latencies;
    bool finite = true;
    float peak = 0.0f;
    long blocks = 0;
};

juce::AudioBuffer<float> render (Proc& p, const juce::AudioBuffer<float>& in, const RenderSpec& spec,
                                 RenderStats* stats = nullptr)
{
    p.setNonRealtime (spec.offline);
    p.setRateAndBufferSizeDetails (spec.sr, spec.maxBlock);
    p.prepareToPlay (spec.sr, spec.maxBlock);
    drainMessageThread (p);

    const int mainIn  = p.getMainBusNumInputChannels();
    const int outCh   = p.getTotalNumOutputChannels();
    const int bufCh   = juce::jmax (p.getTotalNumInputChannels(), outCh);
    const long total  = in.getNumSamples();
    juce::AudioBuffer<float> out (outCh, (int) total);
    juce::AudioBuffer<float> block (bufCh, spec.maxBlock);
    RenderStats local;
    RenderStats& st = stats != nullptr ? *stats : local;
    st.latency = p.getLatencySamples();
    st.latencies.insert (st.latency);

    long blockIndex = 0;
    for (long pos = 0; pos < total; )
    {
        int len = spec.blockLen ? spec.blockLen (pos) : spec.maxBlock;
        len = (int) std::min<long> (juce::jlimit (1, spec.maxBlock, len), total - pos);
        block.clear();
        for (int ch = 0; ch < mainIn; ++ch)
            block.copyFrom (ch, 0, in, juce::jmin (ch, in.getNumChannels() - 1), (int) pos, len);
        juce::AudioBuffer<float> slice (block.getArrayOfWritePointers(), bufCh, len);
        const bool bypassed = spec.before ? spec.before (p, pos, len, blockIndex) : false;
        drainMessageThread (p);   // the host's message thread between callbacks
        juce::MidiBuffer midi;
        if (bypassed) p.processBlockBypassed (slice, midi);
        else          p.processBlock (slice, midi);
        for (int ch = 0; ch < outCh; ++ch)
        {
            out.copyFrom (ch, (int) pos, slice, ch, 0, len);
            const float* d = slice.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
            {
                if (! std::isfinite (d[i])) st.finite = false;
                else st.peak = std::max (st.peak, std::abs (d[i]));
            }
        }
        const int lat = p.getLatencySamples();
        if (lat != st.latency) ++st.latencyChanges;
        st.latencies.insert (lat);
        pos += len;
        ++blockIndex;
    }
    st.blocks = blockIndex;
    return out;
}

struct Diff { double maxAbs = 0.0; long firstIndex = -1; long count = 0; bool sameShape = true; };

Diff compare (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int channels = -1)
{
    Diff d;
    if (a.getNumSamples() != b.getNumSamples()) { d.sameShape = false; d.maxAbs = 1e9; return d; }
    const int ch = channels > 0 ? channels : juce::jmin (a.getNumChannels(), b.getNumChannels());
    for (int c = 0; c < ch; ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const double e = std::abs ((double) a.getSample (c, i) - (double) b.getSample (c, i));
            if (! (e == 0.0))
            {
                if (d.firstIndex < 0 || i < d.firstIndex) d.firstIndex = i;
                ++d.count;
                d.maxAbs = std::max (d.maxAbs, std::isfinite (e) ? e : 1e9);
            }
        }
    return d;
}

std::string diffText (const Diff& d, double sr)
{
    if (! d.sameShape) return "length differs";
    if (d.count == 0) return "bit-identical";
    return fmt ("max |diff| %.3g, %ld samples differ, first at %.3f s", d.maxAbs, d.count,
                (double) d.firstIndex / sr);
}

// Discontinuity ("click") measure: the largest second difference |y[n]-2y[n-1]+y[n-2]|.
// Smooth band-limited audio has small curvature (A*(2*pi*f/sr)^2), a step or a splice
// shows up as a curvature spike of the step's size.
double maxCurvature (const juce::AudioBuffer<float>& b, long from = 0, long to = -1, long* where = nullptr)
{
    if (to < 0 || to > b.getNumSamples()) to = b.getNumSamples();
    from = std::max<long> (from, 2);
    double worst = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* y = b.getReadPointer (c);
        for (long i = from; i < to; ++i)
        {
            const double k = std::abs ((double) y[i] - 2.0 * y[i - 1] + (double) y[i - 2]);
            if (k > worst) { worst = k; if (where) *where = i; }
        }
    }
    return worst;
}

double estimateHzByCrossings (const juce::AudioBuffer<float>& b, long from, long to, double sr)
{
    std::vector<double> crossings;
    const float* y = b.getReadPointer (0);
    for (long i = std::max<long> (from, 1); i < to; ++i)
        if (y[i - 1] < 0.0f && y[i] >= 0.0f)
        {
            const double denom = (double) y[i] - (double) y[i - 1];
            crossings.push_back ((double) (i - 1) + (std::abs (denom) > 1e-12 ? -(double) y[i - 1] / denom : 0.0));
        }
    if (crossings.size() < 2) return 0.0;
    const double period = (crossings.back() - crossings.front()) / (double) (crossings.size() - 1);
    return period > 0.0 ? sr / period : 0.0;
}

// Classic, 100 %, manual C chromatic: the contract's steady-tone fixture.
void configureClassicB3 (Proc& p)
{
    setParam (p, "keyRoot", 0.0f);
    setParam (p, "scale", 2.0f);
    setParam (p, "scaleMode", 2.0f);
    setParam (p, "autoKey", 0.0f);
    setParam (p, "followSongKey", 0.0f);
    setParam (p, "style", 1.0f);
    setParam (p, "amount", 1.0f);
    setParam (p, "speed", 1.0f);
    setParam (p, "enableCorrection", 1.0f);
    setParam (p, "doNoHarm", 0.0f);
    setParam (p, "formantPreserve", 1.0f);
    setParam (p, "classicMode", 0.0f);
}

juce::MemoryBlock stateOf (Proc& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb;
}

int childCount (const juce::MemoryBlock& blob, const char* tag)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    int n = 0;
    if (xml != nullptr)
        for (auto* c : xml->getChildIterator())
            n += c->hasTagName (tag);
    return n;
}

struct SecondsPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSeconds (seconds);
        info.setTimeInSamples ((juce::int64) std::llround (seconds * sampleRate));
        info.setIsPlaying (true);
        return info;
    }
    double seconds = 0.0, sampleRate = 48000.0;
};

// Graph (Pitch Edit) objects captured through the real detector + transport path.
void captureGraphNotes (Proc& p, const juce::AudioBuffer<float>& in);

// --- factory presets (editor-owned, reached through the real UI control) ---------------
juce::ComboBox* findFactoryCombo (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* combo = dynamic_cast<juce::ComboBox*> (child);
            combo != nullptr && combo->getNumItems() >= 2
                && combo->getItemText (0) == "Natural" && combo->getItemText (1) == "Pitch Center")
            return combo;
        if (child != nullptr)
            if (auto* found = findFactoryCombo (*child))
                return found;
    }
    return nullptr;
}

struct PresetLoader
{
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::ComboBox* combo = nullptr;
    explicit PresetLoader (Proc& p) : editor (p.createEditor())
    {
        if (editor != nullptr) combo = findFactoryCombo (*editor);
    }
    int count() const { return combo != nullptr ? combo->getNumItems() : 0; }
    juce::String name (int i) const { return combo->getItemText (i); }
    void load (int i)
    {
        combo->setSelectedId (0, juce::dontSendNotification);   // allow re-selecting the same item
        combo->setSelectedItemIndex (i, juce::sendNotificationSync);
    }
};

void resetAllParamsToDefault (Proc& p)
{
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            r->setValueNotifyingHost (r->getDefaultValue());
}

std::vector<std::pair<juce::String, float>> paramSnapshot (Proc& p)
{
    std::vector<std::pair<juce::String, float>> v;
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param))
            v.emplace_back (r->getParameterID(), r->getValue());
    return v;
}

// --- memory -----------------------------------------------------------------------------
double residentMB()
{
    mach_task_basic_info_data_t info {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &count) != KERN_SUCCESS)
        return -1.0;
    return (double) info.resident_size / 1048576.0;
}

double footprintMB()
{
    task_vm_info_data_t vm {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &vm, &count) != KERN_SUCCESS)
        return -1.0;
    return (double) vm.phys_footprint / 1048576.0;
}

const char* kRoots[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

void captureGraphNotes (Proc& p, const juce::AudioBuffer<float>& in)
{
    SecondsPlayHead playHead;
    p.setPlayHead (&playHead);
    setParam (p, "capturePitchEdits", 1.0f);
    RenderSpec spec;
    spec.before = [&playHead] (Proc&, long pos, int, long) { playHead.seconds = (double) pos / 48000.0; return false; };
    (void) render (p, in, spec);
    setParam (p, "capturePitchEdits", 0.0f);
    p.setPlayHead (nullptr);
}

// ==========================================================================================
//  1. State round trip
// ==========================================================================================
void checkStateRoundTrip()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const auto in = vocalMaterial (sr, VocalGen::kPeriod);

    struct Config { const char* name; std::function<void (Proc&)> apply; bool offline; };
    auto heavy = [] (Proc& p)
    {
        setParam (p, "style", 1.0f);            // Classic
        setParam (p, "amount", 0.7f);
        setParam (p, "speed", 0.62f);
        setParam (p, "keyRoot", 2.0f);          // D
        setParam (p, "scaleMode", 3.0f);        // dorian (extended scale, not the legacy 3-choice)
        setParam (p, "enabledNotes", (float) (4095 & ~(1 << 1)));
        setParam (p, "formantPreserve", 1.0f);
        setParam (p, "formantShift", 3.5f);
        setParam (p, "throat", 1.15f);
        setParam (p, "flexTune", 0.35f);
        setParam (p, "transition", 0.8f);
        setParam (p, "vibrato", 0.5f);
        setParam (p, "addVibrato", 20.0f);
        setParam (p, "vibratoRate", 5.0f);
        setParam (p, "vibratoOnset", 150.0f);
        setParam (p, "referenceDetune", 12.0f);
        setParam (p, "vocalRange", 3.0f);       // Tenor
        setParam (p, "doNoHarm", 0.0f);
        setParam (p, "inputGain", -2.0f);
        setParam (p, "outputGain", 1.5f);
    };
    const std::vector<Config> configs {
        { "default state",                     [] (Proc&) {}, false },
        { "heavy non-default (Classic 0.7 ...)", heavy, false },
        { "heavy + humanize 0.4",              [&] (Proc& p) { heavy (p); setParam (p, "humanize", 0.4f); }, false },
        { "heavy + Neural Pitch (offline)",    [&] (Proc& p) { heavy (p); setParam (p, "neuralPitch", 1.0f); }, true },
        { "Live Mode (Extreme), Classic",      [] (Proc& p) { setParam (p, "liveMode", 1.0f); setParam (p, "style", 1.0f); }, false },
        { "AutoKey on, manual key untouched",  [] (Proc& p) { setParam (p, "autoKey", 1.0f); }, false },
        { "AutoKey on, explicit C major",      [] (Proc& p) { setParam (p, "autoKey", 1.0f); setParam (p, "keyRoot", 0.0f);
                                                              setParam (p, "scaleMode", 0.0f); }, false },
    };

    bool allPass = true;
    int identical = 0, total = 0;
    for (const auto& c : configs)
    {
        RenderSpec spec;
        spec.offline = c.offline;
        float maxConf = 0.0f;
        spec.before = [&maxConf] (Proc& p, long, int, long)
        { maxConf = std::max (maxConf, p.currentKeyOrigin().confidence); return false; };
        Proc p1;
        c.apply (p1);
        RenderStats s1;
        const auto a = render (p1, in, spec, &s1);
        const auto origin = p1.currentKeyOrigin();
        const auto blob = stateOf (p1);

        Proc p2;
        p2.setStateInformation (blob.getData(), (int) blob.getSize());
        const auto b = render (p2, in, spec);

        // Same instance: a project reload into a live instance (host re-prepares after).
        p1.setStateInformation (blob.getData(), (int) blob.getSize());
        const auto c2 = render (p1, in, spec);

        const auto dFresh = compare (a, b), dSame = compare (a, c2);
        const bool pass = dFresh.count == 0 && dSame.count == 0 && s1.finite;
        allPass = allPass && pass;
        total += 2;
        identical += (dFresh.count == 0) + (dSame.count == 0);
        info ("%-4s %-36s fresh: %s | same-instance: %s | peak %.2f%s", pass ? "ok" : "BAD", c.name,
              diffText (dFresh, sr).c_str(), diffText (dSame, sr).c_str(), s1.peak,
              origin.source == Proc::KeyOrigin::Source::AutoKey
                  ? fmt (" | AutoKey max conf %.3f, end %s/%d", maxConf, kRoots[origin.root], origin.scale).c_str() : "");
    }

    // State blob stability: save -> load -> save must reproduce the blob, or repeated
    // project saves drift. Counted per top-level child the processor appends.
    bool blobStable = true;
    {
        Proc g0;
        juce::MemoryBlock blobs[3];
        blobs[0] = stateOf (g0);
        for (int gen = 1; gen < 3; ++gen)
        {
            Proc g;
            g.setStateInformation (blobs[gen - 1].getData(), (int) blobs[gen - 1].getSize());
            blobs[gen] = stateOf (g);
        }
        blobStable = blobs[1] == blobs[0] && blobs[2] == blobs[1];
        info ("%-4s save->load->save blob: %zu -> %zu -> %zu bytes; <KeyMap> children %d -> %d -> %d, <PitchEdits> %d -> %d -> %d",
              blobStable ? "ok" : "BAD", blobs[0].getSize(), blobs[1].getSize(), blobs[2].getSize(),
              childCount (blobs[0], "KeyMap"), childCount (blobs[1], "KeyMap"), childCount (blobs[2], "KeyMap"),
              childCount (blobs[0], "PitchEdits"), childCount (blobs[1], "PitchEdits"), childCount (blobs[2], "PitchEdits"));
    }
    // Consequence probe: graph notes deleted after a reload must stay deleted after the
    // next save + reload (the restore reads the FIRST <PitchEdits> child).
    bool graphOk = true;
    {
        Proc a;
        captureGraphNotes (a, in);
        const auto s1 = stateOf (a);
        const size_t captured = a.getPitchEdits().size();
        Proc b;
        b.setStateInformation (s1.getData(), (int) s1.getSize());
        const size_t restored = b.getPitchEdits().size();
        b.clearPitchEdits();
        const auto s2 = stateOf (b);
        Proc c;
        c.setStateInformation (s2.getData(), (int) s2.getSize());
        const size_t afterDelete = c.getPitchEdits().size();
        graphOk = captured > 0 && restored == captured && afterDelete == 0;
        info ("%-4s graph notes: captured %zu, restored %zu, deleted all + save + reload -> %zu (expected 0)",
              graphOk ? "ok" : "BAD", captured, restored, afterDelete);
    }
    allPass = allPass && blobStable && graphOk;
    verdict (1, "state round trip", allPass, secondsSince (t0),
             fmt ("%d/%d renders bit-identical (%d configs x fresh + same-instance reload); blob stable %s; graph delete persists %s",
                  identical, total, (int) configs.size(), blobStable ? "yes" : "NO", graphOk ? "yes" : "NO"));
}

// ==========================================================================================
//  2. Preset recall
// ==========================================================================================
void checkPresetRecall()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const auto in = vocalMaterial (sr, VocalGen::kPeriod);
    RenderSpec spec;

    int presets = 0;
    {
        Proc probe;
        PresetLoader loader (probe);
        presets = loader.count();
    }
    if (presets == 0)
    {
        verdict (2, "preset recall", false, secondsSince (t0), "factory preset menu not found in the editor");
        return;
    }
    info ("host program API: getNumPrograms() = %d (presets are editor-owned factory workflows: %d)",
          Proc().getNumPrograms(), presets);

    // Identity is judged at 1e-6 (-120 dBFS); anything short of bit-exact is still shown.
    constexpr double tol = 1.0e-6;
    bool allPass = true;
    int reloadOk = 0, trip = 0, humanizeOnly = 0;
    for (int i = 0; i < presets; ++i)
    {
        juce::String name;
        auto run = [&] (bool zeroHumanize, Diff& dReload, Diff& dTrip, std::string& paramDrift)
        {
            Proc p1;
            {
                PresetLoader loader (p1);
                name = loader.name (i);
                loader.load (i);
                if (zeroHumanize) setParam (p1, "humanize", 0.0f);
            }
            const auto r1 = render (p1, in, spec);
            {
                PresetLoader loader (p1);
                loader.load (i);
                if (zeroHumanize) setParam (p1, "humanize", 0.0f);
            }
            const auto r2 = render (p1, in, spec);
            const auto blob = stateOf (p1);
            Proc p3;
            p3.setStateInformation (blob.getData(), (int) blob.getSize());
            const auto r3 = render (p3, in, spec);
            dReload = compare (r1, r2);
            dTrip = compare (r1, r3);
            const auto a = paramSnapshot (p1), b = paramSnapshot (p3);
            for (size_t k = 0; k < a.size(); ++k)
                if (std::memcmp (&a[k].second, &b[k].second, sizeof (float)) != 0)
                    paramDrift += fmt (" %s %.9g->%.9g", a[k].first.toRawUTF8(), a[k].second, b[k].second);
        };
        Diff dReload, dTrip;
        std::string drift;
        run (false, dReload, dTrip, drift);
        const bool pass = dReload.maxAbs <= tol && dTrip.maxAbs <= tol;
        reloadOk += dReload.maxAbs <= tol;
        trip += dTrip.maxAbs <= tol;
        allPass = allPass && pass;
        info ("%-4s %-22s reload: %s | state round trip: %s%s", pass ? "ok" : "BAD", name.toRawUTF8(),
              diffText (dReload, sr).c_str(), diffText (dTrip, sr).c_str(),
              drift.empty() ? "" : (" | normalised params changed by the round trip:" + drift).c_str());
        if (! pass)
        {
            Diff hReload, hTrip;
            std::string hDrift;
            run (true, hReload, hTrip, hDrift);
            const bool cleared = hReload.maxAbs <= tol && hTrip.maxAbs <= tol;
            humanizeOnly += cleared;
            info ("     same preset with humanize forced to 0: reload %s | round trip %s  -> %s",
                  diffText (hReload, sr).c_str(), diffText (hTrip, sr).c_str(),
                  cleared ? "difference is entirely Humanize" : "OTHER CAUSE");
        }
    }

    // Completeness: loading preset i after ANY other preset j must give exactly the
    // parameter values of preset i loaded on a fresh instance (no sticky deltas). A
    // leftover only matters audibly if preset i enables a harmony voice / the parameter
    // is not a harmony-voice parameter.
    int stickyPairs = 0, audiblePairs = 0;
    std::set<std::string> audible, inaudibleParams;
    {
        Proc p;
        PresetLoader loader (p);
        std::vector<std::vector<std::pair<juce::String, float>>> fresh ((size_t) presets);
        std::vector<bool> harmonyOn ((size_t) presets, false);
        for (int i = 0; i < presets; ++i)
        {
            resetAllParamsToDefault (p);
            loader.load (i);
            fresh[(size_t) i] = paramSnapshot (p);
            for (int v = 1; v <= 4; ++v)
                harmonyOn[(size_t) i] = harmonyOn[(size_t) i]
                    || p.parameters().getRawParameterValue ("h" + juce::String (v) + "Enable")->load() > 0.5f;
        }
        for (int j = 0; j < presets; ++j)
            for (int i = 0; i < presets; ++i)
            {
                if (i == j) continue;
                resetAllParamsToDefault (p);
                loader.load (j);
                loader.load (i);
                const auto now = paramSnapshot (p);
                bool sticky = false, isAudible = false;
                std::string which;
                for (size_t k = 0; k < now.size(); ++k)
                    if (std::abs (now[k].second - fresh[(size_t) i][k].second) > 1.0e-6f)
                    {
                        sticky = true;
                        const bool voiceParam = now[k].first.startsWith ("h") && ! now[k].first.startsWith ("harmony")
                                             && ! now[k].first.startsWith ("hiTrack");
                        const bool audibleHere = harmonyOn[(size_t) i] || ! voiceParam;
                        isAudible = isAudible || audibleHere;
                        if (audibleHere) which += " " + now[k].first.toStdString();
                        else inaudibleParams.insert (now[k].first.toStdString());
                    }
                stickyPairs += sticky;
                audiblePairs += isAudible;
                if (isAudible)
                    audible.insert ((loader.name (j) + " -> " + loader.name (i)).toStdString() + ":" + which);
            }
    }
    if (stickyPairs > 0)
    {
        std::string names;
        for (const auto& s : inaudibleParams) names += " " + s;
        info ("%-4s %d of %d (previous -> next) preset pairs leave parameters behind; %d audibly (next preset uses them):",
              audiblePairs ? "BAD" : "note", stickyPairs, presets * (presets - 1), audiblePairs);
        for (const auto& s : audible) info ("       %s", s.c_str());
        info ("       inaudible leftovers (harmony voices disabled in the next preset):%s", names.c_str());
    }
    allPass = allPass && audiblePairs == 0;
    verdict (2, "preset recall", allPass, secondsSince (t0),
             fmt ("%d presets: reload identical %d/%d, state round trip identical %d/%d (%d failures are Humanize only); audible sticky pairs %d",
                  presets, reloadOk, presets, trip, presets, humanizeOnly, audiblePairs));
}

// ==========================================================================================
//  3. AutoKey does not perturb
// ==========================================================================================
void checkAutoKeyNoPerturb()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const float gate = Proc::kMinBusKeyConfidence;
    bool allPass = true;

    // (a) Toggling AutoKey on keyless material with an explicitly chosen manual key.
    // Precondition probe at analysis-hop resolution: 64-sample blocks (the histogram
    // hop is 256 samples), AutoKey on, maximum verdict confidence seen.
    struct Probe { float maxConf = 0.0f; double firstAbove = -1.0; int root = -1, scale = -1; };
    auto probe = [gate, sr] (const juce::AudioBuffer<float>& material)
    {
        Probe r;
        Proc p;
        setParam (p, "keyRoot", 0.0f);
        setParam (p, "scaleMode", 0.0f);
        setParam (p, "autoKey", 1.0f);
        RenderSpec spec;
        spec.maxBlock = 64;
        spec.before = [&r, gate, sr] (Proc& q, long pos, int, long)
        {
            const auto o = q.currentKeyOrigin();
            r.maxConf = std::max (r.maxConf, o.confidence);
            if (o.confidence >= gate && r.firstAbove < 0.0) { r.firstAbove = (double) pos / sr; r.root = o.root; r.scale = o.scale; }
            return false;
        };
        (void) render (p, material, spec);
        return r;
    };
    const auto keyless = keylessMaterial (sr, 21.0);
    const auto pre = probe (keyless);
    const long half = (long) (0.5 * sr);
    for (const int style : { 0, 1 })
    {
        auto setup = [style] (Proc& p)
        {
            setParam (p, "keyRoot", 0.0f);     // explicit C major
            setParam (p, "scaleMode", 0.0f);
            setParam (p, "followSongKey", 0.0f);
            setParam (p, "style", (float) style);
            setParam (p, "autoKey", 0.0f);
        };
        float maxConf = pre.maxConf;
        int mode = 0;   // 0 never on, 1 always on, 2 toggle (starts off), 3 toggle (starts on)
        RenderSpec spec;
        spec.before = [&] (Proc& p, long pos, int, long)
        {
            bool on = mode == 1;
            if (mode >= 2) on = (((pos / half) & 1) == 0) == (mode == 3);
            setParam (p, "autoKey", on ? 1.0f : 0.0f);
            if (on)
            {
                const auto o = p.currentKeyOrigin();
                maxConf = std::max (maxConf, o.confidence);
            }
            return false;
        };
        std::vector<juce::AudioBuffer<float>> outs;
        for (mode = 0; mode < 4; ++mode)
        {
            Proc p;
            setup (p);
            if (! p.manualKeyIsExplicit()) { allPass = false; info ("BAD  manual key not explicit"); }
            outs.push_back (render (p, keyless, spec));
        }
        const auto dOn = compare (outs[0], outs[1]), dT1 = compare (outs[0], outs[2]), dT2 = compare (outs[0], outs[3]);
        const bool precondition = maxConf < gate;
        const bool pass = precondition && dOn.maxAbs <= 1e-6 && dT1.maxAbs <= 1e-6 && dT2.maxAbs <= 1e-6;
        allPass = allPass && pass;
        info ("%-4s style %s, explicit C major, AutoKey max confidence %.3f (gate %.2f%s): always-on %s | toggled 0.5 s (off first) %s | (on first) %s",
              pass ? "ok" : "BAD", style == 0 ? "Natural" : "Classic", maxConf, gate,
              precondition ? "" : ", PRECONDITION NOT MET", diffText (dOn, sr).c_str(),
              diffText (dT1, sr).c_str(), diffText (dT2, sr).c_str());
    }
    // Report only: the same tone with +-10 c vibrato stays on ONE pitch class (B). How
    // close does a one-note histogram come to the gate at the note onset?
    {
        const auto single = probe (keylessMaterial (sr, 3.0, 10.0));
        info ("note single-pitch-class onset (B3 +35c, +-10c vib): max confidence %.4f vs gate %.2f%s",
              single.maxConf, gate,
              single.firstAbove >= 0.0 ? fmt (" -> CROSSES at %.3f s as %s/%d (a key from ~80 ms of one note)",
                                              single.firstAbove, kRoots[juce::jlimit (0, 11, single.root)], single.scale).c_str()
                                       : " (stays below)");
    }

    // (b) AutoKey OFF: touching AutoKey-related controls must change nothing. Material
    // has a confident C-major melody, so the histogram really does build a key.
    const auto vocal = vocalMaterial (sr, 2.0 * VocalGen::kPeriod);
    RenderSpec spec;
    Proc untouched;
    const auto base = render (untouched, vocal, spec);
    const auto baseOrigin = untouched.currentKeyOrigin();
    struct Touch { const char* name; std::function<void (Proc&)> apply; };
    const std::vector<Touch> touches {
        { "AutoKey memory 2 s",           [] (Proc& p) { p.setAutoKeyMemorySeconds (2.0); } },
        { "AutoKey profile Krumhansl",    [] (Proc& p) { p.setAutoKeyHistogramProfile (0); } },
        { "AutoKey profile Bellman",      [] (Proc& p) { p.setAutoKeyHistogramProfile (4); } },
        { "autoKey on->off before play",  [] (Proc& p) { setParam (p, "autoKey", 1.0f); setParam (p, "autoKey", 0.0f); } },
        { "followSongKey on->off",        [] (Proc& p) { setParam (p, "followSongKey", 1.0f); setParam (p, "followSongKey", 0.0f); } },
        { "all of the above",             [] (Proc& p) { p.setAutoKeyMemorySeconds (2.0); p.setAutoKeyHistogramProfile (4);
                                                          setParam (p, "autoKey", 1.0f); setParam (p, "autoKey", 0.0f);
                                                          setParam (p, "followSongKey", 1.0f); setParam (p, "followSongKey", 0.0f); } },
    };
    int identical = 0;
    for (const auto& t : touches)
    {
        Proc p;
        t.apply (p);
        const auto out = render (p, vocal, spec);
        const auto d = compare (base, out);
        identical += d.count == 0;
        allPass = allPass && d.count == 0;
        info ("%-4s AutoKey OFF + %-28s %s", d.count == 0 ? "ok" : "BAD", t.name, diffText (d, sr).c_str());
    }
    // And toggling followSongKey during playback (no bus in this isolated session).
    {
        Proc p;
        RenderSpec toggling;
        toggling.before = [half] (Proc& q, long pos, int, long)
        { setParam (q, "followSongKey", ((pos / half) & 1) ? 1.0f : 0.0f); return false; };
        const auto out = render (p, vocal, toggling);
        const auto d = compare (base, out);
        allPass = allPass && d.count == 0;
        info ("%-4s AutoKey OFF + followSongKey toggled 0.5 s  %s", d.count == 0 ? "ok" : "BAD", diffText (d, sr).c_str());
    }
    info ("(untouched instance's manual key %s/%d)", kRoots[baseOrigin.root], baseOrigin.scale);
    verdict (3, "AutoKey does not perturb", allPass, secondsSince (t0),
             fmt ("toggle below gate identical; %d/%d AutoKey-OFF touches bit-identical", identical, (int) touches.size()));
}

// ==========================================================================================
//  4. Automation stress
// ==========================================================================================
void checkAutomationStress()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const double seconds = 60.0;
    const auto in = vocalMaterial (sr, seconds);
    int scaleModes = 0;
    { Proc probe; scaleModes = numChoices (probe, "scaleMode"); }

    // Baseline: static renders spanning the automated space. No automation, so every
    // curvature spike here is the material + DSP itself.
    double baseline = 0.0;
    std::string worstStatic;
    struct Static { const char* name; std::function<void (Proc&)> apply; };
    std::vector<Static> statics;
    for (int s = 0; s < 5; ++s)
        statics.push_back ({ "style", [s] (Proc& p) { setParam (p, "style", (float) s); setParam (p, "amount", 1.0f);
                                                       setParam (p, "speed", 1.0f); } });
    statics.push_back ({ "formant -12", [] (Proc& p) { setParam (p, "formantShift", -12.0f); } });
    statics.push_back ({ "formant +12", [] (Proc& p) { setParam (p, "formantShift", 12.0f); } });
    statics.push_back ({ "correction off", [] (Proc& p) { setParam (p, "enableCorrection", 0.0f); } });
    statics.push_back ({ "F# blues, speed 0.05", [] (Proc& p) { setParam (p, "keyRoot", 6.0f); setParam (p, "scaleMode", 12.0f);
                                                                   setParam (p, "speed", 0.05f); } });
    std::string staticRow;
    double staticPeak = 0.0;
    for (size_t k = 0; k < statics.size(); ++k)
    {
        Proc p;
        statics[k].apply (p);
        RenderSpec spec;
        RenderStats st;
        const auto out = render (p, in, spec, &st);
        const double c = maxCurvature (out);
        const std::string label = statics[k].name + (k < 5 ? std::to_string (k) : std::string());
        staticRow += fmt (" %s %.4f/%.2f;", label.c_str(), c, st.peak);
        staticPeak = std::max (staticPeak, (double) st.peak);
        if (c > baseline) { baseline = c; worstStatic = label; }
    }
    const double threshold = 2.0 * baseline;
    info ("static renders (curvature/peak):%s", staticRow.c_str());
    info ("baseline: worst static curvature %.4f (%s); click threshold = 2x = %.4f", baseline,
          worstStatic.c_str(), threshold);

    // mask bits: 1 amount, 2 speed, 4 formant, 8 style, 16 key/scale, 32 correction on/off
    struct Run { const char* name; int mask; };
    const Run runs[] = { { "ALL per block (gate)", 63 }, { "amount only", 1 }, { "speed only", 2 },
                         { "formant only", 4 }, { "style only", 8 }, { "key/scale only", 16 },
                         { "enableCorrection only", 32 } };
    bool gatePass = true;
    for (const auto& run : runs)
    {
        struct Row { float amount, speed, formant; int style, key, scale, corr; };
        std::vector<Row> rows;
        Rng rng (0x5A77A41ull + (std::uint64_t) run.mask);
        RenderSpec spec;
        spec.before = [&] (Proc& p, long, int, long)
        {
            if (run.mask & 1)  setParam (p, "amount", (float) rng.uniform());
            if (run.mask & 2)  setParam (p, "speed", (float) (0.05 + 0.95 * rng.uniform()));
            if (run.mask & 4)  setParam (p, "formantShift", (float) (24.0 * rng.uniform() - 12.0));
            if (run.mask & 8)  setParam (p, "style", (float) rng.below (5));
            if (run.mask & 16)
            {
                setParam (p, "keyRoot", (float) rng.below (12));
                setParam (p, "scaleMode", (float) rng.below (scaleModes));
            }
            if (run.mask & 32) setParam (p, "enableCorrection", rng.uniform() < 0.5 ? 1.0f : 0.0f);
            auto v = [&p] (const char* id) { return p.parameters().getRawParameterValue (id)->load(); };
            rows.push_back ({ v ("amount"), v ("speed"), v ("formantShift"), (int) v ("style"), (int) v ("keyRoot"),
                              (int) v ("scaleMode"), (int) v ("enableCorrection") });
            return false;
        };
        Proc p;
        RenderStats st;
        const auto out = render (p, in, spec, &st);
        long where = 0;
        const double c = maxCurvature (out, 0, -1, &where);
        const bool pass = st.finite && st.peak <= 1.5f && c <= threshold;
        if (run.mask == 63) gatePass = pass;
        // Count blocks whose curvature exceeds the threshold, for scale.
        int loud = 0;
        for (long b0 = 0; b0 + 512 <= out.getNumSamples(); b0 += 512)
            loud += maxCurvature (out, b0, b0 + 512) > threshold;
        const size_t blk = (size_t) juce::jlimit (1L, (long) rows.size() - 1, where / 512);
        auto rowText = [] (const Row& r)
        { return fmt ("amt %.2f spd %.2f fmt %+.1f style %d key %d/%d corr %d", r.amount, r.speed, r.formant,
                      r.style, r.key, r.scale, r.corr); };
        info ("%-4s %-22s finite %s, peak %.3f, worst curvature %.4f (%.2fx baseline) at %.4f s (+%ld in block), %d blocks > threshold, latency %s",
              pass ? "ok" : "BAD", run.name, st.finite ? "yes" : "NO", st.peak, c, c / baseline,
              (double) where / sr, where % 512, loud, st.latencyChanges == 0 ? "constant" : "CHANGED");
        if (c > threshold)
            info ("     worst block params: prev [%s] -> this [%s]", rowText (rows[blk - 1]).c_str(), rowText (rows[blk]).c_str());
    }
    verdict (4, "automation stress (60 s)", gatePass, secondsSince (t0),
             fmt ("ALL-parameter run is the gate (finite, peak <= 1.5, curvature <= %.3f); single-parameter runs attribute", threshold));
}

// ==========================================================================================
//  5. Sample-rate x buffer matrix
// ==========================================================================================
void checkRateBlockMatrix()
{
    const auto t0 = Clock::now();
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int blocks[] = { 32, 64, 128, 512, 1024, 2048 };
    bool allPass = true;
    double worstCents = 0.0;
    int runs = 0;
    for (const double sr : rates)
    {
        const auto tone = b3Plus45 (sr, 3.0);
        std::vector<juce::AudioBuffer<float>> outs;
        std::string row;
        auto one = [&] (int maxBlock, std::function<int (long)> blockLen, const char* label)
        {
            Proc p;
            configureClassicB3 (p);
            RenderSpec spec;
            spec.sr = sr;
            spec.maxBlock = maxBlock;
            spec.blockLen = blockLen;
            RenderStats st;
            auto out = render (p, tone, spec, &st);
            const long n = out.getNumSamples();
            const double hz = estimateHzByCrossings (out, n - (long) (0.5 * sr), n, sr);
            const double cents = hz > 0.0 ? std::abs ((hzToMidi (hz) - 59.0) * 100.0) : 999.0;
            const bool pass = st.finite && st.latencyChanges == 0 && cents <= 5.0;
            allPass = allPass && pass;
            worstCents = std::max (worstCents, cents);
            ++runs;
            row += fmt (" %s:%.2fc%s", label, cents, pass ? "" : (st.finite ? (st.latencyChanges ? "!LAT" : "!ACC") : "!NAN"));
            if (outs.empty()) row = fmt ("lat %d |", st.latency) + row;
            return out;
        };
        for (const int b : blocks)
            outs.push_back (one (b, nullptr, std::to_string (b).c_str()));
        Rng rng (0xB10C5ull);
        one (2048, [&rng] (long) { return 1 + rng.below (2048); }, "irregular");
        info ("%.0f Hz: %s", sr, row.c_str());

        std::string ident;
        for (size_t k = 1; k < outs.size(); ++k)
        {
            const auto d = compare (outs[0], outs[k]);
            ident += fmt (" %d:%s", blocks[k], d.count == 0 ? "identical" : fmt ("%.2g", d.maxAbs).c_str());
        }
        info ("        vs block 32 (report only):%s", ident.c_str());
    }
    verdict (5, "sample-rate x block matrix", allPass, secondsSince (t0),
             fmt ("%d runs finite, latency constant, B3+45c worst %.2f c (limit 5 c)", runs, worstCents));
}

// ==========================================================================================
//  6. Host bypass toggling
// ==========================================================================================
void checkBypass()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const auto in = vocalMaterial (sr, 2.0 * VocalGen::kPeriod);
    bool allPass = true;
    for (const int blockSize : { 128, 512 })
    {
        // Continuous (never bypassed) reference for the click baseline and stale check.
        RenderSpec contSpec;
        contSpec.maxBlock = blockSize;
        Proc ref;
        const auto cont = render (ref, in, contSpec);
        const double baseline = maxCurvature (cont);
        const double threshold = 2.0 * baseline;

        // Deterministic schedule: segments of 0.2..1.0 s, alternating processed/bypassed.
        Rng rng (0xB7A55ull + (std::uint64_t) blockSize);
        std::vector<std::pair<long, bool>> segments;   // (start, bypassed)
        {
            bool bypassed = false;
            for (long pos = 0; pos < in.getNumSamples(); )
            {
                segments.push_back ({ pos, bypassed });
                long len = (long) ((0.2 + 0.8 * rng.uniform()) * sr);
                len = std::max<long> (blockSize, (len / blockSize) * blockSize);
                pos += len;
                bypassed = ! bypassed;
            }
        }
        auto bypassedAt = [&segments] (long pos)
        {
            bool b = false;
            for (const auto& s : segments) if (s.first <= pos) b = s.second;
            return b;
        };
        RenderSpec spec;
        spec.maxBlock = blockSize;
        spec.before = [&] (Proc&, long pos, int, long) { return bypassedAt (pos); };
        Proc p;
        RenderStats st;
        const auto out = render (p, in, spec, &st);
        const int latency = st.latency;

        double worstBypassErr = 0.0, worstReengage = 0.0, worstEngage = 0.0;
        double staleWorst = 0.0, staleNowAtWorst = 0.0;
        int reengages = 0, staleEvaluable = 0, staleReplays = 0;
        for (size_t k = 0; k < segments.size(); ++k)
        {
            const long start = segments[k].first;
            const long end = k + 1 < segments.size() ? segments[k + 1].first : in.getNumSamples();
            if (segments[k].second)
            {
                for (long i = start; i < end; ++i)
                {
                    const long src = i - latency;
                    const float expected = src >= 0 ? in.getSample (0, (int) src) : 0.0f;
                    for (int ch = 0; ch < out.getNumChannels(); ++ch)
                        worstBypassErr = std::max (worstBypassErr, (double) std::abs (out.getSample (ch, (int) i) - expected));
                }
                if (start > 0)   // processed -> bypassed
                    worstEngage = std::max (worstEngage, maxCurvature (out, start - 4, std::min (end, start + 64)));
            }
            else if (start > 0)   // bypassed -> processed (re-engage)
            {
                ++reengages;
                const long to = std::min<long> (end, start + latency + 4096);
                worstReengage = std::max (worstReengage, maxCurvature (out, start - 4, to));
                // Stale-audio probe: does the first `latency` of the re-engaged output
                // correlate with the continuous render at THIS time, or with the
                // continuous render from the moment bypass began (a replayed pipeline)?
                const long bypassStart = segments[k - 1].first;
                const long win = std::min<long> (latency, end - start);
                double yy = 0.0, nn = 0.0, ss = 0.0, yn = 0.0, ys = 0.0;
                for (long i = 0; i < win; ++i)
                {
                    const double y = out.getSample (0, (int) (start + i));
                    const double now = cont.getSample (0, (int) (start + i));
                    const double stale = cont.getSample (0, (int) (bypassStart + i));
                    yy += y * y; nn += now * now; ss += stale * stale; yn += y * now; ys += y * stale;
                }
                const double floor = 1.0e-4 * (double) win;   // 0.01 RMS
                if (yy > floor && nn > floor && ss > floor)
                {
                    ++staleEvaluable;
                    const double cNow = yn / std::sqrt (yy * nn), cStale = ys / std::sqrt (yy * ss);
                    if (cStale > 0.8 && cStale > cNow + 0.2) ++staleReplays;
                    staleWorst = std::max (staleWorst, cStale);
                    staleNowAtWorst = cStale >= staleWorst ? cNow : staleNowAtWorst;
                }
            }
        }
        const bool pass = st.finite && worstBypassErr <= 1e-6 && worstReengage <= threshold;
        allPass = allPass && pass;
        info ("%-4s block %d, latency %d: %d re-engages; bypassed == input delayed by latency: max err %.3g; re-engage worst curvature %.4f (%.2fx continuous, limit 2x); engage-bypass worst %.4f (%.2fx, report only); finite %s",
              pass ? "ok" : "BAD", blockSize, latency, reengages, worstBypassErr, worstReengage,
              worstReengage / baseline, worstEngage, worstEngage / baseline, st.finite ? "yes" : "NO");
        info ("     stale-audio probe: %d of %d evaluable re-engages replay the pre-bypass pipeline for the first latency (best corr with pre-bypass render %.2f vs %.2f with the continuous render)",
              staleReplays, staleEvaluable, staleWorst, staleNowAtWorst);
    }
    verdict (6, "host bypass toggling", allPass, secondsSince (t0),
             "bypassed = dry delayed by latency; re-engage finite, no click > 2x continuous");
}

// ==========================================================================================
//  7. Mono / stereo
// ==========================================================================================
void checkMonoStereo()
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const auto in = vocalMaterial (sr, VocalGen::kPeriod);
    bool allPass = true;
    std::string detail;

    auto layout = [] (juce::AudioChannelSet main, juce::AudioChannelSet sc)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (main);
        l.inputBuses.add (sc);
        l.outputBuses.add (main);
        return l;
    };
    for (const bool classic : { false, true })
    {
        auto setup = [classic] (Proc& p) { if (classic) configureClassicB3 (p); };
        RenderSpec spec;

        Proc stereo;
        setup (stereo);
        const bool stereoOk = stereo.setBusesLayout (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled()));
        RenderStats sst;
        const auto so = render (stereo, in, spec, &sst);
        Diff lr;
        {
            juce::AudioBuffer<float> l (1, so.getNumSamples()), r (1, so.getNumSamples());
            l.copyFrom (0, 0, so, 0, 0, so.getNumSamples());
            r.copyFrom (0, 0, so, 1, 0, so.getNumSamples());
            lr = compare (l, r);
        }

        Proc mono;
        setup (mono);
        const bool monoOk = mono.setBusesLayout (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled()));
        RenderStats mst;
        const auto mo = monoOk ? render (mono, in, spec, &mst) : juce::AudioBuffer<float> (1, so.getNumSamples());
        const auto dm = compare (mo, so, 1);

        Proc sc;
        setup (sc);
        const bool scOk = sc.setBusesLayout (layout (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()));
        RenderStats cst;
        const auto co = scOk ? render (sc, in, spec, &cst) : so;
        const auto dsc = compare (co, so);

        const bool pass = stereoOk && monoOk && sst.finite && mst.finite && lr.count == 0 && dm.maxAbs <= 1e-6
                       && mono.getTotalNumOutputChannels() == 1 && mst.latency == sst.latency;
        allPass = allPass && pass;
        info ("%-4s %s: stereo L==R %s | mono (%d ch out, latency %d) vs stereo L: %s | stereo + silent stereo sidechain: %s (report only)",
              pass ? "ok" : "BAD", classic ? "Classic C chromatic" : "defaults", diffText (lr, sr).c_str(),
              mono.getTotalNumOutputChannels(), mst.latency, diffText (dm, sr).c_str(),
              scOk ? diffText (dsc, sr).c_str() : "layout rejected");
    }
    {
        Proc p;
        const bool monoStereo = p.checkBusesLayoutSupported (layout (juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled()));
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::mono());
        l.inputBuses.add (juce::AudioChannelSet::disabled());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        info ("layouts: mono->mono %s, stereo->stereo yes, mono->stereo %s",
              monoStereo ? "yes" : "NO", p.checkBusesLayoutSupported (l) ? "yes" : "no (by design: main in == main out)");
    }
    verdict (7, "mono / stereo", allPass, secondsSince (t0), "both main layouts; L==R; mono == stereo L");
}

// ==========================================================================================
//  8. Long continuous playback
// ==========================================================================================
void checkLongPlayback (double minutes)
{
    const auto t0 = Clock::now();
    const double sr = 48000.0;
    const int blockSize = 512;
    const long total = (long) (minutes * 60.0 * sr);
    const double sectionSec = 300.0;
    static const int keyWalk[12] = { 0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5 };   // circle of fifths

    Proc p;
    setParam (p, "autoKey", 1.0f);
    p.setRateAndBufferSizeDetails (sr, blockSize);
    p.prepareToPlay (sr, blockSize);
    drainMessageThread (p);
    const int latency = p.getLatencySamples();

    VocalGen gen (sr);
    juce::AudioBuffer<float> buffer (2, blockSize);
    bool finite = true;
    int latencyChanges = 0;
    float peak = 0.0f;
    const long tenMin = (long) (600.0 * sr);
    double firstSum = 0.0, lastSum = 0.0;
    long firstN = 0, lastN = 0;
    double rss10 = -1.0, rssEnd = -1.0, fp10 = -1.0, fpEnd = -1.0;
    int sectionsMatched = 0, sections = 0;
    std::string keyTrail;
    long nextMinute = (long) (60.0 * sr);

    for (long pos = 0; pos < total; pos += blockSize)
    {
        const int len = (int) std::min<long> (blockSize, total - pos);
        const int section = (int) ((double) pos / sr / sectionSec);
        gen.transpose = keyWalk[section % 12];
        for (int i = 0; i < len; ++i)
        {
            const float s = gen.next();
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), 2, len);
        juce::MidiBuffer midi;
        const auto b0 = Clock::now();
        p.processBlock (slice, midi);
        const double dt = secondsSince (b0);
        if (pos < tenMin) { firstSum += dt; ++firstN; }
        if (pos >= total - tenMin) { lastSum += dt; ++lastN; }
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = slice.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
            {
                if (! std::isfinite (d[i])) finite = false;
                else peak = std::max (peak, std::abs (d[i]));
            }
        }
        if (p.getLatencySamples() != latency) ++latencyChanges;
        if (pos < tenMin && pos + len >= tenMin) { rss10 = residentMB(); fp10 = footprintMB(); }
        // End of each 5-minute key section: where did AutoKey land?
        const long sectionEnd = (long) ((section + 1) * sectionSec * sr);
        if (pos < sectionEnd && pos + len >= sectionEnd)
        {
            const auto o = p.currentKeyOrigin();
            const int expected = keyWalk[section % 12];
            const bool match = o.root == expected && o.scale == 0 && o.confidence >= Proc::kMinBusKeyConfidence;
            sectionsMatched += match;
            ++sections;
            keyTrail += fmt (" %s%s", kRoots[o.root], match ? "" : "?");
        }
        if (pos + len >= nextMinute)
        {
            nextMinute += (long) (60.0 * sr);
            if (((pos + len) / (long) (60.0 * sr)) % 10 == 0)
                info ("... %3ld min, %.0f s elapsed, RSS %.1f MB", (pos + len) / (long) (60.0 * sr), secondsSince (t0), residentMB());
        }
    }
    rssEnd = residentMB();
    fpEnd = footprintMB();
    const double firstMean = firstN ? firstSum / (double) firstN : 0.0, lastMean = lastN ? lastSum / (double) lastN : 0.0;
    const double ratio = firstMean > 0.0 ? lastMean / firstMean : 0.0;
    const bool haveTenMinutes = minutes >= 20.0;
    const double growth = rssEnd - rss10;
    const bool pass = finite && latencyChanges == 0 && (! haveTenMinutes || (ratio <= 1.5 && growth < 50.0));
    info ("%.0f min audio in %.0f s (%.0fx realtime); latency %d, changes %d; finite %s; peak %.3f",
          minutes, secondsSince (t0), minutes * 60.0 / secondsSince (t0), latency, latencyChanges,
          finite ? "yes" : "NO", peak);
    info ("mean block time first 10 min %.1f us, last 10 min %.1f us (ratio %.2f, limit 1.5)",
          firstMean * 1e6, lastMean * 1e6, ratio);
    info ("RSS min 10 %.1f MB -> end %.1f MB (growth %+.1f MB, limit 50); phys_footprint %.1f -> %.1f MB",
          rss10, rssEnd, growth, fp10, fpEnd);
    info ("AutoKey at end of each 5-min section (expected circle of fifths from C, major):%s  [%d/%d matched, report only]",
          keyTrail.c_str(), sectionsMatched, sections);
    verdict (8, "long playback", pass, secondsSince (t0),
             fmt ("%.0f min: finite, latency constant, block-time ratio %.2f, RSS growth %+.1f MB", minutes, ratio, growth));
}
} // namespace

int main (int argc, char** argv)
{
    std::set<int> only;
    double longMinutes = 120.0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--check") == 0 && i + 1 < argc) only.insert (std::atoi (argv[++i]));
        else if (std::strcmp (argv[i], "--long-minutes") == 0 && i + 1 < argc) longMinutes = std::atof (argv[++i]);
        else { std::fprintf (stderr, "usage: AutoPitchNoHarm [--check N]... [--long-minutes M]\n"); return 2; }
    }
    auto want = [&only] (int n) { return only.empty() || only.count (n) > 0; };

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    // Keep every SessionReader in this process away from an open DAW's key bus.
    setenv ("SATTARI_SESSION_ID", ("auto-pitch-noharm-" + juce::String (juce::Time::currentTimeMillis())).toRawUTF8(), 1);

    std::printf ("Sattari Auto Pitch no-harm matrix\n");
    const auto t0 = Clock::now();
    if (want (1)) checkStateRoundTrip();
    if (want (2)) checkPresetRecall();
    if (want (3)) checkAutoKeyNoPerturb();
    if (want (4)) checkAutomationStress();
    if (want (5)) checkRateBlockMatrix();
    if (want (6)) checkBypass();
    if (want (7)) checkMonoStereo();
    if (want (8)) checkLongPlayback (longMinutes);
    std::printf ("%s: %d failing check(s), %.0f s total\n", failures == 0 ? "NO-HARM MATRIX PASS" : "NO-HARM MATRIX FAIL",
                 failures, secondsSince (t0));
    return failures == 0 ? 0 : 1;
}
