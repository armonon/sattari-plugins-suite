#pragma once

#include <JuceHeader.h>
#include <array>

class RoyalChainAudioProcessor;

// =============================================================================
//  Vocal Crown Compass  —  "Portal × Cymatics"
// -----------------------------------------------------------------------------
//  An interactive, audio-reactive map of the Royal Chain. Visually it is a
//  deep-space *portal* (radial tunnel receding into the centre, swirling dust)
//  crossed with a *cymatic* standing-wave field (concentric Bessel-style ripple
//  rings + rotating Chladni nodal spokes that pulse with the output level).
//
//  Functionally it is unchanged: five module nodes sit at fixed angles around
//  the centre; each node's radial distance equals its macro value (0 at centre,
//  1 at the outer ring). Drag a node radially to set its macro. The five macros
//  also draw a translucent "membrane" polygon so the chain's shape reads at a
//  glance. Everything is deterministic per-frame so it never flickers.
// =============================================================================
class VocalCrownCompass final : public juce::Component, private juce::Timer
{
public:
    explicit VocalCrownCompass (RoyalChainAudioProcessor& processorRef);
    ~VocalCrownCompass() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void timerCallback() override;

private:
    struct Node
    {
        juce::String label;
        juce::String paramId;
        juce::Colour colour;
        float angleRad;       // fixed angle around the ring
    };

    static constexpr int kNumNodes     = 5;
    static constexpr int kNumParticles = 130;   // swirling portal dust
    static constexpr int kNumRipples   = 30;    // concentric cymatic rings
    static constexpr int kNodalSpokes  = 5;     // Chladni angular nodal lines

    Node nodes[kNumNodes];

    // Portal dust: each particle spirals slowly inward toward the centre and is
    // recycled to the outer edge, giving the "falling into the portal" parallax.
    float partAngle  [kNumParticles] {};
    float partRadius [kNumParticles] {};   // 0..~1.15 of ringRadius
    float partSpin   [kNumParticles] {};   // angular drift per tick
    float partSpeed  [kNumParticles] {};   // inward drift factor
    float partSize   [kNumParticles] {};

    int   draggingNode = -1;
    int   hoveredNode  = -1;
    juce::Point<float> centre;
    float ringRadius = 1.0f;

    // Animation / audio-reactive state (advanced in timerCallback).
    float phase = 0.0f;     // master phase: ripple travel + slow vortex spin
    float level = 0.0f;     // smoothed output level, 0..1

    RoyalChainAudioProcessor& processor;

    float getMacro (const juce::String& id) const;
    void  setMacro (const juce::String& id, float value);
    juce::Point<float> nodePosition (int idx) const;
    int   hitTestNode (juce::Point<float> p) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCrownCompass)
};
