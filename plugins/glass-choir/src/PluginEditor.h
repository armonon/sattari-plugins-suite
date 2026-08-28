#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariLookAndFeel.h>
#include <sattari_audio_core/SattariKeyButton.h>
#include "PluginProcessor.h"

#include <array>
#include <memory>
#include <vector>

// =============================================================================
//  Sattari Glass Choir — editor
// -----------------------------------------------------------------------------
//  A clean, sectioned panel in the shared SattariLookAndFeel. The hero is the
//  big CHARACTER macro (HUMAN -> MASSIVE -> DESTROYED); around it sit the mode
//  selector and the six functional sections:
//     ENSEMBLE / HARMONY (+ shared KeyButton) / DESTROY / REVERB + DUCK /
//     WIDTH / OUTPUT.
//  Every APVTS parameter has a control home and is bound via an attachment.
//
//  RT-safe: the editor only reads processor viz atomics (never touches the DSP
//  state); all binding is through APVTS attachments. A 24 Hz timer refreshes the
//  KeyButton state, the character read-out, and the three bus meters.
// =============================================================================
class SattariGlassChoirAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                    private juce::Timer
{
public:
    explicit SattariGlassChoirAudioProcessorEditor (SattariGlassChoirAudioProcessor&);
    ~SattariGlassChoirAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    static constexpr int kNumHarmony = SattariGlassChoirAudioProcessor::kNumHarmony;

    void timerCallback() override;
    void setKeyParam (const char* id, float value);

    // --- Small owning wrappers so the ctor stays declarative ------------------
    struct Knob
    {
        juce::Slider slider;
        juce::Label  caption;
        std::unique_ptr<SliderAttachment> att;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> att;
    };
    struct Choice
    {
        juce::ComboBox box;
        juce::Label    caption;
        std::unique_ptr<ComboAttachment> att;
    };

    // A section is a titled panel with its own child-control rectangle list.
    struct Section
    {
        juce::String name;
        juce::Rectangle<int> bounds;      // whole panel (incl. header)
        juce::Rectangle<int> body;        // usable interior
    };

    void addKnob   (const char* paramId, const juce::String& caption, const juce::String& suffix, Knob& out);
    void addToggle (const char* paramId, const juce::String& text, Toggle& out);
    void addChoice (const char* paramId, const juce::String& caption, const juce::StringArray& items, Choice& out);

    // Layout helpers: pack knobs / a knob row inside a section body.
    static void layoutKnobRow (juce::Rectangle<int> row, std::initializer_list<Knob*> ks);

    SattariGlassChoirAudioProcessor& processor;
    sattari::ui::SattariLookAndFeel lookAndFeel { sattari::brand::productAccent ("glass-choir") };

    // --- Header ---------------------------------------------------------------
    juce::Label  title, subtitle;
    Knob         inputK, outputK;
    Toggle       bypassT;

    // --- Hero macro + mode ----------------------------------------------------
    Knob         characterK;
    juce::Label  characterAnchors;        // "HUMAN — MASSIVE — DESTROYED"
    Choice       modeC;

    // --- ENSEMBLE -------------------------------------------------------------
    Choice       voiceCountC, engineC;
    Knob         detuneK, timingK, humanizeK, formantK, ensembleLevelK;

    // --- HARMONY (+ KeyButton + align) ---------------------------------------
    Toggle       harmonyT, alignT;
    sattari::ui::KeyButton keyButton;
    Knob         harmonyMixK, alignAmountK, alignSearchK, pitchTightenK;
    struct HarmonyStrip
    {
        Toggle on;
        Choice interval;
        Knob   level;
    };
    std::array<HarmonyStrip, kNumHarmony> hvoices;

    // --- DESTROY --------------------------------------------------------------
    Toggle       destroyT;
    Knob         destroyMixK, driveK, crushK, vowelK, ringK;

    // --- REVERB + DUCK --------------------------------------------------------
    Knob         reverbMixK, reverbSizeK, reverbDampK, duckAmountK, duckAttackK, duckReleaseK;

    // --- WIDTH ----------------------------------------------------------------
    Toggle       lowMonoT, monoAuditionT;
    Knob         widthK, lowMonoFreqK;

    // --- TRAILS ---------------------------------------------------------------
    Toggle       trailsT;
    Knob         trailsMixK, trailReverseK, trailSizeK;

    // --- OUTPUT trims ---------------------------------------------------------
    Knob         leadLevelK, choirLevelK, atmosLevelK;

    // --- Live viz -------------------------------------------------------------
    juce::Label  leadMeter, choirMeter, atmosMeter;

    // Section frames (computed in resized(), drawn in paint()).
    std::vector<Section> sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariGlassChoirAudioProcessorEditor)
};
