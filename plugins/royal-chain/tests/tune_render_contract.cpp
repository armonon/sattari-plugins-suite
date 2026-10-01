// Royal Chain — Tune stage offline render contract.
//
// Drives the REAL RoyalChainAudioProcessor offline with deterministic, detuned
// vocal-like material, key C major, Tune = 1, and measures what the chain does to
// pitch with the suite's shared PitchDetector:
//
//   * cents error to the nearest C-major tone, input vs output (median / mean /
//     share of voiced frames within +-10 ct)
//   * the delay the chain actually imposes (amplitude-envelope cross-correlation)
//     against the latency it reports to the host
//   * output finite + non-silent, allocation count and CPU time on the audio path
//   * TuneModule unit checks: Tune 0 is the exact delayed input, mono works, and
//     getLastHz()/getConfidence() track the sung pitch (the Compass feed)
//
// Exit code is non-zero only when a gate FAILS.
//
//   RoyalChainTuneRenderContract [--wav <dir>]

#include "RoyalChainProcessor.h"
#include "TuneModule.h"
#include <sattari_audio_core/SattariPitchDetector.h>
#include <sattari_audio_core/SattariRenderHarness.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <vector>

//==============================================================================
// Allocation counter armed only on the rendering thread, only around processBlock.
namespace
{
thread_local bool gAllocArmed = false;
std::atomic<long> gAllocCount { 0 };

inline void* countedAlloc (std::size_t n)
{
    if (gAllocArmed)
        gAllocCount.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n != 0 ? n : 1);
}
} // namespace

void* operator new (std::size_t n)
{
    if (void* p = countedAlloc (n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (void* p = countedAlloc (n)) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n, const std::nothrow_t&) noexcept   { return countedAlloc (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return countedAlloc (n); }
void operator delete (void* p) noexcept                               { std::free (p); }
void operator delete[] (void* p) noexcept                             { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                { std::free (p); }

namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

// ---- deterministic detuned phrase ------------------------------------------------
// C-major melody, every note deliberately off-key by 20-40 cents, with vibrato,
// four harmonics, breath noise and short gaps (so the amplitude envelope has
// onsets the delay estimator can lock onto).
struct Phrase
{
    juce::AudioBuffer<float> audio;
    std::vector<int> onsets;      // sample index of each note start
};

Phrase makeDetunedPhrase (double sr, int baseMidi)
{
    static constexpr int degrees[] = { 0, 2, 4, 5, 7, 9, 7, 4, 2, 0 };
    static constexpr double detune[] = { +35, -30, +40, -25, +30, -40, +20, -35, +25, -30 };
    constexpr int numNotes = (int) (sizeof (degrees) / sizeof (degrees[0]));
    const int noteLen = (int) std::lround (0.42 * sr);
    const int gapLen  = (int) std::lround (0.04 * sr);
    const int lead    = (int) std::lround (0.10 * sr);

    Phrase p;
    const int total = lead + numNotes * (noteLen + gapLen) + (int) std::lround (0.2 * sr);
    p.audio.setSize (1, total);
    p.audio.clear();
    float* d = p.audio.getWritePointer (0);

    juce::uint32 rng = 0x2545F491u;
    auto noise = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
    };

    double phase = 0.0;
    for (int n = 0; n < numNotes; ++n)
    {
        const int start = lead + n * (noteLen + gapLen);
        p.onsets.push_back (start);
        const double midi = (double) (baseMidi + degrees[n]) + detune[n] / 100.0;
        const double hz = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
        const int attack = (int) std::lround (0.015 * sr), release = (int) std::lround (0.025 * sr);
        for (int i = 0; i < noteLen; ++i)
        {
            const double t = (double) i / sr;
            // 5.5 Hz, +-12 ct vibrato that fades in after 150 ms, like a singer.
            const double vibDepth = 0.12 * std::clamp ((t - 0.15) / 0.1, 0.0, 1.0);
            const double f0 = hz * std::pow (2.0, vibDepth * std::sin (kTwoPi * 5.5 * t) / 12.0);
            phase += kTwoPi * f0 / sr;
            const double s = 1.00 * std::sin (phase) + 0.50 * std::sin (2.0 * phase)
                           + 0.28 * std::sin (3.0 * phase) + 0.14 * std::sin (4.0 * phase)
                           + 0.04 * (double) noise();
            double env = 1.0;
            if (i < attack) env = (double) i / attack;
            if (i > noteLen - release) env = std::max (0.0, (double) (noteLen - i) / release);
            d[start + i] = (float) (0.22 * env * s);
        }
    }
    return p;
}

// ---- measurement helpers ---------------------------------------------------------
double centsToNearestCMajor (double hz)
{
    static constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    double best = 1.0e9;
    for (int oct = -1; oct <= 10; ++oct)
        for (int pc : major)
        {
            const double d = midi - (double) (oct * 12 + pc);
            if (std::abs (d) < std::abs (best)) best = d;
        }
    return 100.0 * best;
}

struct PitchStats
{
    int voicedFrames = 0;
    double medianAbs = 0.0, meanAbs = 0.0, within10 = 0.0, p90Abs = 0.0;
};

// onsets (optional): note starts on the INPUT timeline. Frames whose 45 ms
// analysis window overlaps [onset, onset + settle] (shifted by `delay`) are
// excluded, so the figure is steady-state intonation, not the note transition.
PitchStats measurePitch (const juce::AudioBuffer<float>& buf, double sr, int skipSamples,
                         const std::vector<int>* onsets = nullptr, int delay = 0,
                         const char* traceLabel = nullptr)
{
    sattari::dsp::PitchDetector det;
    det.prepare (sr, 70.0f, 1000.0f, 0.045f);
    const int hop = 256;
    const int n = buf.getNumSamples();
    std::vector<float> mono ((size_t) hop);
    std::vector<double> errs;
    for (int pos = 0; pos + hop <= n; pos += hop)
    {
        for (int i = 0; i < hop; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < buf.getNumChannels(); ++c) m += buf.getSample (c, pos + i);
            mono[(size_t) i] = m / (float) buf.getNumChannels();
        }
        det.pushBlock (mono.data(), hop);
        if (pos < skipSamples) continue;
        const auto r = det.analyse (0.30f);
        if (onsets != nullptr)
        {
            const int frameEnd = pos + hop, frameStart = frameEnd - (int) (0.045 * sr);
            const int settle = (int) (0.08 * sr);
            bool transition = false;
            for (int o : *onsets)
                if (frameEnd > o + delay && frameStart < o + delay + settle) { transition = true; break; }
            if (transition)
            {
                if (traceLabel != nullptr && r.voiced && r.frequencyHz > 0.0f)
                    std::printf ("    trace %s t=%.3f hz=%.2f err=%+.1f conf=%.2f (transition)\n", traceLabel,
                                 (double) (pos + hop) / sr, r.frequencyHz, centsToNearestCMajor (r.frequencyHz), r.confidence);
                continue;
            }
        }
        if (r.voiced && r.frequencyHz > 0.0f && r.confidence > 0.6f)
        {
            const double e = centsToNearestCMajor (r.frequencyHz);
            errs.push_back (std::abs (e));
            if (traceLabel != nullptr)
                std::printf ("    trace %s t=%.3f hz=%.2f err=%+.1f conf=%.2f\n", traceLabel,
                             (double) (pos + hop) / sr, r.frequencyHz, e, r.confidence);
        }
    }
    PitchStats s;
    s.voicedFrames = (int) errs.size();
    if (errs.empty()) return s;
    std::sort (errs.begin(), errs.end());
    double sum = 0.0; int in10 = 0;
    for (double e : errs) { sum += e; if (e <= 10.0) ++in10; }
    s.medianAbs = errs[errs.size() / 2];
    s.p90Abs = errs[std::min (errs.size() - 1, (size_t) (0.9 * (double) errs.size()))];
    s.meanAbs = sum / (double) errs.size();
    s.within10 = (double) in10 / (double) errs.size();
    return s;
}

std::vector<float> envelope (const juce::AudioBuffer<float>& buf, double sr)
{
    const int n = buf.getNumSamples();
    std::vector<float> e ((size_t) n);
    const double a = 1.0 - std::exp (-1.0 / (0.002 * sr));
    double y = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double m = 0.0;
        for (int c = 0; c < buf.getNumChannels(); ++c) m += buf.getSample (c, i);
        y += a * (std::abs (m / buf.getNumChannels()) - y);
        e[(size_t) i] = (float) y;
    }
    // Onset-emphasis: first difference, half-wave rectified.
    std::vector<float> d ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i) d[(size_t) i] = std::max (0.0f, e[(size_t) i] - e[(size_t) i - 1]);
    return d;
}

int estimateDelay (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, double sr, int maxLag)
{
    const auto a = envelope (in, sr), b = envelope (out, sr);
    const int n = (int) std::min (a.size(), b.size());
    int bestLag = 0; double best = -1.0;
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < n; ++i) acc += (double) a[(size_t) i] * b[(size_t) (i + lag)];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    return bestLag;
}

bool allFinite (const juce::AudioBuffer<float>& b, float& rms)
{
    double acc = 0.0; long n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (c, i);
            if (! std::isfinite (x)) return false;
            acc += (double) x * x; ++n;
        }
    rms = (float) std::sqrt (acc / (double) std::max (1L, n));
    return true;
}

void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float plain)
{
    if (auto* p = s.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

// Render through the real processor, counting allocations + CPU on processBlock.
struct RenderResult
{
    juce::AudioBuffer<float> out;
    long allocs = 0;
    double cpuRealtimeRatio = 0.0;   // processing time / audio duration
    int reportedLatency = 0;
};

RenderResult render (RoyalChainAudioProcessor& proc, const juce::AudioBuffer<float>& input,
                     double sr, int blockSize)
{
    proc.setRateAndBufferSizeDetails (sr, blockSize);
    proc.prepareToPlay (sr, blockSize);
    RenderResult r;
    r.reportedLatency = proc.getLatencySamples();
    const int total = input.getNumSamples();
    r.out.setSize (2, total);
    r.out.clear();
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;
    double seconds = 0.0;
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int len = juce::jmin (blockSize, total - pos);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), pos, len);
        juce::AudioBuffer<float> slice (block.getArrayOfWritePointers(), 2, len);
        const auto t0 = std::chrono::steady_clock::now();
        gAllocArmed = true;
        proc.processBlock (slice, midi);
        gAllocArmed = false;
        seconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        for (int ch = 0; ch < 2; ++ch)
            r.out.copyFrom (ch, pos, slice, ch, 0, len);
    }
    r.allocs = gAllocCount.exchange (0);
    r.cpuRealtimeRatio = seconds / ((double) total / sr);
    proc.releaseResources();
    return r;
}

// Detect the refactored TuneModule API (latency + style) without breaking the
// baseline build, so the same file measures before and after.
template <typename T, typename = void> struct HasTuneLatency : std::false_type {};
template <typename T>
struct HasTuneLatency<T, std::void_t<decltype (std::declval<const T&>().getLatencySamples())>> : std::true_type {};

struct Report
{
    int failures = 0;
    void check (const char* name, bool ok, const juce::String& detail)
    {
        if (! ok) ++failures;
        std::printf ("  [%s] %-34s %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
    }
};

template <typename Tune>
void tuneModuleUnitChecks (Report& rep)
{
    if constexpr (HasTuneLatency<Tune>::value)
    {
        constexpr double sr = 48000.0;
        auto phrase = makeDetunedPhrase (sr, 60);

        // (a) Tune 0 = exact delayed input, stereo and mono.
        for (int channels : { 2, 1 })
        {
            Tune tm;
            tm.prepare (sr, 128);
            const int lat = tm.getLatencySamples();
            const int total = phrase.audio.getNumSamples();
            juce::AudioBuffer<float> buf (channels, total);
            for (int c = 0; c < channels; ++c) buf.copyFrom (c, 0, phrase.audio, 0, 0, total);
            for (int pos = 0; pos < total; pos += 128)
            {
                const int len = std::min (128, total - pos);
                juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), channels, pos, len);
                tm.process (slice, 0.0f, 0, sattari::dsp::ScaleId::Major, 2);
            }
            double worst = 0.0;
            for (int c = 0; c < channels; ++c)
                for (int i = lat; i < total; ++i)
                    worst = std::max (worst, (double) std::abs (buf.getSample (c, i) - phrase.audio.getSample (0, i - lat)));
            rep.check (channels == 2 ? "unit: tune=0 null (stereo)" : "unit: tune=0 null (mono)",
                       worst < 1.0e-6,
                       "max |out - in[t-" + juce::String (lat) + "]| = " + juce::String (worst, 9));
        }

        // (a2) Formant preservation is on by default and costs no latency.
        {
            Tune tm;
            tm.prepare (sr, 128);
            const int withFormant = tm.getLatencySamples();
            const bool defaultOn = tm.getFormantPreservation();
            tm.setFormantPreservation (false);
            tm.prepare (sr, 128);
            const int withoutFormant = tm.getLatencySamples();
            rep.check ("unit: formant default on, 2 ms", defaultOn && withFormant == withoutFormant
                                                          && withFormant == (int) std::lround (0.002 * sr),
                       juce::String ("default ") + (defaultOn ? "on" : "off") + ", latency "
                       + juce::String (withFormant) + " (formant off " + juce::String (withoutFormant) + ")");
        }

        // (b) Block-size invariance: the engine's internal control grid (and the
        //     lead-grid stereo linkage) make any host block render exactly like
        //     64-sample blocks at 48 kHz, on both channels.
        {
            const int total = phrase.audio.getNumSamples();
            auto renderAt = [&] (int block)
            {
                Tune tm;
                tm.prepare (sr, block);
                juce::AudioBuffer<float> buf (2, total);
                for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, phrase.audio, 0, 0, total);
                for (int pos = 0; pos < total; pos += block)
                {
                    juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), 2, pos, std::min (block, total - pos));
                    tm.process (slice, 1.0f, 0, sattari::dsp::ScaleId::Major, 0);
                }
                return buf;
            };
            const auto b = renderAt (64);
            double worst = 0.0;
            for (int block : { 7, 512, 2048 })
            {
                const auto a = renderAt (block);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < total; ++i)
                        worst = std::max (worst, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
            }
            rep.check ("unit: block 7/512/2048 == 64", worst < 1.0e-6, "max diff " + juce::String (worst, 9));
        }

        // (c) Compass feed: getLastHz tracks the sung (input) pitch, confidence rises.
        {
            Tune tm;
            tm.prepare (sr, 256);
            const int total = phrase.audio.getNumSamples();
            juce::AudioBuffer<float> buf (1, total);
            buf.copyFrom (0, 0, phrase.audio, 0, 0, total);
            // Sample mid-note 3 (E4 +40 ct, i.e. ~339.2 Hz)
            const int probe = phrase.onsets[2] + (int) (0.12 * sr);
            float hzAtProbe = 0.0f, confAtProbe = 0.0f;
            for (int pos = 0; pos < total; pos += 256)
            {
                const int len = std::min (256, total - pos);
                juce::AudioBuffer<float> slice (buf.getArrayOfWritePointers(), 1, pos, len);
                tm.process (slice, 1.0f, 0, sattari::dsp::ScaleId::Major, 2);
                if (pos <= probe && probe < pos + len) { hzAtProbe = tm.getLastHz(); confAtProbe = tm.getConfidence(); }
            }
            const double expected = 440.0 * std::pow (2.0, (64.40 - 69.0) / 12.0);
            const double errCt = hzAtProbe > 0.0f ? 1200.0 * std::log2 (hzAtProbe / expected) : 9999.0;
            rep.check ("unit: compass feed getLastHz", std::abs (errCt) < 20.0 && confAtProbe > 0.3f,
                       "hz=" + juce::String (hzAtProbe, 2) + " (sung " + juce::String (expected, 2)
                       + ", " + juce::String (errCt, 1) + " ct) conf=" + juce::String (confAtProbe, 2));
        }
    }
    else
    {
        std::printf ("  (baseline TuneModule: no latency/style API, unit checks skipped)\n");
    }
}
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File wavDir;
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--wav" && i + 1 < argc) wavDir = juce::File (juce::String (argv[++i]));

    Report rep;
    const bool engineApi = HasTuneLatency<TuneModule>::value;
    std::printf ("Royal Chain Tune render contract (%s TuneModule)\n", engineApi ? "live-engine" : "baseline");

    tuneModuleUnitChecks<TuneModule> (rep);

    struct Case { const char* name; double sr; int block; int baseMidi; int style; bool isolated; bool glide; };
    const Case cases[] = {
        { "48k/512 female  Rap  isolated", 48000.0, 512, 60, 0, true,  false },
        { "48k/64  female  Rap  isolated", 48000.0,  64, 60, 0, true,  false },
        { "48k/64  female  Pop  isolated", 48000.0,  64, 60, 2, true,  false },
        { "44k/256 male    R&B  isolated", 44100.0, 256, 48, 1, true,  false },
        { "96k/1024 female Hook isolated", 96000.0, 1024, 60, 7, true, false },
        { "44k/7   female  Pop  isolated", 44100.0,   7, 60, 2, true,  false },
        { "48k/512 female  Rap  full chain", 48000.0, 512, 60, 0, false, false },
        { "48k/512 glide   Pop  isolated", 48000.0, 512, 57, 2, true,  true  },
    };

    const char* traceEnv = std::getenv ("RC_TUNE_TRACE");   // case index to trace, e.g. "0"
    int caseIndex = -1;
    for (const auto& c : cases)
    {
        ++caseIndex;
        std::printf ("\n-- %s\n", c.name);
        juce::AudioBuffer<float> input;
        std::vector<int> onsets;
        int skip = (int) (0.3 * c.sr);
        if (c.glide)
            sattari::harness::generateVocalLike (input, c.sr, 4.0);
        else
        {
            auto phrase = makeDetunedPhrase (c.sr, c.baseMidi);
            input = std::move (phrase.audio);
            onsets = std::move (phrase.onsets);
        }

        auto configure = [&c] (RoyalChainAudioProcessor& proc)
        {
            auto& s = proc.parameters();
            setParam (s, "style", (float) c.style);
            setParam (s, "tune", 1.0f);
            setParam (s, "tuneKey", 0.0f);     // C
            setParam (s, "tuneScale", 1.0f);   // Major
            setParam (s, "syncFromAutoPitch", 0.0f);
            if (c.isolated)
            {
                // Neutralise the stages that smear pitch measurement (reverb, width,
                // saturation); dynamics/tone stay as-is.
                setParam (s, "space", 0.0f);
                setParam (s, "width", 0.0f);
                setParam (s, "glow", 0.0f);
            }
        };

        RoyalChainAudioProcessor proc;
        configure (proc);
        const auto r = render (proc, input, c.sr, c.block);
        float rms = 0.0f;
        const bool finite = allFinite (r.out, rms);
        const bool trace = traceEnv != nullptr && std::atoi (traceEnv) == caseIndex;
        const auto pin  = measurePitch (input, c.sr, skip);
        const auto pout = measurePitch (r.out, c.sr, skip);
        const auto* on = onsets.empty() ? nullptr : &onsets;
        const auto steadyIn  = measurePitch (input, c.sr, skip, on, 0);
        const auto steadyOut = measurePitch (r.out, c.sr, skip, on, r.reportedLatency, trace ? "out" : nullptr);

        // Latency: a click train (unvoiced, so every Tune path must pass it at its
        // fixed delay) through a fresh, identically configured chain.
        juce::AudioBuffer<float> clicks;
        std::vector<int> clickAt;
        {
            const int n = (int) (2.0 * c.sr);
            clicks.setSize (1, n);
            clicks.clear();
            for (int p = (int) (0.2 * c.sr); p < n - (int) (0.3 * c.sr); p += (int) (0.25 * c.sr))
            {
                clicks.setSample (0, p, 0.5f);
                clickAt.push_back (p);
            }
        }
        RoyalChainAudioProcessor clickProc;
        configure (clickProc);
        const auto rc = render (clickProc, clicks, c.sr, c.block);
        int clickDelay = 0;
        {
            double best = -1.0e30;
            for (int lag = 0; lag <= (int) (0.2 * c.sr); ++lag)
            {
                double acc = 0.0;
                for (int k : clickAt)
                    if (k + lag < rc.out.getNumSamples())
                        acc += 0.5 * (double) (rc.out.getSample (0, k + lag) + rc.out.getSample (1, k + lag));
                if (acc > best) { best = acc; clickDelay = lag; }
            }
        }
        const int envDelay = estimateDelay (input, r.out, c.sr, (int) (0.25 * c.sr));

        std::printf ("  input : voiced=%d median=%.1f ct mean=%.1f ct p90=%.1f ct within10=%.0f%%\n",
                     pin.voicedFrames, pin.medianAbs, pin.meanAbs, pin.p90Abs, 100.0 * pin.within10);
        std::printf ("  output: voiced=%d median=%.1f ct mean=%.1f ct p90=%.1f ct within10=%.0f%%\n",
                     pout.voicedFrames, pout.medianAbs, pout.meanAbs, pout.p90Abs, 100.0 * pout.within10);
        if (on != nullptr)
        {
            std::printf ("  steady input : frames=%d median=%.1f ct mean=%.1f ct p90=%.1f ct within10=%.0f%%\n",
                         steadyIn.voicedFrames, steadyIn.medianAbs, steadyIn.meanAbs, steadyIn.p90Abs, 100.0 * steadyIn.within10);
            std::printf ("  steady output: frames=%d median=%.1f ct mean=%.1f ct p90=%.1f ct within10=%.0f%%\n",
                         steadyOut.voicedFrames, steadyOut.medianAbs, steadyOut.meanAbs, steadyOut.p90Abs, 100.0 * steadyOut.within10);
        }
        std::printf ("  latency reported=%d click-measured=%d samples (%.2f ms) [vocal env xcorr %d]  rms=%.4f allocs=%ld cpu=%.2f%% RT\n",
                     r.reportedLatency, clickDelay, 1000.0 * clickDelay / c.sr, envDelay, rms,
                     r.allocs + rc.allocs, 100.0 * r.cpuRealtimeRatio);

        if (wavDir != juce::File())
        {
            const auto base = juce::String (c.name).replaceCharacters (" /&", "___").removeCharacters (" ");
            sattari::harness::writeWav (wavDir.getChildFile (base + "_in.wav"), input, c.sr);
            sattari::harness::writeWav (wavDir.getChildFile (base + "_out.wav"), r.out, c.sr);
        }

        rep.check ("finite + non-silent", finite && rms > 1.0e-3f, "rms=" + juce::String (rms, 4));
        rep.check ("no allocations in processBlock", r.allocs + rc.allocs == 0,
                   juce::String (r.allocs + rc.allocs) + " allocs");
        // The chain's other stages are zero-latency (IIR phase only), so the click
        // train must arrive at the reported delay within a few samples.
        const int tol = (int) std::ceil (0.0002 * c.sr);
        rep.check ("reported latency == measured", std::abs (clickDelay - r.reportedLatency) <= tol,
                   "reported " + juce::String (r.reportedLatency) + " vs click " + juce::String (clickDelay)
                   + " (tol " + juce::String (tol) + ")");
        if (c.glide)
        {
            rep.check ("glide pulled toward scale", pout.medianAbs < 0.6 * pin.medianAbs,
                       juce::String (pin.medianAbs, 1) + " -> " + juce::String (pout.medianAbs, 1) + " ct median");
        }
        else
        {
            rep.check ("pitch pulled to C major", pout.medianAbs < 8.0 && pout.within10 > 0.6
                                                   && pout.medianAbs < 0.35 * pin.medianAbs,
                       juce::String (pin.medianAbs, 1) + " -> " + juce::String (pout.medianAbs, 1)
                       + " ct median, within10 " + juce::String (100.0 * pout.within10, 0) + "%");
            rep.check ("steady-state intonation", steadyOut.medianAbs < 5.0 && steadyOut.p90Abs < 15.0,
                       "median " + juce::String (steadyOut.medianAbs, 1) + " ct, p90 " + juce::String (steadyOut.p90Abs, 1) + " ct");
            // Includes note onsets (tracker acquisition + the analysis window
            // straddling the gap): guards against slow glides into each note.
            rep.check ("onsets settle (all-frame p90)", pout.p90Abs < 15.0,
                       "p90 " + juce::String (pout.p90Abs, 1) + " ct over all voiced frames");
        }
    }

    std::printf ("\n%s (%d failure%s)\n", rep.failures == 0 ? "royal chain tune contract ok" : "royal chain tune contract FAILED",
                 rep.failures, rep.failures == 1 ? "" : "s");
    return rep.failures == 0 ? 0 : 1;
}
