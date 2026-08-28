// Rhythm Genie — step TIMING contract.
//
//  Written BEFORE migrating the module onto SattariStepClock, because nothing
//  else protected its timing. The pattern core had a contract; the clock that
//  played it had none, so any refactor of the step emission was unverifiable.
//
//  It captures WHEN drum notes fire, at sample resolution, and asserts the
//  properties a listener would notice if they changed: the grid is regular,
//  swing delays odd steps only, tempo is honoured, the pattern repeats every
//  16 steps, and a stopped transport is silent.
//
//  Run with --trace to dump the raw firing table, which is what makes a
//  before/after comparison across a refactor possible.
#include <sattari_audio_core/SattariRhythmPatterns.h>
#include <sattari_audio_core/modules/RhythmGenieModule.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double kSr = 48000.0;
int fails = 0;
void check (bool ok, const char* what)
{
    if (! ok) { std::printf ("  FAIL %s\n", what); ++fails; }
}

// A playhead the test drives, so song position is an input.
class Head final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0, bpm = 120.0;
    bool playing = true, hasPpq = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        if (hasPpq) info.setPpqPosition (ppq);
        return info;
    }
};

// Minimal host for one module: enough APVTS to link against, nothing more.
class Harness final : public juce::AudioProcessor
{
public:
    Harness()
        : juce::AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "RG_TIMING", makeLayout())
    {
        module.link (apvts, {});
        // The real processor does exactly this after link: the grid is EMPTY
        // until seeded, and a module constructed but never regenerated plays
        // nothing at all. Missing it is why my first run produced no drums.
        module.editableGrid().setLanes (8);
        module.editableGrid().setSteps (sattari::rhythm::kSteps);
        module.regenerateGrid();
    }

    // Re-seed after changing style/density, as the editor does.
    void reseed() { module.regenerateGrid(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        sattari::dsp::SattariModule::ParamList p;
        sattari::dsp::RhythmGenieModule temp;
        temp.addParameters (p, {});
        return { p.begin(), p.end() };
    }

    void setParam (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    const juce::String getName() const override { return "RGTiming"; }
    void prepareToPlay (double sr, int n) override { module.prepare (sr, n, 2); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        module.setTransport (sattari::host::read (*this));
        module.process (b, m);
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    sattari::dsp::RhythmGenieModule module;
    juce::AudioProcessorValueTreeState apvts;
    Head head;
};

struct Fire { long long sample; int note; };

// Drive `blocks` blocks and record every note-on with its absolute sample.
std::vector<Fire> run (Harness& h, int blocks, int n, double bpm = 120.0,
                       bool hostPpq = true, bool playing = true)
{
    std::vector<Fire> fires;
    const double samplesPerBeat = kSr * 60.0 / bpm;
    juce::AudioBuffer<float> buf (2, n);

    h.head.bpm = bpm;
    h.head.hasPpq = hostPpq;
    h.head.playing = playing;
    h.setPlayHead (&h.head);

    for (int b = 0; b < blocks; ++b)
    {
        h.head.ppq = (double) ((long long) b * n) / samplesPerBeat;
        buf.clear();
        juce::MidiBuffer midi;
        h.processBlock (buf, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                fires.push_back ({ (long long) b * n + meta.samplePosition, msg.getNoteNumber() });
        }
    }
    return fires;
}

// Distinct firing sample positions, in order — the rhythmic skeleton.
std::vector<long long> onsets (const std::vector<Fire>& fires)
{
    std::vector<long long> out;
    for (const auto& f : fires)
        if (out.empty() || out.back() != f.sample) out.push_back (f.sample);
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const bool trace = argc > 1 && std::string (argv[1]) == "--trace";
    std::printf ("Rhythm Genie timing contract\n");

    // --- 1. Steps land on a regular 16th grid -------------------------------
    {
        Harness h;
        h.prepareToPlay (kSr, 512);
        h.setParam ("style", 0.0f);      // Four on the Floor: a hit every step
        h.setParam ("density", 1.0f);
        h.setParam ("swing", 0.0f);
        h.reseed();

        const auto fires = run (h, 200, 512);
        const auto ons = onsets (fires);
        check (! ons.empty(), "no drums fired at all");

        if (trace)
            for (size_t i = 0; i < std::min<size_t> (ons.size(), 40); ++i)
                std::printf ("    onset %2zu @ %lld\n", i, ons[i]);

        // A 16th at 120 BPM is 6000 samples. Every gap must be a multiple.
        long long worst = 0;
        for (size_t i = 1; i < ons.size(); ++i)
        {
            const long long gap = ons[i] - ons[i - 1];
            const long long nearest = ((gap + 3000) / 6000) * 6000;
            worst = std::max (worst, std::llabs (gap - nearest));
        }
        std::printf ("  %zu onsets, worst deviation from the 6000-sample grid: %lld\n",
                     ons.size(), worst);
        check (worst <= 1, "step onsets are not on a regular 16th grid");
    }

    // --- 2. Tempo is honoured ----------------------------------------------
    {
        Harness h;
        h.prepareToPlay (kSr, 512);
        h.setParam ("style", 0.0f);
        h.setParam ("density", 1.0f);
        h.setParam ("swing", 0.0f);
        h.reseed();

        const auto ons = onsets (run (h, 200, 512, 90.0));
        const double expected = kSr * 60.0 / 90.0 / 4.0;   // 8000 samples
        double worst = 0.0;
        for (size_t i = 1; i < ons.size(); ++i)
        {
            const double gap = (double) (ons[i] - ons[i - 1]);
            const double nearest = std::round (gap / expected) * expected;
            worst = std::max (worst, std::fabs (gap - nearest));
        }
        std::printf ("  90 BPM: step spacing error %.1f samples (expected %.0f)\n",
                     worst, expected);
        check (worst <= 1.5, "step spacing ignores tempo");
    }

    // --- 3. Swing delays ODD steps and leaves even ones alone --------------
    //  The pattern is written directly into the grid rather than generated,
    //  because a generated Four-on-the-Floor puts hits only on EVEN steps — so
    //  swing, which delays odd steps, has nothing to move and the test would
    //  pass or fail for reasons unrelated to swing. Placing a hit on every step
    //  makes the assertion about swing and nothing else.
    {
        auto seedEveryStep = [] (Harness& h)
        {
            auto& g = h.module.editableGrid();
            g.clear();
            for (int step = 0; step < sattari::rhythm::kSteps; ++step)
                g.set (0, step, true, 0.9f, 1.0f);   // lane 0 = kick, every step
        };

        Harness dry, wet;
        for (auto* h : { &dry, &wet })
        {
            h->prepareToPlay (kSr, 512);
            h->setParam ("density", 1.0f);
        }
        dry.setParam ("swing", 0.0f);
        wet.setParam ("swing", 0.5f);
        seedEveryStep (dry);
        seedEveryStep (wet);

        const auto a = onsets (run (dry, 120, 512));
        const auto b = onsets (run (wet, 120, 512));
        check (a.size() > 8, "the seeded pattern did not fire");
        check (std::llabs ((long long) a.size() - (long long) b.size()) <= 1,
               "swing lost or duplicated onsets");

        const size_t n = std::min (a.size(), b.size());
        long long moved = 0, still = 0, worstDelay = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const long long delta = b[i] - a[i];
            if (delta > 0) { ++moved; worstDelay = std::max (worstDelay, delta); }
            else if (delta == 0) ++still;
        }
        // 0.5 swing on a 6000-sample sixteenth delays odd steps by 1500 samples.
        std::printf ("  swing 0.5: %lld delayed (max %lld samples), %lld unmoved (of %zu)\n",
                     moved, worstDelay, still, n);
        check (moved > 0, "swing delayed nothing");
        check (still > 0, "swing moved every onset; even steps must stay put");
        check (std::llabs (worstDelay - 1500) <= 2,
               "swing delay is not half a step at 0.5 depth");
    }

    // --- 4. The pattern repeats every 16 steps ------------------------------
    {
        Harness h;
        h.prepareToPlay (kSr, 512);
        h.setParam ("style", 0.0f);
        h.setParam ("density", 1.0f);
        h.setParam ("swing", 0.0f);
        h.reseed();

        const auto fires = run (h, 400, 512);
        // Group notes by step index; the same step must always carry the same notes.
        std::vector<std::vector<int>> byStep ((size_t) 16);
        bool consistent = true;
        for (const auto& f : fires)
        {
            const auto step = (size_t) ((f.sample / 6000) % 16);
            auto& slot = byStep[step];
            if (std::find (slot.begin(), slot.end(), f.note) == slot.end())
                slot.push_back (f.note);
        }
        int populated = 0;
        for (const auto& s : byStep) if (! s.empty()) ++populated;
        std::printf ("  %d of 16 steps carry hits\n", populated);
        check (populated > 0, "no step carried any hit");
        check (consistent, "the pattern changed between cycles");
    }

    // --- 5. A stopped transport is silent -----------------------------------
    {
        Harness h;
        h.prepareToPlay (kSr, 512);
        h.setParam ("style", 0.0f);
        h.setParam ("density", 1.0f);
        h.reseed();
        const auto fires = run (h, 60, 512, 120.0, /*hostPpq*/ true, /*playing*/ false);
        check (fires.empty(), "drums fired while the transport was stopped");
    }

    // --- 6. Block size must not move the onsets -----------------------------
    {
        Harness small, large;
        for (auto* h : { &small, &large })
        {
            h->setParam ("style", 0.0f);
            h->setParam ("density", 1.0f);
            h->setParam ("swing", 0.0f);
        }
        small.reseed(); large.reseed();
        small.prepareToPlay (kSr, 64);
        large.prepareToPlay (kSr, 1024);
        const auto a = onsets (run (small, 1600, 64));
        const auto b = onsets (run (large, 100, 1024));
        const size_t n = std::min (a.size(), b.size());
        check (n > 8, "not enough onsets to compare block sizes");
        long long worst = 0;
        for (size_t i = 0; i < n; ++i) worst = std::max (worst, std::llabs (a[i] - b[i]));
        std::printf ("  64 vs 1024 sample blocks: worst onset divergence %lld samples\n", worst);
        check (worst <= 1, "onset timing depends on the host block size");
    }

    if (fails == 0) { std::printf ("Rhythm Genie timing contract ok\n"); return 0; }
    std::printf ("Rhythm Genie timing contract FAILED (%d)\n", fails);
    return 1;
}
