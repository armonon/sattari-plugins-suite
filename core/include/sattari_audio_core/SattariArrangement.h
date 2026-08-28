#pragma once

// =============================================================================
//  SattariArrangement — where we are in the song, not just where we are in time.
// -----------------------------------------------------------------------------
//  Every step sequencer on the market is CONTEXT-FREE: it loops the same bar
//  until you move something. It cannot fill before the chorus because it does not
//  know a chorus is coming.
//
//  Compass publishes sectionId, and the transport gives position, so the suite
//  can know. This turns those two into the questions a generative rhythm actually
//  needs to ask:
//
//      "am I in the last bar of the phrase?"   -> play a fill
//      "did the section just change?"          -> regenerate the pattern
//
//  WHY PHRASE POSITION IS COMPUTED AND NOT READ. sectionId changes AT the
//  boundary, which is far too late to fill INTO it — by the time the value
//  changes, the fill needed to have already played. A fill has to be scheduled
//  from position: the last bar of a repeating phrase. Section changes are still
//  used, but for the thing they can actually answer — "this is new material, so
//  stop repeating the old pattern."
//
//  Pure std. No JUCE, no allocation, no locks; safe on the audio thread.
// =============================================================================

#include <cmath>
#include <cstdint>

namespace sattari::dsp
{

//  Phrase lengths in bars. A "phrase" is the repeating unit a fill belongs to —
//  usually 4 or 8 bars in most popular music, which is why 8 is the default.
inline double phraseBarsFor (int index) noexcept
{
    static const double kBars[] = { 1.0, 2.0, 4.0, 8.0, 16.0 };
    constexpr int n = (int) (sizeof (kBars) / sizeof (kBars[0]));
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    return kBars[index];
}
inline constexpr int phraseCount() noexcept { return 5; }

class ArrangementWatcher
{
public:
    void reset() noexcept
    {
        lastSection_ = -1;
        lastPhrase_  = -1;
        inFill_ = false;
        sectionChanged_ = false;
        phraseChanged_ = false;
    }

    //  ppq            song position in quarter notes
    //  beatsPerBar    almost always 4
    //  phraseBars     the repeating unit a fill belongs to
    //  fillBeats      how long the fill window is, counted back from the phrase
    //                 end. One bar is the common choice; half a bar is tighter.
    //  sectionId      from Compass; 0 when nothing is published
    void update (double ppq, double beatsPerBar, double phraseBars,
                 double fillBeats, int sectionId) noexcept
    {
        if (beatsPerBar <= 0.0) beatsPerBar = 4.0;
        if (phraseBars  <= 0.0) phraseBars  = 8.0;

        const double phraseBeats = beatsPerBar * phraseBars;
        const double phraseIndex = std::floor (ppq / phraseBeats);
        const double intoPhrase  = ppq - phraseIndex * phraseBeats;

        // Clamp so a silly fill length cannot swallow the whole phrase — a fill
        // that never ends is just a different pattern.
        if (fillBeats < 0.0) fillBeats = 0.0;
        if (fillBeats > phraseBeats * 0.5) fillBeats = phraseBeats * 0.5;

        inFill_ = fillBeats > 0.0 && intoPhrase >= (phraseBeats - fillBeats);

        const int phrase = (int) phraseIndex;
        phraseChanged_  = (lastPhrase_  >= 0 && phrase != lastPhrase_);
        sectionChanged_ = (lastSection_ >= 0 && sectionId != lastSection_);
        lastPhrase_  = phrase;
        lastSection_ = sectionId;
    }

    // True while inside the fill window at the end of the current phrase.
    bool inFillWindow() const noexcept { return inFill_; }

    // Edge-triggered: true only on the block where it happened.
    bool phraseJustChanged()  const noexcept { return phraseChanged_; }
    bool sectionJustChanged() const noexcept { return sectionChanged_; }

    int  phraseIndex() const noexcept { return lastPhrase_; }
    int  sectionId()   const noexcept { return lastSection_; }

    //  A seed that is stable WITHIN a phrase and different BETWEEN phrases, so a
    //  generated fill is identical every time you replay that bar but not
    //  identical to the fill before it. Deterministic replay is the whole reason
    //  the generative core is seeded rather than live-random.
    std::uint32_t phraseSeed (std::uint32_t salt = 0) const noexcept
    {
        std::uint64_t x = (std::uint64_t) (std::int64_t) lastPhrase_ * 0x9e3779b97f4a7c15ULL
                        + (std::uint64_t) lastSection_ * 0xbf58476d1ce4e5b9ULL
                        + salt;
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        return (std::uint32_t) (x >> 32) | 1u;
    }

private:
    int  lastSection_ = -1;
    int  lastPhrase_  = -1;
    bool inFill_ = false;
    bool sectionChanged_ = false;
    bool phraseChanged_ = false;
};

} // namespace sattari::dsp
