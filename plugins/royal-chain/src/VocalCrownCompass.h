#pragma once

// =============================================================================
//  Vocal Crown Compass — the "portal plate"
// -----------------------------------------------------------------------------
//  A vibrating circular plate seen through a dark Portal aperture. The sung
//  note, read as a scale degree of the effective key, selects a Bessel mode
//  pair (the tonic is a 5-fold crown whose lobes point at the five pucks), and
//  ~6k grains of sand are Newton-projected onto its nodal lines with short
//  additive trails. Warp streaks flicker along the figure's nodal diameters and
//  outward from a vanishing point that leans toward high-valued nodes; a hit
//  shatters the figure; silence is a calm inward vortex drawing the tonic.
//
//  It is still a macro controller: five pucks on a regular pentagon (clockwise
//  in signal-chain order) sit at radius R * (0.10 + 0.84 v). Mouse, wheel and
//  keyboard all go through one gesture path (begin / set / end).
//
//  Cost model: physics + light are computed into a low-res premultiplied image
//  (CymaticPlate + PortalRenderer, no Graphics calls); paint() is one cached
//  native stage blit, one 1:1 field blit and a handful of vectors. Frames are
//  paced by the display's VBlank (60 Hz, 30 Hz when calm) and only the ring box
//  is repainted. Nothing in advanceFrame() allocates.
// =============================================================================

#include <JuceHeader.h>
#include "CymaticPlate.h"
#include "PortalRenderer.h"
#include "WaterDish.h"
#include "CompassFeed.h"
#include "RoyalChainTheme.h"

#include <array>
#include <functional>
#include <vector>

class RoyalChainAudioProcessor;

class VocalCrownCompass final : public juce::Component,
                                public juce::TooltipClient
{
public:
    explicit VocalCrownCompass (RoyalChainAudioProcessor&);
    ~VocalCrownCompass() override;                 // ends any open gesture

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    juce::String getTooltip() override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::function<void (rc::theme::MacroId)> onFocusMacro;
    void setHighlightedNode (int nodeOrMinus1);

    // Shared gesture path (mouse / wheel / keys / tests).
    bool beginNodeDrag (int node);
    void dragNodeToValue (int node, float v);
    void endNodeDrag (int node);
    juce::Point<float> nodePosition (int node) const;

    // Deterministic driving + hooks.
    void advanceFrames (int n, double dt = 1.0 / 60.0);   // no repaint
    void prepareForScale (float physicalScale);
    void setHoverForTesting (int node);
    const rc::CymaticPlate& plateForTesting() const noexcept     { return plate; }
    const rc::PortalRenderer& rendererForTesting() const noexcept { return renderer; }
    const rc::WaterDish& dishForTesting() const noexcept           { return dish; }
    const rc::CompassState& stateForTesting() const noexcept     { return conditioner.state(); }

    struct Perf
    {
        double simMs, renderMs, paintMs; int ladder; bool calm;
        double paintStageMs, paintFieldMs, paintVectorMs, paintTextMs;   // last paint, by section
    };
    Perf perf() const noexcept;
    void appendCaptionChecks (std::vector<rc::theme::CaptionCheck>&) const;

    // Geometry (compass-local, logical px).
    static constexpr int kWidth = 744, kHeight = 508;
    static constexpr float kCx = 372.0f, kCy = 262.0f, kR = 200.0f;

private:
    struct TextLayer
    {
        juce::Rectangle<int> rect;          // logical, compass-local
        juce::Image image;                  // native, physical scale
        bool needsRebuild = true;
    };

    struct LabelLayout
    {
        juce::Rectangle<int> box;           // the resolved label box
        juce::Rectangle<float> name, value, pill;
        juce::Justification valueJust { juce::Justification::centredLeft };
    };

    struct ReadoutKey
    {
        bool idle = false, voiced = false, rich = false;
        int note = -2, cents = 0, lock = 0, degree = -1, style = -1, key = -1, scale = -1;
        int modeN = -1, modeS = -1, modeHz = 0;   // the water-dish mode actually ringing
        bool operator== (const ReadoutKey& o) const noexcept
        {
            return idle == o.idle && voiced == o.voiced && rich == o.rich && note == o.note && cents == o.cents
                && lock == o.lock && degree == o.degree && style == o.style && key == o.key && scale == o.scale
                && modeN == o.modeN && modeS == o.modeS && modeHz == o.modeHz;
        }
        bool operator!= (const ReadoutKey& o) const noexcept { return ! (*this == o); }
    };

    void tick (double timestampSec);
    void advanceFrame (float dt, bool render);
    rc::CompassFeed readFeed() const;
    void prepareInternal (float physicalScale, bool rewarm);
    void rebuildStageCache();
    void rebuildArcPath (int node, float value);
    void renderLabelLayer (int node);
    void renderPitchLayer();
    void renderFigureLayer();
    juce::Image& prepareLayerImage (TextLayer&);
    void drawLayer (juce::Graphics&, const TextLayer&) const;
    int hitTestNode (juce::Point<float>) const;
    int hitTestSpoke (juce::Point<float>, float& valueOut) const;
    float projectToValue (int node, juce::Point<float>) const;
    void setNodeValueGesture (int node, float v);
    void resetNodeToDefault (int node);
    void setHoverNode (int node);
    void focusNode (int node);
    void applyLadder();
    juce::Rectangle<int> labelRepaintRect (int node) const;

    RoyalChainAudioProcessor& processor;

    // Cached parameter pointers (read with relaxed loads every tick).
    std::atomic<float>* pStyle = nullptr;
    std::atomic<float>* pMacro[10] {};    // clean,tune,control,tone,air,smooth,glow,width,space,royal
    std::atomic<float>* pNode[rc::theme::kNumNodes] {};
    juce::RangedAudioParameter* nodeParam[rc::theme::kNumNodes] {};

    rc::CymaticPlate plate;
    rc::PortalRenderer renderer;
    rc::WaterDish dish;                 // the aperture: the Cymatic Generator's water dish
    std::vector<float> tapOutMid, tapOutSide, tapInMid;   // latest samples for the dish FFTs
    rc::FeedConditioner conditioner;
    rc::Rng rng;
    rc::CompassFeed lastFeed;

    // Node state.
    std::array<juce::Point<float>, rc::theme::kNumNodes> dir {};
    std::array<float, rc::theme::kNumNodes> value {}, hoverAmt {}, dragAmt {}, arcValue {}, arcPathValue {};
    std::array<int, rc::theme::kNumNodes> shownPct {};
    std::array<juce::Path, rc::theme::kNumNodes> arcPath;
    juce::Path membrane;
    int hoverNode = -1, highlightNode = -1, focusedNode = 0, dragNode = -1, gestureNode = -1;
    bool pillShown[rc::theme::kNumNodes] {};
    float dragV0 = 0.0f, dragProj0 = 0.0f;
    bool dragFine = false;
    juce::Point<float> vp, vpVel;

    // Caches.
    juce::Image stageCache;
    float preparedScale = 0.0f, pendingScale = 0.0f;
    std::array<LabelLayout, rc::theme::kNumNodes> labels;
    std::array<TextLayer, rc::theme::kNumNodes> labelLayers;
    TextLayer pitchLayer, figureLayer;
    ReadoutKey readout, readoutShown;
    int labelRepaintMask = 0;
    bool pitchRepaint = false, figureRepaint = false;
    int readoutThrottle = 0;

    // Pacing + budget.
    double lastTickTs = 0.0;
    double simMs = 0.0, renderMs = 0.0, paintMs = 0.0;
    double paintSection[4] {};
    float frameCost[60] {};
    int frameCostIndex = 0, framesSinceLadder = 0, ladder = 0;
    bool calm = false;

    static const juce::Rectangle<int> ringBox, fieldRect;

    juce::VBlankAttachment vblank;   // declared LAST: destroyed first

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCrownCompass)
};
