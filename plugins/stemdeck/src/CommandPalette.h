#pragma once

#include <JuceHeader.h>
#include "DesignTokens.h"

namespace stemdeck
{
class CommandPalette final : public juce::Component,
                             private juce::KeyListener
{
public:
    using juce::Component::keyPressed;

    struct Command
    {
        int id = 0;
        juce::String title, detail, shortcut;
    };

    explicit CommandPalette (std::vector<Command> items)
        : commands (std::move (items))
    {
        setWantsKeyboardFocus (true);
        setSize (560, 430);

        search.setTextToShowWhenEmpty ("Search actions, views, and tools",
                                       tokens::c (tokens::fgMuted));
        search.setTitle ("Command search");
        search.setDescription ("Filter every StemDeck action and workspace");
        search.setFont (juce::FontOptions (15.0f));
        search.setColour (juce::TextEditor::backgroundColourId, tokens::c (tokens::bgWell));
        search.setColour (juce::TextEditor::outlineColourId, tokens::c (tokens::borderSubtle));
        search.setColour (juce::TextEditor::focusedOutlineColourId,
                          tokens::c (tokens::deckA).withAlpha (0.65f));
        search.setColour (juce::TextEditor::textColourId, tokens::c (tokens::fgPrimary));
        search.onTextChange = [this] { rebuildResults(); };
        search.onReturnKey = [this] { invokeSelected(); };
        search.onEscapeKey = [this] { if (onDismiss) onDismiss(); };
        search.addKeyListener (this);
        addAndMakeVisible (search);
        rebuildResults();
    }

    std::function<void(int)> onInvoke;
    std::function<void()> onDismiss;

    void visibilityChanged() override
    {
        if (isVisible())
            juce::MessageManager::callAsync (
                [safe = juce::Component::SafePointer<CommandPalette> (this)]
                {
                    if (safe != nullptr)
                        safe->search.grabKeyboardFocus();
                });
    }

    void resized() override
    {
        search.setBounds (getLocalBounds().reduced (18).removeFromTop (44));
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (bounds, 14.0f);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);

        auto list = getLocalBounds().reduced (18);
        list.removeFromTop (58);
        g.setColour (tokens::c (tokens::fgMuted));
        g.setFont (juce::FontOptions (tokens::tLabel, juce::Font::bold));
        g.drawText ("COMMANDS", list.removeFromTop (18), juce::Justification::centredLeft);

        constexpr int rowHeight = 48;
        for (int visibleRow = 0; visibleRow < kVisibleRows; ++visibleRow)
        {
            const int resultRow = firstVisibleRow + visibleRow;
            if (! juce::isPositiveAndBelow (resultRow, (int) filtered.size()))
                break;
            auto row = list.removeFromTop (rowHeight);
            if (row.isEmpty())
                break;

            const bool selected = resultRow == selectedRow;
            if (selected)
            {
                g.setColour (tokens::activeFill (tokens::c (tokens::deckA)));
                g.fillRoundedRectangle (row.toFloat(), tokens::radiusControl);
            }

            const auto& command = commands[(size_t) filtered[(size_t) resultRow]];
            auto text = row.reduced (12, 4);
            const auto shortcut = text.removeFromRight (88);
            g.setColour (tokens::c (selected ? tokens::fgPrimary : tokens::fgSecondary));
            g.setFont (juce::FontOptions (tokens::tControl, selected ? juce::Font::bold
                                                                    : juce::Font::plain));
            g.drawText (command.title, text.removeFromTop (22), juce::Justification::centredLeft);
            g.setColour (tokens::c (tokens::fgTertiary));
            g.setFont (juce::FontOptions (tokens::tSmall));
            g.drawText (command.detail, text, juce::Justification::centredLeft);
            g.setColour (tokens::c (tokens::fgMuted));
            g.drawText (command.shortcut, shortcut, juce::Justification::centredRight);

            list.removeFromTop (2);
        }

        if (filtered.empty())
        {
            g.setColour (tokens::c (tokens::fgTertiary));
            g.setFont (juce::FontOptions (tokens::tBody));
            g.drawText ("No matching actions", list.removeFromTop (54),
                        juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        const int row = rowAt (event.y);
        if (row != selectedRow)
        {
            selectedRow = row;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        selectedRow = rowAt (event.y);
        invokeSelected();
    }

private:
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (filtered.empty())
            return false;
        if (key.getKeyCode() == juce::KeyPress::downKey)
        {
            selectedRow = juce::jmin ((int) filtered.size() - 1, selectedRow + 1);
            if (selectedRow >= firstVisibleRow + kVisibleRows)
                firstVisibleRow = selectedRow - kVisibleRows + 1;
            repaint();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::upKey)
        {
            selectedRow = juce::jmax (0, selectedRow - 1);
            if (selectedRow < firstVisibleRow)
                firstVisibleRow = selectedRow;
            repaint();
            return true;
        }
        return false;
    }

    void rebuildResults()
    {
        const auto needle = search.getText().trim().toLowerCase();
        filtered.clear();
        for (int i = 0; i < (int) commands.size(); ++i)
        {
            const auto haystack = (commands[(size_t) i].title + " "
                                 + commands[(size_t) i].detail).toLowerCase();
            if (needle.isEmpty() || haystack.contains (needle))
                filtered.push_back (i);
        }
        selectedRow = filtered.empty() ? -1 : 0;
        firstVisibleRow = 0;
        repaint();
    }

    int rowAt (int y) const noexcept
    {
        constexpr int firstRowY = 18 + 58 + 18;
        constexpr int rowStride = 50;
        const int row = (y - firstRowY) / rowStride;
        const int resultRow = firstVisibleRow + row;
        return row >= 0 && row < kVisibleRows
            && juce::isPositiveAndBelow (resultRow, (int) filtered.size())
                 ? resultRow : -1;
    }

    void invokeSelected()
    {
        if (! juce::isPositiveAndBelow (selectedRow, (int) filtered.size()))
            return;
        if (onInvoke)
            onInvoke (commands[(size_t) filtered[(size_t) selectedRow]].id);
    }

    juce::TextEditor search;
    std::vector<Command> commands;
    std::vector<int> filtered;
    int selectedRow = -1;
    int firstVisibleRow = 0;
    static constexpr int kVisibleRows = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommandPalette)
};
} // namespace stemdeck
