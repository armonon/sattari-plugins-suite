#pragma once

// =============================================================================
//  rc::FeedConditioner — processor atomics -> smoothed Compass state
// -----------------------------------------------------------------------------
//  Pure and deterministic (UI thread only). Each tick the Compass takes a raw
//  CompassFeed snapshot (seven meters, detected pitch, effective key/scale,
//  style and the ten macros, all relaxed loads) and this turns it into the
//  musical state the plate and renderer draw: level/energy envelopes, transient
//  pulse + burst, voiced/idle/calm, the displayed note (scale-snapped when TUNE
//  is engaged, so the plate shows the note you HEAR) and its scale degree.
// =============================================================================

namespace rc
{
struct CompassFeed
{
    float in = 0, out = 0, conf = 0, pitchHz = 0, gr = 0, sib = 0, width = 0, lim = 0;
    int key = 0, scale = 0, style = 0;
    float clean = 0.55f, tune = 0, control = 0.58f, tone = 0.52f, air = 0.42f, smooth = 0.44f,
          glow = 0.32f, widthMacro = 0.25f, space = 0.22f, royal = 0.5f;
};

struct CompassState
{
    float level = 0, energy = 0, pulse = 0, confS = 0, resp = 0, cents = 0, sibS = 0, widthMeterS = 0,
          limFlash = 0, overtone = 0.15f, primaryAmp = 1.0f, activity = 0.3f, inN = 0, outN = 0;
    bool voiced = false, burst = false, idle = false, calm = false, realign = false;
    bool rich = false;   // expressive style: the overtone leads (one more nodal ring)
    int note = -1, degree = 0;   // note = -1 when none
};

class FeedConditioner
{
public:
    void reset() noexcept;
    const CompassState& update (const CompassFeed&, float dt) noexcept;
    const CompassState& state() const noexcept { return st; }
    void noteInteraction() noexcept { interactionPending = true; }
    static int degreeOf (int midi, int key) noexcept { return ((midi - key) % 12 + 12) % 12; }

private:
    CompassState st;
    float inSlow = 0.0f, silenceTime = 0.0f, calmTime = 0.0f, sinceBurst = 10.0f;
    float prevPulse = 0.0f;
    int candidate = -1, lastNote = -1;
    float candidateTime = 0.0f;
    float lastMacros[12] {};
    bool haveMacros = false, interactionPending = false;
};
} // namespace rc
