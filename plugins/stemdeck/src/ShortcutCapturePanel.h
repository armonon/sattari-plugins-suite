#pragma once

#include <JuceHeader.h>
#include "DesignTokens.h"

namespace stemdeck
{
class ShortcutCapturePanel final : public juce::Component
{
public:
    explicit ShortcutCapturePanel (juce::String actionName)
        : action (std::move (actionName))
    {
        setWantsKeyboardFocus (true);
        setFocusContainerType (juce::Component::FocusContainerType::keyboardFocusContainer);
        setSize (420, 170);

        clear.setButtonText ("Remove Shortcut");
        clear.onClick = [this] { if (onClear) onClear(); };
        addAndMakeVisible (clear);
    }

    std::function<void(juce::KeyPress)> onCapture;
    std::function<void()> onClear;
    std::function<void()> onCancel;

    void visibilityChanged() override
    {
        if (isVisible())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<ShortcutCapturePanel> (this)]
                {
                    if (safe != nullptr) safe->grabKeyboardFocus();
                });
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::escapeKey)
        {
            if (onCancel) onCancel();
            return true;
        }
        if (onCapture) onCapture (key);
        return true;
    }

    void resized() override
    {
        clear.setBounds (getLocalBounds().reduced (20).removeFromBottom (34).removeFromRight (150));
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (bounds, 14.0f);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);

        auto text = getLocalBounds().reduced (20);
        g.setColour (tokens::c (tokens::fgPrimary));
        g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        g.drawText (action, text.removeFromTop (30), juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgSecondary));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Press the new key combination", text.removeFromTop (30),
                    juce::Justification::centredLeft);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Esc cancels. System shortcuts such as Command-Q stay protected.",
                    text.removeFromTop (24), juce::Justification::centredLeft);
    }

private:
    juce::String action;
    juce::TextButton clear;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShortcutCapturePanel)
};
} // namespace stemdeck
