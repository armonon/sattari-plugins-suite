#include "VocalCrownCompass.h"
#include "RoyalChainProcessor.h"
#include <sattari_audio_core/SattariBranding.h>

namespace
{
constexpr float twoPi   = juce::MathConstants<float>::twoPi;
constexpr float halfPi  = juce::MathConstants<float>::halfPi;

// Suite palette tints used by the portal (all inside the mint / teal / cyan
// family so the Compass reads as the same Auto-Pitch identity as the suite).
const juce::Colour kMint  { 0xff62f5c8 };   // signature accent
const juce::Colour kCyan  { 0xff7dd3fc };   // cool highlight
const juce::Colour kTeal  { 0xff34d8b4 };   // deep ripple
const juce::Colour kVoid0 { 0xff0a1622 };   // portal mouth (deep teal-ink)
const juce::Colour kVoid1 { 0xff05070c };   // outer void (near black)

inline juce::Colour mix (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t));
}
}

VocalCrownCompass::VocalCrownCompass (RoyalChainAudioProcessor& p)
    : processor (p)
{
    // Five module nodes mapped to existing macro params, coloured within the
    // suite mint/teal/cyan family so the five shades stay distinguishable while
    // still reading as one identity.
    nodes[0] = { "TUNE",    "tune",    kCyan,                       -halfPi };           // top
    nodes[1] = { "CONTROL", "control", kMint,                        0.0f };              // right
    nodes[2] = { "FINISH",  "glow",    juce::Colour (0xff26d9ff),    halfPi };            // bottom
    nodes[3] = { "SMOOTH",  "smooth",  kTeal,                        juce::MathConstants<float>::pi }; // left
    nodes[4] = { "WIDTH",   "width",   juce::Colour (0xff8ce0d0),   -halfPi - 0.78f };    // upper-left

    // Seed the portal dust once (deterministic so the field is stable between
    // editor opens; only the per-frame motion animates).
    juce::Random rng (0xC0FFEEu);
    for (int i = 0; i < kNumParticles; ++i)
    {
        partAngle[i]  = (float) rng.nextDouble() * twoPi;
        partRadius[i] = 0.05f + 1.10f * (float) rng.nextDouble();
        partSpin[i]   = (0.004f + 0.012f * (float) rng.nextDouble()) * (rng.nextBool() ? 1.0f : -1.0f);
        partSpeed[i]  = 0.006f + 0.020f * (float) rng.nextDouble();
        partSize[i]   = 0.6f + 1.8f * (float) rng.nextDouble();
    }

    startTimerHz (30);
}

VocalCrownCompass::~VocalCrownCompass() { stopTimer(); }

float VocalCrownCompass::getMacro (const juce::String& id) const
{
    if (auto* v = processor.parameters().getRawParameterValue (id))
        return juce::jlimit (0.0f, 1.0f, v->load());
    return 0.0f;
}

void VocalCrownCompass::setMacro (const juce::String& id, float value)
{
    if (auto* p = processor.parameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
        p->endChangeGesture();
    }
}

juce::Point<float> VocalCrownCompass::nodePosition (int idx) const
{
    const auto& n = nodes[idx];
    const float r = getMacro (n.paramId) * ringRadius;
    return { centre.x + std::cos (n.angleRad) * r,
             centre.y + std::sin (n.angleRad) * r };
}

int VocalCrownCompass::hitTestNode (juce::Point<float> p) const
{
    constexpr float hit = 18.0f;
    for (int i = 0; i < kNumNodes; ++i)
        if (p.getDistanceFrom (nodePosition (i)) < hit)
            return i;
    return -1;
}

void VocalCrownCompass::resized()
{
    // Nudge the centre down a touch so the top node's label clears the panel
    // frame, and keep the radius modest so node labels (which sit outside the
    // ring) stay inside the short, wide panel.
    auto area = getLocalBounds().toFloat().reduced (16.0f);
    centre = area.getCentre().translated (0.0f, 6.0f);
    ringRadius = juce::jmin (area.getWidth(), area.getHeight()) * 0.40f;
}

void VocalCrownCompass::timerCallback()
{
    // Advance the master phase (ripple travel + slow vortex spin).
    phase += 0.085f;
    if (phase > twoPi * 64.0f) phase -= twoPi * 64.0f;

    // Smooth the output level so the portal "breathes" with the vocal instead of
    // jittering. gainToDecibels-style soft mapping into 0..1.
    const float raw = juce::jlimit (0.0f, 1.0f, processor.getOutputMeter() * 1.4f);
    level += (raw - level) * (raw > level ? 0.35f : 0.08f);   // fast attack, slow release

    // Spiral the dust inward; recycle to the outer edge when it reaches the mouth.
    for (int i = 0; i < kNumParticles; ++i)
    {
        partAngle[i]  += partSpin[i] * (1.0f + 0.6f * level);
        partRadius[i] -= partSpeed[i] * (0.18f + partRadius[i]) * (1.0f + 1.2f * level);
        if (partRadius[i] < 0.035f)
        {
            partRadius[i] = 1.08f + 0.10f * partSize[i];
            partAngle[i]  = std::fmod (partAngle[i] + 2.399963f, twoPi); // golden-angle scatter
        }
    }

    repaint();
}

void VocalCrownCompass::paint (juce::Graphics& g)
{
    auto local = getLocalBounds().toFloat();
    const float R = ringRadius;
    const float lvl = level;

    // Clip the whole portal to a rounded panel so it sits inside the suite UI
    // like every other surface (instead of a hard-edged black rectangle).
    const juce::Rectangle<float> panel = local.reduced (1.0f);
    juce::Path panelClip;
    panelClip.addRoundedRectangle (panel, 14.0f);
    g.saveState();
    g.reduceClipRegion (panelClip);

    // -- Layer 0 : portal void ------------------------------------------------
    // A deep radial gradient that pulls the eye into the centre, brightened a
    // touch by the audio level (the portal "lights up" when the vocal plays).
    {
        const auto mouth = mix (kVoid0, kTeal.withMultipliedBrightness (0.5f), 0.25f + 0.45f * lvl);
        juce::ColourGradient bg (mouth, centre.x, centre.y, kVoid1, centre.x, centre.y - R * 2.2f, true);
        bg.addColour (0.55, mix (kVoid0, kVoid1, 0.5f));
        g.setGradientFill (bg);
        g.fillRect (local);
    }

    // -- Layer 1 : portal tunnel rings (receding depth) -----------------------
    // Concentric rings spaced toward the centre, each slightly brighter as it
    // recedes, with a tiny rotating offset so the tunnel feels alive.
    for (int i = 0; i < 9; ++i)
    {
        const float t  = i / 9.0f;
        const float rr = R * (1.06f - 0.96f * t * t);           // pack toward centre
        const float wob = 1.0f + 0.012f * std::sin (phase * 0.6f + (float) i);
        const float a  = (0.05f + 0.05f * (1.0f - t)) * (0.6f + 0.6f * lvl);
        g.setColour (mix (kTeal, kCyan, t).withAlpha (a));
        g.drawEllipse (centre.x - rr * wob, centre.y - rr,
                       rr * 2.0f * wob, rr * 2.0f, 1.0f);
    }

    // -- Layer 2 : cymatic standing-wave field --------------------------------
    // Concentric Bessel-style ripple rings: a wave travels outward from the
    // centre, so each ring brightens/dims as the crest passes through it.
    const float waveK = 19.0f;
    for (int i = 1; i < kNumRipples; ++i)
    {
        const float t = i / (float) (kNumRipples - 1);          // 0..1
        const float r = R * (0.05f + 0.97f * t);
        const float wave = std::sin (t * waveK - phase * 1.7f); // -1..1 traveling crest
        const float crest = 0.5f + 0.5f * wave;
        const float fade  = std::pow (1.0f - t, 0.35f);         // soften toward the rim
        const float a = (0.025f + 0.13f * crest) * fade * (0.45f + 0.85f * lvl + 0.15f);
        g.setColour (mix (kMint, kCyan, t).withAlpha (juce::jlimit (0.0f, 0.4f, a)));
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f,
                       0.8f + 1.4f * crest);
    }

    // -- Layer 3 : Chladni angular nodal spokes (rotating) --------------------
    // Higher cymatic modes add radial nodal lines; we rotate them slowly so the
    // figure shimmers like a real driven plate.
    {
        const float spin = phase * 0.10f;
        for (int s = 0; s < kNodalSpokes; ++s)
        {
            const float ang = spin + s * (twoPi / kNodalSpokes);
            const float inner = R * 0.12f;
            const float outer = R * (0.96f + 0.04f * std::sin (phase * 0.8f + s));
            juce::ColourGradient grad (kCyan.withAlpha (0.0f),
                                       centre.x + std::cos (ang) * inner,
                                       centre.y + std::sin (ang) * inner,
                                       kCyan.withAlpha (0.10f + 0.12f * lvl),
                                       centre.x + std::cos (ang) * outer,
                                       centre.y + std::sin (ang) * outer, false);
            g.setGradientFill (grad);
            g.drawLine (centre.x + std::cos (ang) * inner, centre.y + std::sin (ang) * inner,
                        centre.x + std::cos (ang) * outer, centre.y + std::sin (ang) * outer, 1.0f);
        }
    }

    // -- Layer 4 : swirling portal dust ---------------------------------------
    for (int i = 0; i < kNumParticles; ++i)
    {
        const float rr = partRadius[i] * R;
        const juce::Point<float> p (centre.x + std::cos (partAngle[i]) * rr,
                                    centre.y + std::sin (partAngle[i]) * rr);
        // Brightest in the mid field; fades into the mouth and at the rim.
        const float depth = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (partRadius[i] - 0.55f) * 1.4f);
        const float a = (0.10f + 0.55f * depth) * (0.5f + 0.7f * lvl + 0.1f);
        const float sz = partSize[i] * (0.7f + 0.9f * depth);
        g.setColour (mix (kCyan, juce::Colours::white, 0.3f).withAlpha (juce::jlimit (0.0f, 0.8f, a)));
        g.fillEllipse (p.x - sz, p.y - sz, sz * 2.0f, sz * 2.0f);
    }

    // -- Layer 5 : outer ring (gold-free mint→cyan sweep) ---------------------
    {
        juce::Path ring;
        ring.addEllipse (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f);
        juce::ColourGradient ringGrad (kMint.withAlpha (0.85f), centre.x, centre.y - R,
                                       kCyan.withAlpha (0.6f), centre.x, centre.y + R, false);
        g.setGradientFill (ringGrad);
        g.strokePath (ring, juce::PathStrokeType (1.6f));

        // Faint tick marks at the four cardinal macro axes.
        for (int q = 0; q < 12; ++q)
        {
            const float ang = q * (twoPi / 12.0f);
            const float r0 = R * 0.97f, r1 = R * 1.02f;
            g.setColour (kCyan.withAlpha (0.18f));
            g.drawLine (centre.x + std::cos (ang) * r0, centre.y + std::sin (ang) * r0,
                        centre.x + std::cos (ang) * r1, centre.y + std::sin (ang) * r1, 1.0f);
        }
    }

    // -- Layer 6 : macro membrane (the functional radar polygon) --------------
    // The five macro values define a polygon; we fill it with a translucent
    // accent membrane and stroke the rim so the chain's "shape" reads instantly.
    {
        juce::Path membrane;
        for (int i = 0; i < kNumNodes; ++i)
        {
            const auto pos = nodePosition (i);
            if (i == 0) membrane.startNewSubPath (pos);
            else        membrane.lineTo (pos);
        }
        membrane.closeSubPath();

        juce::ColourGradient memb (kMint.withAlpha (0.22f + 0.10f * lvl), centre.x, centre.y,
                                   kTeal.withAlpha (0.04f), centre.x, centre.y - R, true);
        g.setGradientFill (memb);
        g.fillPath (membrane);
        g.setColour (kMint.withAlpha (0.55f));
        g.strokePath (membrane, juce::PathStrokeType (1.4f));
    }

    // -- Layer 7 : centre core (portal mouth) ---------------------------------
    {
        const float coreR = 9.0f + 5.0f * lvl;
        juce::ColourGradient core (juce::Colours::white.withAlpha (0.85f), centre.x, centre.y,
                                   kMint.withAlpha (0.0f), centre.x + coreR * 3.2f, centre.y, true);
        core.addColour (0.35, kMint.withAlpha (0.7f));
        g.setGradientFill (core);
        g.fillEllipse (centre.x - coreR * 3.2f, centre.y - coreR * 3.2f, coreR * 6.4f, coreR * 6.4f);
    }

    // -- Layer 8 : interactive nodes ------------------------------------------
    for (int i = 0; i < kNumNodes; ++i)
    {
        const auto pos = nodePosition (i);
        const auto& n  = nodes[i];
        const bool active = (i == draggingNode);
        const bool hot    = active || (i == hoveredNode);

        // Radial guide line from centre to the node.
        g.setColour (n.colour.withAlpha (hot ? 0.55f : 0.30f));
        g.drawLine (centre.x, centre.y, pos.x, pos.y, hot ? 1.6f : 1.1f);

        // Each node emits its own cymatic ripple — a ring whose radius pulses and
        // whose intensity equals the macro value (louder macro = stronger wave).
        const float macro = getMacro (n.paramId);
        if (macro > 0.02f)
        {
            const float pr = 10.0f + (16.0f + 10.0f * macro) * (0.5f + 0.5f * std::sin (phase * 1.7f + i));
            g.setColour (n.colour.withAlpha (0.18f * macro));
            g.drawEllipse (pos.x - pr, pos.y - pr, pr * 2.0f, pr * 2.0f, 1.4f);
        }

        // Glow halo.
        const float nodeR = 8.5f + (active ? 3.0f : (hot ? 1.5f : 0.0f));
        g.setColour (n.colour.withAlpha (hot ? 0.30f : 0.20f));
        g.fillEllipse (pos.x - nodeR * 2.4f, pos.y - nodeR * 2.4f, nodeR * 4.8f, nodeR * 4.8f);

        // Node disc with a tiny vertical sheen.
        juce::ColourGradient disc (n.colour.brighter (0.4f), pos.x, pos.y - nodeR,
                                   n.colour.darker (0.2f), pos.x, pos.y + nodeR, false);
        g.setGradientFill (disc);
        g.fillEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (hot ? 0.95f : 0.75f));
        g.drawEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f, hot ? 1.6f : 1.0f);

        // Rail feedback: bright pulse ring when pinned at 0 or 1.
        if (macro <= 0.001f || macro >= 0.999f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            const float pulseR = nodeR + 4.0f;
            g.drawEllipse (pos.x - pulseR, pos.y - pulseR, pulseR * 2.0f, pulseR * 2.0f, 2.0f);
        }

        // Label + value chip, offset away from the centre so they read. The
        // bottom node would push its pair into the drag hint / panel edge, so we
        // lift the label+value *above* that node instead.
        const float labelDist = nodeR + 14.0f;
        const float lx       = pos.x + std::cos (n.angleRad) * labelDist;
        const float outwardY = pos.y + std::sin (n.angleRad) * labelDist;
        float labelY, valueY;
        if (outwardY + 26.0f > local.getBottom() - 20.0f)
        {
            labelY = pos.y - (labelDist + 13.0f);   // stacked above the node
            valueY = pos.y -  labelDist;
        }
        else
        {
            labelY = outwardY;
            valueY = outwardY + 13.0f;
        }

        g.setColour (juce::Colours::white.withAlpha (hot ? 1.0f : 0.92f));
        g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
        g.drawText (n.label, (int) lx - 36, (int) labelY - 8, 72, 16,
                    juce::Justification::centred, false);

        const int pct = (int) std::round (macro * 100.0f);
        g.setColour (n.colour.withAlpha (0.9f));
        g.setFont (sattari::brand::fontOptions (9.0f, juce::Font::bold));
        g.drawText (juce::String (pct), (int) lx - 36, (int) valueY - 6, 72, 12,
                    juce::Justification::centred, false);
    }

    // -- Layer 9 : chrome -----------------------------------------------------
    // Rotated side identity label (Portal-style).
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-halfPi, local.getX() + 18.0f, local.getCentreY()));
    g.setColour (kMint.withAlpha (0.55f));
    g.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    g.drawText ("VOCAL CROWN COMPASS", (int) (local.getX() + 18.0f - 110.0f),
                (int) local.getCentreY() - 7, 220, 14, juce::Justification::centred);
    g.restoreState();

    g.setColour (kCyan.withAlpha (0.6f));
    g.setFont (sattari::brand::fontOptions (12.0f, juce::Font::bold));
    g.drawText ("ROYAL CHAIN", local.removeFromTop (20.0f).reduced (18.0f, 0.0f).toNearestInt(),
                juce::Justification::left, false);

    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.setFont (sattari::brand::fontOptions (9.5f));
    g.drawText ("Drag a node outward to raise its macro  -  centre = 0, ring = 100",
                local.removeFromBottom (18.0f).toNearestInt(), juce::Justification::centred, false);

    // Rounded panel frame on top of the clipped portal.
    g.restoreState();
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (panel, 14.0f, 1.0f);
}

void VocalCrownCompass::mouseDown (const juce::MouseEvent& e)
{
    draggingNode = hitTestNode (e.position);
    repaint();
}

void VocalCrownCompass::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingNode < 0 || ringRadius <= 0.0f)
        return;

    // Project the mouse onto the node's radial axis.
    const auto& n = nodes[draggingNode];
    const juce::Point<float> dir (std::cos (n.angleRad), std::sin (n.angleRad));
    const juce::Point<float> rel = e.position - centre;
    const float proj = rel.x * dir.x + rel.y * dir.y;
    const float value = juce::jlimit (0.0f, 1.0f, proj / ringRadius);
    setMacro (n.paramId, value);
    repaint();
}

void VocalCrownCompass::mouseUp (const juce::MouseEvent&)
{
    draggingNode = -1;
    repaint();
}

void VocalCrownCompass::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hoveredNode)
    {
        hoveredNode = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);
    }
}

void VocalCrownCompass::mouseExit (const juce::MouseEvent&)
{
    hoveredNode = -1;
}
