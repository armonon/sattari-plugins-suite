#pragma once

// =============================================================================
//  SessionRenderer — render a captured SessionModel to a stereo mixdown offline
//  (build-order #11, the payoff of the session foundation).
// -----------------------------------------------------------------------------
//  Walks the deterministic SessionRender plan, advancing per-deck playheads and
//  summing the source audio under each deck's fader. Reads sources with the same
//  16-tap windowed-sinc used by the deck (Interpolation.h) — export inherits
//  high-quality resampling, which is part of why the render beats the
//  performance. Offline: no realtime constraints, no allocation discipline.
//
//  Deterministic: same session + same sources => identical output samples.
//
//  Source audio is supplied via a provider callback keyed by trackId, so this is
//  unit-testable with synthetic buffers and decoupled from file decoding.
// =============================================================================

#include "SessionRender.h"
#include "Interpolation.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>

namespace stemdeck::session
{

class SessionRenderer
{
public:
    static constexpr int kMaxDecks = 4;

    // Returns a pointer to the (caller-owned) source buffer for a trackId, and
    // reports its sample rate via `sourceRateOut`. Return nullptr if unavailable.
    using AudioProvider = std::function<const juce::AudioBuffer<float>* (const juce::String& trackId, double& sourceRateOut)>;

    struct Options
    {
        double tailBeats = 8.0;   // silence/decay rendered after the last event
        int    blockSize = 512;   // action-application granularity
    };

    static juce::AudioBuffer<float> render (const SessionModel& s, const AudioProvider& provider)
    {
        return render (s, provider, Options{});
    }

    static juce::AudioBuffer<float> render (const SessionModel& s, const AudioProvider& provider,
                                            const Options& opt)
    {
        const double outRate = s.sampleRate > 0.0 ? s.sampleRate : 44100.0;

        double lastBeat = 0.0;
        for (const auto& e : s.events) lastBeat = juce::jmax (lastBeat, e.atBeat);
        const juce::int64 total = (juce::int64) std::llround (s.sampleTimeForBeat (lastBeat + opt.tailBeats));

        juce::AudioBuffer<float> out (2, (int) juce::jmax<juce::int64> (1, total));
        out.clear();

        struct Deck
        {
            const juce::AudioBuffer<float>* src = nullptr;
            double srcRate = 44100.0;
            double playhead = 0.0;   // in source samples
            bool   playing = false;
            double fader = 1.0;      // linear gain; default audible if never set
            int    cfSide = 1;       // crossfader bus: 0=A(left), 1=center, 2=B(right)
        };
        std::array<Deck, kMaxDecks> decks {};

        // Master crossfader, 0=A .. 1=B, constant-power. 0.5 (center) is the
        // neutral default: a center-assigned deck is unaffected regardless.
        double crossfader = 0.5;
        const double halfPi = juce::MathConstants<double>::halfPi;

        const auto plan = SessionRender::buildPlan (s);
        size_t pi = 0;

        for (juce::int64 i = 0; i < total; ++i)
        {
            // Apply every action scheduled at or before this output sample.
            while (pi < plan.size() && (juce::int64) std::llround (plan[pi].atSample) <= i)
            {
                const auto& a = plan[pi];
                if (a.deck >= 0 && a.deck < kMaxDecks)
                {
                    auto& d = decks[(size_t) a.deck];
                    switch (a.kind)
                    {
                        case ActionKind::deckLoad:
                            d.src = provider (a.trackId, d.srcRate);
                            if (d.srcRate <= 0.0) d.srcRate = outRate;
                            d.playhead = 0.0;
                            break;
                        case ActionKind::play:  d.playing = true;  break;
                        case ActionKind::pause: d.playing = false; break;
                        case ActionKind::cue:   d.playing = false; d.playhead = 0.0; break;
                        case ActionKind::seek:
                            // positionBeat = musical time into the track.
                            d.playhead = a.value * (60.0 / s.masterTempo) * d.srcRate;
                            break;
                        case ActionKind::param:
                            if (a.paramId == "fader" || a.paramId == "gain") d.fader = a.value;
                            else if (a.paramId == "cfSide")     d.cfSide = (int) a.value;
                            else if (a.paramId == "crossfader") crossfader = juce::jlimit (0.0, 1.0, a.value);
                            break;
                    }
                }
                ++pi;
            }

            double l = 0.0, r = 0.0;
            for (auto& d : decks)
            {
                if (d.playing && d.src != nullptr && d.src->getNumSamples() > 1
                    && d.playhead >= 0.0 && d.playhead < (double) d.src->getNumSamples())
                {
                    // Constant-power crossfader: A rides cos, B rides sin, center flat.
                    const double cf = d.cfSide == 0 ? std::cos (crossfader * halfPi)
                                    : d.cfSide == 2 ? std::sin (crossfader * halfPi)
                                                    : 1.0;
                    const double g  = d.fader * cf;
                    const float sl = stemdeck::readSinc (*d.src, 0, d.playhead);
                    const float sr = stemdeck::readSinc (*d.src, d.src->getNumChannels() > 1 ? 1 : 0, d.playhead);
                    l += g * sl;
                    r += g * sr;
                }
                if (d.playing) d.playhead += d.srcRate / outRate;
            }
            out.setSample (0, (int) i, (float) l);
            out.setSample (1, (int) i, (float) r);
        }
        return out;
    }
};

} // namespace stemdeck::session
