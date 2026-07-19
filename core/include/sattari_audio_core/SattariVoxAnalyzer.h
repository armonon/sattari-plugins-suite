//  Sattari Audio Suite — VoxSynth real-time analysis front-end.
//
//  The make-or-break layer of VoxSynth (voice -> instrument). It turns a mono voice
//  stream into a frame-rate description the synth / MIDI / MPE layers consume:
//     - continuous pitch (Hz + confidence), octave-suppressed and confidence-held
//     - a per-onset event, CLASSIFIED as voiced onset / plosive / fricative / legato
//     - amplitude envelope + spectral centroid (for VCA and filter/timbre mapping)
//     - optional percussion classification (kick / snare / hat) for beatbox mode
//
//  Why the split matters: pitch detection wants a long window (a 65 Hz period is
//  ~15 ms), but note-ON wants a short one. So onset fires from a fast spectral-flux
//  detector the instant a transient arrives, carrying the PREVIOUS voiced pitch, and
//  the pitch tracker corrects within 2-3 frames — the attack masks the correction.
//  That separation is what makes "duh duh duh" beatboxing and sung "aaah" both work,
//  which is exactly where voice-to-MIDI competitors fall down.
//
//  Analysis-only: no synthesis, no MIDI. The plugin maps Frame -> synth/MPE. Pure-std
//  (embedded radix-2 FFT + the shared YIN PitchDetector), RT-safe after prepare().
#pragma once

#include <sattari_audio_core/SattariPitchDetector.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace sattari::dsp
{

class VoxAnalyzer
{
public:
    enum class Event : int { None = 0, VoicedOnset, Plosive, Fricative, Legato };
    enum class Perc  : int { None = 0, Kick, Snare, HatClosed, HatOpen };

    // One per completed hop. sampleOffset locates the hop's end inside the block just
    // processed, so the plugin can emit sample-accurate MIDI / retrigger the synth.
    struct Frame
    {
        int    sampleOffset = 0;
        bool   voiced       = false;
        float  frequencyHz  = 0.0f;   // continuous, octave-suppressed, held on low conf
        float  midi         = -1.0f;  // 69 + 12*log2(f/440); -1 when unvoiced
        float  confidence   = 0.0f;   // YIN periodicity 0..1
        float  amplitude    = 0.0f;   // 0..1 RMS envelope (the "VCA")
        float  centroidHz   = 0.0f;   // spectral centroid (filter / timbre source)
        Event  event        = Event::None;
        float  onsetPitchHz = 0.0f;   // note-on pitch (held from last voiced if plosive)
        Perc   perc         = Perc::None;
    };

    struct Config
    {
        float minF0 = 65.0f;          // C2
        float maxF0 = 1050.0f;        // C6 + falsetto
        int   fftOrder = 10;          // 1024-pt spectral analysis
        int   hop = 256;              // ~5.3 ms @ 48k
        float onsetThreshMult = 1.6f; // adaptive: median(onset fn, 200 ms) * this
        float onsetFloor = 0.04f;     // absolute floor so silence never triggers
        float legatoCents = 80.0f;    // pitch move w/o onset => bend, not retrigger
        float onsetRefractoryMs = 50.0f;
        float confHold = 0.5f;        // below this, freeze last good pitch
        float confTrust = 0.9f;       // above this, accept even an octave jump
        bool  percussionMode = false;
    };

    void prepare (double sampleRate) { prepare (sampleRate, Config{}); }
    void prepare (double sampleRate, const Config& cfg)
    {
        cfg_    = cfg;
        sr_     = sampleRate > 0.0 ? sampleRate : 48000.0;
        fftN_   = 1 << std::max (6, cfg.fftOrder);
        hop_    = std::max (32, cfg.hop);
        bins_   = fftN_ / 2 + 1;
        binHz_  = (float) (sr_ / fftN_);

        ring_.assign ((size_t) fftN_, 0.0f);
        win_.assign  ((size_t) fftN_, 0.0f);
        for (int i = 0; i < fftN_; ++i)
            win_[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * i / (fftN_ - 1));
        re_.assign  ((size_t) fftN_, 0.0f);
        im_.assign  ((size_t) fftN_, 0.0f);
        mag_.assign ((size_t) bins_, 0.0f);
        prevMag_.assign ((size_t) bins_, 0.0f);
        buildFftTables();

        // Adaptive onset threshold history ~200 ms of hops.
        onsetHistLen_ = std::max (8, (int) (0.2 * sr_ / hop_));
        onsetHist_.assign ((size_t) onsetHistLen_, 0.0f);
        onsetHistPos_ = 0;

        pitch_.prepare (sr_, cfg.minF0, cfg.maxF0, 0.045f);

        hopBuf_.assign ((size_t) hop_, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (ring_.begin(), ring_.end(), 0.0f);
        std::fill (prevMag_.begin(), prevMag_.end(), 0.0f);
        std::fill (onsetHist_.begin(), onsetHist_.end(), 0.0f);
        pitch_.reset();
        hopFill_ = 0; ringPos_ = 0; onsetHistPos_ = 0;
        ampEnv_ = 0.0f; ampSlow_ = 0.0f; prevRms_ = 0.0f; prevOnsetFn_ = 0.0f; onsetAvg_ = 0.0f; armed_ = true;
        refractory_ = 0; lastGoodHz_ = 0.0f; prevPitchHz_ = 0.0f; noteOnsetPitch_ = 0.0f;
        framesSinceVoiced_ = 100000; percPending_ = 0; med0_ = med1_ = med2_ = 0.0f;
    }

    void setPercussionMode (bool on) noexcept { cfg_.percussionMode = on; }

    // Reported to the host via setLatencySamples(). Onset itself is ~hop-latency; this
    // is the pitch window, the honest end-to-end analysis latency.
    int latencySamples() const noexcept { return pitch_.windowLength(); }
    int maxFramesForBlock (int n) const noexcept { return n / hop_ + 2; }

    // Fills `out` with one Frame per completed hop; returns the count. RT-safe.
    int process (const float* mono, int n, Frame* out, int maxOut)
    {
        int produced = 0;
        for (int i = 0; i < n; ++i)
        {
            hopBuf_[(size_t) hopFill_++] = mono[i];
            ring_[(size_t) ringPos_] = mono[i];
            ringPos_ = (ringPos_ + 1) % fftN_;

            if (hopFill_ >= hop_)
            {
                hopFill_ = 0;
                if (produced < maxOut)
                    out[produced] = analyseHop (i);
                ++produced;
            }
        }
        return produced;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    Frame analyseHop (int sampleOffset)
    {
        Frame f; f.sampleOffset = sampleOffset;

        // --- spectrum of the last fftN_ samples (Hann) ------------------------
        for (int k = 0; k < fftN_; ++k)
        {
            re_[(size_t) k] = ring_[(size_t) ((ringPos_ + k) % fftN_)] * win_[(size_t) k];
            im_[(size_t) k] = 0.0f;
        }
        fft (re_.data(), im_.data());
        double total = 1.0e-9, centroidAcc = 0.0, low = 0.0, midHigh = 0.0, high = 0.0, flux = 0.0;
        for (int b = 0; b < bins_; ++b)
        {
            const float m = std::sqrt (re_[(size_t) b] * re_[(size_t) b] + im_[(size_t) b] * im_[(size_t) b]);
            mag_[(size_t) b] = m;
            const float fHz = b * binHz_;
            total += m; centroidAcc += (double) fHz * m;
            if (fHz < 200.0f)  low  += m;
            if (fHz > 2000.0f) midHigh += m;
            if (fHz > 6000.0f) high += m;
            const float d = m - prevMag_[(size_t) b];
            if (d > 0.0f) flux += d;
        }
        std::memcpy (prevMag_.data(), mag_.data(), sizeof (float) * (size_t) bins_);
        f.centroidHz = (float) (centroidAcc / total);
        const float lowRatio      = (float) (low  / total);
        const float midHighRatio  = (float) (midHigh / total);
        const float highRatio     = (float) (high / total);
        const float fluxN = (float) (flux / total);

        // --- amplitude env (5 ms attack / 30 ms release) ----------------------
        double e = 0.0; for (int k = 0; k < hop_; ++k) e += (double) hopBuf_[(size_t) k] * hopBuf_[(size_t) k];
        const float rms = (float) std::sqrt (e / hop_);
        const float atk = 1.0f - std::exp (-(float) hop_ / (0.005f * (float) sr_));
        const float rel = 1.0f - std::exp (-(float) hop_ / (0.030f * (float) sr_));
        ampEnv_ += (rms > ampEnv_ ? atk : rel) * (rms - ampEnv_);
        f.amplitude = std::min (1.0f, ampEnv_ * 4.0f);

        // --- pitch (YIN) + octave suppression + confidence hold ---------------
        pitch_.pushBlock (hopBuf_.data(), hop_);
        const auto pr = pitch_.analyse (0.1f);
        float fHz = pr.frequencyHz; const float conf = pr.confidence;
        // median-of-3 on the raw estimate kills isolated octave flips.
        med2_ = med1_; med1_ = med0_; med0_ = fHz;
        const float fMed = median3 (med0_, med1_, med2_);

        bool voiced = false;
        if (conf >= cfg_.confHold && fMed > 0.0f)
        {
            bool octaveJump = false;
            if (lastGoodHz_ > 0.0f && conf < cfg_.confTrust)
            {
                const float r = fMed / lastGoodHz_;
                octaveJump = (std::abs (r - 2.0f) < 0.12f) || (std::abs (r - 0.5f) < 0.06f);
            }
            f.frequencyHz = octaveJump ? lastGoodHz_ : fMed;
            if (! octaveJump) lastGoodHz_ = fMed;
            voiced = true; framesSinceVoiced_ = 0;
        }
        else
        {
            // Freeze the last good pitch — a held note beats a wrong note beats silence.
            f.frequencyHz = lastGoodHz_;
            ++framesSinceVoiced_;
            voiced = (lastGoodHz_ > 0.0f && framesSinceVoiced_ < 6);
        }
        f.voiced = voiced;
        f.confidence = conf;
        f.midi = (f.frequencyHz > 0.0f) ? 69.0f + 12.0f * std::log2 (f.frequencyHz / 440.0f) : -1.0f;

        // --- onset detection --------------------------------------------------
        // For a MONOPHONIC voice the reliable trigger is the amplitude envelope's
        // relative rise: it spikes at every note/consonant attack and sits flat during
        // sustain AND during a glide (where the spectrum moves but the level doesn't).
        // Spectral flux has per-hop ripple on a harmonic tone, so it only contributes
        // as a SECONDARY trigger for true broadband spikes (a "t" that barely moves
        // level). A slow amplitude baseline + hysteresis stops the re-trigger storm.
        prevRms_ = rms;
        const float ratio     = ampEnv_ / (ampSlow_ + 1.0e-4f);
        const bool  ampOnset  = ratio > 1.6f;
        const bool  fluxSpike = (fluxN > 0.35f) && (fluxN > onsetAvg_ * 3.0f);

        if (refractory_ > 0) --refractory_;
        const bool isOnset = armed_ && (ampOnset || fluxSpike) && (refractory_ == 0) && (rms > 1.0e-3f);
        if (isOnset) armed_ = false;
        else if (! armed_ && ratio < 1.15f && fluxN < 0.2f) armed_ = true;   // re-arm at baseline

        // Track the baselines when NOT mid-attack, so the attack itself can't inflate
        // them (which would raise the bar and miss fast re-articulations).
        const float sCoef = 1.0f - std::exp (-(float) hop_ / (0.10f * (float) sr_));
        const float aCoef = 1.0f - std::exp (-(float) hop_ / (0.12f * (float) sr_));
        if (refractory_ == 0)
        {
            ampSlow_  += sCoef * (ampEnv_ - ampSlow_);
            onsetAvg_ += aCoef * (fluxN   - onsetAvg_);
        }
        prevOnsetFn_ = fluxN;

        if (isOnset)
        {
            refractory_ = (int) (cfg_.onsetRefractoryMs * 0.001 * sr_ / hop_);
            f.event = Event::VoicedOnset;   // generic note-on; the class is refined below
            if (cfg_.percussionMode)
            {
                // Defer the drum-class decision a couple hops: at the transient instant
                // the FFT window is still mostly the pre-hit silence, so the spectrum
                // hasn't filled in. The note-on fires now; percPending_ tags the class
                // onto the frame ~11 ms later where the hit's spectrum is fully present.
                percPending_ = 2;
            }
            else
            {
                // Voiced vs plosive vs fricative from the ENERGY DISTRIBUTION, which is
                // reliable at the instant (periodicity/confidence lag the attack): a sung
                // vowel keeps energy low (harmonics), a plosive/burst spreads it upward,
                // a fricative is high-frequency-dominated.
                if (highRatio > 0.40f)        f.event = Event::Fricative;
                else if (midHighRatio > 0.50f) f.event = Event::Plosive;
                else                          f.event = Event::VoicedOnset;
            }
            f.onsetPitchHz = (f.event == Event::VoicedOnset && fMed > 0.0f) ? fMed
                            : (lastGoodHz_ > 0.0f ? lastGoodHz_ : fMed);
            noteOnsetPitch_ = f.onsetPitchHz;   // may be 0 if pitch hasn't resolved yet
        }
        else
        {
            if (percPending_ > 0 && --percPending_ == 0)
                f.perc = classifyPerc (lowRatio, highRatio, f.centroidHz);
            // Capture the note's reference pitch as soon as it resolves (the onset can
            // fire a frame or two before YIN locks), so legato has something to measure
            // against instead of a 0.
            if (voiced && noteOnsetPitch_ <= 0.0f && f.frequencyHz > 0.0f)
                noteOnsetPitch_ = f.frequencyHz;
            if (voiced && noteOnsetPitch_ > 0.0f && f.frequencyHz > 0.0f)
            {
                // Legato = bent >legatoCents from the note's start WITHOUT a new onset.
                // A smooth glide moves only a few cents per hop, so measure cumulative
                // drift from the onset pitch, not the per-frame delta.
                const float cents = 1200.0f * std::log2 (f.frequencyHz / noteOnsetPitch_);
                if (std::abs (cents) > cfg_.legatoCents) f.event = Event::Legato;
            }
        }
        if (f.frequencyHz > 0.0f) prevPitchHz_ = f.frequencyHz;

        return f;
    }

    Perc classifyPerc (float lowRatio, float highRatio, float centroidHz) const
    {
        if (lowRatio > 0.5f) return Perc::Kick;
        if (centroidHz > 6000.0f || highRatio > 0.35f)
            return Perc::HatClosed;   // open/closed needs a decay-slope lookahead (v1.1)
        return Perc::Snare;
    }

    static float median3 (float a, float b, float c)
    {
        return std::max (std::min (a, b), std::min (std::max (a, b), c));
    }
    float medianOf (const std::vector<float>& v) const
    {
        scratch_ = v;
        std::nth_element (scratch_.begin(), scratch_.begin() + scratch_.size() / 2, scratch_.end());
        return scratch_[scratch_.size() / 2];
    }

    // --- minimal iterative radix-2 FFT (in place, forward) --------------------
    void buildFftTables()
    {
        rev_.assign ((size_t) fftN_, 0);
        int order = 0; while ((1 << order) < fftN_) ++order;
        for (int i = 0; i < fftN_; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev_[(size_t) i] = r;
        }
        cos_.assign ((size_t) (fftN_ / 2), 0.0f); sin_.assign ((size_t) (fftN_ / 2), 0.0f);
        for (int i = 0; i < fftN_ / 2; ++i)
        {
            const double a = -2.0 * kPi * i / fftN_;
            cos_[(size_t) i] = (float) std::cos (a); sin_[(size_t) i] = (float) std::sin (a);
        }
    }
    void fft (float* re, float* im) const
    {
        for (int i = 0; i < fftN_; ++i)
            if (i < rev_[(size_t) i]) { std::swap (re[i], re[rev_[(size_t) i]]); std::swap (im[i], im[rev_[(size_t) i]]); }
        for (int len = 2; len <= fftN_; len <<= 1)
        {
            const int half = len >> 1, step = fftN_ / len;
            for (int i = 0; i < fftN_; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const int t = j * step; const float wr = cos_[(size_t) t], wi = sin_[(size_t) t];
                    const int a = i + j, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                }
        }
    }

    Config cfg_;
    double sr_ = 48000.0;
    int    fftN_ = 1024, hop_ = 256, bins_ = 513;
    float  binHz_ = 46.9f;
    PitchDetector pitch_;

    std::vector<float> ring_, win_, re_, im_, mag_, prevMag_, hopBuf_, onsetHist_;
    std::vector<float> cos_, sin_; std::vector<int> rev_;
    mutable std::vector<float> scratch_;
    int    ringPos_ = 0, hopFill_ = 0, onsetHistLen_ = 40, onsetHistPos_ = 0, refractory_ = 0;
    int    framesSinceVoiced_ = 100000, percPending_ = 0;
    bool   armed_ = true;
    float  ampEnv_ = 0.0f, ampSlow_ = 0.0f, prevRms_ = 0.0f, prevOnsetFn_ = 0.0f, onsetAvg_ = 0.0f;
    float  lastGoodHz_ = 0.0f, prevPitchHz_ = 0.0f, noteOnsetPitch_ = 0.0f;
    float  med0_ = 0.0f, med1_ = 0.0f, med2_ = 0.0f;
};

} // namespace sattari::dsp
