#pragma once

// =============================================================================
//  ArrangementView — draws a captured SessionModel as a DAW-style timeline
//  (build-order #5, the first surface that renders the event stream).
// -----------------------------------------------------------------------------
//  Per-deck lanes with track clips, the fader automation curve, transition
//  regions overlaid, a beat/bar grid, and a playhead. It reads from SessionModel
//  + SessionRender::deckStateAt, so it is a pure rendering of the intent — the
//  product's core idea made visible. Reusable: drop into the ARRANGER view later;
//  also renders to a PNG offline for headless inspection.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "SessionModel.h"

class ArrangementView : public juce::Component
{
public:
    ArrangementView() = default;

    void setSession (stemdeck::session::SessionModel s) { session = std::move (s); repaint(); }
    void setPlayheadBeat (double b) { playheadBeat = b; repaint(); }

    void paint (juce::Graphics& g) override;

private:
    stemdeck::session::SessionModel session;
    double playheadBeat = -1.0;

    // layout
    static constexpr int kHeaderH = 28;
    static constexpr int kLaneGap = 6;
    double totalBeats() const;
    float  beatToX (double beat, juce::Rectangle<int> area) const;
    static juce::Colour deckColour (int deck);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
