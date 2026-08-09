#include "DeckStrip.h"
#include "DesignTokens.h"

namespace stemdeck
{

// ── Colour + type tokens ──────────────────────────────────────────────────────
namespace tok
{
    static const juce::Colour bgStrip     { 0xff131316 };
    static const juce::Colour bgControl   { 0xff1C1C20 };
    static const juce::Colour bgHover     { 0xff242429 };
    static const juce::Colour bgActLift   { 0xff1F1F22 };
    static const juce::Colour border      { 0xff2A2A30 };
    static const juce::Colour fg          { 0xffF2F2F5 };
    static const juce::Colour warn        { 0xffFF9F43 };

    static juce::Colour primary  (float a = 1.00f) { return fg.withAlpha (a); }
    static juce::Colour secondary()                { return fg.withAlpha (0.75f); }
    static juce::Colour tertiary ()                { return fg.withAlpha (0.55f); }
    static juce::Colour disabled ()                { return fg.withAlpha (0.30f); }
}

// Stem display order + colours, matching the stem-coloured waveform.
// (State.stem is indexed by DeckPlayer lane: 0 drums, 1 bass, 2 music, 3 vocals.)
namespace
{
    struct StemDef { const char* label; juce::uint32 colour; int lane; };
    const std::array<StemDef, 4> kStems { {
        { "VOX", 0xFFD44FC8, 3 },   // vocals
        { "DRM", 0xFFE05A2B, 0 },   // drums
        { "BAS", 0xFF4A90E2, 1 },   // bass
        { "OTH", 0xFF5BBF7A, 2 },   // music / other
    } };

    const char* quantLabel (int q)
    {
        switch (q) { case 0: return "OFF"; case 1: return "1"; case 2: return "1/2";
                     case 3: return "1/4"; default: return "1/8"; }
    }

    // A tidy beat-length label: 0.5 → "1/2", 0.25 → "1/4", else the integer.
    juce::String loopBeatsLabel (double beats)
    {
        if (beats >= 1.0)  return juce::String ((int) std::lround (beats));
        const int denom = (int) std::lround (1.0 / juce::jmax (1.0e-6, beats));
        return "1/" + juce::String (denom);
    }

    juce::Font grotesque (float h, bool bold, bool italic = false)
    {
        auto f = juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
        if (italic) f = f.italicised();
        return f;
    }
}

// ── Ctor ──────────────────────────────────────────────────────────────────────
DeckStrip::DeckStrip (int deckIndex)
    : deck (deckIndex), accent (accentFor (deckIndex))
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (false);
    lastTickMs = juce::Time::getMillisecondCounterHiRes();
}

juce::Colour DeckStrip::accentFor (int deck)
{
    // Shares the token definition rather than re-listing it — this copy had drifted
    // on D (#FFB84A vs the token's #E8A54A). CVD-safe: no red-green pairing.
    return stemdeck::tokens::deckAccent (deck);
}

// ── State + drawer ────────────────────────────────────────────────────────────
void DeckStrip::setState (const State& s)
{
    const auto differs = [] (double a, double b, double epsilon)
    {
        return std::abs (a - b) > epsilon;
    };
    const bool changed = s.hasTrack != state.hasTrack
                      || s.playing != state.playing
                      || s.cueSet != state.cueSet
                      || s.cueHeld != state.cueHeld
                      || s.loopArmed != state.loopArmed
                      || s.loopActive != state.loopActive
                      || differs (s.loopBeats, state.loopBeats, 0.0001)
                      || s.beatJumpBars != state.beatJumpBars
                      || s.syncOn != state.syncOn
                      || s.syncLocked != state.syncLocked
                      || differs (s.phaseOffset, state.phaseOffset, 0.002)
                      || s.keyCamelot != state.keyCamelot
                      || s.keySemis != state.keySemis
                      || s.keyLowConf != state.keyLowConf
                      || s.slipOn != state.slipOn
                      || s.quantIndex != state.quantIndex
                      || s.stem != state.stem
                      || differs (s.sepProgress, state.sepProgress, 0.001)
                      || s.fx != state.fx
                      || s.focused != state.focused;
    if (! changed)
        return;

    const bool needRing = s.syncOn;
    state = s;
    if (needRing && ! isTimerRunning())
        startTimerHz (30);
    repaint();
}

void DeckStrip::setExpanded (bool shouldExpand, bool animate)
{
    if (expanded == shouldExpand) return;
    expanded = shouldExpand;
    if (! animate)
    {
        drawerAmt = expanded ? 1.0f : 0.0f;
        if (onExpandChanged) onExpandChanged();
        repaint();
        return;
    }
    if (! isTimerRunning())
        startTimerHz (30);
}

int DeckStrip::currentHeight() const
{
    return juce::roundToInt (kCollapsedHeight
                             + drawerAmt * (float) (kDividerHeight + kDrawerHeight));
}

// ── Timer — drawer ease + continuous phase-ring spin ──────────────────────────
void DeckStrip::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.05, (now - lastTickMs) / 1000.0);
    lastTickMs = now;

    bool keepGoing = false;

    // Drawer: 220ms cubic-ish approach toward the target.
    const float target = expanded ? 1.0f : 0.0f;
    if (std::abs (drawerAmt - target) > 0.001f)
    {
        const float rate = dt / 0.22f;                 // ~220ms full travel
        drawerAmt += (target - drawerAmt) * juce::jmin (1.0f, rate * 3.2f);
        if (std::abs (drawerAmt - target) <= 0.004f) drawerAmt = target;
        if (onExpandChanged) onExpandChanged();
        keepGoing = true;
    }

    // Phase ring: keep spinning while sync is engaged.
    if (state.syncOn)
    {
        phaseSpin += dt * (state.syncLocked ? 0.6f : 1.6f);   // faster while drifting
        if (phaseSpin > juce::MathConstants<float>::twoPi) phaseSpin -= juce::MathConstants<float>::twoPi;
        keepGoing = true;
    }

    repaint();
    if (! keepGoing)
        stopTimer();
}

// ── Layout ────────────────────────────────────────────────────────────────────
void DeckStrip::resized()
{
    auto b = getLocalBounds().toFloat();
    layoutRow1 (b.removeFromTop ((float) kCollapsedHeight));
    if (getHeight() > kCollapsedHeight)
    {
        b.removeFromTop ((float) kDividerHeight);
        layoutRow2 (b);
    }
}

static juce::Rectangle<float> centredCell (juce::Rectangle<float> r, float w, float h)
{
    return juce::Rectangle<float> (w, h).withCentre (r.getCentre());
}

void DeckStrip::layoutRow1 (juce::Rectangle<float> row)
{
    for (auto& r : regions) r = {};
    auto set = [this] (Hit h, juce::Rectangle<float> r) { regions[(size_t) h] = r; };

    // Per-column widths are narrow, so below ~470px we go compact: SYNC and TAKE
    // become icon-only and every group tightens. Above that, the full spec layout.
    const bool compact = row.getWidth() < 470.0f;
    compactMode = compact;

    auto r = row.reduced (0.0f, 3.0f);
    r.removeFromLeft (6.0f);                              // gap right of the 2px accent bar
    r.removeFromLeft (compact ? 13.0f : 16.0f);          // deck numeral (painted, not hit)
    r.removeFromLeft (compact ? 3.0f : 4.0f);

    // Primary: play + cue.
    const float playW = compact ? 28.0f : 32.0f, playH = compact ? 28.0f : 32.0f;
    set (Hit::play, centredCell (r.removeFromLeft (playW), playW, playH));
    r.removeFromLeft (compact ? 3.0f : 4.0f);
    const float cueW = compact ? 42.0f : 56.0f;
    set (Hit::cue, centredCell (r.removeFromLeft (cueW), cueW, playH));

    r.removeFromLeft (compact ? 6.0f : 10.0f);           // divider gap

    // Secondary: loop cluster ⟲ [½] [n] [×2].
    const float lh = compact ? 18.0f : 22.0f, ln = compact ? 28.0f : 34.0f;
    set (Hit::loopHalve,  centredCell (r.removeFromLeft (lh), lh, 24.0f));
    set (Hit::loopNum,    centredCell (r.removeFromLeft (ln), ln, 24.0f));
    set (Hit::loopDouble, centredCell (r.removeFromLeft (lh), lh, 24.0f));

    r.removeFromLeft (compact ? 5.0f : 8.0f);

    // Beat jump ⇤ [n] ⇥.
    const float jb = compact ? 18.0f : 24.0f, jn = compact ? 20.0f : 26.0f;
    set (Hit::jumpBack, centredCell (r.removeFromLeft (jb), jb, 24.0f));
    set (Hit::jumpNum,  centredCell (r.removeFromLeft (jn), jn, 24.0f));
    set (Hit::jumpFwd,  centredCell (r.removeFromLeft (jb), jb, 24.0f));

    r.removeFromLeft (compact ? 6.0f : 10.0f);

    // Sync (icon-only when compact) + key.
    const float syncW = compact ? 26.0f : 58.0f;
    set (Hit::sync, centredCell (r.removeFromLeft (syncW), syncW, 26.0f));
    r.removeFromLeft (compact ? 4.0f : 6.0f);
    const float keyW = compact ? 82.0f : 104.0f;
    set (Hit::key,  centredCell (r.removeFromLeft (keyW), keyW, 26.0f));

    // Right cluster: chevron (far right) then TAKE (icon-only when compact).
    set (Hit::chevron, centredCell (row.removeFromRight (compact ? 20.0f : 26.0f).reduced (0.0f, 3.0f), 20.0f, 20.0f));
    row.removeFromRight (compact ? 2.0f : 4.0f);
    const float takeW = compact ? 28.0f : 68.0f;
    set (Hit::take, centredCell (row.removeFromRight (takeW).reduced (0.0f, 3.0f), takeW, 24.0f));
}

void DeckStrip::layoutRow2 (juce::Rectangle<float> row)
{
    auto set = [this] (Hit h, juce::Rectangle<float> r) { regions[(size_t) h] = r; };
    auto r = row.reduced (0.0f, 4.0f);
    r.removeFromLeft (24.0f);                // align under play, past accent+numeral

    // Stem toggles VOX DRM BAS OTH — tertiary, 20px tall.
    const Hit stemHits[] { Hit::stem0, Hit::stem1, Hit::stem2, Hit::stem3 };
    for (int i = 0; i < 4; ++i)
    {
        set (stemHits[i], centredCell (r.removeFromLeft (42.0f), 42.0f, 20.0f));
        r.removeFromLeft (4.0f);
    }
    r.removeFromLeft (8.0f);
    set (Hit::quant, centredCell (r.removeFromLeft (72.0f), 72.0f, 20.0f));
    r.removeFromLeft (10.0f);
    set (Hit::slip, centredCell (r.removeFromLeft (44.0f), 44.0f, 20.0f));

    // Beat-grid editor stays visible in the compact drawer. It opens precise
    // downbeat, nudge, and tempo interpretation actions without exposing more
    // tiny sliders on the performance surface.
    set (Hit::grid, centredCell (row.removeFromRight (48.0f).reduced (0.0f, 4.0f), 46.0f, 20.0f));
    row.removeFromRight (6.0f);

    // FX sends — right cluster. Only two real sends exist (A = Echo, B = Reverb);
    // the former "C" pill routed no audio and was removed.
    const Hit fxHits[] { Hit::fxB, Hit::fxA };
    for (auto h : fxHits)
    {
        set (h, centredCell (row.removeFromRight (28.0f).reduced (0.0f, 4.0f), 26.0f, 20.0f));
        row.removeFromRight (4.0f);
    }
    // "FX" caption region is painted; no hit.
}

juce::Rectangle<float> DeckStrip::regionFor (Hit h) const { return regions[(size_t) h]; }

DeckStrip::Hit DeckStrip::hitTest (juce::Point<float> p) const
{
    for (int i = 1; i < (int) regions.size(); ++i)
        if (! regions[(size_t) i].isEmpty() && regions[(size_t) i].contains (p))
            return (Hit) i;
    return Hit::none;
}

// ── Painting ──────────────────────────────────────────────────────────────────
void DeckStrip::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // Strip background.
    g.setColour (tok::bgStrip);
    g.fillRect (full);

    // Unfocused decks drop ~15% opacity across the board.
    const float dim = state.focused ? 1.0f : 0.85f;
    juce::Graphics::ScopedSaveState ss (g);
    // (opacity applied per-element via tok helpers; we scale by multiplying alphas)

    // 2px accent identity bar, full strip height.
    g.setColour (accent.withAlpha (state.focused ? 1.0f : 0.7f));
    g.fillRect (full.removeFromLeft (2.0f));

    // Focused deck: 1px accent border along the top edge.
    if (state.focused)
    {
        g.setColour (accent.withAlpha (0.9f));
        g.fillRect (getLocalBounds().toFloat().removeFromTop (1.0f).withTrimmedLeft (2.0f));
    }

    paintRow1 (g);

    if (drawerAmt > 0.001f && getHeight() > kCollapsedHeight)
    {
        // Divider rule under row 1.
        auto div = getLocalBounds().toFloat().withTop ((float) kCollapsedHeight)
                                              .withHeight ((float) kDividerHeight);
        g.setColour (tok::border.withAlpha (0.6f * drawerAmt));
        g.fillRect (div.withSizeKeepingCentre (div.getWidth() - 24.0f, 1.0f));
        paintRow2 (g, drawerAmt * dim);
    }
}

void DeckStrip::paintPill (juce::Graphics& g, juce::Rectangle<float> r, bool active,
                           bool hover, juce::Colour acc) const
{
    const float rad = 5.0f;
    g.setColour (active ? acc.withAlpha (0.18f) : (hover ? tok::bgHover : tok::bgControl));
    g.fillRoundedRectangle (r, rad);
    g.setColour (active ? acc.withAlpha (0.9f) : tok::border);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

void DeckStrip::paintPlayGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    auto b = r.withSizeKeepingCentre (12.0f, 13.0f);
    juce::Path p;
    p.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
    g.setColour (c);
    g.fillPath (p);
}

void DeckStrip::paintPauseGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    auto b = r.withSizeKeepingCentre (12.0f, 13.0f);
    const float barW = 3.6f;
    g.setColour (c);
    g.fillRect (b.removeFromLeft (barW));
    g.fillRect (b.removeFromRight (barW));
}

void DeckStrip::paintLoopGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    auto b = r.withSizeKeepingCentre (13.0f, 13.0f);
    juce::Path ring;
    ring.addCentredArc (b.getCentreX(), b.getCentreY(), b.getWidth() * 0.42f, b.getHeight() * 0.42f,
                        0.0f, 0.6f, juce::MathConstants<float>::twoPi, true);
    g.setColour (c);
    g.strokePath (ring, juce::PathStrokeType (1.6f));
    // arrowhead
    juce::Path head;
    const float hx = b.getCentreX() + b.getWidth() * 0.42f, hy = b.getY() + 1.0f;
    head.addTriangle (hx - 2.6f, hy, hx + 2.6f, hy, hx, hy + 4.4f);
    g.fillPath (head);
}

void DeckStrip::paintPhaseRing (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
{
    auto b = r.withSizeKeepingCentre (16.0f, 16.0f);
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float rad = 7.0f;
    // Track.
    g.setColour (tok::border);
    juce::Path track; track.addEllipse (b.reduced (1.0f));
    g.strokePath (track, juce::PathStrokeType (1.4f));

    if (! state.syncOn) return;

    // Arc proportional to how far off the downbeat we are; warn colour past ~15ms.
    const float off = juce::jlimit (-1.0f, 1.0f, state.phaseOffset);
    const bool  drifting = std::abs (off) > 0.12f && ! state.syncLocked;
    const auto  ringCol = drifting ? tok::warn : c;
    const float sweep = state.syncLocked ? juce::MathConstants<float>::twoPi
                                         : juce::jmax (0.18f, std::abs (off)) * juce::MathConstants<float>::twoPi;
    const float start  = phaseSpin;
    juce::Path arc;
    arc.addCentredArc (cx, cy, rad, rad, 0.0f, start, start + sweep, true);
    g.setColour (ringCol);
    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void DeckStrip::paintChevron (juce::Graphics& g, juce::Rectangle<float> r, float openAmount, juce::Colour c) const
{
    auto b = r.withSizeKeepingCentre (11.0f, 7.0f);
    juce::Path p;
    p.startNewSubPath (b.getX(), b.getY());
    p.lineTo (b.getCentreX(), b.getBottom());
    p.lineTo (b.getRight(), b.getY());
    // rotate 0..180° around centre by openAmount.
    p.applyTransform (juce::AffineTransform::rotation (openAmount * juce::MathConstants<float>::pi,
                                                       b.getCentreX(), b.getCentreY()));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void DeckStrip::paintRow1 (juce::Graphics& g)
{
    const bool live = state.hasTrack;
    const float dim = state.focused ? 1.0f : 0.85f;
    auto dimc = [dim] (juce::Colour c) { return c.withMultipliedAlpha (dim); };

    // Deck numeral, immediately right of the accent bar.
    {
        auto n = juce::Rectangle<float> (2.0f, 0.0f, 20.0f, (float) kCollapsedHeight);
        g.setColour (dimc (state.focused ? accent : tok::fg.withAlpha (0.40f)));
        g.setFont (grotesque (11.0f, true).withExtraKerningFactor (0.02f));
        g.drawText (juce::String (deck + 1), n.toNearestInt(), juce::Justification::centred);
    }

    // ── Play / pause (primary) ──
    {
        auto rr = regionFor (Hit::play);
        const bool playing = state.playing;
        const bool hover = hovered == Hit::play;
        g.setColour (playing ? tok::bgActLift : (hover ? tok::bgHover : tok::bgControl));
        g.fillRoundedRectangle (rr, 6.0f);
        g.setColour (playing ? accent.withAlpha (0.9f) : (state.cueHeld ? accent : tok::border));
        g.drawRoundedRectangle (rr.reduced (0.5f), 6.0f, state.cueHeld ? 1.3f : 1.0f);
        const auto glyphC = dimc (live ? tok::primary (playing ? 1.0f : 0.75f) : tok::disabled());
        if (playing) paintPauseGlyph (g, rr, glyphC);
        else         paintPlayGlyph  (g, rr, glyphC);
    }

    // ── CUE (primary) ──
    {
        auto rr = regionFor (Hit::cue);
        const bool held = state.cueHeld;
        const bool hover = hovered == Hit::cue;
        g.setColour (held ? accent.withAlpha (0.85f) : (hover ? tok::bgHover : tok::bgControl));
        g.fillRoundedRectangle (rr, 6.0f);
        g.setColour (held ? accent : tok::border);
        g.drawRoundedRectangle (rr.reduced (0.5f), 6.0f, 1.0f);
        const float a = ! state.cueSet ? 0.45f : (held ? 1.0f : 0.75f);
        g.setColour (dimc (held ? juce::Colours::white : tok::primary (a)));
        g.setFont (grotesque (15.0f, true));
        g.drawText ("CUE", rr.toNearestInt(), juce::Justification::centred);
    }

    // ── Loop cluster (secondary) ──
    {
        const bool on = state.loopActive;
        const bool armed = state.loopArmed;
        auto half = regionFor (Hit::loopHalve);
        auto num  = regionFor (Hit::loopNum);
        auto dbl  = regionFor (Hit::loopDouble);

        paintPill (g, num, on, hovered == Hit::loopNum, accent);
        // ⟲ glyph tucked at the left of the halve control doubles as loop indicator.
        paintLoopGlyph (g, half.withWidth (14.0f).translated (-1.0f, 0.0f),
                        dimc (on ? accent : (armed ? tok::secondary() : tok::tertiary())));

        auto tick = [&] (juce::Rectangle<float> rr, const juce::String& t, bool hov)
        {
            g.setColour (dimc (hov ? tok::primary (0.9f) : tok::secondary()));
            g.setFont (grotesque (12.0f, true));
            g.drawText (t, rr.toNearestInt(), juce::Justification::centred);
        };
        tick (half.withTrimmedLeft (10.0f), juce::CharPointer_UTF8 ("\xc2\xbd"), hovered == Hit::loopHalve);
        tick (dbl, juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + "2", hovered == Hit::loopDouble);

        g.setColour (dimc (on ? tok::primary() : tok::secondary()));
        g.setFont (grotesque (13.0f, true).withHorizontalScale (1.0f));
        g.drawText (loopBeatsLabel (state.loopBeats), num.toNearestInt(), juce::Justification::centred);
    }

    // ── Beat jump (secondary) ──
    {
        auto back = regionFor (Hit::jumpBack);
        auto num  = regionFor (Hit::jumpNum);
        auto fwd  = regionFor (Hit::jumpFwd);
        auto arrow = [&] (juce::Rectangle<float> rr, bool left, bool hov)
        {
            auto b = rr.withSizeKeepingCentre (12.0f, 11.0f);
            juce::Path p;
            if (left) { p.addTriangle (b.getRight(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getCentreY());
                        p.addRectangle (b.getX() - 0.5f, b.getY(), 1.6f, b.getHeight()); }
            else      { p.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
                        p.addRectangle (b.getRight() - 1.1f, b.getY(), 1.6f, b.getHeight()); }
            g.setColour (dimc (hov ? tok::primary (0.9f) : tok::secondary()));
            g.fillPath (p);
        };
        arrow (back, true, hovered == Hit::jumpBack);
        arrow (fwd, false, hovered == Hit::jumpFwd);
        g.setColour (dimc (hovered == Hit::jumpNum ? tok::primary (0.9f) : tok::secondary()));
        g.setFont (grotesque (13.0f, true));
        g.drawText (juce::String (state.beatJumpBars), num.toNearestInt(), juce::Justification::centred);
    }

    // ── SYNC + phase ring (secondary; icon-only when compact) ──
    {
        auto rr = regionFor (Hit::sync);
        paintPill (g, rr, state.syncOn, hovered == Hit::sync, accent);
        if (compactMode)
        {
            paintPhaseRing (g, rr, accent);          // ring fills the pill, no label
        }
        else
        {
            auto ringArea = rr.removeFromRight (20.0f);
            g.setColour (dimc (state.syncOn ? tok::primary() : tok::secondary()));
            g.setFont (grotesque (11.0f, true).withExtraKerningFactor (0.03f));
            g.drawText ("SYNC", rr.toNearestInt(), juce::Justification::centred);
            paintPhaseRing (g, ringArea, accent);
        }
    }

    // ── KEY (secondary — the differentiator) ──
    // Fixed KEY caption (left) + fixed ± slot (right) + Camelot filling the middle,
    // all drawn with ellipsis disabled so the transposition never clips to "+...".
    {
        auto rr = regionFor (Hit::key);
        paintPill (g, rr, false, hovered == Hit::key, accent);
        auto inner = rr.reduced (compactMode ? 6.0f : 8.0f, 0.0f);

        g.setColour (dimc (tok::tertiary()));
        g.setFont (grotesque (compactMode ? 9.0f : 10.0f, true).withExtraKerningFactor (0.03f));
        g.drawText ("KEY", inner.removeFromLeft (compactMode ? 20.0f : 26.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);

        // ± transposition — fixed right slot.
        const juce::String tp = (state.keySemis > 0 ? "+" : "") + juce::String (state.keySemis);
        auto pmSlot = inner.removeFromRight (compactMode ? 22.0f : 26.0f);
        g.setColour (dimc (state.keySemis != 0 ? accent : tok::tertiary()));
        g.setFont (grotesque (compactMode ? 11.0f : 12.0f, true));
        g.drawText (tp, pmSlot.toNearestInt(), juce::Justification::centredRight, false);

        // Camelot value — italic + faded when low-confidence.
        g.setColour (dimc (state.keyLowConf ? tok::fg.withAlpha (0.60f) : tok::primary()));
        g.setFont (grotesque (compactMode ? 12.0f : 13.0f, true, state.keyLowConf));
        g.drawText (state.keyCamelot, inner.toNearestInt(), juce::Justification::centredLeft, false);
    }

    // ── MARK: drop a marker at the playhead (secondary; icon-only when compact).
    //    Named "MARK", not "TAKE", because it sets a cue point — it is not take/comp capture. ──
    {
        auto rr = regionFor (Hit::take);
        const bool hover = hovered == Hit::take;
        paintPill (g, rr, false, hover, accent);
        const auto dotCol = dimc (hover ? juce::Colour (0xffFF7A7A) : juce::Colour (0xffFF5A5A));
        if (compactMode)
        {
            g.setColour (dotCol);
            g.fillEllipse (rr.withSizeKeepingCentre (7.0f, 7.0f));
        }
        else
        {
            auto dot = rr.removeFromLeft (22.0f).withSizeKeepingCentre (7.0f, 7.0f);
            g.setColour (dotCol);
            g.fillEllipse (dot);
            g.setColour (dimc (hover ? tok::primary (0.95f) : tok::secondary()));
            g.setFont (grotesque (12.0f, true).withExtraKerningFactor (0.03f));
            g.drawText ("MARK", rr.toNearestInt(), juce::Justification::centredLeft);
        }
    }

    // ── Expand chevron (tertiary) ──
    paintChevron (g, regionFor (Hit::chevron), drawerAmt,
                  dimc (hovered == Hit::chevron ? tok::primary (0.8f) : tok::fg.withAlpha (0.40f)));
}

void DeckStrip::paintRow2 (juce::Graphics& g, float alpha)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));

    const bool separating = state.sepProgress < 0.999f;

    // Stem toggles.
    for (int i = 0; i < 4; ++i)
    {
        auto rr = regionFor ((Hit) ((int) Hit::stem0 + i));
        const auto col = juce::Colour (kStems[(size_t) i].colour);
        const int  st  = state.stem[(size_t) kStems[(size_t) i].lane];   // 0 active,1 muted,2 solo
        const bool hover = hovered == (Hit) ((int) Hit::stem0 + i);

        // Base pill.
        g.setColour (separating ? tok::bgControl : (st == 1 ? tok::bgControl : col.withAlpha (0.16f)));
        g.fillRoundedRectangle (rr, 4.0f);

        if (separating)
        {
            // speculative: 20% fill with a subtle progress bar.
            g.setColour (col.withAlpha (0.20f));
            g.fillRoundedRectangle (rr.withWidth (rr.getWidth() * juce::jlimit (0.05f, 1.0f, state.sepProgress)), 4.0f);
        }

        g.setColour (st == 2 ? col : (hover ? col.withAlpha (0.8f) : tok::border));
        g.drawRoundedRectangle (rr.reduced (0.5f), 4.0f, st == 2 ? 1.3f : 1.0f);

        // Label.
        const float la = separating ? 0.35f : (st == 1 ? 0.30f : 1.0f);
        g.setColour ((st == 1 ? tok::fg : col.brighter (0.4f)).withAlpha (la));
        g.setFont (grotesque (11.0f, true).withExtraKerningFactor (0.04f));
        g.drawText (kStems[(size_t) i].label, rr.toNearestInt(), juce::Justification::centred);

        // Muted strike-through.
        if (st == 1 && ! separating)
        {
            g.setColour (tok::fg.withAlpha (0.45f));
            g.fillRect (rr.withSizeKeepingCentre (rr.getWidth() - 12.0f, 1.0f));
        }
    }

    // Quantize (resolution visible).
    {
        auto rr = regionFor (Hit::quant);
        const bool on = state.quantIndex != 0;
        paintPill (g, rr, on, hovered == Hit::quant, accent);
        auto inner = rr.reduced (7.0f, 0.0f);
        g.setColour (tok::tertiary());
        g.setFont (grotesque (10.0f, true).withExtraKerningFactor (0.04f));
        g.drawText ("QUANT", inner.removeFromLeft (36.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (on ? tok::primary (0.9f) : tok::tertiary());
        g.setFont (grotesque (12.0f, true));
        g.drawText (quantLabel (state.quantIndex), inner.toNearestInt(), juce::Justification::centredRight);
    }

    // Slip.
    {
        auto rr = regionFor (Hit::slip);
        paintPill (g, rr, state.slipOn, hovered == Hit::slip, accent);
        g.setColour (state.slipOn ? tok::primary (0.9f) : tok::tertiary());
        g.setFont (grotesque (11.0f, true).withExtraKerningFactor (0.04f));
        g.drawText ("SLIP", rr.toNearestInt(), juce::Justification::centred);
    }

    // Editable beat grid.
    {
        auto rr = regionFor (Hit::grid);
        paintPill (g, rr, false, hovered == Hit::grid, accent);
        g.setColour (hovered == Hit::grid ? tok::primary (0.95f) : tok::tertiary());
        g.setFont (grotesque (10.0f, true).withExtraKerningFactor (0.04f));
        g.drawText ("GRID", rr.toNearestInt(), juce::Justification::centred);
    }

    // FX sends A / B / C + caption.
    {
        auto capRegion = regionFor (Hit::fxA);   // leftmost (fxA laid rightmost-first)
        // Two real sends: A = Echo, B = Reverb.
        const char* labels[] { "A", "B" };
        const Hit hits[] { Hit::fxA, Hit::fxB };
        juce::Rectangle<float> leftMost { 1.0e6f, 0, 0, 0 };
        for (int i = 0; i < 2; ++i)
        {
            auto rr = regionFor (hits[i]);
            leftMost = rr.getX() < leftMost.getX() ? rr : leftMost;
            paintPill (g, rr, state.fx[(size_t) i], hovered == hits[i], accent);
            g.setColour (state.fx[(size_t) i] ? tok::primary (0.95f) : tok::tertiary());
            g.setFont (grotesque (11.0f, true));
            g.drawText (labels[i], rr.toNearestInt(), juce::Justification::centred);
        }
        juce::ignoreUnused (capRegion);
        auto cap = juce::Rectangle<float> (leftMost.getX() - 26.0f, leftMost.getY(), 24.0f, leftMost.getHeight());
        g.setColour (tok::tertiary());
        g.setFont (grotesque (10.0f, true).withExtraKerningFactor (0.04f));
        g.drawText ("FX", cap.toNearestInt(), juce::Justification::centredRight);
    }
}

// ── Interaction ───────────────────────────────────────────────────────────────
void DeckStrip::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h != hovered) { hovered = h; repaint(); }
}

void DeckStrip::mouseExit (const juce::MouseEvent&)
{
    if (hovered != Hit::none) { hovered = Hit::none; repaint(); }
}

void DeckStrip::mouseDown (const juce::MouseEvent& e)
{
    if (callbacks.onRequestFocus && ! state.focused) callbacks.onRequestFocus();

    pressed = hitTest (e.position);
    const bool alt = e.mods.isAltDown();

    switch (pressed)
    {
        case Hit::play:       if (callbacks.onTogglePlay)    callbacks.onTogglePlay(); break;
        case Hit::cue:        if (callbacks.onCue)           callbacks.onCue (true); break;   // press = jump+play
        case Hit::loopHalve:  if (callbacks.onLoopHalve)     callbacks.onLoopHalve(); break;
        case Hit::loopDouble: if (callbacks.onLoopDouble)    callbacks.onLoopDouble(); break;
        case Hit::loopNum:    if (callbacks.onLoopToggle)    callbacks.onLoopToggle(); break;
        case Hit::jumpBack:   if (callbacks.onBeatJump)      callbacks.onBeatJump (-1); break;
        case Hit::jumpFwd:    if (callbacks.onBeatJump)      callbacks.onBeatJump (+1); break;
        case Hit::jumpNum:    if (callbacks.onCycleBeatJump) callbacks.onCycleBeatJump(); break;
        case Hit::sync:       if (callbacks.onToggleSync)    callbacks.onToggleSync(); break;
        case Hit::key:        if (callbacks.onOpenKey)       callbacks.onOpenKey(); break;
        case Hit::take:       if (callbacks.onMarkTake)      callbacks.onMarkTake(); break;
        case Hit::chevron:    setExpanded (! expanded); break;
        case Hit::stem0: case Hit::stem1: case Hit::stem2: case Hit::stem3:
        {
            const int idx = kStems[(size_t) ((int) pressed - (int) Hit::stem0)].lane;
            if (alt) { if (callbacks.onSoloStem)  callbacks.onSoloStem (idx); }
            else     { if (callbacks.onToggleStem) callbacks.onToggleStem (idx); }
            break;
        }
        case Hit::quant: if (callbacks.onCycleQuantize) callbacks.onCycleQuantize(); break;
        case Hit::slip:  if (callbacks.onToggleSlip)    callbacks.onToggleSlip(); break;
        case Hit::grid:  if (callbacks.onOpenBeatGrid)  callbacks.onOpenBeatGrid(); break;
        case Hit::fxA:   if (callbacks.onToggleFx)      callbacks.onToggleFx (0); break;
        case Hit::fxB:   if (callbacks.onToggleFx)      callbacks.onToggleFx (1); break;
        case Hit::fxC:   break;   // removed: had no audio routing
        default: break;
    }
    repaint();
}

void DeckStrip::mouseUp (const juce::MouseEvent&)
{
    if (pressed == Hit::cue && callbacks.onCue)
        callbacks.onCue (false);   // release = return to cue point
    pressed = Hit::none;
    repaint();
}

void DeckStrip::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Scroll over the key readout nudges transposition in semitones.
    if (hitTest (e.position) == Hit::key && callbacks.onNudgeKey)
    {
        const int steps = w.deltaY > 0.02f ? 1 : (w.deltaY < -0.02f ? -1 : 0);
        if (steps != 0) callbacks.onNudgeKey (steps);
    }
    else
    {
        Component::mouseWheelMove (e, w);
    }
}

} // namespace stemdeck
