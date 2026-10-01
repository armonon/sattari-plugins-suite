#include <sattari_audio_core/SattariLivePitchEngine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double kTwoPi = 6.2831853071795864769;

// ---- shared signal + measurement helpers for the regression sections -------

double midiToHz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }

struct Note { int start = 0, end = 0; double inputHz = 0.0, targetHz = 0.0; };

// Phase-continuous four-harmonic "vowel" notes, each detuned from the target
// semitone. gapMs = 0 gives legato note transitions; > 0 gives onsets after
// silence. h1 < 1 weakens the fundamental (octave-error stress).
std::vector<float> makeNotes (double sr, const std::vector<double>& midis,
                              const std::vector<int>& targets, double noteMs,
                              double gapMs, double leadMs, std::vector<Note>& notes,
                              double h1 = 1.0, double h2 = 0.5, double h3 = 0.28, double h4 = 0.14)
{
    const int noteLen = (int) std::lround (noteMs * 0.001 * sr);
    const int gapLen = (int) std::lround (gapMs * 0.001 * sr);
    const int lead = (int) std::lround (leadMs * 0.001 * sr);
    std::vector<float> x ((size_t) (lead + (int) midis.size() * (noteLen + gapLen) + (int) (0.1 * sr)), 0.0f);
    double phase = 0.0;
    unsigned rng = 0x2545F491u;
    int pos = lead;
    notes.clear();
    for (size_t n = 0; n < midis.size(); ++n)
    {
        const double hz = midiToHz (midis[n]);
        notes.push_back ({ pos, pos + noteLen, hz, midiToHz (targets[n]) });
        const bool attack = gapLen > 0 || n == 0;
        for (int i = 0; i < noteLen; ++i)
        {
            phase += kTwoPi * hz / sr;
            double env = attack ? std::min (1.0, (double) i / (0.015 * sr)) : 1.0;
            if (gapLen > 0 || n + 1 == midis.size())
                env = std::min (env, std::max (0.0, (double) (noteLen - i) / (0.025 * sr)));
            rng = rng * 1664525u + 1013904223u;
            const double v = h1 * std::sin (phase) + h2 * std::sin (2 * phase)
                           + h3 * std::sin (3 * phase) + h4 * std::sin (4 * phase)
                           + 0.02 * (((double) rng / 4294967295.0) * 2.0 - 1.0);
            x[(size_t) (pos + i)] = (float) (0.22 * env * v);
        }
        pos += noteLen + gapLen;
    }
    return x;
}

// Normalised-difference period estimate searched +-25 % around expectedHz;
// analysis span is about one period (>= 5 ms) centred on `centre`.
double outputHz (const std::vector<float>& x, double sr, int centre, double expectedHz)
{
    const double period = sr / expectedHz;
    const int w = std::max ((int) std::ceil (period), (int) (0.005 * sr));
    const int lo = std::max (2, (int) std::floor (period / 1.25));
    const int hi = (int) std::ceil (period * 1.25);
    const int s = centre - w / 2;
    if (s < 0 || s + w + hi + 2 >= (int) x.size()) return 0.0;
    std::vector<double> d ((size_t) hi + 2, 2.0);
    for (int tau = lo - 1; tau <= hi + 1; ++tau)
    {
        double diff = 0.0, e0 = 1.0e-12, e1 = 1.0e-12;
        for (int i = 0; i < w; ++i)
        {
            const double a = x[(size_t) (s + i)], b = x[(size_t) (s + i + tau)];
            diff += (a - b) * (a - b); e0 += a * a; e1 += b * b;
        }
        d[(size_t) tau] = diff / (e0 + e1);
    }
    int best = lo;
    for (int tau = lo; tau <= hi; ++tau) if (d[(size_t) tau] < d[(size_t) best]) best = tau;
    double lag = best;
    const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
    if (std::abs (a - 2 * b + c) > 1.0e-12) lag += 0.5 * (a - c) / (a - 2 * b + c);
    return b < 0.25 ? sr / lag : 0.0;
}

// ms after the note start (input timeline) until the output is within
// +-tolerance cents of the target and stays there for 30 ms; -1 if never.
double settleMs (const std::vector<float>& y, double sr, const Note& n, int latency,
                 double tolerance = 10.0)
{
    const int step = std::max (1, (int) (0.0005 * sr));
    const int hold = (int) (0.030 * sr) / step;
    std::vector<char> ok;
    for (int t = n.start; t < n.end - (int) (0.035 * sr); t += step)
    {
        const double hz = outputHz (y, sr, t + latency, n.targetHz);
        ok.push_back (hz > 0.0 && std::abs (1200.0 * std::log2 (hz / n.targetHz)) <= tolerance);
    }
    for (size_t k = 0; k + (size_t) hold < ok.size(); ++k)
    {
        bool all = true;
        for (size_t j = k; j < k + (size_t) hold && all; ++j) all = ok[j] != 0;
        if (all) return 1000.0 * (double) (k * (size_t) step) / sr;
    }
    return -1.0;
}

sattari::dsp::LiveCorrectionPlanner::Settings hardChromatic()
{
    sattari::dsp::LiveCorrectionPlanner::Settings s;
    s.amount = 1.0; s.retuneMilliseconds = 1.5; s.preserveVibrato = 0.0;
    s.transitionAmount = 1.0; s.noteHysteresisCents = 18.0;
    return s;
}

std::vector<float> renderEngine (const std::vector<float>& x, double sr, int block,
                                 const sattari::dsp::LiveCorrectionPlanner::Settings& settings,
                                 int& latency, bool formant = false,
                                 const std::vector<Note>* audit = nullptr,
                                 int* octaveErrors = nullptr)
{
    sattari::dsp::ExtremeLivePitchEngine engine;
    engine.prepare (sr, block, 70.0, 1100.0);
    engine.setFormantPreservation (formant);
    engine.reset();
    latency = engine.getLatencySamples();
    std::vector<float> y (x.size(), 0.0f);
    for (int pos = 0; pos < (int) x.size(); pos += block)
    {
        const int n = std::min (block, (int) x.size() - pos);
        engine.processCorrected (x.data() + pos, y.data() + pos, n, settings);
        const auto pitch = engine.getPitchResult();
        if (audit != nullptr && octaveErrors != nullptr && pitch.voiced)
            for (const auto& note : *audit)
                if (pos + n > note.start && pos + n < note.end - (int) (0.03 * sr)
                    && std::abs (1200.0 * std::log2 (pitch.frequencyHz / note.inputHz)) > 150.0)
                    ++*octaveErrors;
    }
    return y;
}

// ---- formant metric: LPC envelope peaks of a vowel-like source --------------

struct Resonator
{
    double a1 = 0.0, a2 = 0.0, gain = 1.0, z1 = 0.0, z2 = 0.0;
    Resonator (double sr, double hz, double bandwidth)
    {
        const double r = std::exp (-3.14159265358979 * bandwidth / sr);
        a1 = -2.0 * r * std::cos (kTwoPi * hz / sr); a2 = r * r; gain = 1.0 + a1 + a2;
    }
    double process (double x) { const double y = gain * x - a1 * z1 - a2 * z2; z2 = z1; z1 = y; return y; }
};

// Glottal pulse train (5 Hz, +-60 ct vibrato so harmonics sweep the envelope)
// through a four-formant vocal tract and lip radiation.
std::vector<float> makeVowel (double sr, double f0, const std::array<double, 4>& formants, double seconds)
{
    std::array<Resonator, 4> tract { Resonator (sr, formants[0], 60.0), Resonator (sr, formants[1], 90.0),
                                     Resonator (sr, formants[2], 120.0), Resonator (sr, formants[3], 160.0) };
    std::vector<float> out ((size_t) (seconds * sr));
    double phase = 0.0, g1 = 0.0, g2 = 0.0, previous = 0.0, peak = 1.0e-9;
    const double pole = std::exp (-kTwoPi * 100.0 / sr);
    for (size_t i = 0; i < out.size(); ++i)
    {
        phase += f0 * std::pow (2.0, 60.0 * std::sin (kTwoPi * 5.0 * (double) i / sr) / 1200.0) / sr;
        double pulse = 0.0;
        if (phase >= 1.0) { phase -= 1.0; pulse = 50.0; }
        g1 = pole * g1 + (1.0 - pole) * pulse;
        g2 = pole * g2 + (1.0 - pole) * g1;
        double v = g2;
        for (auto& r : tract) v = r.process (v);
        out[i] = (float) (v - previous);
        previous = v;
        peak = std::max (peak, (double) std::abs (out[i]));
    }
    for (auto& v : out) v = (float) (0.3 * v / peak);
    return out;
}

// Order-12 LPC envelope (dB, 100..4500 Hz, 2 Hz grid) of the signal from
// `start`, decimated to sr/4, pre-emphasised, from frame-averaged autocorrelation.
std::vector<double> lpcEnvelope (const std::vector<float>& x, double sr, int start)
{
    constexpr int taps = 127, order = 12, frame = 1024, hop = 512;
    std::array<double, taps> h {};
    const double fc = 5000.0 / sr;
    for (int i = 0; i < taps; ++i)
    {
        const double m = i - (taps - 1) / 2.0;
        const double sinc = m == 0.0 ? 2.0 * fc : std::sin (kTwoPi * fc * m) / (3.14159265358979 * m);
        h[(size_t) i] = sinc * (0.42 - 0.5 * std::cos (kTwoPi * i / (taps - 1)) + 0.08 * std::cos (2 * kTwoPi * i / (taps - 1)));
    }
    std::vector<double> d;
    for (int i = start + taps; i < (int) x.size(); i += 4)
    {
        double acc = 0.0;
        for (int k = 0; k < taps; ++k) acc += h[(size_t) k] * x[(size_t) (i - k)];
        d.push_back (acc);
    }
    for (size_t i = d.size() - 1; i > 0; --i) d[i] -= 0.9 * d[i - 1];
    std::array<double, order + 1> r {};
    for (size_t s = 0; s + frame <= d.size(); s += hop)
        for (int lag = 0; lag <= order; ++lag)
            for (int i = 0; i + lag < frame; ++i)
            {
                const double wa = 0.5 - 0.5 * std::cos (kTwoPi * (i + 0.5) / frame);
                const double wb = 0.5 - 0.5 * std::cos (kTwoPi * (i + lag + 0.5) / frame);
                r[(size_t) lag] += d[s + (size_t) i] * wa * d[s + (size_t) (i + lag)] * wb;
            }
    std::array<double, order + 1> a {}, previous {};
    a[0] = 1.0;
    double error = r[0];
    for (int i = 1; i <= order; ++i)
    {
        double acc = r[(size_t) i];
        for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
        const double k = -acc / error;
        previous = a;
        for (int j = 1; j < i; ++j) a[(size_t) j] = previous[(size_t) j] + k * previous[(size_t) (i - j)];
        a[(size_t) i] = k;
        error *= 1.0 - k * k;
    }
    std::vector<double> envelope;
    for (double f = 100.0; f <= 4500.0; f += 2.0)
    {
        double re = 0.0, im = 0.0;
        for (int j = 0; j <= order; ++j)
        {
            re += a[(size_t) j] * std::cos (kTwoPi * f * j / (sr / 4.0));
            im -= a[(size_t) j] * std::sin (kTwoPi * f * j / (sr / 4.0));
        }
        envelope.push_back (-10.0 * std::log10 (re * re + im * im));
    }
    return envelope;
}

double strongestPeak (const std::vector<double>& envelope, double lo, double hi)
{
    double best = 0.0, bestDb = -1.0e9;
    for (size_t i = 1; i + 1 < envelope.size(); ++i)
    {
        const double f = 100.0 + 2.0 * (double) i;
        if (f >= lo && f <= hi && envelope[i] > envelope[i - 1] && envelope[i] >= envelope[i + 1]
            && envelope[i] > bestDb) { bestDb = envelope[i]; best = f; }
    }
    return best;
}

double tonePower (const std::vector<float>& x, int start, double sr, double hz)
{
    double re = 0.0, im = 0.0;
    const double w = 6.2831853071795864769 * hz / sr;
    for (int i = start; i < (int) x.size(); ++i)
    {
        re += (double) x[(size_t) i] * std::cos (w * (double) i);
        im -= (double) x[(size_t) i] * std::sin (w * (double) i);
    }
    return re * re + im * im;
}
}

int main()
{
    constexpr double sr = 48000.0;
    constexpr int total = 96000;
    constexpr int block = 64;
    constexpr double sourceHz = 220.0;
    constexpr double targetHz = 261.625565;
    sattari::dsp::ExtremePitchShifter shifter;
    shifter.prepare (sr, 512);
    std::vector<float> input ((size_t) total), output ((size_t) total);
    for (int i = 0; i < total; ++i)
        input[(size_t) i] = 0.2f * (float) std::sin (
            6.2831853071795864769 * sourceHz * (double) i / sr);
    const double ratio = targetHz / sourceHz;
    for (int offset = 0; offset < total; offset += block)
        shifter.process (input.data() + offset, output.data() + offset,
                         std::min (block, total - offset), ratio, true,
                         sr / sourceHz);

    double bestHz = 180.0, bestPower = -1.0;
    for (double hz = 180.0; hz <= 340.0; hz += 0.25)
    {
        const double p = tonePower (output, total / 2, sr, hz);
        if (p > bestPower) { bestPower = p; bestHz = hz; }
    }
    const double targetPower = tonePower (output, total / 2, sr, targetHz);
    const double sourcePower = tonePower (output, total / 2, sr, sourceHz);
    double outputEnergy = 0.0, differenceEnergy = 0.0;
    for (int i = total / 2; i < total; ++i)
    {
        outputEnergy += (double) output[(size_t) i] * output[(size_t) i];
        const double dry = (double) input[(size_t) (i - shifter.getLatencySamples())];
        const double d = (double) output[(size_t) i] - dry;
        differenceEnergy += d * d;
    }
    const double cents = 1200.0 * std::log2 (bestHz / targetHz);
    std::printf ("Extreme latency=%d samples best=%.2f Hz error=%.2f ct target/source=%.3f diffRms=%.5f outRms=%.5f\n",
                 shifter.getLatencySamples(), bestHz, cents,
                 targetPower / std::max (1.0e-20, sourcePower),
                 std::sqrt (differenceEnergy / (double) (total / 2)),
                 std::sqrt (outputEnergy / (double) (total / 2)));

    sattari::dsp::ExtremePitchShifter largeBlockShifter;
    largeBlockShifter.prepare (sr, 512);
    std::vector<float> largeBlockOutput ((size_t) total);
    for (int offset = 0; offset < total; offset += 512)
        largeBlockShifter.process (input.data() + offset, largeBlockOutput.data() + offset,
                                   std::min (512, total - offset), ratio, true,
                                   sr / sourceHz);
    double largeBestHz = 240.0, largeBestPower = -1.0;
    for (double hz = 240.0; hz <= 280.0; hz += 0.25)
    {
        const double p = tonePower (largeBlockOutput, total / 2, sr, hz);
        if (p > largeBestPower) { largeBestPower = p; largeBestHz = hz; }
    }
    double energy64 = 0.0, energy512 = 0.0, boundaryJump = 0.0;
    for (int i = total / 2; i < total; ++i)
    {
        energy64 += (double) output[(size_t) i] * output[(size_t) i];
        energy512 += (double) largeBlockOutput[(size_t) i] * largeBlockOutput[(size_t) i];
        if (i % block == 0 || i % 512 == 0)
            boundaryJump = std::max (boundaryJump,
                std::abs ((double) output[(size_t) i] - (double) output[(size_t) (i - 1)]));
    }
    const double blockPitchDelta = std::abs (1200.0 * std::log2 (bestHz / largeBestHz));
    const double blockEnergyRatio = std::sqrt (energy64 / std::max (1.0e-20, energy512));

    sattari::dsp::ExtremeLivePitchEngine acquisitionEngine;
    acquisitionEngine.prepare (sr, block, 70.0, 1100.0);
    sattari::dsp::LiveCorrectionPlanner::Settings settings;
    settings.forcedTargetMidi = 69;
    settings.amount = 1.0;
    settings.retuneMilliseconds = 1.5;
    settings.preserveVibrato = 0.0;
    settings.transitionAmount = 1.0;
    std::vector<float> acquisitionOut ((size_t) block);
    int voicedAt = -1, correctedAt = -1;
    double acquisitionPhase = 0.0;
    for (int offset = 0; offset < total / 2 && correctedAt < 0; offset += block)
    {
        std::vector<float> acquisitionIn ((size_t) block);
        for (int i = 0; i < block; ++i)
        {
            acquisitionIn[(size_t) i] = 0.2f * (float) std::sin (acquisitionPhase);
            acquisitionPhase += 6.2831853071795864769 * 456.0 / sr;
        }
        acquisitionEngine.processCorrected (acquisitionIn.data(), acquisitionOut.data(),
                                            block, settings);
        const auto pitch = acquisitionEngine.getPitchResult();
        if (pitch.voiced && voicedAt < 0) voicedAt = offset + block;
        if (pitch.voiced && std::abs (acquisitionEngine.getCorrectionCents()) >= 50.0)
            correctedAt = offset + block;
    }
    const double voicedMs = 1000.0 * (double) voicedAt / sr;
    const double correctedMs = 1000.0 * (double) correctedAt / sr;

    sattari::dsp::ExtremeLivePitchEngine lowRangeEngine;
    lowRangeEngine.prepare (sr, block, 70.0, 1100.0);
    std::vector<float> lowRangeIn ((size_t) block), lowRangeOut ((size_t) block);
    constexpr double lowRangeHz = 82.406889; // E2
    double lowRangePhase = 0.0;
    for (int offset = 0; offset < total; offset += block)
    {
        for (int i = 0; i < block; ++i)
        {
            lowRangeIn[(size_t) i] = 0.2f * (float) std::sin (lowRangePhase);
            lowRangePhase += 6.2831853071795864769 * lowRangeHz / sr;
        }
        lowRangeEngine.processCorrected (lowRangeIn.data(), lowRangeOut.data(),
                                        block, settings);
    }
    const auto lowPitch = lowRangeEngine.getPitchResult();
    const double lowRangeError = lowPitch.voiced
        ? std::abs (1200.0 * std::log2 (lowPitch.frequencyHz / lowRangeHz)) : 9999.0;

    sattari::dsp::ExtremePitchShifter unityShifter;
    unityShifter.prepare (sr, 512);
    std::vector<float> unity ((size_t) total);
    for (int offset = 0; offset < total; offset += block)
        unityShifter.process (input.data() + offset, unity.data() + offset,
                              std::min (block, total - offset), 1.0, true,
                              sr / sourceHz);
    double unityError = 0.0;
    for (int i = unityShifter.getLatencySamples(); i < total; ++i)
        unityError = std::max (unityError,
            std::abs ((double) unity[(size_t) i]
                    - (double) input[(size_t) (i - unityShifter.getLatencySamples())]));
    std::printf ("  blockDelta=%.3f ct energyRatio=%.4f boundaryJump=%.5f unityError=%.9f\n",
                 blockPitchDelta, blockEnergyRatio, boundaryJump, unityError);
    std::printf ("  acquisition voiced=%.2f ms correction=%.2f ms\n",
                 voicedMs, correctedMs);
    std::printf ("  low-range E2 tracker error=%.2f ct voiced=%s\n",
                 lowRangeError, lowPitch.voiced ? "yes" : "no");

    // ---- Regression 1: host block size must not change the output ----------
    // Before the internal control grid the engine re-locked once per call: a
    // 512-sample host block glided into each note over ~80-100 ms instead of
    // ~40 ms at 64 and the rendered audio differed by up to 0.63 FS.
    bool blockInvariant = true;
    double worstBlockDiff = 0.0, worstLegatoSettle = 0.0;
    {
        std::vector<Note> notes;
        const auto phrase = makeNotes (sr, { 60.35, 61.70, 64.40, 67.30, 68.60, 63.65 },
                                       { 60, 62, 64, 67, 69, 64 }, 350.0, 0.0, 120.0, notes);
        auto legatoSettings = hardChromatic();
        legatoSettings.retuneMilliseconds = 3.0;
        int latency = 0;
        const auto reference = renderEngine (phrase, sr, 64, legatoSettings, latency);
        for (int hostBlock : { 1, 7, 512, 2048 })
        {
            const auto y = renderEngine (phrase, sr, hostBlock, legatoSettings, latency);
            for (size_t i = 0; i < y.size(); ++i)
                worstBlockDiff = std::max (worstBlockDiff, (double) std::abs (y[i] - reference[i]));
            if (hostBlock == 512)
                for (size_t n = 1; n < notes.size(); ++n)   // legato transitions
                {
                    const double ms = settleMs (y, sr, notes[n], latency);
                    worstLegatoSettle = ms < 0.0 ? 1.0e9 : std::max (worstLegatoSettle, ms);
                }
        }
        blockInvariant = worstBlockDiff < 1.0e-7 && worstLegatoSettle <= 35.0;
        std::printf ("  block invariance: max |y(1,7,512,2048) - y(64)| = %.3g, legato settle @512 max %.1f ms\n",
                     worstBlockDiff, worstLegatoSettle);
    }

    // ---- Regression 2: onsets after silence --------------------------------
    // Detuned (+40 ct) notes after 300 ms of silence. Before onset trimming the
    // short tracker had to fill its 21 ms frame: E4/A4/C5 settled to +-10 ct in
    // ~40-55 ms (first note) at hard retune. Octave safety: no voiced tracker
    // reading may sit > 150 ct from the sung pitch, also with a weak fundamental.
    bool onsetOk = true;
    {
        double worst = 0.0, sum = 0.0;
        int count = 0, octaveErrors = 0;
        for (double base : { 64.0, 69.0, 72.0 })
        {
            std::vector<Note> notes;
            const auto x = makeNotes (sr, { base + 0.40, base - 0.40 }, { (int) base, (int) base },
                                      400.0, 300.0, 300.0, notes);
            int latency = 0;
            const auto y = renderEngine (x, sr, 64, hardChromatic(), latency, false, &notes, &octaveErrors);
            for (const auto& n : notes)
            {
                const double ms = settleMs (y, sr, n, latency);
                worst = ms < 0.0 ? 1.0e9 : std::max (worst, ms);
                sum += ms; ++count;
            }
        }
        // Weak fundamental (H1 0.15, H2 1.0, H3 0.6, H4 0.3), low notes. (A
        // near octave-symmetric wave - weak H1 and weak odd harmonics - is read
        // an octave up for the whole note by this YIN tracker before and after
        // the onset change; that is a separate, pre-existing limitation.)
        for (double base : { 45.0, 50.0, 57.0, 64.0 })
        {
            std::vector<Note> notes;
            const auto x = makeNotes (sr, { base + 0.40, base - 0.40 }, { (int) base, (int) base },
                                      400.0, 300.0, 300.0, notes, 0.15, 1.0, 0.6, 0.3);
            int latency = 0;
            renderEngine (x, sr, 64, hardChromatic(), latency, false, &notes, &octaveErrors);
        }
        const double mean = sum / std::max (1, count);
        onsetOk = worst <= 25.0 && mean <= 20.0 && octaveErrors == 0;
        std::printf ("  onset after silence (E4/A4/C5 +-40 ct, hard): mean %.1f ms, max %.1f ms, tracker octave errors %d\n",
                     mean, worst, octaveErrors);
    }

    // ---- Regression 3: formant preservation --------------------------------
    // F1/F2 of vowel-like /e/ (530/1840 Hz) at 130 and 220 Hz shifted +-1 and
    // +-3 semitones. The bare shifter moves the envelope with the pitch (about
    // +-6 % and +-19 %); the LPC option must hold it, at the same latency.
    bool formantOk = true;
    {
        double legacyF2 = 0.0, worstF1 = 0.0, worstF2 = 0.0;
        int formantLatency = 0, legacyLatency = 0;
        for (double f0 : { 130.0, 220.0 })
        {
            const auto vowel = makeVowel (sr, f0, { 530.0, 1840.0, 2480.0, 3500.0 }, 1.6);
            const auto inputEnvelope = lpcEnvelope (vowel, sr, (int) (0.4 * sr));
            const double f1In = strongestPeak (inputEnvelope, 320.0, 770.0);
            const double f2In = strongestPeak (inputEnvelope, 1290.0, 2480.0);
            for (double semitones : { -3.0, -1.0, 1.0, 3.0 })
                for (bool formant : { false, true })
                {
                    sattari::dsp::ExtremeLivePitchEngine engine;
                    engine.prepare (sr, block, 70.0, 1100.0);
                    engine.setFormantPreservation (formant);
                    engine.reset();
                    (formant ? formantLatency : legacyLatency) = engine.getLatencySamples();
                    std::vector<float> y (vowel.size());
                    for (int pos = 0; pos < (int) vowel.size(); pos += block)
                        engine.process (vowel.data() + pos, y.data() + pos,
                                        std::min (block, (int) vowel.size() - pos),
                                        std::pow (2.0, semitones / 12.0));
                    const auto envelope = lpcEnvelope (y, sr, (int) (0.4 * sr));
                    const double f1 = 100.0 * std::abs (strongestPeak (envelope, 320.0, 770.0) / f1In - 1.0);
                    const double f2 = 100.0 * std::abs (strongestPeak (envelope, 1290.0, 2480.0) / f2In - 1.0);
                    if (formant) { worstF1 = std::max (worstF1, f1); worstF2 = std::max (worstF2, f2); }
                    else if (std::abs (semitones) > 2.0) legacyF2 = std::max (legacyF2, f2);
                }
        }

        // Unity and unvoiced output stay the exact delayed input with the option on.
        sattari::dsp::ExtremeLivePitchEngine nullEngine;
        nullEngine.prepare (sr, block, 70.0, 1100.0);
        nullEngine.setFormantPreservation (true);
        nullEngine.reset();
        std::vector<float> nullOut (input.size());
        for (int pos = 0; pos < total; pos += block)
            nullEngine.process (input.data() + pos, nullOut.data() + pos, std::min (block, total - pos), 1.0);
        double nullError = 0.0;
        for (int i = formantLatency; i < total; ++i)
            nullError = std::max (nullError, (double) std::abs (nullOut[(size_t) i] - input[(size_t) (i - formantLatency)]));

        formantOk = worstF1 <= 7.0 && worstF2 <= 5.0 && legacyF2 >= 12.0
                 && formantLatency == legacyLatency && nullError < 1.0e-6;
        std::printf ("  formant /e/ +-1,+-3 st: legacy max F2 shift (3 st) %.1f %%, formant mode max |dF1| %.1f %% |dF2| %.1f %%, latency %d vs %d samples, unity error %.3g\n",
                     legacyF2, worstF1, worstF2, formantLatency, legacyLatency, nullError);
    }

    // ---- Regression 4: formant mode across pauses ---------------------------
    // A voice with silent gaps (0.5 s sung, 0.25 s silent, +40 c) through the
    // formant path. Previously the silent-frame LPC update re-applied its last
    // glide step every hop, the lattice coefficients ramped past |k| = 1 and the
    // next note blew up to Inf/NaN (and, once that was held instead, the stale
    // model overshot onsets by ~+20 dB). Output must stay finite and bounded.
    bool pauseOk = true;
    {
        const double psr = 48000.0;
        const int total = (int) (psr * 3.0), block = 512;
        std::vector<float> in ((size_t) total), out ((size_t) total);
        double ph = 0.0;
        float inPeak = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            ph += 2.0 * 3.141592653589793 * 220.0 * std::pow (2.0, 40.0 / 1200.0) / psr;
            const double env = std::fmod (i / psr, 0.75) < 0.5 ? 0.25 : 0.0;
            in[(size_t) i] = (float) (env * (std::sin (ph) + 0.5 * std::sin (2.0 * ph) + 0.3 * std::sin (3.0 * ph)));
            inPeak = std::max (inPeak, std::abs (in[(size_t) i]));
        }
        sattari::dsp::ExtremeLivePitchEngine engine;
        engine.prepare (psr, block);
        engine.setFormantPreservation (true);
        engine.reset();
        sattari::dsp::LiveCorrectionPlanner::Settings s;
        s.amount = 1.0;
        s.retuneMilliseconds = 6.0;
        s.enabledNotes.fill (true);
        for (int p = 0; p < total; p += block)
            engine.processCorrected (in.data() + p, out.data() + p, std::min (block, total - p), s);
        int nonFinite = 0;
        float outPeak = 0.0f;
        for (float v : out)
        {
            if (! std::isfinite (v)) ++nonFinite;
            else outPeak = std::max (outPeak, std::abs (v));
        }
        pauseOk = nonFinite == 0 && outPeak <= 1.6f * inPeak;
        std::printf ("  formant across pauses: non-finite %d, peak %.3f vs input %.3f (limit 1.6x)\n",
                     nonFinite, outPeak, inPeak);
    }

    const bool ok = blockInvariant && onsetOk && formantOk && pauseOk
                 && shifter.getLatencySamples() <= 110
                 && std::abs (cents) < 20.0
                 && targetPower > 4.0 * sourcePower
                 && blockPitchDelta < 2.0
                 && blockEnergyRatio > 0.98 && blockEnergyRatio < 1.02
                 && boundaryJump < 0.05
                 && voicedAt > 0 && correctedAt > 0
                 && voicedMs <= 30.0 && correctedMs <= 45.0
                 && lowPitch.voiced && lowRangeError < 10.0
                 && unityError < 1.0e-6;
    std::printf (ok ? "extreme live pitch contract ok\n"
                    : "extreme live pitch contract FAILED\n");
    return ok ? 0 : 1;
}
