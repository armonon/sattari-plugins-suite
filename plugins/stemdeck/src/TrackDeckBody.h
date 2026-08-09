#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// TrackDeckBody — the TrackDeck specialization of ChannelBody. Encapsulates one
// deck's render machinery (DeckPlayer + real-time stem separation + per-stem FX +
// built-in DeckFX + beat-grid/warp plan) that today lives inline in
// MainPluginProcessor::processBlock's per-deck loop (lines ~498-650).
//
// processInto() produces the deck's DRY stereo mix into the caller-provided scratch.
// The Channel frame then adds the FX-chain, recorder, peak metering, and crossfader
// bus sum — those are frame concerns, not body concerns.
//
// Params: the audio thread fills `params` from its APVTS reads immediately before
// calling processInto (same thread, sequential — no race). This keeps TrackDeckBody
// decoupled from the processor's parameter model, so it's unit-testable in isolation
// and forward-compatible with the Step-2 slot pool.
// ─────────────────────────────────────────────────────────────────────────────

#include <JuceHeader.h>
#include <array>
#include "ChannelBody.h"
#include "DeckPlayer.h"
#include "DeckFX.h"
#include "RealTimeStemSeparator.h"
#include "MasterFx.h"
#include "WarpEngine.h"
#include "BeatSyncEngine.h"
#include "TrackAnalysis.h"
#include "StemGain.h"

// Per-block parameter snapshot for one TrackDeck. Plain values; filled by the owner
// each block. Stem gains are raw PERCENT (converted to linear inside processInto,
// matching the processor). Mirrors the reads at MainPluginProcessor.cpp:498-650.
struct TrackDeckParams
{
    bool  play = false;
    float volume = 0.0f, fader = 1.0f;
    float low = 0.0f, mid = 0.0f, high = 0.0f, filter = 0.5f;
    float pitch = 0.0f, bpm = 120.0f, beatOffset = 0.0f, downbeat = 0.0f;
    int   tempoInterp = 0, syncMode = 0, keyTarget = 0;
    bool  keyLock = false, liveKey = false;
    std::array<float, DeckPlayer::numStemLanes> stemGainPercent { { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f } };
    std::array<float, DeckPlayer::numStemLanes> stemPitch { {} };
    std::array<float, DeckPlayer::numStemLanes> stemFilt  { { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f } };
    std::array<float, DeckPlayer::numStemLanes> stemSend  { {} };
    float revMix = 0.0f, echoMix = 0.0f;

    // Globals the deck render reads.
    bool  hostSync = false, beatSync = false, deckSync = true;
    int   masterKey = 0;

    // Live-key harmonic-match inputs (published from the deck's live analysis).
    int   liveKeyPitchClass = -1;
    float liveKeyConfidence  = 0.0f;
};

class TrackDeckBody : public ChannelBody
{
public:
    TrackDeckBody() = default;

    ChannelType type() const noexcept override { return ChannelType::trackDeck; }
    DeckPlayer*       trackDeckPlayer() noexcept override { return &deck; }
    TrackDeckParams*  trackDeckParams() noexcept override { return &params; }

    // Real-time separator tuning (atomic inside RealTimeStemSeparator).
    void setDrumsSensitivity (float s) noexcept { rtss.setDrumsSensitivity (s); }
    void setVocalsMidWeight  (float w) noexcept { rtss.setVocalsMidWeight (w); }
    void setStemPluginProcessingRequired (bool required) noexcept
    {
        stemPluginProcessingRequired.store (required, std::memory_order_release);
    }
    std::uint64_t getRealtimeSeparationBlocksForTest() const noexcept
    {
        return realtimeSeparationBlocks;
    }

    DeckPlayer&       deckPlayer()       noexcept { return deck; }
    const DeckPlayer& deckPlayer() const noexcept { return deck; }

    TrackDeckParams params;   // filled by the owner each block, read on the audio thread

    void prepare (double sampleRate, int blockSize, int numChannels) override
    {
        preparedRate     = sampleRate > 0.0 ? sampleRate : 44100.0;
        preparedChannels = juce::jmax (1, numChannels);
        const int block  = juce::jmax (1, blockSize);

        deck.prepare (preparedRate, block, preparedChannels);
        deckFX.prepare (preparedRate, block);
        rtss.prepare (preparedRate, block);
        for (auto& fx : stemFx) fx.prepare (preparedRate, block, preparedChannels);

        // Half a second of headroom per stem covers any realistic plugin latency and is
        // allocated ONCE here — never on the audio thread.
        stemDelayCapacity = juce::jmax (1, (int) (preparedRate * 0.5));
        for (auto& d : stemDelayLine)
        {
            d.setSize (juce::jmax (1, preparedChannels), stemDelayCapacity, false, true, true);
            d.clear();
        }
        stemDelayWrite.fill (0);
        stemDelayApplied.fill (-1);
        warp.prepare (preparedRate, block, preparedChannels);

        for (auto* b : { &drums, &bass, &music, &vocals })
            b->setSize (preparedChannels, block, false, false, true);
        preFaderScratch.setSize (preparedChannels, block, false, false, true);

        gridDirty = true;
    }

    // Key-lock (RubberBand) adds latency; the plain-resample path adds none.
    int getLatencySamples() const noexcept override { return deck.getTimeStretchLatencySamples(); }

    void release() override { rtss.reset(); }

    // AUDIO THREAD — RT-safe. Renders the deck's dry mix into `scratch` (already
    // sized to ctx.numSamples and cleared by the Channel frame).
    void processInto (juce::AudioBuffer<float>& scratch, const BlockContext& ctx) noexcept override
    {
        const int n = ctx.numSamples;
        if (n <= 0 || scratch.getNumSamples() < n)
            return;

        const auto deckAudio = deck.getLoadedAudio();
        // Prefer the background-refined analysis once the neural pass has landed
        // (§A3 hot-swap). Atomic load — no lock, no allocation.
        const auto refined = deck.getRefinedAnalysis();
        const auto& analysis = refined != nullptr ? *refined
                             : (deckAudio ? deckAudio->analysis : fallbackAnalysis);

        // ── Beat grid (rebuild only when its inputs change — makeGrid sorts vectors) ──
        const GridKey key { deckAudio.get(), refined.get(), params.bpm, params.beatOffset,
                            params.downbeat, params.tempoInterp, params.syncMode };
        if (gridDirty || key != cachedKey)
        {
            // Prefer a grid built on the MESSAGE thread. makeGrid sorts and copies
            // vectors, i.e. it allocates, and building it here would do that mid-render.
            // Publishing is an atomic shared_ptr swap (same idiom as setStemAudio).
            if (auto pub = std::atomic_load (&publishedGrid))
                if (! (pub->key != key))   // GridKey defines only !=
                {
                    // HOLD the published grid rather than copying it: assigning to
                    // cachedGrid copies its vectors, which allocates — the very thing
                    // publishing was meant to avoid. A shared_ptr assignment is just a
                    // refcount bump.
                    heldGrid  = std::move (pub);
                    cachedKey = key;
                    gridDirty = false;
                }

            // If a matching publication has not arrived yet, keep rendering the
            // previous coherent grid. MainPluginProcessor's AsyncUpdater is already
            // building the replacement off the audio thread. The previous fallback
            // copied/sorted vectors here and could glitch exactly when a performer
            // dragged BPM or downbeat—the worst possible moment for a live tool.
        }

        const bool hostTempoSync = params.hostSync && ctx.transport.bpm > 1.0;

        // ── Harmonic-match key (live estimate first, else the loaded analysis) ──
        int keyPc = -1; bool keyHas = false;
        if (params.liveKey && params.liveKeyPitchClass >= 0 && params.liveKeyPitchClass < 12 && params.liveKeyConfidence > 0.0f)
        {
            keyHas = true; keyPc = params.liveKeyPitchClass;
        }
        else if (deckAudio && analysis.hasKey())
        {
            keyHas = true; keyPc = analysis.keyPitchClass;
        }
        const int   targetKey  = keyTargetToPitchClass (params.masterKey > 0 ? params.masterKey : params.keyTarget);
        const float matchShift = (keyHas && targetKey >= 0)
                               ? (float) TrackAnalyzer::nearestSemitoneDelta (keyPc, targetKey) : 0.0f;

        const auto& gridInUse = heldGrid != nullptr ? heldGrid->grid : cachedGrid;
        const auto warpPlan = warp.makePlan (ctx.transport, gridInUse,
                                             deckAudio ? deckAudio->sourceSampleRate : preparedRate,
                                             deckAudio ? (double) deckAudio->numSourceSamples() : 0.0,
                                             params.pitch + matchShift,
                                             params.keyLock,
                                             hostTempoSync && params.beatSync && params.deckSync,
                                             /*wantStatus*/ false);

        // ── Per-stem gains (percent → linear) and pitch ──
        std::array<float, DeckPlayer::numStemLanes> stemGains {}, stemPitchArr {};
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        {
            stemGains[(size_t) lane]    = stemdeck::stemgain::percentToLinear (params.stemGainPercent[(size_t) lane]);
            stemPitchArr[(size_t) lane] = (lane >= DeckPlayer::drumsStem && lane <= DeckPlayer::vocalsStem)
                                        ? params.stemPitch[(size_t) lane] : 0.0f;
        }

        // ── Deck playback (writes the summed stem mix into the cleared scratch) ──
        preFaderScratch.clear (0, n);
        deck.process (scratch, 0, n, params.play, params.volume, params.fader,
                      params.low, params.mid, params.high, params.filter, 1.0f,
                      warpPlan.timePitch, stemGains, stemPitchArr, warpPlan.beatSync,
                      &preFaderScratch);

        // The clean source recorder must run even while stopped/faded out so every
        // take lane remains sample-aligned with the master and printed safety render.
        if (onPreFaderCapture)
            onPreFaderCapture (preFaderScratch, n);

        // ── Real-time stem separation (only when no offline stem files are loaded) ──
        renderRealtimeSeparation (scratch, n);

        // ── Built-in FX (reverb + echo) ──
        deckFX.process (scratch, params.revMix, params.echoMix);
    }

private:
    struct GridKey
    {
        const void* audio = nullptr; const void* refined = nullptr;
        float bpm = 0, beatOff = 0, downbeat = 0; int interp = 0, syncMode = 0;
        bool operator!= (const GridKey& o) const noexcept
        {
            return audio != o.audio || refined != o.refined || bpm != o.bpm || beatOff != o.beatOff
                || downbeat != o.downbeat || interp != o.interp || syncMode != o.syncMode;
        }
    };

    static int keyTargetToPitchClass (int choiceIndex) noexcept
    {
        return choiceIndex > 0 ? juce::jlimit (0, 11, choiceIndex - 1) : -1;
    }

    void renderRealtimeSeparation (juce::AudioBuffer<float>& deckBuffer, int n) noexcept
    {
        bool hasOfflineStems = false;
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            hasOfflineStems = hasOfflineStems || deck.getStemAudio (lane) != nullptr;

        if (hasOfflineStems || deckBuffer.getNumChannels() < 2)
            return;

        using stemdeck::stemgain::percentToLinear;
        const float gDrums  = percentToLinear (params.stemGainPercent[(size_t) DeckPlayer::drumsStem]);
        const float gBass   = percentToLinear (params.stemGainPercent[(size_t) DeckPlayer::bassStem]);
        const float gMusic  = percentToLinear (params.stemGainPercent[(size_t) DeckPlayer::musicStem]);
        const float gVocals = percentToLinear (params.stemGainPercent[(size_t) DeckPlayer::vocalsStem]);
        const bool anyStemMoved = (gDrums  < 0.999f || gDrums  > 1.001f)
                               || (gBass   < 0.999f || gBass   > 1.001f)
                               || (gMusic  < 0.999f || gMusic  > 1.001f)
                               || (gVocals < 0.999f || gVocals > 1.001f);
        bool anyBuiltInStemFx = false;
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            anyBuiltInStemFx = anyBuiltInStemFx
                || std::abs (params.stemFilt[(size_t) lane] - 0.5f) > 0.005f
                || params.stemSend[(size_t) lane] > 0.0005f;
        const bool hasStemPlugins = stemPluginProcessingRequired.load (std::memory_order_acquire);

        // Separating and immediately re-summing a unity, effect-free full mix cannot
        // change the sound. Avoid the separator entirely in the overwhelmingly common
        // case; it is one of the most expensive jobs in the live render path.
        if (! anyStemMoved && ! anyBuiltInStemFx && ! hasStemPlugins)
            return;

        const int ch = deckBuffer.getNumChannels();
        for (auto* b : { &drums, &bass, &music, &vocals })
            if (b->getNumSamples() < n)
                return; // host exceeded prepare() contract; never allocate in render

        rtss.process (deckBuffer.getReadPointer (0), deckBuffer.getReadPointer (1), n,
                      drums .getWritePointer (0), drums .getWritePointer (ch > 1 ? 1 : 0),
                      bass  .getWritePointer (0), bass  .getWritePointer (ch > 1 ? 1 : 0),
                      music .getWritePointer (0), music .getWritePointer (ch > 1 ? 1 : 0),
                      vocals.getWritePointer (0), vocals.getWritePointer (ch > 1 ? 1 : 0));
        ++realtimeSeparationBlocks;

        juce::AudioBuffer<float>* stemBufs[4] = { &drums, &bass, &music, &vocals };
        bool anyStemFx = false;
        for (int s = 0; s < 4; ++s)
        {
            const int lane = DeckPlayer::drumsStem + s;
            const float filt = params.stemFilt[(size_t) lane];
            const float send = params.stemSend[(size_t) lane];
            if (std::abs (filt - 0.5f) > 0.005f || send > 0.0005f)
            {
                anyStemFx = true;
                juce::AudioBuffer<float> view (stemBufs[s]->getArrayOfWritePointers(), ch, n);
                stemFx[(size_t) s].process (view, filt, send);
            }
        }

        // Per-stem plugin chains, then align them all to the slowest one.
        if (hasStemPlugins && onStemFx)
        {
            for (int s = 0; s < 4; ++s)
            {
                juce::AudioBuffer<float> view (stemBufs[s]->getArrayOfWritePointers(), ch, n);
                onStemFx (s, view);
            }
            anyStemFx = true;   // buffers may have changed even at unity gain
        }
        applyStemCompensation (stemBufs, ch, n);

        if (anyStemMoved || anyStemFx)
        {
            // Re-mix with each stem's own gain ONLY — deck.process already applied the
            // full-mix gain to the signal these stems were split from (avoid gFull^2).
            deckBuffer.clear (0, n);
            for (int c = 0; c < ch; ++c)
            {
                deckBuffer.addFrom (c, 0, drums,  c, 0, n, gDrums);
                deckBuffer.addFrom (c, 0, bass,   c, 0, n, gBass);
                deckBuffer.addFrom (c, 0, music,  c, 0, n, gMusic);
                deckBuffer.addFrom (c, 0, vocals, c, 0, n, gVocals);
            }
        }
    }

    DeckPlayer               deck;
    DeckFX                   deckFX;
    RealTimeStemSeparator    rtss;
    // Set once by the host; called on the AUDIO thread for each stem. std::function
    // dispatch does not allocate, and this mirrors how Channel::onPostBody already
    // reaches the host's plugin graph.
public:
    std::function<void (int stemIndex, juce::AudioBuffer<float>&)> onStemFx;
    std::function<void (juce::AudioBuffer<float>&, int numSamples)> onPreFaderCapture;

    // Message thread: publish each stem chain's reported latency.
    void setStemChainLatency (int stemIndex, int samples) noexcept
    {
        if (juce::isPositiveAndBelow (stemIndex, 4))
            stemChainLatency[(size_t) stemIndex].store (juce::jmax (0, samples), std::memory_order_relaxed);
    }
    int getMaxStemChainLatency() const noexcept
    {
        int m = 0;
        for (const auto& a : stemChainLatency) m = juce::jmax (m, a.load (std::memory_order_relaxed));
        return m;
    }

    // Test seam: the phase-alignment contract is the whole reason stem FX is safe, so
    // it has to be assertable without standing up a full deck + separator + transport.
    void testApplyStemCompensation (juce::AudioBuffer<float>* stems[4], int ch, int n) noexcept
    { applyStemCompensation (stems, ch, n); }

private:
    // Delay every stem by (maxLatency - itsOwnLatency) so they re-sum phase-aligned.
    void applyStemCompensation (juce::AudioBuffer<float>* stems[4], int ch, int n) noexcept
    {
        int want[4], maxLat = 0;
        for (int s = 0; s < 4; ++s)
        {
            want[s] = stemChainLatency[(size_t) s].load (std::memory_order_relaxed);
            maxLat = juce::jmax (maxLat, want[s]);
        }
        if (maxLat <= 0) return;   // nothing hosted on any stem: no work, no delay

        for (int s = 0; s < 4; ++s)
        {
            const int delay = juce::jlimit (0, stemDelayCapacity - 1, maxLat - want[s]);
            auto& line = stemDelayLine[(size_t) s];

            // A changed delay would otherwise read stale samples and click; clearing is
            // a one-block mute rather than a glitch, and only happens when a plugin is
            // added or removed, never during steady playback.
            if (stemDelayApplied[(size_t) s] != delay)
            { line.clear(); stemDelayWrite[(size_t) s] = 0; stemDelayApplied[(size_t) s] = delay; }

            if (delay == 0) continue;

            auto* buf = stems[s];
            int w = stemDelayWrite[(size_t) s];
            for (int c = 0; c < ch && c < line.getNumChannels(); ++c)
            {
                auto* io = buf->getWritePointer (c);
                auto* dl = line.getWritePointer (c);
                int wi = w;
                for (int i = 0; i < n; ++i)
                {
                    const int ri = (wi - delay + stemDelayCapacity) % stemDelayCapacity;
                    const float out = dl[ri];
                    dl[wi] = io[i];
                    io[i]  = out;
                    wi = (wi + 1) % stemDelayCapacity;
                }
                if (c == ch - 1 || c == line.getNumChannels() - 1) w = wi;
            }
            stemDelayWrite[(size_t) s] = w;
        }
    }

    std::array<MasterFx, 4>  stemFx;      // per-stem filter + send (drums/bass/music/vocals)

    // ── Per-stem plugin FX + its delay compensation ──────────────────────────
    // The four stems are split from ONE source and summed straight back together, so
    // unequal chain latency does not merely delay the deck — it comb-filters it. Every
    // stem is therefore delayed to the deck's slowest stem before the sum.
    std::array<juce::AudioBuffer<float>, 4> stemDelayLine;   // pre-allocated in prepare()
    std::array<int, 4> stemDelayWrite { 0, 0, 0, 0 };
    std::array<std::atomic<int>, 4> stemChainLatency {};     // message thread -> audio
    std::atomic<bool> stemPluginProcessingRequired { false }; // message thread -> audio
    std::array<int, 4> stemDelayApplied { -1, -1, -1, -1 };  // last-applied, to detect change
    int stemDelayCapacity = 0;
    WarpEngine               warp;
    juce::AudioBuffer<float> drums, bass, music, vocals;   // RTSS scratch
    juce::AudioBuffer<float> preFaderScratch;               // clean take source tap

    const TrackAnalysis fallbackAnalysis {};   // referenced when nothing is loaded
    BeatSyncEngine::TrackBeatGrid cachedGrid;
    GridKey cachedKey;

public:
    // Message thread: build the grid for these inputs and publish it, so the audio
    // thread finds it ready instead of building it mid-render.
    struct PublishedGrid { GridKey key; BeatSyncEngine::TrackBeatGrid grid; };

    void publishGridForAnalysis (const TrackAnalysis& analysis,
                                 const void* audioPtr, const void* refinedPtr,
                                 float bpm, float beatOffset, float downbeat,
                                 int tempoInterp, int syncMode)
    {
        auto pg = std::make_shared<PublishedGrid>();
        pg->key  = GridKey { audioPtr, refinedPtr, bpm, beatOffset, downbeat, tempoInterp, syncMode };
        pg->grid = BeatSyncEngine::makeGrid (analysis, bpm, beatOffset, downbeat,
                                             BeatSyncEngine::tempoInterpretationFromChoice (tempoInterp),
                                             BeatSyncEngine::syncModeFromChoice (syncMode));
        std::atomic_store (&publishedGrid, std::shared_ptr<const PublishedGrid> (std::move (pg)));
    }

private:
    std::shared_ptr<const PublishedGrid> publishedGrid;   // message thread publishes
    std::shared_ptr<const PublishedGrid> heldGrid;        // audio thread holds (no copy)
    bool    gridDirty = true;
    double  preparedRate = 44100.0;
    int     preparedChannels = 2;
    std::uint64_t realtimeSeparationBlocks = 0; // audio-thread-owned test/telemetry seam

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackDeckBody)
};
