// Auto Pitch — Live Mode / sample-rate x buffer qualification matrix.
//
// For Studio and each Live profile (Extreme 2MS, Ultra Quality C3+, Extended Range),
// over sample rate x host block size, measures with the shipped processor:
//
//   latency    reported delay vs MEASURED delay: broadband clicks on silence, clicks
//              riding a voiced tone, and a vocal (whole-signal cross-correlation and
//              high-pass "transient" cross-correlation), all in the output timeline.
//   cpu        per-block processing time on this (single) thread after warm-up: thread
//              CPU time (CLOCK_THREAD_CPUTIME_ID, immune to preemption) and wall time;
//              mean / p99 / max against the block's real-time budget, in N trials.
//   accuracy   steady B3 +45 c, low / high extremes, a vibrato tone, semitone and fifth
//              note-transition settling time.
//   artifacts  NaN/Inf, dropouts (zero runs inside continuous tones), click measure
//              (max second difference) relative to the ideal clean signal.
//   buffer     is the output at one sample rate identical across block sizes?
//
// This binary only MEASURES and writes raw JSON. Gates are applied by --merge, which
// combines fragments into one JSON + a markdown table with PASS/FAIL per row, so the
// thresholds live in one place (kGate* below) and every verdict is reproducible from
// the raw numbers.
//
// Usage:
//   AutoPitchLiveMatrix [--mode studio|extreme|ultra|range]... [--sr HZ]... [--block N]...
//                       [--vocal file.wav] [--cpu-seconds S] [--cpu-trials T] [--dump-dir DIR] --out frag.json
//   (--cpu-seconds 0 skips the CPU measurement, e.g. for a real-vocal timing pass)
//   AutoPitchLiveMatrix --merge out.json out.md frag1.json [frag2.json ...]
//
// Defaults: all four modes; SR {44100, 48000, 88200, 96000}; Live blocks {32,64,128,256},
// Studio additionally {512, 1024}; synthetic vocal; 6 s x 3 CPU trials.
#include "PluginProcessor.h"

#include <mach/mach.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using Proc = SattariAutoPitchAudioProcessor;
using Clock = std::chrono::steady_clock;

std::string fmt (const char* f, ...)
{
    char buf[2048];
    va_list args;
    va_start (args, f);
    std::vsnprintf (buf, sizeof (buf), f, args);
    va_end (args);
    return buf;
}

void logf (const char* f, ...)
{
    va_list args;
    va_start (args, f);
    std::vprintf (f, args);
    va_end (args);
    std::fflush (stdout);
}

// --- host emulation (same pattern as AutoPitchNoHarm) -----------------------------------
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

void drainMessageThread (Proc& p) { if (! std::getenv ("LM_NO_DRAIN")) ((juce::AsyncUpdater&) p).handleUpdateNowIfNeeded(); }

double midiToHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
double hzToMidi (double hz)   { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

struct Rng
{
    std::uint64_t s;
    explicit Rng (std::uint64_t seed) : s (seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    double uniform() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    float  bipolar() { return (float) (uniform() * 2.0 - 1.0); }
};

// --- modes ------------------------------------------------------------------------------
struct Mode
{
    const char* key;
    const char* label;
    bool live;
    bool extendedRange;   // liveProfile = 1
    bool extreme;         // extremeLive = 1 (only meaningful with liveProfile = 0)
    double lowMidi;       // low-extreme accuracy note (profile's documented floor)
};

const Mode kModes[] = {
    { "studio",  "Studio",                  false, false, false, 40.0 },   // E2
    { "extreme", "Live Extreme 2MS",        true,  false, true,  40.0 },   // E2
    { "ultra",   "Live Ultra Quality C3+",  true,  false, false, 48.0 },   // C3 (its documented floor)
    { "range",   "Live Extended Range",     true,  true,  false, 40.0 },   // E2
};

const Mode* findMode (const std::string& key)
{
    for (const auto& m : kModes)
        if (key == m.key) return &m;
    return nullptr;
}

void configureMode (Proc& p, const Mode& m)
{
    setParam (p, "liveMode", m.live ? 1.0f : 0.0f);
    setParam (p, "liveProfile", m.extendedRange ? 1.0f : 0.0f);
    if (! std::getenv ("LM_NO_XL")) setParam (p, "extremeLive", m.extreme ? 1.0f : 0.0f);
    if (! std::getenv ("LM_NO_NP")) setParam (p, "neuralPitch", 0.0f);
    setParam (p, "autoKey", 0.0f);
    setParam (p, "followSongKey", 0.0f);
    setParam (p, "enableCorrection", 1.0f);
    setParam (p, "doNoHarm", 0.0f);
}

// The contract's steady-tone fixture: Classic, 100 %, fastest, manual C chromatic.
void configureTone (Proc& p)
{
    setParam (p, "keyRoot", 0.0f);
    setParam (p, "scale", 2.0f);
    setParam (p, "scaleMode", 2.0f);
    setParam (p, "style", 1.0f);
    setParam (p, "amount", 1.0f);
    setParam (p, "speed", 1.0f);
    setParam (p, "formantPreserve", 1.0f);
    setParam (p, "classicMode", 0.0f);
    // Sound Source "Wide" (55-1100 Hz): the default "Alto / Tenor" (90-850 Hz) leaves
    // Studio blind to E2 by design; Wide is the setting that claims the E2-C5 span.
    if (! std::getenv ("LM_NO_VR")) setParam (p, "vocalRange", 0.0f);
}

// A realistic vocal setting: C major, Natural, 100 %, speed 0.9 (the objective bench's CPU setting).
void configureVocal (Proc& p)
{
    setParam (p, "keyRoot", 0.0f);
    setParam (p, "scale", 0.0f);
    setParam (p, "scaleMode", 0.0f);
    setParam (p, "style", 0.0f);
    setParam (p, "amount", 1.0f);
    setParam (p, "speed", 0.9f);
    setParam (p, "classicMode", 0.0f);
}

// --- material -----------------------------------------------------------------------------
using Mono = std::vector<float>;

// Synthetic vocal (identical to AutoPitchNoHarm's VocalGen, 11.3 s): C4 +30 c with vibrato,
// a glide, a 20-note detuned C-major melody, silences and a -72 dBFS noise floor. With
// `bursts`, every melody note and phrase start also gets a 4 ms decaying white-noise
// "consonant" (peak 0.25): hard, broadband transients inside real phrasing.
Mono syntheticVocal (double sr, bool bursts)
{
    static const int deg[20]    = { 0, 2, 4, 5, 7, 5, 4, 2, 0, 4, 7, 9, 7, 5, 4, 2, 4, 2, 0, 0 };
    static const int detune[20] = { 18, -12, 25, -20, 8, -5, 15, -22, 10, -15, 20, -8, 5, -18, 12, -10, 22, -6, 14, -3 };
    const double period = 11.3;
    const long n = (long) std::round (sr * period);
    Mono out ((size_t) n);
    Rng noise (0xC0FFEEull), burstNoise (0xB0257ull);
    auto gate = [] (double t, double dur, double attack, double release)
    {
        if (t < 0.0 || t >= dur) return 0.0;
        return juce::jlimit (0.0, 1.0, std::min (t / attack, (dur - t) / release));
    };
    std::vector<double> burstTimes { 0.0, 2.4 };
    for (int k = 0; k < 20; ++k) burstTimes.push_back (4.8 + 0.3 * k);
    const double twoPi = juce::MathConstants<double>::twoPi;
    double phase = 0.0;
    for (long i = 0; i < n; ++i)
    {
        const double tp = (double) i / sr;
        double midi = 57.0, env = 0.0;
        if (tp < 2.0) { midi = 60.30 + 0.25 * std::sin (twoPi * 5.5 * tp); env = gate (tp, 2.0, 0.012, 0.03); }
        else if (tp >= 2.4 && tp < 4.4)
        {
            const double u = tp - 2.4;
            midi = 55.0 + 7.0 * (u / 2.0) + 0.15 * std::sin (twoPi * 5.0 * u);
            env = gate (u, 2.0, 0.012, 0.03);
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
        phase += twoPi * midiToHz (midi) / sr;
        const double s = std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)
                       + 0.15 * std::sin (4.0 * phase) + 0.08 * std::sin (5.0 * phase);
        double y = 0.16 * env * s + 2.5e-4 * noise.bipolar();
        if (bursts)
            for (double bt : burstTimes)
            {
                const double u = tp - bt;
                if (u >= 0.0 && u < 0.004)
                    y += 0.25 * std::exp (-u / 0.0012) * burstNoise.bipolar();
            }
        out[(size_t) i] = (float) y;
    }
    return out;
}

std::vector<double> burstTimesSeconds()
{
    std::vector<double> t { 0.0, 2.4 };
    for (int k = 0; k < 20; ++k) t.push_back (4.8 + 0.3 * k);
    return t;
}

// Phase-continuous sine following midi(t), amplitude 0.18.
Mono sineTone (double sr, double seconds, const std::function<double (double)>& midiAt)
{
    Mono b ((size_t) std::round (sr * seconds));
    double phase = 0.0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        b[i] = 0.18f * (float) std::sin (phase);
        phase += juce::MathConstants<double>::twoPi * midiToHz (midiAt ((double) i / sr)) / sr;
    }
    return b;
}

Mono loadWavMono (const std::string& path, double targetSr, double maxSeconds)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (path)));
    if (r == nullptr) { std::fprintf (stderr, "cannot read %s\n", path.c_str()); std::exit (2); }
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * maxSeconds));
    juce::AudioBuffer<float> b ((int) r->numChannels, n);
    r->read (&b, 0, n, 0, true, true);
    Mono mono ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    if (std::abs (r->sampleRate - targetSr) < 0.5) return mono;
    const double ratio = r->sampleRate / targetSr;   // input samples per output sample
    Mono out ((size_t) std::floor ((double) n / ratio) - 4);
    juce::LagrangeInterpolator interp;
    interp.process (ratio, mono.data(), out.data(), (int) out.size());
    return out;
}

// --- rendering ----------------------------------------------------------------------------
std::string gDumpDir;   // --dump-dir: write every render as <tag>.wav (ch0 = output L, ch1 = input)

void dumpWav (const std::string& tag, const std::vector<float>& in, const std::vector<float>& out, double sr)
{
    if (gDumpDir.empty()) return;
    juce::AudioBuffer<float> b (2, (int) out.size());
    b.clear();
    for (size_t i = 0; i < out.size(); ++i) { b.setSample (0, (int) i, out[i]); if (i < in.size()) b.setSample (1, (int) i, in[i]); }
    const juce::File f (juce::String (gDumpDir) + "/" + tag + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 32, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Render
{
    std::vector<Mono> out;     // per output channel
    int latency = -1;
    int latencyChanges = 0;
    bool finite = true;
    float peak = 0.0f;
};

Render render (Proc& p, const Mono& in, double sr, int block)
{
    p.setNonRealtime (false);
    if (! std::getenv ("LM_NO_RATEDETAILS")) p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    drainMessageThread (p);
    const int mainIn = p.getMainBusNumInputChannels();
    const int outCh = p.getTotalNumOutputChannels();
    const int bufCh = std::getenv ("LM_2CH") ? outCh : juce::jmax (p.getTotalNumInputChannels(), outCh);
    if (std::getenv ("LM_VIB_FIRST")) std::printf ("layout: totalIn %d totalOut %d mainIn %d bufCh %d\n", p.getTotalNumInputChannels(), outCh, mainIn, bufCh);
    if (std::getenv ("AP_DUMP_PARAMS"))
        for (auto* prm : p.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                std::fprintf (stderr, "PARAM %s=%g\n", rp->getParameterID().toRawUTF8(), (double) rp->convertFrom0to1 (rp->getValue()));
    Render r;
    r.out.assign ((size_t) outCh, Mono (in.size(), 0.0f));
    r.latency = p.getLatencySamples();
    juce::AudioBuffer<float> buf (bufCh, block);
    const long total = (long) in.size();
    for (long pos = 0; pos < total; pos += block)
    {
        const int len = (int) std::min<long> (block, total - pos);
        buf.clear();
        for (int ch = 0; ch < mainIn; ++ch)
            std::memcpy (buf.getWritePointer (ch), in.data() + pos, sizeof (float) * (size_t) len);
        juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), bufCh, len);
        drainMessageThread (p);
        juce::MidiBuffer midi;
        p.processBlock (slice, midi);
        for (int ch = 0; ch < outCh; ++ch)
        {
            const float* d = slice.getReadPointer (ch);
            for (int i = 0; i < len; ++i)
            {
                if (! std::isfinite (d[i])) r.finite = false;
                else r.peak = std::max (r.peak, std::abs (d[i]));
                r.out[(size_t) ch][(size_t) (pos + i)] = std::isfinite (d[i]) ? d[i] : 0.0f;
            }
        }
        if (p.getLatencySamples() != r.latency) ++r.latencyChanges;
    }
    return r;
}

// --- analysis ------------------------------------------------------------------------------
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * juce::MathConstants<double>::pi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

// Centred second difference: d[i] = x[i+1] - 2 x[i] + x[i-1] (no timing bias).
Mono secondDiff (const Mono& x)
{
    Mono d (x.size(), 0.0f);
    for (size_t i = 1; i + 1 < x.size(); ++i) d[i] = x[i + 1] - 2.0f * x[i] + x[i - 1];
    return d;
}

// Amplitude envelope: |x| through a 2 ms one-pole, then mean removed. Pitch shifting
// changes the waveform but not the envelope, so this aligns phrase and note onsets
// without locking onto a neighbouring pitch period of strongly periodic material.
Mono envelope (const Mono& x, double sr)
{
    Mono e (x.size());
    const double a = 1.0 - std::exp (-1.0 / (0.002 * sr));
    double s = 0.0, sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i) { s += a * (std::abs ((double) x[i]) - s); e[i] = (float) s; sum += s; }
    const float mean = (float) (sum / (double) std::max<size_t> (1, x.size()));
    for (auto& v : e) v -= mean;
    return e;
}

// Lag (samples, fractional) maximising sum x[n] * y[n + lag] over lag in [lo, hi].
double xcorrLag (const Mono& x, const Mono& y, long lo, long hi, double* peakNorm = nullptr)
{
    const size_t n = std::max (x.size(), y.size());
    size_t size = 1;
    while (size < n + (size_t) std::max (std::abs (lo), std::abs (hi)) + 1) size <<= 1;
    std::vector<std::complex<double>> X (size), Y (size);
    for (size_t i = 0; i < x.size(); ++i) X[i] = x[i];
    for (size_t i = 0; i < y.size(); ++i) Y[i] = y[i];
    fft (X, false);
    fft (Y, false);
    for (size_t i = 0; i < size; ++i) X[i] = std::conj (X[i]) * Y[i];
    fft (X, true);
    auto at = [&] (long lag) { return X[(size_t) ((lag % (long) size + (long) size) % (long) size)].real(); };
    long best = lo;
    for (long l = lo; l <= hi; ++l)
        if (at (l) > at (best)) best = l;
    double frac = 0.0;
    const double a = at (best - 1), b = at (best), c = at (best + 1);
    const double den = a - 2.0 * b + c;
    if (std::abs (den) > 1e-30) frac = juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
    if (peakNorm)
    {
        double ex = 0.0, ey = 0.0;
        for (float v : x) ex += (double) v * v;
        for (float v : y) ey += (double) v * v;
        *peakNorm = (ex > 0.0 && ey > 0.0) ? b / std::sqrt (ex * ey) : 0.0;
    }
    return (double) best + frac;
}

// Arrival of each event: argmax |d2 out| in [t + lo, t + hi] minus t (samples).
std::vector<double> eventLags (const Mono& outD2, const std::vector<long>& events, long lo, long hi)
{
    std::vector<double> lags;
    for (long e : events)
    {
        long best = -1;
        float bestV = 0.0f;
        for (long i = std::max (0L, e + lo); i <= e + hi && i < (long) outD2.size(); ++i)
            if (std::abs (outD2[(size_t) i]) > bestV) { bestV = std::abs (outD2[(size_t) i]); best = i; }
        if (best >= 0) lags.push_back ((double) (best - e));
    }
    return lags;
}

double median (std::vector<double> v)
{
    if (v.empty()) return std::nan ("");
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

double pctile (std::vector<double> v, double q)
{
    if (v.empty()) return std::nan ("");
    std::sort (v.begin(), v.end());
    return v[(size_t) juce::jlimit (0.0, (double) v.size() - 1.0, std::floor (q * (double) v.size()))];
}

// Upward zero crossings (fractional sample positions) in [from, to).
std::vector<double> crossings (const Mono& y, long from, long to)
{
    std::vector<double> c;
    for (long i = std::max (from, 1L); i < to && i < (long) y.size(); ++i)
        if (y[(size_t) i - 1] < 0.0f && y[(size_t) i] >= 0.0f)
        {
            const double den = (double) y[(size_t) i] - (double) y[(size_t) i - 1];
            c.push_back ((double) (i - 1) + (std::abs (den) > 1e-12 ? -(double) y[(size_t) i - 1] / den : 0.0));
        }
    return c;
}

// Instantaneous pitch track: (time s, midi), each estimate spans K periods.
std::vector<std::pair<double, double>> pitchTrack (const Mono& y, long from, long to, double sr, int K = 4)
{
    const auto c = crossings (y, from, to);
    std::vector<std::pair<double, double>> t;
    for (size_t i = 0; i + (size_t) K < c.size(); ++i)
    {
        const double span = c[i + (size_t) K] - c[i];
        if (span <= 0.0) continue;
        t.emplace_back (0.5 * (c[i] + c[i + (size_t) K]) / sr, hzToMidi (sr * K / span));
    }
    return t;
}

// Settled pitch over [from, to): 10 %-trimmed mean of the 4-period track. Not a plain
// crossing count: one splice discontinuity near a zero crossing adds a spurious
// crossing, which biases a count-based estimate by tens of cents although the note is
// right. Trimming drops those outliers; the mean keeps the perceived (average) pitch.
double trimmedMeanMidi (const Mono& y, long from, long to, double sr)
{
    const auto t = pitchTrack (y, from, to, sr);
    std::vector<double> m;
    for (const auto& e : t) m.push_back (e.second);
    if (m.size() < 5) return std::nan ("");
    std::sort (m.begin(), m.end());
    const size_t cut = m.size() / 10;
    double sum = 0.0;
    for (size_t i = cut; i < m.size() - cut; ++i) sum += m[i];
    return sum / (double) (m.size() - 2 * cut);
}

// 5-point running median of a pitch track (same robustness argument, for settling).
std::vector<std::pair<double, double>> medianFiltered (const std::vector<std::pair<double, double>>& t)
{
    auto out = t;
    for (size_t i = 2; i + 2 < t.size(); ++i)
    {
        double w[5] = { t[i - 2].second, t[i - 1].second, t[i].second, t[i + 1].second, t[i + 2].second };
        std::sort (w, w + 5);
        out[i].second = w[2];
    }
    return out;
}

// Max second difference over [from, to).
double maxCurvature (const Mono& y, long from, long to)
{
    double worst = 0.0;
    for (long i = std::max (from, 2L); i < to && i < (long) y.size(); ++i)
        worst = std::max (worst, (double) std::abs (y[(size_t) i] - 2.0f * y[(size_t) i - 1] + y[(size_t) i - 2]));
    return worst;
}

float peakAbs (const Mono& y, long from, long to)
{
    float p = 0.0f;
    for (long i = std::max (from, 0L); i < to && i < (long) y.size(); ++i) p = std::max (p, std::abs (y[(size_t) i]));
    return p;
}

// Longest run (samples) of |y| < 1e-6 inside [from, to).
long longestZeroRun (const Mono& y, long from, long to)
{
    long run = 0, worst = 0;
    for (long i = std::max (from, 0L); i < to && i < (long) y.size(); ++i)
    {
        run = std::abs (y[(size_t) i]) < 1.0e-6f ? run + 1 : 0;
        worst = std::max (worst, run);
    }
    return worst;
}

double maxAbsDiff (const Mono& a, const Mono& b)
{
    double d = 0.0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) d = std::max (d, (double) std::abs (a[i] - b[i]));
    return d;
}

// --- CPU ---------------------------------------------------------------------------------------
double threadCpuUs()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double) ts.tv_sec * 1.0e6 + (double) ts.tv_nsec / 1.0e3;
}

juce::var cpuMeasure (const Mode& m, const Mono& material, double sr, int block, double seconds, int trials)
{
    Proc p;
    configureMode (p, m);
    configureVocal (p);
    p.setNonRealtime (false);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    drainMessageThread (p);
    const int mainIn = p.getMainBusNumInputChannels();
    const int bufCh = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (bufCh, block);
    long pos = 0;
    const long matN = (long) material.size();
    auto one = [&] (double* cpuUs, double* wallUs)
    {
        buf.clear();
        for (int ch = 0; ch < mainIn; ++ch)
        {
            float* d = buf.getWritePointer (ch);
            for (int i = 0; i < block; ++i) d[i] = material[(size_t) ((pos + i) % matN)];
        }
        pos += block;
        drainMessageThread (p);
        juce::MidiBuffer midi;
        const double c0 = threadCpuUs();
        const auto w0 = Clock::now();
        p.processBlock (buf, midi);
        const auto w1 = Clock::now();
        const double c1 = threadCpuUs();
        if (cpuUs) *cpuUs = c1 - c0;
        if (wallUs) *wallUs = std::chrono::duration<double, std::micro> (w1 - w0).count();
    };
    // Warm-up: 1.5 s of audio (engines primed, caches and branch predictors hot).
    for (long b = 0; b < (long) (1.5 * sr / block); ++b) one (nullptr, nullptr);

    const double budgetUs = 1.0e6 * block / sr;
    const long blocksPerTrial = (long) std::ceil (seconds * sr / block);
    juce::Array<juce::var> trialArr;
    std::vector<double> p99s, maxs, means, wallP99s, wallMaxs;
    for (int t = 0; t < trials; ++t)
    {
        std::vector<double> cpu ((size_t) blocksPerTrial), wall ((size_t) blocksPerTrial);
        for (long b = 0; b < blocksPerTrial; ++b) one (&cpu[(size_t) b], &wall[(size_t) b]);
        double sum = 0.0;
        for (double v : cpu) sum += v;
        const double mean = sum / (double) cpu.size();
        const double p99 = pctile (cpu, 0.99), mx = *std::max_element (cpu.begin(), cpu.end());
        const double wp99 = pctile (wall, 0.99), wmx = *std::max_element (wall.begin(), wall.end());
        long over = 0;
        for (double v : cpu) over += v > budgetUs;
        auto* o = new juce::DynamicObject();
        o->setProperty ("mean_us", mean);
        o->setProperty ("p50_us", pctile (cpu, 0.5));
        o->setProperty ("p99_us", p99);
        o->setProperty ("max_us", mx);
        o->setProperty ("wall_p99_us", wp99);
        o->setProperty ("wall_max_us", wmx);
        o->setProperty ("blocks", (int) blocksPerTrial);
        o->setProperty ("blocks_over_budget", (int) over);
        trialArr.add (juce::var (o));
        p99s.push_back (p99); maxs.push_back (mx); means.push_back (mean);
        wallP99s.push_back (wp99); wallMaxs.push_back (wmx);
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("budget_us", budgetUs);
    o->setProperty ("trials", trialArr);
    o->setProperty ("mean_us", median (means));
    o->setProperty ("p99_us", median (p99s));
    o->setProperty ("max_us", median (maxs));
    o->setProperty ("wall_p99_us", median (wallP99s));
    o->setProperty ("wall_max_us", median (wallMaxs));
    o->setProperty ("rtf", median (means) / budgetUs);
    o->setProperty ("p99_pct", 100.0 * median (p99s) / budgetUs);
    o->setProperty ("max_pct", 100.0 * median (maxs) / budgetUs);
    o->setProperty ("instances_at_p99", (int) std::floor (budgetUs / std::max (1e-9, median (p99s))));
    o->setProperty ("instances_at_mean", (int) std::floor (budgetUs / std::max (1e-9, median (means))));
    return juce::var (o);
}

// --- one configuration ------------------------------------------------------------------------
struct Shared   // per (mode, sr): references for the buffer-dependence comparison
{
    int refBlock = -1;
    Mono vocalRef, toneRef;
};

double safe (double v) { return std::isfinite (v) ? v : -1.0; }

juce::var runConfig (const Mode& m, double sr, int block, const std::string& vocalPath,
                     double cpuSeconds, int cpuTrials, Shared& shared)
{
    const auto t0 = Clock::now();
    auto* row = new juce::DynamicObject();
    row->setProperty ("mode", juce::String (m.key));
    row->setProperty ("label", juce::String (m.label));
    row->setProperty ("sr", sr);
    row->setProperty ("block", block);

    bool finite = true;
    float peak = 0.0f;
    int latencyChanges = 0;
    int reported = -1;
    auto track = [&] (const Render& r)
    {
        finite = finite && r.finite;
        peak = std::max (peak, r.peak);
        latencyChanges += r.latencyChanges;
        reported = r.latency;
    };
    const std::string tagBase = fmt ("%s_%.0f_%d_", m.key, sr, block);
    auto toneProc = [&] (const Mono& in, const char* name)
    {
        Proc p;
        configureMode (p, m);
        configureTone (p);
        auto r = render (p, in, sr, block);
        track (r);
        dumpWav (tagBase + name, in, r.out[0], sr);
        return r;
    };
    auto vocalProc = [&] (const Mono& in, const char* name)
    {
        Proc p;
        configureMode (p, m);
        configureVocal (p);
        auto r = render (p, in, sr, block);
        track (r);
        dumpWav (tagBase + name, in, r.out[0], sr);
        return r;
    };
    const double ms = 1000.0 / sr;

    // ---- latency -----------------------------------------------------------------------
    if (std::getenv ("LM_VIB_FIRST"))   // diagnostic: the vibrato case alone, first in a fresh process
    {
        const auto in = sineTone (sr, 3.0, [] (double t) { return 57.0 + 0.40 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t); });
        const auto r = toneProc (in, "vibrato_first");
        const long n = (long) r.out[0].size();
        const auto tr = pitchTrack (r.out[0], n - (long) (2.0 * sr), n, sr);
        std::vector<double> err;
        for (const auto& e : tr) err.push_back (std::abs (e.second - 57.0) * 100.0);
        std::printf ("VIB_FIRST sr %.0f block %d p95 %.1f c\n", sr, block, pctile (err, 0.95));
    }
    auto* lat = new juce::DynamicObject();
    {
        // (a) Broadband clicks on silence: single-sample impulses, 0.5, every 0.3 s.
        Mono clicks ((size_t) std::round (3.0 * sr), 0.0f);
        std::vector<long> ev;
        for (int k = 0; k < 8; ++k) { const long at = (long) std::round ((0.25 + 0.3 * k) * sr); clicks[(size_t) at] = 0.5f; ev.push_back (at); }
        const auto r = toneProc (clicks, "clicks_silence");
        const long lo = -(long) (0.005 * sr), hi = r.latency + (long) (0.06 * sr);
        const auto lags = eventLags (secondDiff (r.out[0]), ev, lo, hi);
        const double xl = xcorrLag (clicks, r.out[0], lo, hi);
        lat->setProperty ("reported_samples", r.latency);
        lat->setProperty ("reported_ms", r.latency * ms);
        lat->setProperty ("click_silence_xcorr_ms", xl * ms);
        lat->setProperty ("click_silence_median_ms", median (lags) * ms);
        lat->setProperty ("click_silence_min_ms", lags.empty() ? -1.0 : *std::min_element (lags.begin(), lags.end()) * ms);
        lat->setProperty ("click_silence_max_ms", lags.empty() ? -1.0 : *std::max_element (lags.begin(), lags.end()) * ms);
    }
    {
        // (b) Clicks riding a voiced, corrected tone (B3 +45 c): the shifter is engaged.
        const double hz = midiToHz (59.45);
        Mono in ((size_t) std::round (3.5 * sr));
        std::vector<long> ev;
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.18f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sr);
        for (int k = 0; k < 8; ++k) { const long at = (long) std::round ((0.9 + 0.3 * k) * sr); in[(size_t) at] += 0.5f; ev.push_back (at); }
        const auto r = toneProc (in, "clicks_tone");
        const long lo = -(long) (0.005 * sr), hi = r.latency + (long) (0.06 * sr);
        const auto lags = eventLags (secondDiff (r.out[0]), ev, lo, hi);
        lat->setProperty ("click_tone_median_ms", median (lags) * ms);
        lat->setProperty ("click_tone_min_ms", lags.empty() ? -1.0 : *std::min_element (lags.begin(), lags.end()) * ms);
        lat->setProperty ("click_tone_max_ms", lags.empty() ? -1.0 : *std::max_element (lags.begin(), lags.end()) * ms);
    }
    Mono vocalClean;
    {
        // (c) Vocal: whole-signal cross-correlation (as the 2026-09-25 real-vocal timing
        // check), high-passed (second difference) cross-correlation, and per-consonant
        // arrivals for the synthetic vocal's bursts.
        const bool wav = ! vocalPath.empty();
        const Mono in = wav ? loadWavMono (vocalPath, sr, 20.0) : syntheticVocal (sr, true);
        vocalClean = wav ? in : syntheticVocal (sr, false);
        const auto r = vocalProc (in, "vocal_bursts");
        const long lo = -(long) (0.02 * sr), hi = r.latency + (long) (0.1 * sr);
        double norm = 0.0, normHf = 0.0;
        const double whole = xcorrLag (in, r.out[0], lo, hi, &norm);
        const auto inD2 = secondDiff (in), outD2 = secondDiff (r.out[0]);
        const double hf = xcorrLag (inD2, outD2, lo, hi, &normHf);
        double normEnv = 0.0;
        const double env = xcorrLag (envelope (in, sr), envelope (r.out[0], sr), lo, hi, &normEnv);
        lat->setProperty ("vocal_env_xcorr_ms", env * ms);
        lat->setProperty ("vocal_env_xcorr_peak_norm", normEnv);
        lat->setProperty ("vocal_source", wav ? juce::String (vocalPath) : juce::String ("synthetic (VocalGen + consonant bursts)"));
        lat->setProperty ("vocal_xcorr_ms", whole * ms);
        lat->setProperty ("vocal_xcorr_peak_norm", norm);
        lat->setProperty ("vocal_hf_xcorr_ms", hf * ms);
        lat->setProperty ("vocal_hf_xcorr_peak_norm", normHf);
        if (! wav)
        {
            std::vector<long> ev;
            for (double t : burstTimesSeconds()) ev.push_back ((long) std::round (t * sr));
            const auto lags = eventLags (outD2, ev, -(long) (0.005 * sr), r.latency + (long) (0.06 * sr));
            lat->setProperty ("vocal_burst_median_ms", median (lags) * ms);
            lat->setProperty ("vocal_burst_max_ms", lags.empty() ? -1.0 : *std::max_element (lags.begin(), lags.end()) * ms);
        }
    }
    row->setProperty ("latency", juce::var (lat));

    // ---- accuracy + artifacts ---------------------------------------------------------------
    auto* acc = new juce::DynamicObject();
    auto* art = new juce::DynamicObject();
    double worstCurvRatio = 0.0, worstStepDb = -200.0, worstEventsPerS = 0.0;
    std::string worstCurvWhere, worstStepWhere;
    long worstZeroRun = 0;
    std::string worstZeroWhere;
    // Artifact scan on a continuous-tone render: region after latency + 150 ms.
    auto artifactScan = [&] (const char* name, const Render& r, double maxTargetMidi)
    {
        const long from = r.latency + (long) (0.15 * sr);
        const long to = (long) r.out[0].size();
        for (size_t ch = 0; ch < r.out.size(); ++ch)
        {
            const long z = longestZeroRun (r.out[ch], from, to);
            if (z > worstZeroRun) { worstZeroRun = z; worstZeroWhere = name; }
        }
        // Ideal curvature of a sine of the output's amplitude at the highest target
        // (+50 c allowance): A * (2 pi f / sr)^2. A click or splice shows as a multiple.
        // The multiple depends on f and SR (a sample-level step is a bigger multiple of a
        // 96 kHz sine's curvature), so the gate uses the step level instead: the largest
        // second difference relative to the output peak, in dB. A step discontinuity of
        // size s shows as a second difference of ~s, so -20 dB = a jump of 10 % of the
        // note's amplitude. A clean sine sits at 20 log10 ((2 pi f / sr)^2): -45 dB for
        // C5 at 44.1 kHz, lower for everything else here.
        const double f = midiToHz (maxTargetMidi + 0.5);
        const double A = peakAbs (r.out[0], from, to);
        const double ideal = A * std::pow (juce::MathConstants<double>::twoPi * f / sr, 2.0);
        const double curv = maxCurvature (r.out[0], from, to);
        const double ratio = ideal > 0.0 ? curv / ideal : 0.0;
        const double stepDb = A > 0.0 && curv > 0.0 ? 20.0 * std::log10 (curv / A) : -200.0;
        // Splice events: second difference above 1 % of the peak (-40 dB), grouped when
        // closer than 1 ms, counted per second of scanned audio.
        long events = 0, lastEvent = -1000000;
        const float* y = r.out[0].data();
        for (long i = std::max (from, 2L); i < to; ++i)
            if (std::abs (y[i] - 2.0f * y[i - 1] + y[i - 2]) > 0.01f * (float) A)
            {
                if (i - lastEvent > (long) (0.001 * sr)) ++events;
                lastEvent = i;
            }
        const double scanned = (double) std::max (1L, to - from) / sr;
        art->setProperty (juce::String ("curv_ratio_") + name, ratio);
        art->setProperty (juce::String ("step_db_") + name, stepDb);
        art->setProperty (juce::String ("events_per_s_") + name, (double) events / scanned);
        if (stepDb > worstStepDb) { worstStepDb = stepDb; worstStepWhere = name; worstEventsPerS = (double) events / scanned; }
        if (ratio > worstCurvRatio) { worstCurvRatio = ratio; worstCurvWhere = name; }
    };
    Render b3r;
    {
        const auto in = sineTone (sr, 3.0, [] (double) { return 59.45; });
        b3r = toneProc (in, "b3");
        const long n = (long) b3r.out[0].size();
        acc->setProperty ("b3_cents", safe (std::abs (trimmedMeanMidi (b3r.out[0], n - (long) (0.5 * sr), n, sr) - 59.0) * 100.0));
        artifactScan ("b3", b3r, 59.45);
    }
    {
        const double low = m.lowMidi + 0.30;   // +30 c -> corrects down to the note
        const auto in = sineTone (sr, 3.0, [low] (double) { return low; });
        const auto r = toneProc (in, "low");
        const long n = (long) r.out[0].size();
        acc->setProperty ("low_note", m.lowMidi == 40.0 ? "E2+30c" : "C3+30c");
        acc->setProperty ("low_cents", safe (std::abs (trimmedMeanMidi (r.out[0], n - (long) (1.0 * sr), n, sr) - m.lowMidi) * 100.0));
        artifactScan ("low", r, low);
    }
    {
        const auto in = sineTone (sr, 3.0, [] (double) { return 71.70; });   // C5 -30 c
        const auto r = toneProc (in, "high");
        const long n = (long) r.out[0].size();
        acc->setProperty ("high_cents", safe (std::abs (trimmedMeanMidi (r.out[0], n - (long) (0.5 * sr), n, sr) - 72.0) * 100.0));
        artifactScan ("high", r, 72.0);
    }
    {
        // Vibrato: A3, 5.5 Hz +-40 c (never crosses the +-50 c decision boundary).
        const auto in = sineTone (sr, 3.0, [] (double t) { return 57.0 + 0.40 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t); });
        const auto r = toneProc (in, "vibrato");
        const long n = (long) r.out[0].size();
        const auto tr = pitchTrack (r.out[0], n - (long) (2.0 * sr), n, sr);
        std::vector<double> err;
        for (const auto& e : tr) err.push_back (std::abs (e.second - 57.0) * 100.0);
        acc->setProperty ("vibrato_median_cents", safe (median (err)));
        acc->setProperty ("vibrato_p95_cents", safe (pctile (err, 0.95)));
        acc->setProperty ("vibrato_max_cents", err.empty() ? -1.0 : *std::max_element (err.begin(), err.end()));
        artifactScan ("vibrato", r, 57.4);
    }
    auto settle = [&] (const char* key, double fromMidi, double toMidi)
    {
        // Step at 1.0 s (input timeline); settled = the last time the output pitch is
        // outside +-10 c of the new target, measured from the input step. Includes latency.
        const double stepT = 1.0, len = 2.0;
        const auto in = sineTone (sr, len, [=] (double t) { return t < stepT ? fromMidi + 0.2 : toMidi + 0.2; });
        const auto r = toneProc (in, key);
        const auto tr = medianFiltered (pitchTrack (r.out[0], (long) (stepT * sr), (long) r.out[0].size(), sr));
        double lastOut = stepT;
        bool everIn = false;
        for (const auto& e : tr)
        {
            const bool inBand = std::abs (e.second - toMidi) * 100.0 <= 10.0;
            everIn = everIn || inBand;
            if (! inBand) lastOut = e.first;
        }
        const bool settled = everIn && ! tr.empty() && lastOut < tr.back().first - 0.05;
        acc->setProperty (juce::String (key) + "_settle_ms", settled ? (lastOut - stepT) * 1000.0 : -1.0);
        acc->setProperty (juce::String (key) + "_settle_after_latency_ms",
                          settled ? (lastOut - stepT) * 1000.0 - r.latency * ms : -1.0);
        artifactScan (key, r, std::max (fromMidi, toMidi) + 0.2);
    };
    settle ("semitone", 57.0, 58.0);
    settle ("fifth", 57.0, 64.0);

    // Note-change click rate. One step samples one waveform phase at the splice; a
    // train of 12 note changes (semitones, tones, thirds, fourths, fifths, up and down,
    // 0.25 s notes, +20 c) samples many. For each change: the largest second difference
    // in the output window [change + latency - 10 ms, + 120 ms], in dB re the peak.
    {
        static const double notes[13] = { 57, 64, 57, 58, 57, 62, 57, 59, 57, 64, 60, 57, 55 };
        static constexpr double noteLen = 0.25;
        const auto in = sineTone (sr, noteLen * 13, [] (double t)
        { return notes[juce::jlimit (0, 12, (int) (t / noteLen))] + 0.2; });
        const auto r = toneProc (in, "transitions");
        const float* y = r.out[0].data();
        const long n = (long) r.out[0].size();
        const double A = peakAbs (r.out[0], r.latency + (long) (0.15 * sr), n);
        juce::Array<juce::var> perChange;
        int over20 = 0, over30 = 0;
        double worst = -200.0;
        for (int k = 1; k < 13; ++k)
        {
            const long c = (long) std::round (k * noteLen * sr) + r.latency;
            double mx = 0.0;
            for (long i = std::max (2L, c - (long) (0.010 * sr)); i < std::min (n, c + (long) (0.120 * sr)); ++i)
                mx = std::max (mx, (double) std::abs (y[i] - 2.0f * y[i - 1] + y[i - 2]));
            const double db = A > 0.0 && mx > 0.0 ? 20.0 * std::log10 (mx / A) : -200.0;
            perChange.add (db);
            over20 += db >= -20.0;
            over30 += db >= -30.0;
            worst = std::max (worst, db);
        }
        art->setProperty ("transition_step_db", perChange);
        art->setProperty ("transition_clicks_20db", over20);
        art->setProperty ("transition_clicks_30db", over30);
        art->setProperty ("transition_worst_db", worst);
        art->setProperty ("transition_count", 12);
    }

    // Vocal (no bursts) for the click measure relative to the clean input, and for
    // buffer dependence.
    Render vr = vocalProc (vocalClean, "vocal");
    {
        const long L = vr.latency;
        const double inCurv = maxCurvature (vocalClean, 0, (long) vocalClean.size());
        const double outCurv = maxCurvature (vr.out[0], L, (long) vr.out[0].size());
        const double inPeak = peakAbs (vocalClean, 0, (long) vocalClean.size());
        const double outPeak = peakAbs (vr.out[0], L, (long) vr.out[0].size());
        art->setProperty ("vocal_curv_ratio", inCurv > 0.0 ? outCurv / inCurv : 0.0);
        // Same SR-independent step level as the tones (the ratio above grows ~4x from
        // 48 to 96 kHz for the same sample-level splice, because the input's own
        // curvature shrinks with (f / sr)^2).
        art->setProperty ("vocal_step_db", outPeak > 0.0 && outCurv > 0.0 ? 20.0 * std::log10 (outCurv / outPeak) : -200.0);
        art->setProperty ("vocal_input_step_db", inPeak > 0.0 && inCurv > 0.0 ? 20.0 * std::log10 (inCurv / inPeak) : -200.0);
        double lr = 0.0;
        if (vr.out.size() > 1) lr = maxAbsDiff (vr.out[0], vr.out[1]);
        art->setProperty ("vocal_lr_maxdiff", lr);
    }
    art->setProperty ("finite", finite);
    art->setProperty ("peak", peak);
    art->setProperty ("worst_curv_ratio", worstCurvRatio);
    art->setProperty ("worst_curv_where", juce::String (worstCurvWhere));
    art->setProperty ("worst_step_db", worstStepDb);
    art->setProperty ("worst_step_where", juce::String (worstStepWhere));
    art->setProperty ("worst_step_events_per_s", worstEventsPerS);
    art->setProperty ("longest_zero_run_ms", worstZeroRun * ms);
    art->setProperty ("longest_zero_run_where", juce::String (worstZeroWhere));
    row->setProperty ("accuracy", juce::var (acc));
    row->setProperty ("artifacts", juce::var (art));
    row->setProperty ("latency_changes", latencyChanges);
    row->setProperty ("reported_latency_samples", reported);

    // ---- buffer dependence ---------------------------------------------------------------------
    auto* buf = new juce::DynamicObject();
    if (shared.refBlock < 0)
    {
        shared.refBlock = block;
        shared.vocalRef = vr.out[0];
        shared.toneRef = b3r.out[0];
    }
    const double dv = maxAbsDiff (shared.vocalRef, vr.out[0]);
    const double dt = maxAbsDiff (shared.toneRef, b3r.out[0]);
    buf->setProperty ("ref_block", shared.refBlock);
    buf->setProperty ("vocal_maxdiff", dv);
    buf->setProperty ("tone_maxdiff", dt);
    buf->setProperty ("identical", dv == 0.0 && dt == 0.0);
    row->setProperty ("buffer", juce::var (buf));

    // ---- CPU ---------------------------------------------------------------------------------------
    if (cpuSeconds > 0.0)
        row->setProperty ("cpu", cpuMeasure (m, vocalClean, sr, block, cpuSeconds, cpuTrials));
    row->setProperty ("seconds", std::chrono::duration<double> (Clock::now() - t0).count());
    return juce::var (row);
}

// JSON has no NaN/Inf: replace non-finite numbers with null before writing.
juce::var sanitize (const juce::var& v)
{
    if (v.isDouble()) return std::isfinite ((double) v) ? v : juce::var();
    if (auto* arr = v.getArray())
    {
        juce::Array<juce::var> out;
        for (const auto& e : *arr) out.add (sanitize (e));
        return out;
    }
    if (auto* obj = v.getDynamicObject())
    {
        for (auto& prop : obj->getProperties())
            obj->setProperty (prop.name, sanitize (prop.value));
        return v;
    }
    return v;
}

// ============================================================================================
//  Gates (applied by --merge). Kept together so each verdict is traceable to one constant.
// ============================================================================================
constexpr double kGateCpuP99Pct      = 70.0;   // p99 thread-CPU time <= 70 % of the block budget
constexpr double kGateCpuMaxPct      = 100.0;  // worst block (median over trials) <= 100 %
constexpr double kGateB3Cents        = 5.0;    // steady B3 +45 c fixture (AutoPitchNoHarm check 5)
constexpr double kGateExtremeCents   = 10.0;   // E2 / C3 / C5 extremes
constexpr double kGateVibratoP95     = 50.0;   // vibrato: output p95 |pitch - A3| must not exceed the
                                               // input's own +-40 c depth by more than 10 c (no widening, no flip)
constexpr double kGateSettleAbsMs    = 300.0;  // semitone / fifth settle, from the input step
constexpr double kGateSettleRelMs    = 20.0;   // ... and no more than ref + max(20 ms, 50 %)
constexpr double kGateLatencyClickMs = 0.25;   // Live: |click-on-silence - reported| (exact dry path)
constexpr double kGateLatencyHfMs    = 0.5;    // Live: |vocal high-pass xcorr - reported|
constexpr double kGateZeroRunMs      = 1.0;    // dropout: zero run inside a continuous tone
constexpr double kGateStepFailDb     = -20.0;  // tone discontinuity >= 10 % of the note's amplitude
constexpr double kGateStepNoteDb     = -40.0;  // >= 1 %: reported as a note
constexpr double kGateStepRelDb      = 6.0;    // ... FAIL if also > ref + 6 dB (worse than the reference config)
constexpr double kGatePeak           = 1.0;

double num (const juce::var& o, const char* path)
{
    juce::var v = o;
    juce::StringArray parts;
    parts.addTokens (path, ".", "");
    for (const auto& p : parts)
    {
        if (auto* d = v.getDynamicObject()) v = d->getProperty (juce::Identifier (p));
        else return std::nan ("");
    }
    if (v.isVoid()) return std::nan ("");
    return (double) v;
}

int refBlockFor (const juce::String& mode) { return mode == "studio" ? 512 : 128; }

int merge (const std::string& outJson, const std::string& outMd, const std::vector<std::string>& frags)
{
    std::vector<juce::var> rows;
    juce::var meta;
    for (const auto& f : frags)
    {
        const auto v = juce::JSON::parse (juce::File (f));
        if (auto* d = v.getDynamicObject())
        {
            if (meta.isVoid()) meta = d->getProperty ("meta");
            if (auto* arr = d->getProperty ("rows").getArray())
                for (const auto& r : *arr) rows.push_back (r);
        }
        else { std::fprintf (stderr, "cannot parse %s\n", f.c_str()); return 2; }
    }
    const char* order[] = { "studio", "extreme", "ultra", "range" };
    auto modeIdx = [&] (const juce::String& m) { for (int i = 0; i < 4; ++i) if (m == order[i]) return i; return 9; };
    std::sort (rows.begin(), rows.end(), [&] (const juce::var& a, const juce::var& b)
    {
        const int ma = modeIdx (a["mode"].toString()), mb = modeIdx (b["mode"].toString());
        if (ma != mb) return ma < mb;
        if ((double) a["sr"] != (double) b["sr"]) return (double) a["sr"] < (double) b["sr"];
        return (int) a["block"] < (int) b["block"];
    });
    auto findRef = [&] (const juce::String& mode) -> juce::var
    {
        for (const auto& r : rows)
            if (r["mode"].toString() == mode && (double) r["sr"] == 48000.0 && (int) r["block"] == refBlockFor (mode))
                return r;
        return {};
    };

    std::string md;
    md += "| Mode | SR | Block | Verdict | Matrix | Artifacts | Reported (smp) | Click on silence | Click on voiced tone | Vocal envelope | Vocal HF | CPU mean / p99 / max (% budget) | Inst @p99 | B3 / low / C5 err (c) | Vib p95 (c) | Settle semi / fifth (ms) | Step dB tone / vocal (input) | Buffer dep. | Reasons |\n";
    md += "|---|---:|---:|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
    int fails = 0;
    juce::Array<juce::var> outRows;
    for (auto& r : rows)
    {
        const auto mode = r["mode"].toString();
        const bool live = mode != "studio";
        const auto ref = findRef (mode);
        std::vector<std::string> reasons, artReasons, notes;
        const double rep = num (r, "latency.reported_ms");
        const double clickSil = num (r, "latency.click_silence_median_ms");
        const double clickTone = num (r, "latency.click_tone_median_ms");
        const double vocHf = num (r, "latency.vocal_hf_xcorr_ms");
        const double p99 = num (r, "cpu.p99_pct"), mx = num (r, "cpu.max_pct"), mean = 100.0 * num (r, "cpu.rtf");
        const double b3 = num (r, "accuracy.b3_cents"), lowc = num (r, "accuracy.low_cents"), high = num (r, "accuracy.high_cents");
        const double vib = num (r, "accuracy.vibrato_p95_cents");
        const double semi = num (r, "accuracy.semitone_settle_ms"), fifth = num (r, "accuracy.fifth_settle_ms");
        const double vocEnv = num (r, "latency.vocal_env_xcorr_ms");
        const double zero = num (r, "artifacts.longest_zero_run_ms");
        const double peakV = num (r, "artifacts.peak");
        const bool finiteV = (bool) r["artifacts"]["finite"];

        if (! finiteV) reasons.push_back ("NaN/Inf");
        if (peakV > kGatePeak) reasons.push_back (fmt ("peak %.2f > %.1f", peakV, kGatePeak));
        if ((int) r["latency_changes"] != 0) reasons.push_back ("latency changed mid-render");
        if (zero >= kGateZeroRunMs) reasons.push_back (fmt ("dropout %.1f ms (%s)", zero, r["artifacts"]["longest_zero_run_where"].toString().toRawUTF8()));
        if (p99 > kGateCpuP99Pct) reasons.push_back (fmt ("CPU p99 %.0f%% > %.0f%%", p99, kGateCpuP99Pct));
        if (mx > kGateCpuMaxPct) reasons.push_back (fmt ("CPU max %.0f%% > %.0f%%", mx, kGateCpuMaxPct));
        if (! (b3 >= 0.0 && b3 <= kGateB3Cents)) reasons.push_back (fmt ("B3 err %.1f c", b3));
        if (! (lowc >= 0.0 && lowc <= kGateExtremeCents)) reasons.push_back (fmt ("%s err %.1f c", r["accuracy"]["low_note"].toString().toRawUTF8(), lowc));
        if (! (high >= 0.0 && high <= kGateExtremeCents)) reasons.push_back (fmt ("C5 err %.1f c", high));
        if (! (vib >= 0.0 && vib < kGateVibratoP95)) reasons.push_back (fmt ("vibrato widened: p95 %.0f c vs input 40 c", vib));
        for (auto [name, v, key] : { std::tuple<const char*, double, const char*> { "semitone", semi, "accuracy.semitone_settle_ms" },
                                     { "fifth", fifth, "accuracy.fifth_settle_ms" } })
        {
            if (! (v >= 0.0 && v <= kGateSettleAbsMs)) { reasons.push_back (fmt ("%s never settles <= %.0f ms", name, kGateSettleAbsMs)); continue; }
            const double rv = ref.isVoid() ? std::nan ("") : num (ref, key);
            if (rv >= 0.0 && v > rv + std::max (kGateSettleRelMs, 0.5 * rv))
                reasons.push_back (fmt ("%s settle %.0f ms vs ref %.0f", name, v, rv));
        }
        if (live && std::abs (clickSil - rep) > kGateLatencyClickMs) reasons.push_back (fmt ("click delay %+.2f ms vs reported", clickSil - rep));
        if (live && std::abs (vocHf - rep) > kGateLatencyHfMs) reasons.push_back (fmt ("vocal HF delay %+.2f ms vs reported", vocHf - rep));
        {
            // Artifact gates (separate verdict: they describe the engine, not the SR/buffer cell).
            const double stepDb = num (r, "artifacts.worst_step_db");
            const double refDb = ref.isVoid() ? std::nan ("") : num (ref, "artifacts.worst_step_db");
            const auto where = r["artifacts"]["worst_step_where"].toString();
            if (stepDb >= kGateStepFailDb)
                artReasons.push_back (fmt ("tone discontinuity %.0f dB re peak (%s, %.0f events/s)", stepDb, where.toRawUTF8(), num (r, "artifacts.worst_step_events_per_s")));
            else if (stepDb >= kGateStepNoteDb && refDb == refDb && stepDb > refDb + kGateStepRelDb)
                artReasons.push_back (fmt ("tone discontinuity %.0f dB (%s) vs ref %.0f dB", stepDb, where.toRawUTF8(), refDb));
            else if (stepDb >= kGateStepNoteDb)
                notes.push_back (fmt ("tone splices %.0f dB re peak (%s)", stepDb, where.toRawUTF8()));
            const double vStep = num (r, "artifacts.vocal_step_db"), vIn = num (r, "artifacts.vocal_input_step_db");
            if (vStep >= kGateStepFailDb) artReasons.push_back (fmt ("vocal discontinuity %.0f dB re peak (input %.0f dB)", vStep, vIn));
            else if (vStep > vIn + kGateStepRelDb) notes.push_back (fmt ("vocal splices %.0f dB re peak (input %.0f dB)", vStep, vIn));
            const int clicks = (int) num (r, "artifacts.transition_clicks_20db");
            if (clicks > 0) artReasons.push_back (fmt ("%d/12 note changes click >= -20 dB (worst %.0f dB)", clicks, num (r, "artifacts.transition_worst_db")));
            const int clicks30 = (int) num (r, "artifacts.transition_clicks_30db");
            if (clicks == 0 && clicks30 > 0) notes.push_back (fmt ("%d/12 note changes splice >= -30 dB", clicks30));
        }
        // Notes (not gates)
        if (live && std::abs (clickTone - rep) > 1.0) notes.push_back (fmt ("transients on voiced tone %+.1f ms", clickTone - rep));
        if (! live) notes.push_back (fmt ("transients %+.1f ms late (known Rubber Band smearing)", 0.5 * ((clickSil - rep) + (vocHf - rep))));
        const bool matrixPass = reasons.empty(), artPass = artReasons.empty();
        const bool pass = matrixPass && artPass;
        fails += ! pass;
        auto* d = r.getDynamicObject();
        d->setProperty ("verdict", pass ? "PASS" : "FAIL");
        d->setProperty ("matrix_verdict", matrixPass ? "PASS" : "FAIL");
        d->setProperty ("artifact_verdict", artPass ? "PASS" : "FAIL");
        juce::StringArray rs, as, ns;
        for (auto& s : reasons) rs.add (s);
        for (auto& s : artReasons) as.add (s);
        for (auto& s : notes) ns.add (s);
        d->setProperty ("fail_reasons", rs);
        d->setProperty ("artifact_fail_reasons", as);
        d->setProperty ("notes", ns);
        outRows.add (r);

        const double bdv = num (r, "buffer.vocal_maxdiff"), bdt = num (r, "buffer.tone_maxdiff");
        const int refB = (int) num (r, "buffer.ref_block");
        std::string bufText = (int) r["block"] == refB ? fmt ("ref (%d)", refB)
                            : (bdv == 0.0 && bdt == 0.0) ? "identical"
                            : fmt ("%.0f dB", 20.0 * std::log10 (std::max (1e-12, std::max (bdv, bdt))));
        std::string all;
        for (auto& s : reasons) all += (all.empty() ? "" : "; ") + s;
        for (auto& s : artReasons) all += (all.empty() ? "art: " : "; art: ") + s;
        for (auto& s : notes) all += (all.empty() ? "note: " : "; note: ") + s;
        md += fmt ("| %s | %.1f k | %d | **%s** | %s | %s | %.2f ms (%d) | %+.2f | %+.2f | %+.2f | %+.2f | %.0f / %.0f / %.0f | %d | %.1f / %.1f / %.1f | %.0f | %.0f / %.0f | %.0f / %.0f (%.0f) | %s | %s |\n",
                   r["label"].toString().toRawUTF8(), (double) r["sr"] / 1000.0, (int) r["block"], pass ? "PASS" : "FAIL",
                   matrixPass ? "PASS" : "FAIL", artPass ? "PASS" : fmt ("FAIL (%d/12 clicks)", (int) num (r, "artifacts.transition_clicks_20db")).c_str(),
                   rep, (int) r["reported_latency_samples"], clickSil - rep, clickTone - rep, vocEnv - rep, vocHf - rep,
                   mean, p99, mx, (int) num (r, "cpu.instances_at_p99"), b3, lowc, high, vib, semi, fifth, num (r, "artifacts.worst_step_db"), num (r, "artifacts.vocal_step_db"), num (r, "artifacts.vocal_input_step_db"),
                   bufText.c_str(), all.c_str());
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("meta", meta);
    auto* gates = new juce::DynamicObject();
    gates->setProperty ("cpu_p99_pct_max", kGateCpuP99Pct);
    gates->setProperty ("cpu_max_pct_max", kGateCpuMaxPct);
    gates->setProperty ("b3_cents_max", kGateB3Cents);
    gates->setProperty ("extreme_cents_max", kGateExtremeCents);
    gates->setProperty ("vibrato_p95_cents_max", kGateVibratoP95);
    gates->setProperty ("settle_abs_ms_max", kGateSettleAbsMs);
    gates->setProperty ("settle_rel_ms", kGateSettleRelMs);
    gates->setProperty ("latency_click_live_ms_max", kGateLatencyClickMs);
    gates->setProperty ("latency_vocal_hf_live_ms_max", kGateLatencyHfMs);
    gates->setProperty ("zero_run_ms_max", kGateZeroRunMs);
    gates->setProperty ("step_fail_db", kGateStepFailDb);
    gates->setProperty ("step_note_db", kGateStepNoteDb);
    gates->setProperty ("step_rel_to_ref_db", kGateStepRelDb);
    gates->setProperty ("peak_max", kGatePeak);
    root->setProperty ("gates", juce::var (gates));
    root->setProperty ("rows", outRows);
    root->setProperty ("failing_rows", fails);
    juce::File (outJson).replaceWithText (juce::JSON::toString (sanitize (juce::var (root)), true, 5));
    juce::File (outMd).replaceWithText (md);
    std::printf ("merged %zu rows, %d failing -> %s, %s\n", rows.size(), fails, outJson.c_str(), outMd.c_str());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    std::vector<std::string> args (argv + 1, argv + argc);
    if (! args.empty() && args[0] == "--merge")
    {
        if (args.size() < 4) { std::fprintf (stderr, "usage: --merge out.json out.md frag.json...\n"); return 2; }
        juce::ScopedJuceInitialiser_GUI init;
        return merge (args[1], args[2], std::vector<std::string> (args.begin() + 3, args.end()));
    }

    std::vector<const Mode*> modes;
    std::vector<double> rates;
    std::vector<int> blocks;
    std::string vocalPath, outPath;
    double cpuSeconds = 6.0;
    int cpuTrials = 3;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const bool hasNext = i + 1 < args.size();
        if (a == "--mode" && hasNext)
        {
            const auto* m = findMode (args[++i]);
            if (m == nullptr) { std::fprintf (stderr, "unknown mode %s\n", args[i].c_str()); return 2; }
            modes.push_back (m);
        }
        else if (a == "--sr" && hasNext) rates.push_back (std::atof (args[++i].c_str()));
        else if (a == "--block" && hasNext) blocks.push_back (std::atoi (args[++i].c_str()));
        else if (a == "--vocal" && hasNext) vocalPath = args[++i];
        else if (a == "--cpu-seconds" && hasNext) cpuSeconds = std::atof (args[++i].c_str());
        else if (a == "--cpu-trials" && hasNext) cpuTrials = std::max (1, std::atoi (args[++i].c_str()));
        else if (a == "--out" && hasNext) outPath = args[++i];
        else if (a == "--dump-dir" && hasNext) gDumpDir = args[++i];
        else
        {
            std::fprintf (stderr, "usage: AutoPitchLiveMatrix [--mode studio|extreme|ultra|range]... [--sr HZ]... [--block N]...\n"
                                  "       [--vocal file.wav] [--cpu-seconds S (0 = skip)] [--cpu-trials T] [--dump-dir DIR] --out frag.json\n"
                                  "       AutoPitchLiveMatrix --merge out.json out.md frag.json...\n");
            return 2;
        }
    }
    if (outPath.empty()) { std::fprintf (stderr, "--out is required\n"); return 2; }
    if (modes.empty()) for (const auto& m : kModes) modes.push_back (&m);
    if (rates.empty()) rates = { 44100.0, 48000.0, 88200.0, 96000.0 };

    // A DAW's audio thread runs on a performance core; ask for the same class so the
    // scheduler does not park the measurement on an efficiency core.
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    setenv ("SATTARI_SESSION_ID", ("auto-pitch-live-matrix-" + juce::String (juce::Time::currentTimeMillis())).toRawUTF8(), 1);

    juce::Array<juce::var> rows;
    for (const auto* m : modes)
        for (const double sr : rates)
        {
            std::vector<int> bl = blocks;
            if (bl.empty())
            {
                bl = { 32, 64, 128, 256 };
                if (! m->live) { bl.push_back (512); bl.push_back (1024); }
            }
            Shared shared;
            for (const int b : bl)
            {
                const auto row = runConfig (*m, sr, b, vocalPath, cpuSeconds, cpuTrials, shared);
                rows.add (row);
                logf ("%-24s %6.0f Hz %5d | lat rep %.2f clickSil %.2f clickTone %.2f vocal %.2f hf %.2f ms | CPU mean %.0f%% p99 %.0f%% max %.0f%% | B3 %.2f low %.2f C5 %.2f vib %.0f | settle %.0f/%.0f ms | curv %.2f (%s) zero %.2f ms | buf %.2g | %.0f s\n",
                      m->label, sr, b, num (row, "latency.reported_ms"), num (row, "latency.click_silence_median_ms"),
                      num (row, "latency.click_tone_median_ms"), num (row, "latency.vocal_xcorr_ms"), num (row, "latency.vocal_hf_xcorr_ms"),
                      100.0 * num (row, "cpu.rtf"), num (row, "cpu.p99_pct"), num (row, "cpu.max_pct"),
                      num (row, "accuracy.b3_cents"), num (row, "accuracy.low_cents"), num (row, "accuracy.high_cents"),
                      num (row, "accuracy.vibrato_max_cents"), num (row, "accuracy.semitone_settle_ms"), num (row, "accuracy.fifth_settle_ms"),
                      num (row, "artifacts.worst_curv_ratio"), row["artifacts"]["worst_curv_where"].toString().toRawUTF8(),
                      num (row, "artifacts.longest_zero_run_ms"), std::max (num (row, "buffer.vocal_maxdiff"), num (row, "buffer.tone_maxdiff")),
                      num (row, "seconds"));
            }
        }

    auto* meta = new juce::DynamicObject();
    meta->setProperty ("tool", "AutoPitchLiveMatrix");
    meta->setProperty ("cpu", juce::SystemStats::getCpuModel());
    meta->setProperty ("cpu_seconds_per_trial", cpuSeconds);
    meta->setProperty ("cpu_trials", cpuTrials);
    meta->setProperty ("vocal", vocalPath.empty() ? juce::String ("synthetic") : juce::String (vocalPath));
    meta->setProperty ("channels", "stereo in / stereo out (default layout), mono material duplicated");
    auto* root = new juce::DynamicObject();
    root->setProperty ("meta", juce::var (meta));
    root->setProperty ("rows", rows);
    juce::File (outPath).replaceWithText (juce::JSON::toString (sanitize (juce::var (root)), true, 5));
    logf ("wrote %d rows -> %s\n", rows.size(), outPath.c_str());
    return 0;
}
