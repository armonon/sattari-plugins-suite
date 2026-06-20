#include <cassert>
#include <string>

#include <sattari_audio_core/SattariAudioCore.h>

using namespace sattari::audio_core;

int main() {
    const KeySignature cMajor{0, MusicalMode::Major, 0.96};
    const KeySignature aMinor{9, MusicalMode::Minor, 0.94};
    const KeySignature gMajor{7, MusicalMode::Major, 0.92};
    const KeySignature dMajor{2, MusicalMode::Major, 0.90};
    const KeySignature fSharpMinor{6, MusicalMode::Minor, 0.89};
    const KeySignature eFlatMajor{3, MusicalMode::Major, 0.70};

    assert(toDisplayString(toSattariWheelCode(cMajor)) == "8M");
    assert(toDisplayString(toSattariWheelCode(aMinor)) == "8m");
    assert(toDisplayString(toSattariWheelCode(gMajor)) == "9M");
    assert(toDisplayString(toSattariWheelCode(dMajor)) == "10M");
    assert(toDisplayString(toSattariWheelCode(fSharpMinor)) == "11m");

    assert(classifyHarmonicMove(cMajor, cMajor) == HarmonicMove::PerfectLock);
    assert(classifyHarmonicMove(cMajor, aMinor) == HarmonicMove::RelativeMode);
    assert(classifyHarmonicMove(cMajor, gMajor) == HarmonicMove::SmoothSideStep);
    assert(classifyHarmonicMove(gMajor, dMajor) == HarmonicMove::SmoothSideStep);
    assert(classifyHarmonicMove(cMajor, fSharpMinor) == HarmonicMove::RiskyClash);
    assert(classifyHarmonicMove(cMajor, eFlatMajor) == HarmonicMove::RiskyClash);

    TrackAnalysis analysis;
    analysis.trackId = "owned-demo-001";
    analysis.title = "Owned Demo";
    analysis.bpm = 124.0;
    analysis.key = aMinor;
    analysis.energy0to1 = 0.73;
    analysis.energySegments.push_back({0.0, 32.0, 0.35, "intro"});
    analysis.energySegments.push_back({64.0, 32.0, 0.86, "drop"});
    analysis.cuePoints.push_back({64.0, CuePointKind::Drop, "first drop", true});
    analysis.tags["sattari_wheel"] = toDisplayString(toSattariWheelCode(analysis.key));
    analysis.tags["energy_level"] = std::to_string(energyLevel1to10(analysis.energy0to1));

    assert(analysis.tags["sattari_wheel"] == "8m");
    assert(analysis.tags["energy_level"] == "8");
    assert(analysis.energySegments.size() == 2);
    assert(analysis.cuePoints.front().snappedToPlayback);

    return 0;
}
