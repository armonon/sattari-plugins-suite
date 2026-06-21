#include "VocalCrownCompass.h"
#include "RoyalChainProcessor.h"
#include <sattari_audio_core/SattariBranding.h>

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;
}

VocalCrownCompass::VocalCrownCompass (RoyalChainAudioProcessor& p)
    : processor (p)
{
    // Five module nodes mapped to existing macro params.
    nodes[0] = { "TUNE",    "tune",    juce::Colour (0xff7dd3fc), -juce::MathConstants<float>::halfPi };                       // top
    nodes[1] = { "CONTROL", "control", juce::Colour (0xff62f5c8),  0.0f };                                                       // right
    nodes[2] = { "FINISH",  "glow",    juce::Colour (0xfffacc15),  juce::MathConstants<float>::halfPi };                       // bottom
    nodes[3] = { "SMOOTH",  "smooth",  juce::Colour (0xfff472b6),  juce::MathConstants<float>::pi };                            // left
    nodes[4] = { "WIDTH",   "width",   juce::Colour (0xffa78bfa), -juce::MathConstants<float>::halfPi - 0.78f };                 // upper-left

    // Star-field rays — fixed once so the layout stays steady (no flicker).
    juce::Random rng (0xC0FFEEu);
    for (int i = 0; i < kNumRays; ++i)
    {
        rayAngles[i]  = (float) rng.nextDouble() * twoPi;
        rayLengths[i] = 0.35f + 0.65f * (float) rng.nextDouble();
        rayJitter[i]  = 0.92f + 0.16f * (float) rng.nextDouble();
    }

    startTimerHz (24);
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

void VocalCrownCompass::resized()
{
    auto area = getLocalBounds().toFloat().reduced (16.0f);
    centre = area.getCentre();
    ringRadius = juce::jmin (area.getWidth(), area.getHeight()) * 0.42f;
}

void VocalCrownCompass::timerCallback()
{
    // While dragging we already repaint live in mouseDrag; otherwise track
    // automation changes from the DAW.
    repaint();
}

void VocalCrownCompass::paint (juce::Graphics& g)
{
    auto local = getLocalBounds().toFloat();
    const auto accent = juce::Colour (sattari::brand::productAccent ("royal-chain"));
    const auto cyan   = juce::Colour (0xff7dd3fc);

    // Backdrop: soft radial gradient from a deep indigo at the centre to almost
    // black at the corners (the Portal "into deep space" vibe).
    juce::ColourGradient bg (juce::Colour (0xff1a1b2c), centre.x, centre.y,
                              juce::Colour (0xff07080d), local.getRight(), local.getBottom(), true);
    g.setGradientFill (bg);
    g.fillRect (local);

    // Outer ring with a gold→cyan gradient stroke (suggesting the Portal hue
    // sweep while staying on the gold Royal Chain accent).
    juce::Path ring;
    ring.addEllipse (centre.x - ringRadius, centre.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f);
    juce::ColourGradient ringGrad (accent.withAlpha (0.85f), centre.x, centre.y - ringRadius,
                                    cyan.withAlpha (0.65f), centre.x, centre.y + ringRadius, false);
    g.setGradientFill (ringGrad);
    g.strokePath (ring, juce::PathStrokeType (1.5f));

    // Inner faint ring for depth.
    g.setColour (accent.withAlpha (0.15f));
    g.drawEllipse (centre.x - ringRadius * 0.6f, centre.y - ringRadius * 0.6f,
                   ringRadius * 1.2f, ringRadius * 1.2f, 1.0f);

    // Star-field rays from centre outward — each at a fixed angle, varying
    // length. Brighter near outer edge so the centre stays readable.
    for (int i = 0; i < kNumRays; ++i)
    {
        const float a = rayAngles[i];
        const float r1 = ringRadius * 0.10f * rayJitter[i];
        const float r2 = ringRadius * rayLengths[i];
        const juce::Point<float> p1 (centre.x + std::cos (a) * r1, centre.y + std::sin (a) * r1);
        const juce::Point<float> p2 (centre.x + std::cos (a) * r2, centre.y + std::sin (a) * r2);
        g.setColour (juce::Colours::white.withAlpha (0.06f + 0.10f * rayLengths[i]));
        g.drawLine (p1.x, p1.y, p2.x, p2.y, 0.9f);
        // Dot at the outer end.
        const float dotR = 1.4f + 1.2f * rayJitter[i];
        g.setColour (juce::Colours::white.withAlpha (0.35f + 0.45f * rayLengths[i]));
        g.fillEllipse (p2.x - dotR, p2.y - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    // Side labels (rotated, Portal-style).
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                      local.getX() + 18.0f, local.getCentreY()));
    g.setColour (accent.withAlpha (0.65f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("VOCAL CROWN COMPASS", (int) (local.getX() + 18.0f - 110.0f),
                (int) local.getCentreY() - 7, 220, 14, juce::Justification::centred);
    g.restoreState();

    g.setColour (cyan.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("ROYAL CHAIN", local.removeFromTop (20.0f).reduced (18.0f, 0.0f).toNearestInt(),
                juce::Justification::left, false);

    // Centre core: a small accent disc.
    const float coreR = 8.0f;
    juce::ColourGradient core (accent.withAlpha (0.9f), centre.x, centre.y,
                                accent.withAlpha (0.0f), centre.x + coreR * 2.5f, centre.y, true);
    g.setGradientFill (core);
    g.fillEllipse (centre.x - coreR * 2.5f, centre.y - coreR * 2.5f, coreR * 5.0f, coreR * 5.0f);

    // Five interactive nodes.
    for (int i = 0; i < kNumNodes; ++i)
    {
        const auto pos = nodePosition (i);
        const auto& n  = nodes[i];

        // Radial guide line from centre to the node.
        g.setColour (n.colour.withAlpha (0.35f));
        g.drawLine (centre.x, centre.y, pos.x, pos.y, 1.2f);

        // Glow halo.
        const float nodeR = 9.0f + (i == draggingNode ? 3.0f : 0.0f);
        g.setColour (n.colour.withAlpha (0.20f));
        g.fillEllipse (pos.x - nodeR * 2.2f, pos.y - nodeR * 2.2f, nodeR * 4.4f, nodeR * 4.4f);

        // Node disc.
        g.setColour (n.colour);
        g.fillEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawEllipse (pos.x - nodeR, pos.y - nodeR, nodeR * 2.0f, nodeR * 2.0f, 1.0f);

        // Snap feedback: when the macro is at exactly 0 or 1, draw a bright
        // pulse ring around the node so the user gets a visual cue that the
        // limit's been hit (otherwise dragging into the rail feels mushy).
        const float macro = getMacro (n.paramId);
        if (macro <= 0.001f || macro >= 0.999f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            const float pulseR = nodeR + 4.0f;
            g.drawEllipse (pos.x - pulseR, pos.y - pulseR, pulseR * 2.0f, pulseR * 2.0f, 2.0f);
        }

        // Label outside the node, offset away from the centre so it reads.
        const float labelDist = nodeR + 14.0f;
        const juce::Point<float> labelPos (pos.x + std::cos (n.angleRad) * labelDist,
                                            pos.y + std::sin (n.angleRad) * labelDist);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (n.label,
                    (int) labelPos.x - 36, (int) labelPos.y - 8, 72, 16,
                    juce::Justification::centred, false);

        // Tiny value chip below the label (0..100).
        const int pct = (int) std::round (getMacro (n.paramId) * 100.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (pct),
                    (int) labelPos.x - 36, (int) labelPos.y + 5, 72, 12,
                    juce::Justification::centred, false);
    }

    // Drag hint at the bottom.
    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("Drag nodes radially to set macros - centre = 0, outer ring = 100",
                local.removeFromBottom (18.0f).toNearestInt(), juce::Justification::centred, false);
}

void VocalCrownCompass::mouseDown (const juce::MouseEvent& e)
{
    draggingNode = -1;
    const juce::Point<float> p (e.position);
    constexpr float hit = 18.0f;
    for (int i = 0; i < kNumNodes; ++i)
        if (p.getDistanceFrom (nodePosition (i)) < hit)
        {
            draggingNode = i;
            break;
        }
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
