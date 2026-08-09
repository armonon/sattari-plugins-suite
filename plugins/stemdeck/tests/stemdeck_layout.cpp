// Unit test for the LayoutStack constraint solver.
//
// This is the gate that stops the two failures LayoutStack was written to kill
// from coming back:
//
//   * header items overlapping when the bar is too narrow, and
//   * the stem waveform being squeezed to a sliver by fixed-height chrome.
//
// The solver is pure geometry, so this test needs no editor, no processor and
// no audio device — it runs in milliseconds and is safe for CI.

#include "LayoutStack.h"
#include "FxTargetModel.h"
#include "StemDeckDragPayload.h"
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}

void checkEq (int actual, int expected, const juce::String& what)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << what << " — expected " << expected
                  << ", got " << actual << "\n";
        ++failures;
    }
}

using namespace stemdeck::layout;

// ── 1. Everything fits: leftover space goes to the growers, by weight ────────
void testGrowDistribution()
{
    const std::vector<Item> items {
        Item::fixed (20),                          // chrome
        Item::flexible (10, 10, 1.0f),             // grows x1
        Item::flexible (10, 10, 3.0f),             // grows x3
    };
    const auto s = resolve (items, 100);
    checkEq (s[0], 20, "fixed row keeps its size");
    // 100 - (20+10+10) = 60 leftover, split 1:3 -> 15 / 45.
    checkEq (s[1], 10 + 15, "grow=1 takes a quarter of the leftover");
    checkEq (s[2], 10 + 45, "grow=3 takes three quarters of the leftover");
    checkEq (s[0] + s[1] + s[2], 100, "stack fills the track exactly");
}

// Integer division must not lose units — the stack has to fill its track.
void testGrowRemainderIsNotLost()
{
    const std::vector<Item> items {
        Item::flexible (0, 0, 1.0f),
        Item::flexible (0, 0, 1.0f),
        Item::flexible (0, 0, 1.0f),
    };
    const auto s = resolve (items, 100);   // 100/3 does not divide evenly
    checkEq (s[0] + s[1] + s[2], 100, "remainder is handed to the last grower");
}

// ── 2. Too tight for preferred: shrink LOWEST priority first ────────────────
void testShrinkOrderFollowsPriority()
{
    const std::vector<Item> items {
        { 10, 40, 0.0f, /*priority*/ 5, 0 },   // important
        { 10, 40, 0.0f, /*priority*/ 1, 0 },   // expendable
    };
    // Needs 80 at preferred, mins total 20. Give it 50: 30 must come off.
    const auto s = resolve (items, 50);
    checkEq (s[1], 10, "low-priority item is shrunk to its min first");
    checkEq (s[0], 40, "high-priority item keeps its preferred size");
    checkEq (s[0] + s[1], 50, "shrink fills the track exactly");
}

// ── 3. Too tight even for the mins: SHED, never overlap ─────────────────────
// This is the header-collision regression test.
void testShedRatherThanOverlap()
{
    const std::vector<Item> items {
        Item::fixed (100, /*priority*/ 9),   // title — must survive
        Item::fixed (100, /*priority*/ 5),
        Item::fixed (100, /*priority*/ 1),   // first to go
    };
    const auto s = resolve (items, 250);
    checkEq (s[2], 0, "lowest-priority item is shed when mins do not fit");
    checkEq (s[0], 100, "highest-priority item survives");
    checkEq (s[1], 100, "mid-priority item survives");
    check (s[0] + s[1] + s[2] <= 250, "shed stack never exceeds the track");
}

// The real failure mode: rectangles must never overlap, at ANY width.
void testRectanglesNeverOverlapAtAnyWidth()
{
    const std::vector<Item> items {
        Item::fixed (66, 9, 4), Item::fixed (58, 8, 4), Item::fixed (56, 7, 4),
        Item::fixed (118, 6, 4), Item::fixed (160, 2, 4), Item::fixed (150, 1, 4),
    };
    for (int w = 0; w <= 900; ++w)
    {
        const auto r = columns (juce::Rectangle<int> (0, 0, w, 40), items);
        for (size_t i = 0; i < r.size(); ++i)
        {
            if (r[i].isEmpty()) continue;
            check (r[i].getRight() <= w, "item stays inside the track at width " + juce::String (w));
            for (size_t j = i + 1; j < r.size(); ++j)
            {
                if (r[j].isEmpty()) continue;
                check (! r[i].intersects (r[j]),
                       "items " + juce::String ((int) i) + "/" + juce::String ((int) j)
                           + " must not overlap at width " + juce::String (w));
            }
        }
    }
}

// ── 4. The waveform floor: the deck's signature row keeps its minimum ───────
// Models the real deck lane. Chrome sheds before the waveform is starved.
void testWaveformKeepsItsFloorUnderPressure()
{
    constexpr int kWaveFloor = 120;
    const std::vector<Item> items {
        Item::fixed (22, /*priority*/ 9, 6),                       // header
        Item::flexible (kWaveFloor, 300, 1.0f, /*priority*/ 10, 6), // waveform — top priority
        Item::fixed (46, /*priority*/ 6, 6),                       // stem knobs
        Item::fixed (28, /*priority*/ 7, 6),                       // transport
        Item::fixed (78, /*priority*/ 5, 6),                       // mix band
        Item::fixed (26, /*priority*/ 4, 3),                       // tab strip
        Item::fixed (58, /*priority*/ 3, 4),                       // tab content
        Item::fixed (13, /*priority*/ 1, 0),                       // status line
    };

    // Roomy: everything present, waveform takes the slack.
    {
        const auto s = resolve (items, 600);
        for (size_t i = 0; i < items.size(); ++i)
            check (s[i] > 0, "at 600px every row is present");
        check (s[1] >= kWaveFloor, "waveform at or above its floor when roomy");
    }

    // The old failure point: 378px band. Chrome must yield, not the waveform.
    {
        const auto s = resolve (items, 378);
        check (s[1] >= kWaveFloor,
               "waveform holds its " + juce::String (kWaveFloor)
                   + "px floor at the 378px band (got " + juce::String (s[1]) + ")");
        check (s[7] == 0 || s[6] == 0,
               "low-priority chrome is shed before the waveform is starved");
    }

    // Brutal: even here the waveform outlives every piece of chrome.
    {
        const auto s = resolve (items, 200);
        check (s[1] > 0, "waveform is the last row standing");
        for (size_t i = 0; i < items.size(); ++i)
            if (i != 1 && s[i] > 0)
                check (items[i].priority >= items[5].priority || s[1] >= kWaveFloor,
                       "surviving chrome outranks what was shed");
    }
}

// ── 5. Gaps ─────────────────────────────────────────────────────────────────
void testGapsOnlyCountBetweenPresentItems()
{
    const std::vector<Item> items {
        Item::fixed (10, 5, /*gapAfter*/ 100),
        Item::fixed (10, 1, /*gapAfter*/ 100),
    };
    // Item 0 + gap + item 1 = 120. In 115 the second is shed, and its leading
    // gap must go with it, so the survivor fits comfortably.
    const auto s = resolve (items, 115);
    checkEq (s[1], 0, "second item is shed");
    checkEq (s[0], 10, "survivor keeps its size");

    const auto r = rows (juce::Rectangle<int> (0, 0, 50, 115), items);
    check (r[1].isEmpty(), "shed item gets an empty rectangle");
    checkEq (r[0].getHeight(), 10, "survivor's rectangle is correct");
}

void testTrailingGapIsNeverRendered()
{
    const std::vector<Item> items {
        Item::fixed (10, 5, /*gapAfter*/ 5),
        Item::fixed (10, 5, /*gapAfter*/ 5),   // trailing gap must not be consumed
    };
    const auto r = rows (juce::Rectangle<int> (0, 0, 50, 25), items);
    checkEq (r[0].getY(), 0, "first row at the top");
    checkEq (r[1].getY(), 15, "second row after exactly one gap");
    checkEq (r[1].getBottom(), 25, "no trailing gap after the last row");
}

// ── 6. Degenerate inputs ────────────────────────────────────────────────────
void testDegenerateInputs()
{
    check (resolve ({}, 100).empty(), "empty item list yields no sizes");

    const std::vector<Item> one { Item::fixed (10) };
    const auto zero = resolve (one, 0);
    checkEq (zero[0], 0, "zero track sheds everything");

    const auto negative = resolve (one, -50);
    checkEq (negative[0], 0, "negative track is treated as empty");

    const auto r = rows (juce::Rectangle<int> (0, 0, 0, 0), one);
    check (r[0].isEmpty(), "zero bounds yields an empty rectangle");
}

// The stage FX router must never change destination merely because the surrounding
// channel UI was rebuilt or a different deck received focus.
void testFxTargetSelectionIsExplicitAndStable()
{
    stemdeck::FxTargetModel rack;
    juce::StringArray names { "Master Bus", "Channel A", "Channel A > Vocals" };
    juce::Array<int> chains { 5, 0, 21 };
    juce::Array<int> kinds {
        (int) stemdeck::FxTargetModel::Kind::master,
        (int) stemdeck::FxTargetModel::Kind::channel,
        (int) stemdeck::FxTargetModel::Kind::stem };

    rack.setTargets (names, chains, kinds);
    checkEq (rack.currentChain(), 5, "FX router defaults to Master Bus");
    check (rack.currentKind() == stemdeck::FxTargetModel::Kind::master,
           "default FX target is identified as master");

    check (rack.selectChain (21), "a specific stem chain can be selected");
    checkEq (rack.currentChain(), 21, "stem selection addresses its real HostGraph chain");
    check (rack.currentName() == "Channel A > Vocals",
           "the full channel/stem breadcrumb remains visible");

    // Simulate channel/focus refresh with a changed display name and order.
    rack.setTargets ({ "Master Bus", "Channel A > Lead Vox", "Channel A" },
                     { 5, 21, 0 },
                     { (int) stemdeck::FxTargetModel::Kind::master,
                       (int) stemdeck::FxTargetModel::Kind::stem,
                       (int) stemdeck::FxTargetModel::Kind::channel });
    checkEq (rack.currentChain(), 21, "target survives a channel-list rebuild by chain identity");
    check (rack.currentName() == "Channel A > Lead Vox",
           "target breadcrumb refreshes without changing the destination");

    // Removing the destination is the one legitimate automatic fallback.
    rack.setTargets ({ "Master Bus", "Channel A" }, { 5, 0 },
                     { (int) stemdeck::FxTargetModel::Kind::master,
                       (int) stemdeck::FxTargetModel::Kind::channel });
    checkEq (rack.currentChain(), 5, "removed target safely falls back to Master Bus");
}

// Explore, Music Library, decks and performance pads all share this payload
// contract. Keep it typed so a plugin can never be mistaken for audio (or vice
// versa) as the UI is rearranged.
void testDragPayloadRoundTrips()
{
    auto audio = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("stemdeck_drag_payload_test.WAV");
    audio.replaceWithData ("RIFF", 4);

    const auto audioPayload = stemdeck::drag::makeAudio (audio);
    check (stemdeck::drag::kind (audioPayload) == stemdeck::drag::audioKind,
           "audio drag payload keeps its type");
    check (stemdeck::drag::audioFile (audioPayload) == audio,
           "audio drag payload preserves the absolute path");
    check (stemdeck::drag::isSupportedAudioFile (audio),
           "audio extension matching is case-insensitive");

    const auto pluginPayload = stemdeck::drag::makePlugin (7, "Sattari Warp");
    check (stemdeck::drag::kind (pluginPayload) == stemdeck::drag::pluginKind,
           "plugin drag payload keeps its type");
    checkEq (stemdeck::drag::pluginIndex (pluginPayload), 7,
             "plugin drag payload preserves the scanned index");
    check (stemdeck::drag::pluginName (pluginPayload) == "Sattari Warp",
           "plugin drag payload preserves its display name");
    check (stemdeck::drag::audioFile (pluginPayload) == juce::File(),
           "plugin payload cannot resolve as audio");

    check (stemdeck::drag::kind (juce::var (stemdeck::drag::fileBrowserKind))
               == stemdeck::drag::fileBrowserKind,
           "file-browser drag marker remains compatible with FileTreeComponent");

    audio.deleteFile();
}
} // namespace

int main()
{
    testGrowDistribution();
    testGrowRemainderIsNotLost();
    testShrinkOrderFollowsPriority();
    testShedRatherThanOverlap();
    testRectanglesNeverOverlapAtAnyWidth();
    testWaveformKeepsItsFloorUnderPressure();
    testGapsOnlyCountBetweenPresentItems();
    testTrailingGapIsNeverRendered();
    testDegenerateInputs();
    testFxTargetSelectionIsExplicitAndStable();
    testDragPayloadRoundTrips();

    if (failures == 0) std::cout << "StemDeckLayout: all checks passed\n";
    else               std::cerr << "StemDeckLayout: " << failures << " check(s) failed\n";
    return failures == 0 ? 0 : 1;
}
