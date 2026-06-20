#pragma once

#include <JuceHeader.h>
#include <array>

class RoyalChainAudioProcessor;

// Portal-inspired interactive map of the Royal Chain. Each of the five module
// nodes sits at a fixed angle around the centre; its radial distance equals the
// corresponding macro value (0 at centre, 1 at the outer ring). The user drags
// a node radially to set its macro. The background is a radial star-field of
// random rays (deterministic per editor lifetime so it doesn't flicker).
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
    void timerCallback() override;

private:
    struct Node
    {
        juce::String label;
        juce::String paramId;
        juce::Colour colour;
        float angleRad;       // fixed angle around the ring
    };

    static constexpr int kNumNodes = 5;
    static constexpr int kNumRays  = 110;

    Node      nodes[kNumNodes];
    float     rayAngles[kNumRays] {};
    float     rayLengths[kNumRays] {};
    float     rayJitter[kNumRays] {};

    int       draggingNode = -1;
    juce::Point<float> centre;
    float ringRadius = 1.0f;

    RoyalChainAudioProcessor& processor;

    float getMacro (const juce::String& id) const;
    void  setMacro (const juce::String& id, float value);
    juce::Point<float> nodePosition (int idx) const;
};
