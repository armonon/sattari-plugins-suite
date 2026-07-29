#pragma once

// EnergyArc — the ScenePilot-style energy-arc arranger for StemDeck.
//
// Instead of a bare clip timeline, the arrangement is framed as an EMOTIONAL ARC:
// Restraint -> Pulse -> Build -> Burst -> Release. The "Auto Director" builds the arc
// from the track's structure analysis (intro/build/drop/breakdown/outro), and the
// user refines the energy of each section by dragging its node. This is the high-level
// intent surface; the full clip timeline lives underneath ("Auto Pro Arrangement").
//
// Header-only + drawing-first so it PNG-verifies standalone. Dark, high-contrast,
// bold — the ScenePilot look.

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace stemdeck
{

// The five ScenePilot arc states, low -> peak -> release.
enum class ArcState { restraint, pulse, build, burst, release };

inline juce::String arcStateName (ArcState s)
{
    switch (s) { case ArcState::restraint: return "Restraint"; case ArcState::pulse: return "Pulse";
                 case ArcState::build: return "Build"; case ArcState::burst: return "Burst";
                 default: return "Release"; }
}
inline float arcStateEnergy (ArcState s)
{
    switch (s) { case ArcState::restraint: return 0.20f; case ArcState::pulse: return 0.48f;
                 case ArcState::build: return 0.68f; case ArcState::burst: return 1.0f; default: return 0.30f; }
}
inline juce::Colour arcStateColour (ArcState s)
{
    switch (s) { case ArcState::restraint: return juce::Colour (0xff4A9EFF);   // blue
                 case ArcState::pulse:     return juce::Colour (0xff4AD9C4);    // teal
                 case ArcState::build:     return juce::Colour (0xffB47AFF);    // violet
                 case ArcState::burst:     return juce::Colour (0xffD4537E);    // rose (peak)
                 default:                  return juce::Colour (0xffFFB84A); }  // amber (release)
}

struct ArcSection
{
    float    startNorm = 0.0f, endNorm = 0.0f;   // 0..1 along the track
    ArcState state = ArcState::pulse;
    float    energy = 0.5f;                        // 0..1 (editable; seeded from state)
    juce::String label;                            // source structure label (INTRO/DROP/...)
};

// Map a StemDeck structure label to an arc state.
inline ArcState arcStateForStructure (const juce::String& structLabel)
{
    const auto l = structLabel.toUpperCase();
    if (l == "INTRO")     return ArcState::restraint;
    if (l == "BUILD")     return ArcState::build;
    if (l == "DROP")      return ArcState::burst;
    if (l == "BREAKDOWN") return ArcState::release;
    if (l == "OUTRO")     return ArcState::release;
    return ArcState::pulse;   // verse / generic groove
}

// Auto Director: build the arc from normalised structure regions (start,end,label).
inline std::vector<ArcSection> buildEnergyArc (const std::vector<std::tuple<float, float, juce::String>>& structure)
{
    std::vector<ArcSection> arc;
    for (const auto& [s, e, lab] : structure)
    {
        ArcSection a;
        a.startNorm = s; a.endNorm = e; a.label = lab;
        a.state = arcStateForStructure (lab);
        a.energy = arcStateEnergy (a.state);
        arc.push_back (a);
    }
    return arc;
}

// Draw the arc as a smooth filled energy curve with labelled state regions + playhead.
inline void drawEnergyArc (juce::Graphics& g, juce::Rectangle<float> area,
                           const std::vector<ArcSection>& arc, float playheadNorm,
                           int hoveredIndex = -1)
{
    g.setColour (juce::Colour (0xff0B0B0D));
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (juce::Colour (0xff2A2A30));
    g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto plot = area.reduced (10.0f, 8.0f);
    auto labels = plot.removeFromBottom (16.0f);
    plot.removeFromBottom (2.0f);

    auto xFor = [&] (float n) { return plot.getX() + juce::jlimit (0.0f, 1.0f, n) * plot.getWidth(); };
    auto yFor = [&] (float e) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, e) * plot.getHeight(); };

    // Faint energy gridlines (25/50/75/100%).
    g.setColour (juce::Colour (0xff1C1C20));
    for (float e : { 0.25f, 0.5f, 0.75f, 1.0f })
        g.drawHorizontalLine ((int) yFor (e), plot.getX(), plot.getRight());

    if (arc.empty())
    {
        g.setColour (juce::Colour (0xff5A5A62));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 (
                        "Auto Director \xe2\x80\x94 load a track to build the energy arc"),
                    area.toNearestInt(),
                    juce::Justification::centred);
        return;
    }

    // Node points at each section centre.
    std::vector<juce::Point<float>> pts;
    for (const auto& a : arc) pts.push_back ({ xFor ((a.startNorm + a.endNorm) * 0.5f), yFor (a.energy) });

    // Filled area under a smooth curve through the nodes.
    juce::Path fill;
    fill.startNewSubPath (plot.getX(), plot.getBottom());
    fill.lineTo (pts.front().x, pts.front().y);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        const auto a = pts[i - 1], b = pts[i];
        const float mx = (a.x + b.x) * 0.5f;
        fill.cubicTo (mx, a.y, mx, b.y, b.x, b.y);   // smooth S between nodes
    }
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4AD9C4).withAlpha (0.30f), plot.getX(), plot.getY(),
                                             juce::Colour (0xffD4537E).withAlpha (0.10f), plot.getX(), plot.getBottom(), false));
    g.fillPath (fill);

    // The curve line itself, coloured by each segment's peak state.
    for (size_t i = 1; i < pts.size(); ++i)
    {
        juce::Path seg;
        const auto a = pts[i - 1], b = pts[i];
        const float mx = (a.x + b.x) * 0.5f;
        seg.startNewSubPath (a.x, a.y);
        seg.cubicTo (mx, a.y, mx, b.y, b.x, b.y);
        g.setColour (arcStateColour (arc[i].energy >= arc[i - 1].energy ? arc[i].state : arc[i - 1].state).withAlpha (0.95f));
        g.strokePath (seg, juce::PathStrokeType (2.0f));
    }

    // Section dividers + state labels + editable nodes.
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (size_t i = 0; i < arc.size(); ++i)
    {
        const auto& a = arc[i];
        const float x0 = xFor (a.startNorm), x1 = xFor (a.endNorm);
        g.setColour (juce::Colour (0xff2A2A30));
        g.drawVerticalLine ((int) x0, plot.getY(), plot.getBottom());

        // State label chip under the plot.
        auto chip = juce::Rectangle<float> (x0, labels.getY(), juce::jmax (0.0f, x1 - x0), labels.getHeight()).reduced (2.0f, 1.0f);
        g.setColour (arcStateColour (a.state).withAlpha (0.16f));
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (arcStateColour (a.state));
        g.drawText (arcStateName (a.state), chip.toNearestInt(), juce::Justification::centred);

        // Node handle.
        const auto p = pts[i];
        const bool hov = (int) i == hoveredIndex;
        g.setColour (juce::Colour (0xff0B0B0D));
        g.fillEllipse (p.x - (hov ? 7.0f : 5.0f), p.y - (hov ? 7.0f : 5.0f), hov ? 14.0f : 10.0f, hov ? 14.0f : 10.0f);
        g.setColour (arcStateColour (a.state));
        g.fillEllipse (p.x - (hov ? 5.0f : 3.5f), p.y - (hov ? 5.0f : 3.5f), hov ? 10.0f : 7.0f, hov ? 10.0f : 7.0f);
    }

    // Playhead.
    g.setColour (juce::Colour (0xffF2F2F5).withAlpha (0.9f));
    g.fillRect (xFor (playheadNorm) - 0.5f, plot.getY(), 1.0f, plot.getHeight());
}

// Interactive arranger: Auto Director + draggable section energy nodes.
class EnergyArcArranger : public juce::Component
{
public:
    EnergyArcArranger() = default;
    void setArc (std::vector<ArcSection> a) { arc = std::move (a); repaint(); }
    const std::vector<ArcSection>& getArc() const { return arc; }
    void setPlayhead (float n) { playhead = juce::jlimit (0.0f, 1.0f, n); repaint(); }
    std::function<void()> onArcEdited;

    juce::Rectangle<float> plotArea() const
    {
        auto plot = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        plot.removeFromBottom (18.0f);
        return plot;
    }

    void paint (juce::Graphics& g) override { drawEnergyArc (g, getLocalBounds().toFloat(), arc, playhead, hovered); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = nodeAt (e.position);
        if (h != hovered) { hovered = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { dragIdx = nodeAt (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIdx < 0 || dragIdx >= (int) arc.size()) return;
        auto plot = plotArea();
        arc[(size_t) dragIdx].energy = juce::jlimit (0.0f, 1.0f, (float) (plot.getBottom() - e.position.y) / plot.getHeight());
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragIdx >= 0 && onArcEdited) onArcEdited(); dragIdx = -1; }

private:
    int nodeAt (juce::Point<float> p) const
    {
        auto plot = plotArea();
        for (size_t i = 0; i < arc.size(); ++i)
        {
            const float x = plot.getX() + (arc[i].startNorm + arc[i].endNorm) * 0.5f * plot.getWidth();
            const float y = plot.getBottom() - arc[i].energy * plot.getHeight();
            if (p.getDistanceFrom ({ x, y }) < 12.0f) return (int) i;
        }
        return -1;
    }

    std::vector<ArcSection> arc;
    float playhead = 0.0f;
    int hovered = -1, dragIdx = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnergyArcArranger)
};

} // namespace stemdeck
