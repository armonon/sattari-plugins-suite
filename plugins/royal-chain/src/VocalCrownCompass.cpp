#include "VocalCrownCompass.h"
#include "RoyalChainProcessor.h"
#include <sattari_audio_core/SattariPitchDetector.h>

#include <algorithm>
#include <cmath>

namespace th = rc::theme;

namespace
{
constexpr float kPi  = juce::MathConstants<float>::pi;
constexpr float kDeg = kPi / 180.0f;

#if JUCE_DEBUG
constexpr int kComputeSize = 256, kGrains = 4096, kStartLadder = 2;
constexpr bool kDebugPacing = true;
#else
constexpr int kComputeSize = 440, kGrains = 6144, kStartLadder = 0;
constexpr bool kDebugPacing = false;
#endif

enum MacroIdx { mClean, mTune, mControl, mTone, mAir, mSmooth, mGlow, mWidth, mSpace, mRoyal };
constexpr const char* kMacroIds[10] = { "clean", "tune", "control", "tone", "air", "smooth", "glow", "width", "space", "royal" };
constexpr float kMacroDefaults[10] = { 0.55f, 0.0f, 0.58f, 0.52f, 0.42f, 0.44f, 0.32f, 0.25f, 0.22f, 0.5f };

inline float load (const std::atomic<float>* p, float fallback) noexcept
{
    return p != nullptr ? p->load (std::memory_order_relaxed) : fallback;
}

inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
// Caches are built at the physical scale (0.5x .. 4x); paint() compares against
// the same clamp so an out-of-range scale can never re-trigger a rebuild loop.
inline float clampScale (float s) noexcept { return s > 0.0f ? juce::jlimit (0.5f, 4.0f, s) : 1.0f; }
inline float kOf (float dt, float tau) noexcept { return 1.0f - std::exp (-dt / tau); }
inline double msNow() noexcept { return juce::Time::getMillisecondCounterHiRes(); }
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

// Puck radius along its spoke: a value of 0 never sits on the centre, 1 stays
// inside the rim.
inline float radiusFor (float v) noexcept { return VocalCrownCompass::kR * (0.10f + 0.84f * v); }

juce::String noteName (int midi)
{
    if (midi < 0)
        return utf8 ("\xe2\x80\x94");
    return juce::String (th::kNoteNames[midi % 12]) + juce::String (midi / 12 - 1);
}

juce::String figureStructure (const rc::PlateMode& m)
{
    const int spokes = rc::modeSpokes (m), rings = rc::modeRings (m);
    juce::String s;
    if (spokes > 0)
        s << spokes << " SPOKES";
    if (rings > 0)
    {
        if (s.isNotEmpty())
            s << utf8 (" \xc2\xb7 ");
        s << rings << (rings == 1 ? " RING" : " RINGS");
    }
    return s;
}

// Draw a string once into a cached image (never per frame).
void drawString (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r,
                 const juce::FontOptions& f, juce::Colour c, juce::Justification j)
{
    g.setColour (c);
    g.setFont (juce::Font (f));
    g.drawText (t, r, j, false);
}
} // namespace

const juce::Rectangle<int> VocalCrownCompass::ringBox   { 142, 32, 460, 460 };   // c +- (R + 30)
const juce::Rectangle<int> VocalCrownCompass::fieldRect { 152, 42, 440, 440 };   // c +- 1.10 R

//==============================================================================
VocalCrownCompass::VocalCrownCompass (RoyalChainAudioProcessor& p)
    : processor (p),
      vblank (this, [this] (double t) { tick (t); })
{
    setOpaque (true);
    setComponentID ("rc.compass");
    setWantsKeyboardFocus (true);
    setTitle ("Vocal Crown Compass");
    setDescription ("Five chain macros on a vibrating plate; each sung scale degree draws its own figure. "
                    "Left and Right choose a node, Up and Down adjust it (Shift coarse, Command fine), "
                    "Page Up and Page Down step by 10, Home and End set 0 or 100, Delete resets.");

    auto& apvts = processor.parameters();
    pStyle = apvts.getRawParameterValue ("style");
    for (int i = 0; i < 10; ++i)
        pMacro[i] = apvts.getRawParameterValue (kMacroIds[i]);
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        pNode[n] = apvts.getRawParameterValue (th::kNodes[n].paramId);
        nodeParam[n] = apvts.getParameter (th::kNodes[n].paramId);
        const float a = th::kNodes[n].angleDeg * kDeg;
        dir[(size_t) n] = { std::cos (a), std::sin (a) };
        value[(size_t) n] = clamp01 (load (pNode[n], 0.0f));
        shownPct[(size_t) n] = -1;
        arcPath[(size_t) n].preallocateSpace (96);
    }
    membrane.preallocateSpace (48);

    plate.prepare (kGrains, 0x5A77A21u);
    dish.prepare();
    tapOutMid.assign ((size_t) rc::WaterDish::kFFT, 0.0f);
    tapOutSide.assign ((size_t) rc::WaterDish::kFFT, 0.0f);
    tapInMid.assign ((size_t) rc::WaterDish::kFFT, 0.0f);
    conditioner.reset();

    float scale = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = (float) d->scale;
    prepareInternal (scale, false);

    // Open on a settled idle crown: 45 physics frames, the last 8 rendered.
    advanceFrames (45);
}

VocalCrownCompass::~VocalCrownCompass()
{
    if (gestureNode >= 0)
        endNodeDrag (gestureNode);
}

//==============================================================================
void VocalCrownCompass::prepareForScale (float physicalScale)
{
    prepareInternal (physicalScale, true);
}

void VocalCrownCompass::prepareInternal (float physicalScale, bool rewarm)
{
    const float s = clampScale (physicalScale);
    const int up = s >= 1.5f ? 2 : 1;
    dish.setOutputScale (up);   // physical-resolution dish on Retina: 1:1 blit

    preparedScale = s;
    pendingScale = 0.0f;
    ladder = kStartLadder;
    framesSinceLadder = 0;
    frameCostIndex = 0;
    std::fill (std::begin (frameCost), std::end (frameCost), 0.0f);
    applyLadder();

    rebuildStageCache();
    for (auto& l : labelLayers) l.needsRebuild = true;
    pitchLayer.needsRebuild = figureLayer.needsRebuild = true;

    if (rewarm)
        advanceFrames (8);
}

void VocalCrownCompass::applyLadder()
{
    renderer.setQuality (ladder);
    plate.setGrainCount (ladder >= 2 ? juce::jmin (kGrains, 4096) : kGrains);
}

//==============================================================================
rc::CompassFeed VocalCrownCompass::readFeed() const
{
    rc::CompassFeed f;
    f.in = processor.getInputMeter();
    f.out = processor.getOutputMeter();
    f.conf = processor.getTuneConfidence();
    f.pitchHz = processor.getPitchHz();
    f.gr = processor.getControlMeter();
    f.sib = processor.getSibilanceMeter();
    f.width = processor.getWidthMeter();
    f.lim = processor.getLimiterMeter();
    f.key = processor.getEffectiveTuneKey();
    f.scale = processor.getEffectiveTuneScale();
    f.style = juce::roundToInt (load (pStyle, 0.0f));
    f.clean = load (pMacro[mClean], kMacroDefaults[mClean]);
    f.tune = load (pMacro[mTune], kMacroDefaults[mTune]);
    f.control = load (pMacro[mControl], kMacroDefaults[mControl]);
    f.tone = load (pMacro[mTone], kMacroDefaults[mTone]);
    f.air = load (pMacro[mAir], kMacroDefaults[mAir]);
    f.smooth = load (pMacro[mSmooth], kMacroDefaults[mSmooth]);
    f.glow = load (pMacro[mGlow], kMacroDefaults[mGlow]);
    f.widthMacro = load (pMacro[mWidth], kMacroDefaults[mWidth]);
    f.space = load (pMacro[mSpace], kMacroDefaults[mSpace]);
    f.royal = load (pMacro[mRoyal], kMacroDefaults[mRoyal]);
    return f;
}

void VocalCrownCompass::advanceFrames (int n, double dt)
{
    for (int i = 0; i < n; ++i)
        advanceFrame ((float) dt, i >= n - 8);
}

void VocalCrownCompass::advanceFrame (float dt, bool render)
{
    juce::ScopedNoDenormals noDenormals;
    const double t0 = msNow();

    lastFeed = readFeed();
    const auto& st = conditioner.update (lastFeed, dt);
    const bool focused = hasKeyboardFocus (false);

    // Pucks follow the raw parameter atomics: automation and presets move them
    // on the next frame.
    float maxDrag = 0.0f;
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const size_t i = (size_t) n;
        value[i] = clamp01 (load (pNode[n], value[i]));
        const bool hot = n == hoverNode || n == highlightNode || (focused && n == focusedNode);
        hoverAmt[i] += ((hot ? 1.0f : 0.0f) - hoverAmt[i]) * kOf (dt, 0.040f);
        const bool dragging = n == dragNode;
        dragAmt[i] += ((dragging ? 1.0f : 0.0f) - dragAmt[i]) * kOf (dt, dragging ? 0.030f : 0.130f);
        maxDrag = std::max (maxDrag, dragAmt[i]);

        const int pct = juce::roundToInt (value[i] * 100.0f);
        if (pct != shownPct[i] || hot != pillShown[n])
        {
            shownPct[i] = pct;
            pillShown[n] = hot;
            labelLayers[i].needsRebuild = true;
            labelRepaintMask |= 1 << n;
        }
    }

    // Node activity arcs.
    arcValue[0] = st.confS * (lastFeed.tune > 0.001f ? 1.0f : 0.35f);
    arcValue[1] = clamp01 (lastFeed.gr);
    arcValue[2] = st.sibS;
    arcValue[3] = st.level * lastFeed.glow;
    arcValue[4] = st.widthMeterS;
    for (int n = 0; n < th::kNumNodes; ++n)
        if (std::abs (arcValue[(size_t) n] - arcPathValue[(size_t) n]) > 0.005f)
            rebuildArcPath (n, arcValue[(size_t) n]);

    // Vanishing point leans toward high-valued nodes (critically damped, w = 18).
    {
        juce::Point<float> target;
        for (int n = 0; n < th::kNumNodes; ++n)
            target += dir[(size_t) n] * (0.12f * (value[(size_t) n] - 0.5f));
        constexpr float w = 18.0f;
        const juce::Point<float> acc = (target - vp) * (w * w) - vpVel * (2.0f * w);
        vpVel += acc * dt;
        vp += vpVel * dt;
    }

    rc::PlateDrive d;
    d.degree = st.degree;
    d.primary = st.primaryAmp;
    d.overtone = st.overtone;
    d.resp = st.resp;
    d.idleFloor = 0.12f;
    d.energy = st.energy;
    d.pulse = st.pulse;
    d.burst = st.burst;
    d.voiced = st.voiced;
    d.realign = st.realign;
    d.conf = st.confS;
    d.tune = lastFeed.tune;
    d.control = lastFeed.control;
    d.width = lastFeed.widthMacro;
    d.widthMeter = st.widthMeterS;
    juce::ignoreUnused (d);   // the grain plate is no longer drawn; the water dish is
    {
        // The dish sees exactly what the chain is doing: the processed output (mid +
        // side), the unprocessed input, the scale note Tune is steering toward, and
        // the limiter.
        const auto& outTap = processor.getCompassTap();
        outTap.readLatest (tapOutMid.data(), tapOutSide.data(), rc::WaterDish::kFFT);
        processor.getCompassInputTap().readLatest (tapInMid.data(), nullptr, rc::WaterDish::kFFT);
        rc::WaterDish::Frame fr;
        fr.outMid = tapOutMid.data();
        fr.outSide = tapOutSide.data();
        fr.inMid = tapInMid.data();
        fr.sampleRate = outTap.sampleRate();
        // Target = the note Tune is steering this pitch toward RIGHT NOW: the tracked
        // (sung) pitch snapped to the key/scale (nearest semitone when Tune is off),
        // without the display hysteresis, which lags fast real phrases.
        fr.targetHz = 0.0f;
        if (lastFeed.pitchHz > 60.0f && lastFeed.pitchHz < 1500.0f)
        {
            int m = (int) std::lround (sattari::dsp::hzToMidi (lastFeed.pitchHz));
            if (lastFeed.tune >= 0.05f)
                m = sattari::dsp::snapMidiToScale (m, ((lastFeed.key % 12) + 12) % 12,
                                                   (sattari::dsp::ScaleId) juce::jlimit (0, 7, lastFeed.scale));
            fr.targetHz = 440.0f * std::exp2 ((float) (m - 69) / 12.0f);
        }
        fr.trackedHz = lastFeed.pitchHz;
        fr.limiter = lastFeed.lim;
        fr.dt = dt;
        dish.update (fr);
    }
    const double t1 = msNow();

    if (render)
    {
        rc::PortalLook lk;
        lk.glow = lastFeed.glow;
        lk.smooth = lastFeed.smooth;
        lk.space = lastFeed.space;
        lk.air = lastFeed.air;
        lk.royal = lastFeed.royal;
        lk.clean = lastFeed.clean;
        lk.tone = lastFeed.tone;
        lk.level = st.level;
        lk.energy = st.energy;
        lk.pulse = st.pulse;
        lk.sibilance = st.sibS;
        lk.limFlash = st.limFlash;
        lk.activity = st.activity;
        lk.dragAmt = maxDrag;
        lk.dragNode = dragNode;
        if (dragNode >= 0)
        {
            lk.dragDirX = dir[(size_t) dragNode].x;
            lk.dragDirY = dir[(size_t) dragNode].y;
            lk.dragRadius = 0.10f + 0.84f * value[(size_t) dragNode];
        }
        lk.vpX = vp.x;
        lk.vpY = vp.y;
        lk.ringAlpha = juce::jlimit (0.0f, 1.0f, 0.06f + 0.25f * st.level + 0.5f * st.limFlash);
        lk.membraneFill = 0.03f;   // ~ white at 2.3 % over the dark aperture
        for (int n = 0; n < th::kNumNodes; ++n)
        {
            const float r = 0.10f + 0.84f * value[(size_t) n];
            lk.memX[n] = dir[(size_t) n].x * r;
            lk.memY[n] = dir[(size_t) n].y * r;
        }
        juce::ignoreUnused (lk);
        dish.render (0.76f);   // generator default Glow 76 %
        renderMs = msNow() - t1;
    }
    simMs = t1 - t0;
    calm = st.calm;

    // Readouts: evaluated at <= 12 Hz; rebuilt (in paint) only when text changes.
    if (++readoutThrottle >= 5 || readout.degree < 0)
    {
        readoutThrottle = 0;
        ReadoutKey k;
        k.idle = st.idle;
        k.voiced = st.voiced && st.note >= 0;
        k.note = k.voiced ? st.note : -1;
        k.cents = k.voiced ? juce::roundToInt (st.cents) : 0;
        k.lock = k.voiced ? juce::roundToInt (st.resp * 100.0f) : 0;
        k.degree = st.degree;
        k.rich = st.rich;
        k.style = juce::jlimit (0, th::kNumStyles - 1, lastFeed.style);
        k.key = st.idle ? ((lastFeed.key % 12) + 12) % 12 : 0;
        k.scale = st.idle ? juce::jlimit (0, 7, lastFeed.scale) : 0;
        k.modeN = dish.dominantMode().n;
        k.modeS = dish.dominantMode().s;
        k.modeHz = juce::roundToInt (dish.dominantHz());
        // PITCH text changes with every cent of vibrato; FIGURE only with the
        // degree / style. Rebuild (and repaint) each block only when its text changes.
        const bool pitchChanged = k.idle != readout.idle || k.voiced != readout.voiced || k.note != readout.note
                               || k.cents != readout.cents || k.lock != readout.lock || k.key != readout.key
                               || k.scale != readout.scale;
        const bool figureChanged = k.degree != readout.degree || k.rich != readout.rich || k.style != readout.style
                                || k.modeN != readout.modeN || k.modeS != readout.modeS || k.modeHz != readout.modeHz;
        readout = k;
        if (pitchChanged)  { pitchLayer.needsRebuild = true;  pitchRepaint = true; }
        if (figureChanged) { figureLayer.needsRebuild = true; figureRepaint = true; }
    }
}

void VocalCrownCompass::rebuildArcPath (int node, float v)
{
    const size_t i = (size_t) node;
    arcPathValue[i] = v;
    arcPath[i].clear();
    if (v <= 0.005f)
        return;
    const float thC = th::kNodes[node].angleDeg * kDeg;
    const float a0 = thC - 11.0f * kDeg;
    const float a1 = a0 + 22.0f * kDeg * std::min (1.0f, v);
    const float r = kR + 16.0f;
    arcPath[i].addCentredArc (kCx, kCy, r, r, 0.0f, a0 + 0.5f * kPi, a1 + 0.5f * kPi, true);
}

//==============================================================================
void VocalCrownCompass::tick (double timestampSec)
{
    if (! isShowing())
    {
        lastTickTs = 0.0;
        return;
    }

    const double hz = (calm || ladder >= 3 || dish.isResting() || kDebugPacing) ? 30.0 : 60.0;
    double dt = 1.0 / 60.0;
    if (lastTickTs > 0.0)
    {
        dt = timestampSec - lastTickTs;
        if (dt < 1.0 / hz - 0.001)
            return;
    }
    lastTickTs = timestampSec;
    dt = juce::jlimit (1.0 / 240.0, 0.05, dt);

    if (pendingScale > 0.0f)
        prepareForScale (pendingScale);

    advanceFrame ((float) dt, true);
    repaint (ringBox);

    for (int n = 0; n < th::kNumNodes; ++n)
        if ((labelRepaintMask & (1 << n)) != 0)
            repaint (labelRepaintRect (n));
    labelRepaintMask = 0;
    if (pitchRepaint)  { repaint (pitchLayer.rect);  pitchRepaint = false; }
    if (figureRepaint) { repaint (figureLayer.rect); figureRepaint = false; }

    // Degrade ladder: p95 of (sim + render + paint) over the last 60 frames.
    frameCost[frameCostIndex] = (float) (simMs + renderMs + paintMs);
    frameCostIndex = (frameCostIndex + 1) % 60;
    if (++framesSinceLadder >= 60)
    {
        framesSinceLadder = 0;
        float sorted[60];
        std::copy (std::begin (frameCost), std::end (frameCost), std::begin (sorted));
        std::nth_element (std::begin (sorted), std::begin (sorted) + 56, std::end (sorted));
        if (sorted[56] > 3.0f && ladder < 3)
        {
            ++ladder;
            applyLadder();
        }
    }
}

VocalCrownCompass::Perf VocalCrownCompass::perf() const noexcept
{
    return { simMs, renderMs, paintMs, ladder, calm, paintSection[0], paintSection[1], paintSection[2], paintSection[3] };
}

void VocalCrownCompass::appendCaptionChecks (std::vector<th::CaptionCheck>& out) const
{
    // Resolved label boxes (name + 5 px + a 26 px value slot must fit).
    constexpr float boxW[th::kNumNodes] = { 100.0f, 110.0f, 110.0f, 100.0f, 75.0f };
    for (int n = 0; n < th::kNumNodes; ++n)
        out.push_back ({ "compass label", th::kNodes[n].label, th::caps (10.0f), boxW[n] - 31.0f });

    out.push_back ({ "bracket caption", "VOCAL CROWN", th::caps (8.5f, 0.2f), 88.0f });
    out.push_back ({ "bracket caption", "COMPASS", th::caps (8.5f, 0.2f), 80.0f });
    out.push_back ({ "eyebrow", "PITCH", th::caps (9.0f, 0.2f), 160.0f });
    out.push_back ({ "eyebrow", "FIGURE", th::caps (9.0f, 0.2f), 200.0f });
    out.push_back ({ "hint", utf8 ("DRAG \xc2\xb7 DOUBLE-CLICK RESETS \xc2\xb7 \xe2\x87\xa7 FINE"),
                     th::caps (8.0f, 0.12f, juce::Font::plain), 240.0f });
    // Worst-case dynamic readouts.
    out.push_back ({ "pitch note", "A#4", th::numerals (26.0f), 160.0f });
    out.push_back ({ "pitch detail", utf8 ("IDLE \xc2\xb7 C# HARM MINOR"), th::caps (10.0f, 0.08f, juce::Font::plain), 200.0f });
    out.push_back ({ "pitch detail", utf8 ("\xe2\x88\x92" "199\xc2\xa2 \xc2\xb7 LOCK 100%"), th::caps (10.0f, 0.08f, juce::Font::plain), 200.0f });
    for (const auto* degree : th::kDegreeNames)
        out.push_back ({ "figure degree", degree, th::caps (15.0f, 0.06f, juce::Font::plain), 200.0f });
    for (const auto& m : rc::kModes)
        out.push_back ({ "figure structure", figureStructure (m), th::caps (9.0f, 0.12f, juce::Font::plain), 200.0f });
    for (const auto* style : th::kStyleCaps)
        out.push_back ({ "figure style", style, th::caps (9.0f, 0.14f), 200.0f });
}

//==============================================================================
juce::Point<float> VocalCrownCompass::nodePosition (int node) const
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes))
        return { kCx, kCy };
    return juce::Point<float> (kCx, kCy) + dir[(size_t) node] * radiusFor (value[(size_t) node]);
}

float VocalCrownCompass::projectToValue (int node, juce::Point<float> m) const
{
    const auto rel = m - juce::Point<float> (kCx, kCy);
    const auto d = dir[(size_t) node];
    const float along = rel.x * d.x + rel.y * d.y;
    return (along / kR - 0.10f) / 0.84f;   // unclamped
}

int VocalCrownCompass::hitTestNode (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 16.0f;
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const float d = p.getDistanceFrom (nodePosition (n));
        if (d <= bestD)
        {
            bestD = d;
            best = n;
        }
    }
    return best;
}

int VocalCrownCompass::hitTestSpoke (juce::Point<float> p, float& valueOut) const
{
    const auto rel = p - juce::Point<float> (kCx, kCy);
    int best = -1;
    float bestPerp = 10.0f;
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const auto d = dir[(size_t) n];
        const float along = rel.x * d.x + rel.y * d.y;
        const float perp = std::abs (rel.x * d.y - rel.y * d.x);
        if (perp <= bestPerp && along >= 0.06f * kR && along <= 1.02f * kR)
        {
            bestPerp = perp;
            best = n;
            valueOut = clamp01 ((along / kR - 0.10f) / 0.84f);
        }
    }
    return best;
}

//==============================================================================
bool VocalCrownCompass::beginNodeDrag (int node)
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes) || nodeParam[node] == nullptr)
        return false;
    if (gestureNode == node)
        return false;
    if (gestureNode >= 0)
        endNodeDrag (gestureNode);

    nodeParam[node]->beginChangeGesture();
    gestureNode = node;
    dragNode = node;
    conditioner.noteInteraction();
    focusNode (node);
    return true;
}

void VocalCrownCompass::dragNodeToValue (int node, float v)
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes) || nodeParam[node] == nullptr)
        return;
    auto* prm = nodeParam[node];
    prm->setValueNotifyingHost (prm->convertTo0to1 (clamp01 (v)));
    value[(size_t) node] = clamp01 (load (pNode[node], v));
    conditioner.noteInteraction();
}

void VocalCrownCompass::endNodeDrag (int node)
{
    if (node != gestureNode || gestureNode < 0)
        return;
    if (nodeParam[node] != nullptr)
        nodeParam[node]->endChangeGesture();
    gestureNode = -1;
    if (dragNode == node)
        dragNode = -1;
}

void VocalCrownCompass::setNodeValueGesture (int node, float v)
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes))
        return;
    if (gestureNode == node)
    {
        dragNodeToValue (node, v);
        return;
    }
    const bool began = beginNodeDrag (node);
    dragNodeToValue (node, v);
    if (began)
        endNodeDrag (node);
}

void VocalCrownCompass::resetNodeToDefault (int node)
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes) || nodeParam[node] == nullptr)
        return;
    const float def = nodeParam[node]->convertFrom0to1 (nodeParam[node]->getDefaultValue());
    setNodeValueGesture (node, def);
    if (gestureNode == node)
    {
        dragV0 = value[(size_t) node];
        dragProj0 = projectToValue (node, getMouseXYRelative().toFloat());
    }
}

void VocalCrownCompass::setHoverNode (int node)
{
    if (node == hoverNode)
        return;
    hoverNode = node;
    if (node >= 0 && onFocusMacro)
        onFocusMacro (th::macroForNode (node));
}

void VocalCrownCompass::focusNode (int node)
{
    if (! juce::isPositiveAndBelow (node, th::kNumNodes))
        return;
    focusedNode = node;
    if (onFocusMacro)
        onFocusMacro (th::macroForNode (node));
}

void VocalCrownCompass::setHighlightedNode (int nodeOrMinus1)
{
    highlightNode = juce::isPositiveAndBelow (nodeOrMinus1, th::kNumNodes) ? nodeOrMinus1 : -1;
}

void VocalCrownCompass::setHoverForTesting (int node)
{
    hoverNode = juce::isPositiveAndBelow (node, th::kNumNodes) ? node : -1;
}

//==============================================================================
void VocalCrownCompass::mouseMove (const juce::MouseEvent& e)
{
    int n = hitTestNode (e.position);
    auto cursor = juce::MouseCursor::NormalCursor;
    if (n >= 0)
    {
        cursor = juce::MouseCursor::DraggingHandCursor;
    }
    else
    {
        float v = 0.0f;
        n = hitTestSpoke (e.position, v);
        if (n >= 0)
            cursor = juce::MouseCursor::PointingHandCursor;
    }
    setMouseCursor (cursor);
    setHoverNode (n);
}

void VocalCrownCompass::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
    setHoverNode (-1);
}

void VocalCrownCompass::mouseDown (const juce::MouseEvent& e)
{
    conditioner.noteInteraction();
    if (! hasKeyboardFocus (false))
        grabKeyboardFocus();

    float spokeValue = 0.0f;
    int n = hitTestNode (e.position);
    const bool onSpoke = n < 0 && (n = hitTestSpoke (e.position, spokeValue)) >= 0;
    if (n < 0)
        return;

    focusNode (n);

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Reset " + juce::String (th::kNodes[n].label) + " to default");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe = juce::Component::SafePointer<VocalCrownCompass> (this), n] (int result)
                            {
                                if (safe != nullptr && result == 1)
                                    safe->resetNodeToDefault (n);
                            });
        return;
    }

    if (e.mods.isAltDown() && ! onSpoke)
    {
        resetNodeToDefault (n);
        return;
    }

    beginNodeDrag (n);
    if (onSpoke)
        dragNodeToValue (n, spokeValue);
    dragV0 = value[(size_t) n];
    dragProj0 = projectToValue (n, e.position);
    dragFine = e.mods.isShiftDown();
}

void VocalCrownCompass::mouseDrag (const juce::MouseEvent& e)
{
    if (gestureNode < 0 || dragNode != gestureNode)
        return;
    const int n = gestureNode;
    const float proj = projectToValue (n, e.position);
    const bool fine = e.mods.isShiftDown();
    if (fine != dragFine)
    {
        // Re-base when Shift changes mid-drag so the puck never jumps.
        dragFine = fine;
        dragV0 = value[(size_t) n];
        dragProj0 = proj;
    }
    const float v = dragV0 + (fine ? 0.25f : 1.0f) * (proj - dragProj0);
    dragNodeToValue (n, clamp01 (v));
}

void VocalCrownCompass::mouseUp (const juce::MouseEvent&)
{
    if (gestureNode >= 0)
        endNodeDrag (gestureNode);
}

void VocalCrownCompass::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int n = hitTestNode (e.position);
    if (n >= 0)
        resetNodeToDefault (n);
}

void VocalCrownCompass::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int n = hitTestNode (e.position);
    if (n < 0 && hoverNode >= 0 && e.position.getDistanceFrom (nodePosition (hoverNode)) <= 16.0f)
        n = hoverNode;
    if (n < 0)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    float dy = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;
    if (wheel.isReversed)
        dy = -dy;
    const float dv = wheel.isSmooth ? juce::jlimit (-0.05f, 0.05f, dy * 0.2f)
                                    : (dy > 0.0f ? 0.02f : (dy < 0.0f ? -0.02f : 0.0f));
    if (dv == 0.0f)
        return;
    conditioner.noteInteraction();
    focusNode (n);
    setNodeValueGesture (n, value[(size_t) n] + dv);
}

bool VocalCrownCompass::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const int n = focusedNode;
    conditioner.noteInteraction();

    if (code == juce::KeyPress::leftKey)  { focusNode ((n + th::kNumNodes - 1) % th::kNumNodes); return true; }
    if (code == juce::KeyPress::rightKey) { focusNode ((n + 1) % th::kNumNodes); return true; }

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const float step = mods.isShiftDown() ? 0.05f : ((mods.isCommandDown() || mods.isCtrlDown()) ? 0.001f : 0.01f);
        setNodeValueGesture (n, value[(size_t) n] + (code == juce::KeyPress::upKey ? step : -step));
        return true;
    }
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
    {
        setNodeValueGesture (n, value[(size_t) n] + (code == juce::KeyPress::pageUpKey ? 0.10f : -0.10f));
        return true;
    }
    if (code == juce::KeyPress::homeKey) { setNodeValueGesture (n, 0.0f); return true; }
    if (code == juce::KeyPress::endKey)  { setNodeValueGesture (n, 1.0f); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        resetNodeToDefault (n);
        return true;
    }
    return false;
}

void VocalCrownCompass::focusGained (FocusChangeType)
{
    focusNode (focusedNode);
}

void VocalCrownCompass::focusLost (FocusChangeType)
{
    if (gestureNode >= 0)
        endNodeDrag (gestureNode);
}

juce::String VocalCrownCompass::getTooltip()
{
    if (juce::isPositiveAndBelow (hoverNode, th::kNumNodes))
    {
        const auto& nd = th::kNodes[hoverNode];
        return juce::String (nd.label) + " " + juce::String (juce::roundToInt (value[(size_t) hoverNode] * 100.0f))
             + utf8 ("% \xe2\x80\x94 ") + nd.description
             + utf8 (" Drag along the spoke \xc2\xb7 double-click resets \xc2\xb7 \xe2\x87\xa7 fine.");
    }
    return utf8 ("Vocal Crown Compass \xe2\x80\x94 each sung scale degree draws its own figure.");
}

std::unique_ptr<juce::AccessibilityHandler> VocalCrownCompass::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

void VocalCrownCompass::resized()
{
    // Fixed logical geometry (the view scales us with a transform); caches only
    // depend on the physical scale, which paint() detects.
}

juce::Rectangle<int> VocalCrownCompass::labelRepaintRect (int node) const
{
    return labelLayers[(size_t) node].rect.getUnion (labels[(size_t) node].box).expanded (2);
}

//==============================================================================
void VocalCrownCompass::rebuildStageCache()
{
    using juce::Colour;
    const float s = preparedScale > 0.0f ? preparedScale : 1.0f;
    stageCache = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) kWidth * s),
                              juce::roundToInt ((float) kHeight * s), true, juce::NativeImageType());
    juce::Graphics g (stageCache);
    g.addTransform (juce::AffineTransform::scale (s));

    const float w = (float) kWidth, h = (float) kHeight;
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, w, h);
    const juce::Point<float> c (kCx, kCy);

    // 1-2. Canvas corners, matte card with a soft top-left lift.
    g.fillAll (th::col (th::canvas));
    g.setColour (th::col (th::stage));
    g.fillRoundedRectangle (bounds, 16.0f);
    {
        juce::ColourGradient lift (th::col (th::stageLift), 0.28f * w, 0.22f * h,
                                   th::col (th::stageLift).withAlpha (0.0f), 0.28f * w + 0.9f * w, 0.22f * h, true);
        g.setGradientFill (lift);
        g.fillRoundedRectangle (bounds, 16.0f);
    }

    // 3. One diagonal sheen band (30 degrees, 0.35 w wide, feathered).
    {
        const juce::Point<float> n (std::cos (30.0f * kDeg), std::sin (30.0f * kDeg));
        const juce::Point<float> mid (0.30f * w, 0.46f * h);
        const float half = 0.175f * w;
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), mid - n * half,
                                    juce::Colours::white.withAlpha (0.0f), mid + n * half, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.025f));
        g.setGradientFill (sheen);
        g.fillRoundedRectangle (bounds, 16.0f);
    }

    // 4. Dot grid, kept away from the aperture and the card edge.
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    for (int y = 8; y <= kHeight - 8; y += 16)
        for (int x = 8; x <= kWidth - 8; x += 16)
        {
            const juce::Point<float> p ((float) x, (float) y);
            if (p.getDistanceFrom (c) < 1.2f * kR)
                continue;
            const float cxr = x < 16 ? 16.0f : (x > kWidth - 16 ? (float) kWidth - 16.0f : (float) x);
            const float cyr = y < 16 ? 16.0f : (y > kHeight - 16 ? (float) kHeight - 16.0f : (float) y);
            if (p.getDistanceFrom ({ cxr, cyr }) > 8.0f)
                continue;   // inside a rounded corner's 8 px margin
            g.fillRect ((float) x - 0.5f, (float) y - 0.5f, 1.0f, 1.0f);
        }

    // 5. The aperture.
    {
        juce::ColourGradient ap (th::col (th::voidCore), c.x, c.y, th::col (th::voidRim), c.x + kR, c.y, true);
        g.setGradientFill (ap);
        g.fillEllipse (c.x - kR, c.y - kR, 2.0f * kR, 2.0f * kR);
    }

    // 6a. Guide spokes (0.10 R -> R) and activity-arc tracks.
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const auto d = dir[(size_t) n];
        const auto nc = th::col (th::kNodes[n].colour);
        g.setColour (nc.withAlpha (0.08f));
        g.drawLine ({ c + d * (0.10f * kR), c + d * kR }, 1.0f);

        const float a = th::kNodes[n].angleDeg * kDeg;
        juce::Path trackArc;
        trackArc.addCentredArc (c.x, c.y, kR + 16.0f, kR + 16.0f, 0.0f,
                                a - 11.0f * kDeg + 0.5f * kPi, a + 11.0f * kDeg + 0.5f * kPi, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.strokePath (trackArc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 6b. The rim: a 1.5 px conic ramp, violet top-left -> mint bottom-right.
    for (int k = 0; k < 128; ++k)
    {
        const float a0 = (float) k / 128.0f * 2.0f * kPi;
        const float a1 = (float) (k + 1) / 128.0f * 2.0f * kPi;
        const float t = 0.5f + 0.5f * std::cos (0.5f * (a0 + a1) + 135.0f * kDeg);
        juce::Path seg;
        seg.addCentredArc (c.x, c.y, kR, kR, 0.0f, a0 + 0.5f * kPi - 0.003f, a1 + 0.5f * kPi + 0.003f, true);
        g.setColour (th::col (th::rimMint).interpolatedWith (th::col (th::rimViolet), t));
        g.strokePath (seg, juce::PathStrokeType (1.5f));
    }

    // 6c. Ticks: 72 minor every 5 degrees, a major tick at each node.
    for (int k = 0; k < 72; ++k)
    {
        const float deg = (float) k * 5.0f;
        bool nearNode = false;
        for (const auto& nd : th::kNodes)
        {
            float diff = std::fmod (std::abs (deg - nd.angleDeg), 360.0f);
            diff = std::min (diff, 360.0f - diff);
            nearNode = nearNode || diff < 6.0f;
        }
        if (nearNode)
            continue;
        const juce::Point<float> d (std::cos (deg * kDeg), std::sin (deg * kDeg));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine ({ c + d * (kR + 22.0f), c + d * (kR + 25.0f) }, 1.0f);
    }
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const auto d = dir[(size_t) n];
        g.setColour (th::col (th::kNodes[n].colour).withAlpha (0.55f));
        g.drawLine ({ c + d * (kR + 20.0f), c + d * (kR + 28.0f) }, 1.5f);
    }

    // 6d. Node names (tracked caps, node colour) + label layout for the live values.
    const juce::Font nameFont (th::caps (10.0f));
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const auto d = dir[(size_t) n];
        const auto anchor = c + d * (kR + 40.0f);
        const float nameW = std::ceil (juce::GlyphArrangement::getStringWidth (nameFont, th::kNodes[n].label));
        constexpr float gap = 5.0f, valueW = 26.0f;
        const float groupW = nameW + gap + valueW;
        float x0;
        if (std::abs (d.x) < 0.3f)  x0 = anchor.x - groupW * 0.5f;
        else if (d.x > 0.0f)        x0 = anchor.x + 6.0f;
        else                        x0 = anchor.x - 6.0f - groupW;
        x0 = std::round (x0);
        const float y0 = std::round (anchor.y - 8.0f);

        auto& L = labels[(size_t) n];
        L.name = { x0, y0, nameW, 16.0f };
        L.value = { x0 + nameW + gap, y0, valueW, 16.0f };
        L.pill = juce::Rectangle<float> (x0 - 7.0f, y0, groupW + 14.0f, 16.0f);
        L.valueJust = juce::Justification::centredLeft;
        L.box = L.pill.getSmallestIntegerContainer();
        labelLayers[(size_t) n].rect = L.box;

        drawString (g, th::kNodes[n].label, L.name, th::caps (10.0f), th::col (th::kNodes[n].colour),
                    juce::Justification::centredLeft);
    }

    // 7. Portal corner brackets (top-right, bottom-left).
    g.setColour (th::col (th::bracket).withAlpha (0.55f));
    {
        juce::Path br;
        br.startNewSubPath (730.0f - 44.0f, 14.0f);
        br.lineTo (730.0f, 14.0f);
        br.lineTo (730.0f, 14.0f + 44.0f);
        br.startNewSubPath (14.0f, 494.0f - 44.0f);
        br.lineTo (14.0f, 494.0f);
        br.lineTo (14.0f + 44.0f, 494.0f);
        g.strokePath (br, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    // 8. Bracket captions.
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (-0.5f * kPi).translated (18.0f, 476.0f));
        drawString (g, "VOCAL CROWN", { 0.0f, 0.0f, 88.0f, 12.0f }, th::caps (8.5f, 0.2f),
                    th::col (th::violet), juce::Justification::centredLeft);
    }
    drawString (g, "COMPASS", { 36.0f, 482.0f, 80.0f, 10.0f }, th::caps (8.5f, 0.2f),
                th::col (th::mint), juce::Justification::centredLeft);

    // 9. Static eyebrows and the gesture hint.
    drawString (g, "PITCH", { 24.0f, 18.0f, 160.0f, 12.0f }, th::caps (9.0f, 0.2f),
                th::col (th::textDim), juce::Justification::centredLeft);
    drawString (g, "FIGURE", { 520.0f, 18.0f, 200.0f, 12.0f }, th::caps (9.0f, 0.2f),
                th::col (th::textDim), juce::Justification::centredRight);
    drawString (g, utf8 ("DRAG \xc2\xb7 DOUBLE-CLICK RESETS \xc2\xb7 \xe2\x87\xa7 FINE"),
                { 480.0f, 486.0f, 240.0f, 12.0f }, th::caps (8.0f, 0.12f, juce::Font::plain),
                th::col (th::textFaint), juce::Justification::centredRight);

    pitchLayer.rect = { 24, 32, 200, 46 };
    figureLayer.rect = { 520, 32, 200, 48 };
}

void VocalCrownCompass::renderLabelLayer (int node)
{
    auto& layer = labelLayers[(size_t) node];
    const auto& L = labels[(size_t) node];
    const float s = preparedScale;
    const int w = juce::roundToInt ((float) layer.rect.getWidth() * s);
    const int h = juce::roundToInt ((float) layer.rect.getHeight() * s);
    if (! layer.image.isValid() || layer.image.getWidth() != w || layer.image.getHeight() != h)
        layer.image = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true, juce::NativeImageType());
    else
        layer.image.clear (layer.image.getBounds());

    juce::Graphics g (layer.image);
    g.addTransform (juce::AffineTransform::translation ((float) -layer.rect.getX(), (float) -layer.rect.getY()).scaled (s));

    if (pillShown[node])
    {
        g.setColour (th::col (th::pill));
        g.fillRoundedRectangle (L.pill, 8.0f);
        drawString (g, th::kNodes[node].label, L.name, th::caps (10.0f), th::col (th::kNodes[node].colour),
                    juce::Justification::centredLeft);
    }
    drawString (g, juce::String (shownPct[(size_t) node]), L.value, th::body (10.0f), th::col (th::text), L.valueJust);
    layer.needsRebuild = false;
}

juce::Image& VocalCrownCompass::prepareLayerImage (TextLayer& layer)
{
    const float s = preparedScale;
    const int w = juce::roundToInt ((float) layer.rect.getWidth() * s);
    const int h = juce::roundToInt ((float) layer.rect.getHeight() * s);
    if (! layer.image.isValid() || layer.image.getWidth() != w || layer.image.getHeight() != h)
        layer.image = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true, juce::NativeImageType());
    else
        layer.image.clear (layer.image.getBounds());
    return layer.image;
}

void VocalCrownCompass::renderPitchLayer()
{
    const float s = preparedScale;
    // PITCH: the note you hear, cents + lock, or the listening / idle state.
    {
        juce::Graphics g (prepareLayerImage (pitchLayer));
        g.addTransform (juce::AffineTransform::translation ((float) -pitchLayer.rect.getX(), (float) -pitchLayer.rect.getY()).scaled (s));
        juce::String note = utf8 ("\xe2\x80\x94"), detail;
        if (readout.idle)
        {
            detail = utf8 ("IDLE \xc2\xb7 ") + th::kNoteNames[readout.key] + " " + th::kScaleNames[readout.scale];
        }
        else if (readout.voiced)
        {
            note = noteName (readout.note);
            const int ct = readout.cents;
            const juce::String sign = ct > 0 ? juce::String ("+") : (ct < 0 ? utf8 ("\xe2\x88\x92") : juce::String());
            detail = sign + juce::String (std::abs (ct)) + utf8 ("\xc2\xa2 \xc2\xb7 LOCK ") + juce::String (readout.lock) + "%";
        }
        else
        {
            detail = "LISTENING";
        }
        drawString (g, note, { 24.0f, 32.0f, 160.0f, 30.0f }, th::numerals (26.0f), th::col (th::text),
                    juce::Justification::centredLeft);
        drawString (g, detail, { 24.0f, 64.0f, 200.0f, 14.0f }, th::caps (10.0f, 0.08f, juce::Font::plain),
                    th::col (th::textDim), juce::Justification::centredLeft);
        pitchLayer.needsRebuild = false;
    }
}

void VocalCrownCompass::renderFigureLayer()
{
    const float s = preparedScale;
    // FIGURE: the scale degree, what it draws, and the vocal style.
    {
        juce::Graphics g (prepareLayerImage (figureLayer));
        g.addTransform (juce::AffineTransform::translation ((float) -figureLayer.rect.getX(), (float) -figureLayer.rect.getY()).scaled (s));
        // The water-dish eigenmode actually ringing: (n, s), its nodal structure
        // (2n diameters, s-1 circles; s for n = 0) and the frequency it sits on.
        const int mn = juce::jmax (0, readout.modeN), ms = juce::jmax (1, readout.modeS);
        const rc::PlateMode pm { mn, ms, 0.0f };
        drawString (g, utf8 ("MODE (") + juce::String (mn) + ", " + juce::String (ms) + ")", { 520.0f, 32.0f, 200.0f, 20.0f },
                    th::caps (15.0f, 0.06f, juce::Font::plain), th::col (th::text), juce::Justification::centredRight);
        juce::String structure = figureStructure (pm);
        if (readout.modeHz > 0)
            structure << utf8 (" \xc2\xb7 ") << readout.modeHz << " HZ";
        drawString (g, structure, { 520.0f, 54.0f, 200.0f, 12.0f }, th::caps (9.0f, 0.12f, juce::Font::plain),
                    th::col (th::textDim), juce::Justification::centredRight);
        drawString (g, th::kStyleCaps[juce::jlimit (0, th::kNumStyles - 1, readout.style)], { 520.0f, 68.0f, 200.0f, 12.0f },
                    th::caps (9.0f, 0.14f), th::col (th::textDim), juce::Justification::centredRight);
        figureLayer.needsRebuild = false;
    }
}

void VocalCrownCompass::drawLayer (juce::Graphics& g, const TextLayer& layer) const
{
    if (! layer.image.isValid() || ! g.clipRegionIntersects (layer.rect))
        return;
    g.drawImageTransformed (layer.image,
                            juce::AffineTransform::scale (1.0f / preparedScale)
                                .translated ((float) layer.rect.getX(), (float) layer.rect.getY()));
}

//==============================================================================
void VocalCrownCompass::paint (juce::Graphics& g)
{
    const double t0 = msNow();

    const float physical = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (physical > 0.0f && std::abs (clampScale (physical) - preparedScale) > 0.01f)
        pendingScale = clampScale (physical);

    // L0: stage cache (native image, physical scale -> 1:1 blit).
    if (stageCache.isValid())
        g.drawImageTransformed (stageCache, juce::AffineTransform::scale (1.0f / preparedScale));
    else
        g.fillAll (th::col (th::canvas));

    const double t1 = msNow();

    // L1: the field. Its own alpha forms the disc: never clip with a Path.
    const auto& field = dish.image();
    if (field.isValid() && g.clipRegionIntersects (fieldRect))
    {
        // The dish image is exactly the field rect in logical px: a 1:1 (or, on Retina,
        // exact 2:1) blit. Nearest-neighbour keeps it a copy instead of a resample —
        // the smooth upsample already happened inside the dish renderer.
        const float k = (float) fieldRect.getWidth() / (float) field.getWidth();
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (field, juce::AffineTransform::scale (k)
                                           .translated ((float) fieldRect.getX(), (float) fieldRect.getY()));
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    }

    const double t2 = msNow();
    const juce::Point<float> c (kCx, kCy);

    // L2 (the level / limiter hot ring at R) and the membrane's 3 % fill are
    // rendered inside the field image: CoreGraphics charged ~0.18 ms each to
    // rasterise them as large anti-aliased shapes every frame.

    // L3: node activity arcs (value part; cached paths).
    const juce::PathStrokeType arcStroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    for (int n = 0; n < th::kNumNodes; ++n)
        if (arcPathValue[(size_t) n] > 0.005f)
        {
            g.setColour (th::col (th::kNodes[n].colour).withAlpha (0.85f));
            g.strokePath (arcPath[(size_t) n], arcStroke);
        }

    // L4: the membrane through the five pucks.
    membrane.clear();
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const auto p = nodePosition (n);
        if (n == 0) membrane.startNewSubPath (p);
        else        membrane.lineTo (p);
    }
    membrane.closeSubPath();
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (membrane, juce::PathStrokeType (1.0f));

    // L5: value spokes.
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const size_t i = (size_t) n;
        float a = 0.28f + 0.12f * hoverAmt[i];
        a += (0.60f - a) * dragAmt[i];
        const auto p0 = c + dir[i] * (0.10f * kR);
        const auto p1 = nodePosition (n);
        g.setColour (th::col (th::kNodes[n].colour).withAlpha (a));
        g.drawLine (p0.x, p0.y, p1.x, p1.y, 1.5f);
    }

    // L6: pucks (crisp double rings), drag underlay, focus ring.
    const bool focused = hasKeyboardFocus (false);
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        const size_t i = (size_t) n;
        const auto p = nodePosition (n);
        const auto nc = th::col (th::kNodes[n].colour);
        if (dragAmt[i] > 0.01f)
        {
            g.setColour (nc.withAlpha (0.16f * dragAmt[i]));
            g.drawEllipse (p.x - 16.0f, p.y - 16.0f, 32.0f, 32.0f, 6.0f);
        }
        g.setColour (th::col (th::stage).withAlpha (0.75f));
        g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
        float ro = 9.0f + 2.0f * hoverAmt[i];
        ro += (12.0f - ro) * dragAmt[i];
        g.setColour (nc);
        g.drawEllipse (p.x - ro, p.y - ro, 2.0f * ro, 2.0f * ro, 1.5f);
        g.setColour (th::col (th::kNodes[n].inner).withAlpha (0.8f));
        g.drawEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f, 1.5f);
        g.setColour (th::col (th::hot));
        g.fillEllipse (p.x - 1.6f, p.y - 1.6f, 3.2f, 3.2f);
        if (focused && n == focusedNode)
        {
            g.setColour (th::col (th::focus).withAlpha (0.85f));
            g.drawEllipse (p.x - 15.0f, p.y - 15.0f, 30.0f, 30.0f, 2.0f);
        }
    }

    const double t3 = msNow();

    // L7 / L8: live values and readouts (cached text layers, rebuilt on change).
    for (int n = 0; n < th::kNumNodes; ++n)
    {
        auto& layer = labelLayers[(size_t) n];
        if (layer.needsRebuild)
            renderLabelLayer (n);
        drawLayer (g, layer);
    }
    if (pitchLayer.needsRebuild)
        renderPitchLayer();
    if (figureLayer.needsRebuild)
        renderFigureLayer();
    drawLayer (g, pitchLayer);
    drawLayer (g, figureLayer);

    const double t4 = msNow();
    paintMs = t4 - t0;
    paintSection[0] = t1 - t0;
    paintSection[1] = t2 - t1;
    paintSection[2] = t3 - t2;
    paintSection[3] = t4 - t3;
}
