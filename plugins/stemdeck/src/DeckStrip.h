#pragma once

// DeckStrip — the compact per-deck control strip that sits under a deck's waveform.
//
// Two rows. Row 1 (40px) is always visible: accent bar, deck numeral, play/pause,
// CUE, loop cluster (halve / length / double), beat jump, sync-with-phase, key,
// take marker, expand chevron. Row 2 (32px, +4px divider) is an expandable drawer:
// stem toggles, quantize, slip, FX sends.
//
// Three visual tiers (primary / secondary / tertiary) drive size + contrast so the
// control you hit hundreds of times a session (play) reads bigger and brighter than
// the mode toggles you set once. See the spec in the design doc.
//
// The component is self-contained and PNG-verifiable: push a State each UI tick and
// wire the Callbacks to the processor. No JUCE-header / processor dependency, so it
// renders offline for verification.

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>

namespace stemdeck
{

class DeckStrip : public juce::Component,
                  private juce::Timer
{
public:
    explicit DeckStrip (int deckIndex);

    static constexpr int kCollapsedHeight = 40;
    static constexpr int kDividerHeight   = 4;
    static constexpr int kDrawerHeight     = 32;
    static constexpr int kExpandedHeight   = kCollapsedHeight + kDividerHeight + kDrawerHeight; // 76

    // Deck identity colour — same hue used for the waveform outline + Arrange lane.
    // Distinguishable under protanopia/deuteranopia (no red/green pairing).
    static juce::Colour accentFor (int deck);

    // ---- Live state pushed from the editor each tick -----------------------------
    struct State
    {
        bool hasTrack   = false;   // no track → primary controls dim to "disabled"
        bool playing    = false;
        bool cueSet     = false;
        bool cueHeld    = false;   // playing-from-cue (button held)
        bool loopArmed  = false;   // loop set but not currently looping through
        bool loopActive = false;   // looping right now
        double loopBeats = 4.0;    // current loop length in beats (1/8 .. 32)
        int    beatJumpBars = 4;   // 1 / 2 / 4 / 8 / 16
        bool   syncOn    = false;
        bool   syncLocked = false; // on + phase-locked to the downbeat
        float  phaseOffset = 0.0f; // signed fraction of a beat off the downbeat (-1..1)
        juce::String keyCamelot = "--";
        int    keySemis   = 0;     // current transposition, semitones
        bool   keyLowConf = false; // low-confidence detection → render italic/faded
        bool   slipOn     = false;
        int    quantIndex = 3;     // 0 off, 1 bar, 2 1/2, 3 1/4, 4 1/8
        std::array<int, 4> stem { { 0, 0, 0, 0 } };  // per stem: 0 active, 1 muted, 2 solo
        float  sepProgress = 1.0f; // < 1 → separation still running (speculative render)
        std::array<bool, 3> fx { { false, false, false } };
        bool   focused = false;    // keyboard-focused deck (full contrast + top border)
    };
    void setState (const State& s);
    const State& getState() const { return state; }

    // ---- Interaction callbacks ---------------------------------------------------
    struct Callbacks
    {
        std::function<void()>        onTogglePlay;
        std::function<void(bool)>    onCue;         // true = press (jump+play), false = release
        std::function<void()>        onLoopHalve;
        std::function<void()>        onLoopDouble;
        std::function<void()>        onLoopToggle;  // arm/disarm the loop
        std::function<void(int)>     onBeatJump;    // -1 back / +1 forward (by current bar count)
        std::function<void()>        onCycleBeatJump;
        std::function<void()>        onToggleSync;
        std::function<void()>        onOpenKey;     // click the key readout
        std::function<void(int)>     onNudgeKey;    // scroll = ± semitones
        std::function<void()>        onMarkTake;
        std::function<void(int)>     onToggleStem;  // 0..3 mute/unmute
        std::function<void(int)>     onSoloStem;    // 0..3 alt-click solo
        std::function<void()>        onCycleQuantize;
        std::function<void()>        onToggleSlip;
        std::function<void()>        onOpenBeatGrid;
        std::function<void(int)>     onToggleFx;    // 0..2
        std::function<void()>        onRequestFocus;
    };
    Callbacks callbacks;

    // ---- Drawer ------------------------------------------------------------------
    void setExpanded (bool shouldExpand, bool animate = true);
    bool isExpanded() const { return expanded; }
    // Current animated height (collapsed..expanded) — the parent uses this to grow
    // the strip's bounds while the drawer opens.
    int  currentHeight() const;
    std::function<void()> onExpandChanged;   // ask the parent to re-run its layout

    // juce::Component
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    // Control regions (hit-tested). Filled by resized().
    enum class Hit
    {
        none, play, cue, loopHalve, loopNum, loopDouble,
        jumpBack, jumpNum, jumpFwd, sync, key, take, chevron,
        stem0, stem1, stem2, stem3, quant, slip, grid, fxA, fxB, fxC
    };
    Hit hitTest (juce::Point<float>) const;
    juce::Rectangle<float> regionFor (Hit) const;

    void layoutRow1 (juce::Rectangle<float> row);
    void layoutRow2 (juce::Rectangle<float> row);

    void paintRow1 (juce::Graphics&);
    void paintRow2 (juce::Graphics&, float alpha);

    // small drawing helpers
    void paintPill (juce::Graphics&, juce::Rectangle<float>, bool active, bool hover, juce::Colour accent) const;
    void paintPlayGlyph (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
    void paintPauseGlyph (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
    void paintLoopGlyph (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
    void paintPhaseRing (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;
    void paintChevron (juce::Graphics&, juce::Rectangle<float>, float openAmount, juce::Colour) const;

    const int deck;
    juce::Colour accent;
    State state;

    bool  expanded = false;
    bool  compactMode = false; // narrow (per-column) width → icon-only SYNC/TAKE
    float drawerAmt = 0.0f;    // 0 collapsed .. 1 fully open (eased)
    float phaseSpin = 0.0f;    // continuous ring rotation while syncing
    double lastTickMs = 0.0;

    Hit hovered = Hit::none;
    Hit pressed = Hit::none;

    std::array<juce::Rectangle<float>, 23> regions {};   // indexed by (int) Hit

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckStrip)
};

} // namespace stemdeck
