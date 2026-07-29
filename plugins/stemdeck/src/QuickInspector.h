#pragma once

#include <JuceHeader.h>
#include "DesignTokens.h"

namespace stemdeck
{
class QuickInspector final : public juce::Component
{
public:
    struct State
    {
        juce::String deckName, trackName, source, tempo, musicalKey, analysis;
        juce::String audioStatus, storageStatus, midiStatus, captureStatus;
        bool loaded = false;
        bool playing = false;
        bool focusMode = false;
    };

    QuickInspector()
    {
        setWantsKeyboardFocus (true);
        setSize (340, 450);
        configure (focus, "Focus");
        configure (load, "Choose Track");
        configure (split, "Create Stems");
        configure (sync, "Sync Decks");
        configure (clear, "Clear");
        focus.onClick = [this] { if (onToggleFocus) onToggleFocus(); };
        load.onClick  = [this] { if (onLoad) onLoad(); };
        split.onClick = [this] { if (onSplit) onSplit(); };
        sync.onClick  = [this] { if (onSync) onSync(); };
        clear.onClick = [this] { if (onClear) onClear(); };
        clear.setColour (juce::TextButton::textColourOffId,
                         juce::Colour (0xffff6961));
    }

    std::function<void()> onToggleFocus, onLoad, onSplit, onSync, onClear;

    void visibilityChanged() override
    {
        if (isVisible())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<QuickInspector> (this)]
                {
                    if (safe != nullptr)
                        safe->grabKeyboardFocus();
                });
    }

    void setState (State next)
    {
        state = std::move (next);
        focus.setButtonText (state.focusMode ? "Exit Focus" : "Focus");
        split.setEnabled (state.loaded);
        clear.setEnabled (state.loaded);
        repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        area.removeFromTop (280);
        auto first = area.removeFromTop (38);
        focus.setBounds (first.removeFromLeft ((first.getWidth() - 6) / 2));
        first.removeFromLeft (6);
        load.setBounds (first);
        area.removeFromTop (6);
        auto second = area.removeFromTop (38);
        split.setBounds (second.removeFromLeft ((second.getWidth() - 6) / 2));
        second.removeFromLeft (6);
        sync.setBounds (second);
        area.removeFromTop (6);
        clear.setBounds (area.removeFromTop (36));
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (bounds, 14.0f);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);

        auto area = getLocalBounds().reduced (18);
        g.setColour (tokens::deckAccent (deckIndex()));
        g.setFont (juce::FontOptions (tokens::tSmall, juce::Font::bold));
        g.drawText (state.deckName.toUpperCase(), area.removeFromTop (18),
                    juce::Justification::centredLeft);

        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawFittedText (state.trackName.isNotEmpty() ? state.trackName : "No track loaded",
                          area.removeFromTop (30), juce::Justification::centredLeft, 1);

        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (tokens::tSmall));
        g.drawFittedText (state.source, area.removeFromTop (20),
                          juce::Justification::centredLeft, 1);
        area.removeFromTop (8);

        drawMetric (g, area.removeFromTop (30), "TEMPO", state.tempo);
        drawMetric (g, area.removeFromTop (30), "KEY", state.musicalKey);
        drawMetric (g, area.removeFromTop (30), "ANALYSIS", state.analysis);
        area.removeFromTop (6);

        drawHealth (g, area.removeFromTop (24), state.audioStatus);
        drawHealth (g, area.removeFromTop (24), state.storageStatus);
        drawHealth (g, area.removeFromTop (24), state.midiStatus);
        drawHealth (g, area.removeFromTop (24), state.captureStatus);
    }

private:
    void configure (juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText (text);
        button.setTitle (text);
        button.setDescription ("Quick inspector action: " + text);
        button.setColour (juce::TextButton::buttonColourId, tokens::c (tokens::bgControl));
        button.setColour (juce::TextButton::textColourOffId, tokens::c (tokens::fgPrimary));
        addAndMakeVisible (button);
    }

    int deckIndex() const noexcept
    {
        const auto last = state.deckName.getLastCharacter();
        return last >= 'A' && last <= 'D' ? (int) (last - 'A') : 0;
    }

    static void drawMetric (juce::Graphics& g, juce::Rectangle<int> row,
                            const juce::String& label, const juce::String& value)
    {
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (tokens::tLabel, juce::Font::bold));
        g.drawText (label, row.removeFromLeft (96), juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgSecondary));
        g.setFont (tokens::numeric (tokens::tControl, true));
        g.drawText (value, row, juce::Justification::centredRight);
    }

    static void drawHealth (juce::Graphics& g, juce::Rectangle<int> row,
                            const juce::String& text)
    {
        const bool warning = text.containsIgnoreCase ("warning")
                          || text.containsIgnoreCase ("missing");
        g.setColour (warning ? tokens::c (tokens::warn)
                             : tokens::c (tokens::deckA));
        g.fillEllipse ((float) row.getX(), (float) row.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (tokens::c (tokens::fgTertiary));
        g.setFont (juce::FontOptions (tokens::tSmall));
        g.drawText (text, row.withTrimmedLeft (14), juce::Justification::centredLeft);
    }

    State state;
    juce::TextButton focus, load, split, sync, clear;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuickInspector)
};
} // namespace stemdeck
