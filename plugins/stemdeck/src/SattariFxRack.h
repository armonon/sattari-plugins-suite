#pragma once

// SattariFxRack — a first-class FX-rack panel for the Master Control Band (it takes
// the crossfader's slot). Pick a chain (deck / mic / master), add Sattari-suite FX
// with "+ FX", and each loaded plugin gets a row with an [edit] (opens its editor
// window) and [x] (remove). The host supplies the data + actions via callbacks.

#include <juce_gui_basics/juce_gui_basics.h>
#include "DesignTokens.h"
#include <vector>
#include <functional>

namespace stemdeck
{

class SattariFxRack : public juce::Component
{
public:
    // Host wiring.
    std::function<int()>              getPluginCount;   // for the selected chain
    std::function<juce::String (int)> getPluginName;
    std::function<void()>             onAdd;
    std::function<void (int)>         onEdit, onRemove;
    std::function<void()>             onChainChanged;
    std::function<bool (int)>         isBypassed;      // per-insert bypass state
    std::function<void (int)>         onToggleBypass;
    std::function<void (int,int)>     onReorder;       // drag an insert to a new position

    // Scope selector. The reference (§5) shows master / channel / per-STEM scopes; the
    // engine has master, deckA-D and mic chains only, so only those are offered. A
    // per-stem scope needs stem-level plugin chains in HostGraph, which do not exist.
    // Segment labels and the CHAIN each one targets. These are not the same numbering:
    // the engine has 22 chains, and the selector only ever offers the handful that are
    // in context (master, the focused deck, that deck's four stems).
    void setScopes (juce::StringArray names, juce::Array<int> chainIndices)
    {
        scopes = std::move (names);
        scopeChains = std::move (chainIndices);
        scope = juce::jlimit (0, juce::jmax (0, scopes.size() - 1), scope);
        repaint();
    }
    int  currentScope() const noexcept { return scope; }
    void setScope (int i) { scope = juce::jlimit (0, juce::jmax (0, scopes.size() - 1), i); repaint(); }

    SattariFxRack()
    {
        // The chain combo and "+ FX" button are gone: the scope is now a painted
        // segmented control in the header, and adding is the always-present "add FX"
        // slot at the end of the rail. Leaving them as children drew a stray combo box
        // over the new header.
    }

    int currentChainIndex() const
    { return juce::isPositiveAndBelow (scope, scopeChains.size()) ? scopeChains[scope] : 0; }
    void setStatus (juce::String s) { status = std::move (s); repaint(); }

    // Everything is painted, so there are no child bounds to assign. Hit regions are
    // recorded during paint() and tested in mouseUp().
    void resized() override {}

    // Anchor for the add-FX popup menu; the slot is painted, not a component.
    juce::Rectangle<int> addSlotBounds() const noexcept { return addRect; }

    void paint (juce::Graphics& g) override
    {
        namespace T = stemdeck::tokens;
        auto full = getLocalBounds();

        g.setColour (T::c (T::bgPanel2));
        g.fillRoundedRectangle (full.toFloat(), 8.0f);
        g.setColour (T::c (T::borderSubtle));
        g.drawRoundedRectangle (full.toFloat().reduced (0.5f), 8.0f, 1.0f);

        auto body = full.reduced (T::gap7);

        // ── Header: label + scope segmented control ──
        auto header = body.removeFromTop (22);
        g.setColour (T::c (T::fgTertiary));
        g.setFont (juce::FontOptions (T::tSmall, juce::Font::bold).withKerningFactor (0.06f));
        auto labelR = header.removeFromLeft (68);
        g.drawText ("SATTARI FX", labelR, juce::Justification::centredLeft);
        header.removeFromLeft (T::gap6);

        scopeRects.clear();
        if (! scopes.isEmpty())
        {
            int wanted = 0;
            g.setFont (juce::FontOptions (T::tSmall));
            for (const auto& n : scopes)
                wanted += (int) std::ceil (g.getCurrentFont().getStringWidthFloat (n)) + 22;

            auto track = header.removeFromLeft (juce::jmin (wanted + 2 * T::gap2, header.getWidth()));
            g.setColour (T::c (T::bgWell2));
            g.fillRoundedRectangle (track.toFloat(), 7.0f);

            auto seg = track.reduced (T::gap2);
            for (int i = 0; i < scopes.size() && seg.getWidth() > 0; ++i)
            {
                const int w = juce::jmin (seg.getWidth(),
                    (int) std::ceil (g.getCurrentFont().getStringWidthFloat (scopes[i])) + 22);
                auto cell = seg.removeFromLeft (w);
                scopeRects.push_back ({ i, cell });
                if (i == scope)
                {
                    g.setColour (T::c (T::bgControl));
                    g.fillRoundedRectangle (cell.toFloat(), T::radiusInner);
                }
                g.setColour (T::c (i == scope ? T::fgPrimary : T::fgTertiary));
                g.drawText (scopes[i], cell, juce::Justification::centred);
            }
        }

        body.removeFromTop (T::gap5);

        // ── Insert rail: bg-well holding left-to-right tiles with flow arrows ──
        auto rail = body;
        g.setColour (T::c (T::bgWell));
        g.fillRoundedRectangle (rail.toFloat(), 8.0f);

        editRects.clear(); removeRects.clear(); bypassRects.clear(); tileRects.clear(); addRect = {};

        auto lane = rail.reduced (T::gap7);
        const int count = getPluginCount ? getPluginCount() : 0;
        constexpr int kTileW = 118, kArrowW = 16, kAddW = 60;

        for (int i = 0; i < count; ++i)
        {
            if (lane.getWidth() < kTileW + kArrowW) break;   // rail full; remainder scrolls off
            auto tile = lane.removeFromLeft (kTileW);
            tileRects.push_back ({ i, tile });
            paintInsertTile (g, tile, i);

            lane.removeFromLeft (T::gap3);
            auto arrow = lane.removeFromLeft (kArrowW);
            g.setColour (T::c (T::fgFaint));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), arrow, juce::Justification::centred);
            lane.removeFromLeft (T::gap3);
        }

        // Empty state is an "add FX" SLOT, never a dead "No FX loaded" message — the
        // reference is explicit about this. The slot is always present, so adding the
        // first insert and the fifth are the same gesture in the same place.
        if (lane.getWidth() >= kAddW)
        {
            addRect = lane.removeFromLeft (kAddW).withSizeKeepingCentre (kAddW, juce::jmin (100, lane.getHeight()));
            juce::Path dashed;
            dashed.addRoundedRectangle (addRect.toFloat().reduced (0.5f), T::radiusWell);
            const float dash[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, dashed, dash, 2);
            g.setColour (T::c (T::borderSubtle));
            g.fillPath (dashed);

            auto inner = addRect.reduced (4);
            g.setColour (accent);
            g.setFont (juce::FontOptions (20.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xef\xbc\x8b")),
                        inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredBottom);
            g.setColour (T::c (T::fgTertiary));
            g.setFont (juce::FontOptions (T::tLabel));
            g.drawText ("add FX", inner, juce::Justification::centredTop);
        }

        if (status.isNotEmpty())
        {
            g.setColour (T::c (T::fgSecondary));
            g.setFont (juce::FontOptions (T::tSmall));
            g.drawText (status, full.removeFromBottom (14).reduced (T::gap7, 2),
                        juce::Justification::centredLeft, true);
        }
    }

    void paintInsertTile (juce::Graphics& g, juce::Rectangle<int> tile, int i)
    {
        namespace T = stemdeck::tokens;
        const bool byp = isBypassed && isBypassed (i);

        g.setColour (T::c (T::bgTile));
        g.fillRoundedRectangle (tile.toFloat(), T::radiusWell);
        g.setColour (T::c (T::borderSubtle));
        g.drawRoundedRectangle (tile.toFloat().reduced (0.5f), T::radiusWell, 0.5f);

        auto in = tile.reduced (T::gap7);
        const float dim = byp ? 0.45f : 1.0f;

        // Drag handle — real: dragging a tile reorders the chain via onReorder.
        auto grip = in.removeFromTop (10);
        auto gripR = grip.removeFromRight (10);
        g.setColour (T::c (T::fgMuted));
        g.setFont (juce::FontOptions (T::tLabel));
        g.drawText ("::", gripR, juce::Justification::centredRight);

        g.setColour (accent.withMultipliedAlpha (dim));
        g.setFont (juce::FontOptions (T::tLabel));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), grip, juce::Justification::centredLeft);

        in.removeFromTop (T::gap1);
        g.setColour (T::c (T::fgPrimary).withMultipliedAlpha (dim));
        g.setFont (juce::FontOptions (T::tControl));
        g.drawFittedText (getPluginName ? getPluginName (i) : juce::String ("FX"),
                          in.removeFromTop (26), juce::Justification::topLeft, 2);

        // on / edit chips.
        auto chips = in.removeFromBottom (16);
        auto onR = chips.removeFromLeft (chips.getWidth() / 2 - 2);
        chips.removeFromLeft (4);
        auto edR = chips;
        bypassRects.push_back ({ i, onR });
        editRects.push_back ({ i, edR });

        g.setColour (byp ? T::c (T::bgControl) : T::c (T::stemBasLight).withAlpha (0.12f));
        g.fillRoundedRectangle (onR.toFloat(), T::radiusPill);
        g.setColour (byp ? T::c (T::fgMuted) : T::c (T::stemBasLight));
        g.setFont (juce::FontOptions (T::tCaption));
        g.drawText (byp ? "byp" : "on", onR, juce::Justification::centred);

        g.setColour (T::c (T::bgControl));
        g.fillRoundedRectangle (edR.toFloat(), T::radiusPill);
        g.setColour (T::c (T::fgTertiary));
        g.drawText ("edit", edR, juce::Justification::centred);

        // Remove target: the grip corner doubles as it on right-click; keep an explicit
        // hit box so removal never requires a context menu.
        removeRects.push_back ({ i, gripR });
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = -1;
        for (const auto& [i, rect] : tileRects)
            if (rect.contains (e.getPosition())) { dragFrom = i; break; }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        // A drag that ends over a different tile reorders; a click falls through.
        if (dragFrom >= 0 && e.getDistanceFromDragStart() > 6)
        {
            for (const auto& [i, rect] : tileRects)
                if (i != dragFrom && rect.contains (p))
                { const int from = dragFrom; dragFrom = -1; if (onReorder) onReorder (from, i); repaint(); return; }
            dragFrom = -1;
            return;
        }
        dragFrom = -1;

        for (const auto& [i, rect] : scopeRects)
            if (rect.contains (p)) { setScope (i); if (onChainChanged) onChainChanged(); return; }
        for (const auto& [i, rect] : bypassRects) if (rect.contains (p) && onToggleBypass) { onToggleBypass (i); repaint(); return; }
        for (const auto& [i, rect] : editRects)   if (rect.contains (p) && onEdit)   { onEdit (i);   return; }
        for (const auto& [i, rect] : removeRects) if (rect.contains (p) && onRemove) { onRemove (i); return; }
        if (! addRect.isEmpty() && addRect.contains (p) && onAdd) { onAdd(); return; }
    }

    void setAccent (juce::Colour c) { accent = c; repaint(); }

private:
    juce::String status;
    std::vector<std::pair<int, juce::Rectangle<int>>> editRects, removeRects, bypassRects, tileRects, scopeRects;
    juce::Rectangle<int> addRect;
    juce::StringArray scopes;
    juce::Array<int> scopeChains;
    int scope = 0;
    int dragFrom = -1;
    juce::Colour accent { stemdeck::tokens::deckA };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariFxRack)
};

// A window hosting a hosted plugin's own editor.
class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow (juce::AudioProcessor& proc, const juce::String& title, std::function<void (PluginEditorWindow*)> onClose)
        : juce::DocumentWindow (title, stemdeck::tokens::c (stemdeck::tokens::bgPanel2), juce::DocumentWindow::closeButton),
          closer (std::move (onClose)), owner (&proc)
    {
        setUsingNativeTitleBar (true);
        if (auto* ed = proc.createEditorIfNeeded())
            setContentOwned (ed, true);
        else
        {
            auto* lbl = new juce::Label ({}, proc.getName() + " has no editor UI");
            lbl->setSize (280, 80); lbl->setJustificationType (juce::Justification::centred);
            setContentOwned (lbl, true);
        }
        setResizable (true, false);
        centreWithSize (juce::jmax (320, getWidth()), juce::jmax (120, getHeight()));
        setVisible (true);
    }
    void closeButtonPressed() override { if (closer) closer (this); }

    // A plugin has exactly ONE editor instance: createEditorIfNeeded() hands back the
    // same object on a second call, so a second window would co-own it and double-free.
    juce::AudioProcessor* ownedProcessor() const noexcept { return owner; }

private:
    std::function<void (PluginEditorWindow*)> closer;
    juce::AudioProcessor* owner = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};

} // namespace stemdeck
