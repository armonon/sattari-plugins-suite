#include "CompassFeed.h"
#include "RoyalChainTheme.h"
#include <sattari_audio_core/SattariPitchDetector.h>

#include <algorithm>
#include <cmath>

namespace rc
{
namespace
{
inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float smoothstep (float a, float b, float x) noexcept
{
    const float t = clamp01 ((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// One-pole coefficient for time constant tau: k = 1 - exp(-dt / tau).
inline float kOf (float dt, float tau) noexcept { return 1.0f - std::exp (-dt / tau); }

inline float follow (float cur, float target, float dt, float attack, float release) noexcept
{
    return cur + (target - cur) * kOf (dt, target > cur ? attack : release);
}

// Peak meter (linear) -> 0..1 over -48..-6 dBFS.
inline float dbNorm (float gain) noexcept
{
    const float db = gain > 0.0f ? std::max (-100.0f, 20.0f * std::log10 (gain)) : -100.0f;
    return clamp01 ((db + 48.0f) / 42.0f);
}
} // namespace

void FeedConditioner::reset() noexcept
{
    st = CompassState {};
    inSlow = silenceTime = calmTime = candidateTime = prevPulse = 0.0f;
    sinceBurst = 10.0f;
    candidate = lastNote = -1;
    haveMacros = interactionPending = false;
}

const CompassState& FeedConditioner::update (const CompassFeed& f, float dt) noexcept
{
    dt = std::max (1.0e-4f, std::min (0.25f, dt));

    // Envelopes.
    st.inN = dbNorm (f.in);
    st.outN = dbNorm (f.out);
    st.level = follow (st.level, st.outN, dt, 0.020f, 0.250f);
    st.energy = st.level * (1.0f - 0.5f * clamp01 (f.gr));

    // Transient: fast input rise over its own slow follower.
    inSlow += (st.inN - inSlow) * kOf (dt, 0.150f);
    const float tr = clamp01 ((st.inN - inSlow) * 4.0f);
    st.pulse = std::max (st.pulse * std::exp (-dt / 0.12f), tr);
    sinceBurst += dt;
    st.burst = prevPulse < 0.35f && st.pulse >= 0.35f && sinceBurst >= 0.12f;
    if (st.burst)
        sinceBurst = 0.0f;
    prevPulse = st.pulse;

    st.confS += (clamp01 (f.conf) - st.confS) * kOf (dt, 0.080f);
    st.sibS += (clamp01 (f.sib) - st.sibS) * kOf (dt, 0.050f);
    st.widthMeterS += (clamp01 (f.width) - st.widthMeterS) * kOf (dt, 0.200f);
    st.limFlash = follow (st.limFlash, clamp01 (f.lim), dt, 0.030f, 0.300f);

    // Voicing and the note you hear.
    const float hz = f.pitchHz;
    st.voiced = hz > 60.0f && hz < 1200.0f && st.confS > 0.45f && st.level > 0.05f;
    const int key = ((f.key % 12) + 12) % 12;
    if (st.voiced)
    {
        const float midiF = sattari::dsp::hzToMidi (hz);
        int m = (int) std::lround (midiF);
        if (f.tune >= 0.05f)
            m = sattari::dsp::snapMidiToScale (m, key, (sattari::dsp::ScaleId) std::max (0, std::min (7, f.scale)));

        if (m == lastNote)
        {
            candidate = -1;
            candidateTime = 0.0f;
        }
        else if (m == candidate)
        {
            candidateTime += dt;
            if (candidateTime >= 0.070f)
            {
                lastNote = m;
                candidate = -1;
                candidateTime = 0.0f;
            }
        }
        else
        {
            candidate = m;
            candidateTime = 0.0f;
        }
        st.cents = lastNote >= 0 ? (midiF - (float) lastNote) * 100.0f : 0.0f;
    }
    else
    {
        candidate = -1;
        candidateTime = 0.0f;
    }
    st.note = st.voiced ? lastNote : -1;

    const float respTarget = st.voiced ? smoothstep (0.45f, 0.85f, st.confS) * smoothstep (0.05f, 0.35f, st.level) : 0.0f;
    st.resp = follow (st.resp, respTarget, dt, 0.080f, 0.600f);

    // Idle: the figure returns to the tonic crown and eases back onto the pucks.
    if (! st.voiced && st.level < 0.02f)
        silenceTime += dt;
    else
        silenceTime = 0.0f;
    st.idle = silenceTime > 1.5f;
    if (st.idle)
        lastNote = -1;
    st.degree = (! st.idle && lastNote >= 0) ? degreeOf (lastNote, key) : 0;
    st.realign = st.idle && st.resp < 0.15f;

    // Calm (30 Hz) after 8 s of idle with no interaction and no parameter change.
    const float macros[12] = { f.clean, f.tune, f.control, f.tone, f.air, f.smooth, f.glow, f.widthMacro,
                               f.space, f.royal, (float) f.style, (float) (f.key * 16 + f.scale) };
    bool changed = interactionPending;
    interactionPending = false;
    if (haveMacros)
        for (int i = 0; i < 12; ++i)
            changed = changed || std::abs (macros[i] - lastMacros[i]) > 1.0e-6f;
    std::copy (std::begin (macros), std::end (macros), std::begin (lastMacros));
    haveMacros = true;
    calmTime = (st.idle && ! changed) ? calmTime + dt : 0.0f;
    st.calm = calmTime > 8.0f;

    // Style -> figure complexity. Overtone weight alone only nudges ring radii
    // (a same-n mode pair keeps its topology for any weight <= 1), so expressive
    // styles let the overtone LEAD: every figure then draws one more nodal ring.
    const int style = std::max (0, std::min (theme::kNumStyles - 1, f.style));
    const float complexity = theme::kStyleComplexity[style];
    st.rich = complexity >= 0.7f;
    st.primaryAmp = st.rich ? 0.35f : 1.0f;
    st.overtone = st.rich ? 1.0f
                          : std::max (0.0f, std::min (0.75f, 0.15f + 0.35f * clamp01 (f.tune) + 0.25f * complexity
                                                                 + (lastNote >= 64 ? 0.15f : 0.0f)));
    st.activity = std::max (0.30f, st.energy);
    return st;
}
} // namespace rc
