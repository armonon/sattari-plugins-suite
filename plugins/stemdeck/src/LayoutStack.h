#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// LayoutStack — a priority/constraint stack solver for the perform surface.
//
// WHY THIS EXISTS
//
// resized() grew into ~750 lines of sequential removeFromTop/Bottom(<literal>)
// with no notion of a minimum size, a priority, or what to do when the window is
// too small. Two concrete failures came out of that, both reproducible with
// StemDeckEditorRender at 1280x800:
//
//   1. The header's right cluster is a chain of removeFromRight(<fixed>). Once
//      the fixed widths exceed the bar, the rectangles OVERLAP — the user-presets
//      combo paints on top of the tempo chip and the title is squeezed to zero.
//      juce::Rectangle::removeFromRight clamps at the left edge; it does not
//      report failure, so the layout silently corrupts instead of adapting.
//
//   2. A deck lane spends ~311px on fixed-height chrome (status/tabs/mix/
//      transport/knobs) and gives the waveform whatever remains. At the 378px
//      minimum band height that is 67px for the waveform — ~17px per stem lane —
//      so the stem-coloured waveform, the Tier-1 differentiator, degrades into
//      four flat bars exactly when screen space is scarce.
//
// Both are the same bug: fixed sizes with no priority and no floor. This solver
// fixes the class of bug rather than the two instances.
//
// THE MODEL
//
// A stack is a list of Items laid out along one axis inside a bounds rectangle.
// Each Item declares what it needs, what it wants, and how badly it wants to
// stay on screen:
//
//   min       — never smaller than this while the item is present.
//   preferred — the target size when there is room.
//   grow      — share of any LEFTOVER space (0 = stays at preferred).
//   priority  — higher survives longer. Ties shed in declaration order.
//   gapAfter  — spacing to the next PRESENT item (never trails the last one).
//
// Resolution runs in three escalating stages:
//
//   1. Everything fits at preferred     -> hand out leftover space by `grow`.
//   2. Fits at min but not at preferred -> shrink toward min, LOWEST priority
//                                          first, so important rows keep their
//                                          preferred size the longest.
//   3. Does not fit even at min         -> drop whole items, lowest priority
//                                          first, and re-resolve.
//
// A dropped item gets an empty rectangle, which every caller already handles:
// the codebase's convention is `if (r.isEmpty()) return;` / setBounds({}). So
// shedding degrades the surface gracefully instead of overlapping it.
//
// This header is pure geometry — juce_graphics only, no Components — so it is
// unit-testable without instantiating an editor. See tests/stemdeck_layout.cpp.
// ─────────────────────────────────────────────────────────────────────────────

#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <initializer_list>
#include <vector>

namespace stemdeck::layout
{

struct Item
{
    int   min       = 0;
    int   preferred = 0;
    float grow      = 0.0f;
    int   priority  = 0;
    int   gapAfter  = 0;

    // A fixed-size item: no growth, cannot shrink below its own size.
    static Item fixed (int size, int priority = 0, int gapAfter = 0)
    {
        return { size, size, 0.0f, priority, gapAfter };
    }

    // An item that keeps a floor but expands into whatever space is left over.
    static Item flexible (int minSize, int preferredSize, float grow = 1.0f,
                          int priority = 0, int gapAfter = 0)
    {
        return { minSize, juce::jmax (minSize, preferredSize), grow, priority, gapAfter };
    }
};

// The resolved size of each item, in declaration order. A size of 0 means the
// item was shed and its rectangle will be empty.
using Sizes = std::vector<int>;

namespace detail
{
    // Sum of gaps that are actually rendered: a gap only applies when the item
    // is present AND some later item is also present, so shedding a trailing
    // item never leaves a dangling gap.
    inline int liveGapTotal (const std::vector<Item>& items, const std::vector<bool>& present)
    {
        int total = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (! present[i]) continue;
            bool anyLater = false;
            for (size_t j = i + 1; j < items.size(); ++j)
                if (present[j]) { anyLater = true; break; }
            if (anyLater) total += items[i].gapAfter;
        }
        return total;
    }

    inline int sumWhere (const std::vector<Item>& items, const std::vector<bool>& present,
                         bool usePreferred)
    {
        int total = 0;
        for (size_t i = 0; i < items.size(); ++i)
            if (present[i]) total += usePreferred ? items[i].preferred : items[i].min;
        return total;
    }

    // Index of the lowest-priority present item; ties break toward the LAST
    // declared, so earlier items (which read as more primary in source order)
    // survive marginally longer. Returns -1 when nothing is present.
    inline int lowestPriorityPresent (const std::vector<Item>& items, const std::vector<bool>& present)
    {
        int worst = -1;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (! present[i]) continue;
            if (worst < 0 || items[i].priority <= items[(size_t) worst].priority)
                worst = (int) i;
        }
        return worst;
    }
}

// Resolve the item sizes for a track of `available` units.
inline Sizes resolve (const std::vector<Item>& items, int available)
{
    Sizes out (items.size(), 0);
    if (items.empty() || available <= 0)
        return out;

    std::vector<bool> present (items.size(), true);

    // Stage 3 first, as a loop guard: shed until the mins plus live gaps fit.
    while (true)
    {
        const int need = detail::sumWhere (items, present, false)
                       + detail::liveGapTotal (items, present);
        if (need <= available)
            break;
        const int victim = detail::lowestPriorityPresent (items, present);
        if (victim < 0)
            return out;                 // nothing left to shed; everything is empty
        present[(size_t) victim] = false;
    }

    const int gaps = detail::liveGapTotal (items, present);
    int budget = available - gaps;

    // Start everyone at preferred, then claw back toward min if that overshoots.
    for (size_t i = 0; i < items.size(); ++i)
        out[i] = present[i] ? items[i].preferred : 0;

    int used = 0;
    for (size_t i = 0; i < items.size(); ++i) used += out[i];

    if (used > budget)
    {
        // Stage 2: shrink lowest priority first, each item down to its own min.
        int excess = used - budget;
        std::vector<int> order;
        for (size_t i = 0; i < items.size(); ++i)
            if (present[i]) order.push_back ((int) i);
        std::stable_sort (order.begin(), order.end(),
                          [&items] (int a, int b)
                          { return items[(size_t) a].priority < items[(size_t) b].priority; });

        for (int idx : order)
        {
            if (excess <= 0) break;
            const int slack = out[(size_t) idx] - items[(size_t) idx].min;
            const int take  = juce::jmin (slack, excess);
            out[(size_t) idx] -= take;
            excess -= take;
        }
        // `excess` is guaranteed 0 here: the shed loop above proved the mins fit.
    }
    else if (used < budget)
    {
        // Stage 1: hand out the leftover by grow weight.
        float totalGrow = 0.0f;
        for (size_t i = 0; i < items.size(); ++i)
            if (present[i]) totalGrow += items[i].grow;

        if (totalGrow > 0.0f)
        {
            const int leftover = budget - used;
            int handedOut = 0;
            int lastGrower = -1;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (! present[i] || items[i].grow <= 0.0f) continue;
                const int share = (int) ((float) leftover * (items[i].grow / totalGrow));
                out[i] += share;
                handedOut += share;
                lastGrower = (int) i;
            }
            // Integer division loses up to (growers-1) units; give the remainder
            // to the last grower so the stack fills its track exactly.
            if (lastGrower >= 0)
                out[(size_t) lastGrower] += leftover - handedOut;
        }
    }

    return out;
}

// ── Rectangle helpers ────────────────────────────────────────────────────────
// Each returns one rectangle per item, in declaration order; a shed item's
// rectangle is empty. The input `bounds` is consumed along the relevant axis.

inline std::vector<juce::Rectangle<int>> rows (juce::Rectangle<int> bounds,
                                               const std::vector<Item>& items)
{
    const auto sizes = resolve (items, bounds.getHeight());
    std::vector<juce::Rectangle<int>> out (items.size());
    auto remaining = bounds;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (sizes[i] <= 0) { out[i] = {}; continue; }
        out[i] = remaining.removeFromTop (sizes[i]);
        // Only consume the gap when a later item still needs the space.
        bool anyLater = false;
        for (size_t j = i + 1; j < items.size(); ++j)
            if (sizes[j] > 0) { anyLater = true; break; }
        if (anyLater) remaining.removeFromTop (items[i].gapAfter);
    }
    return out;
}

inline std::vector<juce::Rectangle<int>> columns (juce::Rectangle<int> bounds,
                                                  const std::vector<Item>& items)
{
    const auto sizes = resolve (items, bounds.getWidth());
    std::vector<juce::Rectangle<int>> out (items.size());
    auto remaining = bounds;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (sizes[i] <= 0) { out[i] = {}; continue; }
        out[i] = remaining.removeFromLeft (sizes[i]);
        bool anyLater = false;
        for (size_t j = i + 1; j < items.size(); ++j)
            if (sizes[j] > 0) { anyLater = true; break; }
        if (anyLater) remaining.removeFromLeft (items[i].gapAfter);
    }
    return out;
}

} // namespace stemdeck::layout
