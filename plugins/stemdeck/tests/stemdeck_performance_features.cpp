#include "PerformanceFeatures.h"
#include "LiveLooper.h"
#include <cmath>
#include <iostream>

namespace
{
int failures = 0;
void check (bool condition, const char* label)
{
    std::cout << (condition ? "OK   " : "FAIL ") << label << '\n';
    if (! condition) ++failures;
}
}

int main()
{
    using namespace stemdeck;
    using namespace stemdeck::performance;

    const auto start = StemMorphEngine::frame (TransitionRecipe::bassSwap, 0.0f);
    const auto finish = StemMorphEngine::frame (TransitionRecipe::bassSwap, 1.0f);
    check (start.outgoing[0] == 1.0f && start.incoming[0] == 0.0f,
           "stem morph starts entirely on outgoing deck");
    check (finish.outgoing[3] == 0.0f && finish.incoming[3] == 1.0f,
           "stem morph finishes entirely on incoming deck");
    const auto bassSwap = StemMorphEngine::swapPoints (TransitionRecipe::bassSwap);
    check (bassSwap[3] < bassSwap[2] && bassSwap[2] < bassSwap[1]
           && bassSwap[1] < bassSwap[0], "bass-swap recipe stages vocals/music/bass/drums");

    const auto echo = AdaptiveFxMacro::frame (MacroProfile::echoOut, 1.0f);
    const auto bloom = AdaptiveFxMacro::frame (MacroProfile::spaceBloom, 1.0f);
    check (echo.echo > 0.9f && echo.filter < -0.8f, "echo-out macro has a decisive tail and low-pass");
    check (bloom.reverb > 0.85f && bloom.stemSend > 0.8f, "space-bloom macro opens stem sends");

    SetCandidate current { 0, 120.0f, 0, false, 0.5f };
    std::vector<SetCandidate> candidates {
        { 1, 121.0f, 7, false, 0.6f },
        { 2, 142.0f, 1, false, 0.4f },
        { 3, 119.5f, 9, true, 0.55f }
    };
    const auto ranked = SmartSetAssistant::rank (current, candidates);
    check (ranked.size() == 3 && (ranked[0] == 1 || ranked[0] == 3),
           "smart set ranks tempo/harmonic candidates before a clash");

    RehearsalInput bad;
    bad.audioCallbackHealthy = false; bad.missingMedia = true; bad.droppedBuffers = 2;
    const auto report = RehearsalCoach::assess (bad);
    check (! report.showReady() && report.score < 30, "rehearsal coach blocks an unsafe show path");
    RehearsalInput good; good.loadedDecks = good.analyzedDecks = 2; good.storedScenes = 1;
    const auto ready = RehearsalCoach::assess (good);
    check (ready.showReady() && ready.score == 100, "rehearsal coach passes a prepared show");

    EmergencyContinuity continuity;
    auto decision = continuity.update ({ true, 0.2f, 0, 0 }, true);
    check (! decision.engageSafeMode, "continuity remains transparent under healthy load");
    decision = continuity.update ({ true, 0.2f, 2, 0 }, true);
    check (decision.engageSafeMode, "continuity engages after repeated deadline misses");
    continuity.reset();
    decision = continuity.update ({ true, 0.2f, 0, 1 }, true);
    check (decision.pluginDryFallback && ! decision.engageSafeMode,
           "plugin crash falls back dry without stopping transport");

    LiveLooper looper;
    looper.prepare (8000.0, 1000, 2);
    looper.requestCapture (1);
    juce::AudioBuffer<float> block (2, 1000);
    for (int n = 0; n < 8; ++n)
    {
        for (int ch = 0; ch < 2; ++ch)
            std::fill_n (block.getWritePointer (ch), 1000, 0.25f);
        looper.process (block, (juce::int64) n * 1000, 240.0);
    }
    check (looper.hasLoop() && looper.getState() == LiveLooper::State::playing,
           "looper captures exactly one quantized bar");
    block.clear();
    looper.process (block, 8000, 240.0);
    check (std::abs (block.getSample (0, 10) - 0.25f) < 1.0e-5f,
           "looper returns captured performance audio");
    looper.requestClear(); looper.process (block, 9000, 240.0);
    check (! looper.hasLoop() && looper.getState() == LiveLooper::State::empty,
           "looper clears without callback-time buffer zeroing");

    std::cout << (failures == 0 ? "PERFORMANCE_FEATURES_OK\n" : "PERFORMANCE_FEATURES_FAIL\n");
    return failures == 0 ? 0 : 1;
}
