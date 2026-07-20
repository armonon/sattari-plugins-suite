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
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>
#include <vector>
#include "SessionModel.h"
#include "StructureAnalysis.h"

class ArrangementView : public juce::Component
{
public:
    ArrangementView() = default;

    void setSession (stemdeck::session::SessionModel s) { session = std::move (s); repaint(); }
    void setPlayheadBeat (double b) { playheadBeat = b; repaint(); }

    // Optional: supply source audio per trackId so clips draw their waveform.
    using AudioProvider = std::function<const juce::AudioBuffer<float>* (const juce::String& trackId, double& srcRateOut)>;
    void setAudioProvider (AudioProvider p) { audioProvider = std::move (p); repaint(); }

    // Optional: per-track separated stems (drums/bass/music/vocals) -> stem-coloured
    // waveforms. Any null entry is skipped. Takes precedence over the mono provider.
    using StemProvider = std::function<std::array<const juce::AudioBuffer<float>*, 4> (const juce::String& trackId, double& srcRateOut)>;
    void setStemProvider (StemProvider p) { stemProvider = std::move (p); repaint(); }

    // Optional: structure sections per track for the intro/build/drop ribbon.
    using StructureProvider = std::function<std::vector<stemdeck::Section> (const juce::String& trackId)>;
    void setStructureProvider (StructureProvider p) { structureProvider = std::move (p); repaint(); }

    void paint (juce::Graphics& g) override;

private:
    stemdeck::session::SessionModel session;
    AudioProvider audioProvider;
    StemProvider stemProvider;
    StructureProvider structureProvider;
    double playheadBeat = -1.0;

    // layout
    static constexpr int kHeaderH = 28;
    static constexpr int kLaneGap = 6;
    double totalBeats() const;
    float  beatToX (double beat, juce::Rectangle<int> area) const;
    static juce::Colour deckColour (int deck);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangementView)
};
