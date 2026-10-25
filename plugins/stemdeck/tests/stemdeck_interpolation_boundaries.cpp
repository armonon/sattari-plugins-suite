#include "Interpolation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <cstdlib>

static void near(float actual, float expected, const char* label) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-5f) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}
int main() {
    juce::AudioBuffer<float> audio(2, 4);
    for (int i = 0; i < 4; ++i) {
        audio.setSample(0, i, 1.0f + 2.0f * i);
        audio.setSample(1, i, 2.0f + 2.0f * i);
    }
    near(stemdeck::readLinear(audio, 0, -0.25), 1.0f, "linear negative position clamps to first sample");
    near(stemdeck::readSinc(audio, 0, -0.25), 1.0f, "sinc negative position clamps to first sample");
    for (auto read : {stemdeck::readLinear, stemdeck::readSinc}) {
        near(read(audio, 0, 1.0), 3.0f, "integer sample remains exact");
        near(read(audio, 99, 1.0), 4.0f, "channel clamps to last channel");
        near(read(audio, -99, 1.0), 3.0f, "channel clamps to first channel");
        near(read(audio, 0, 1.0e300), 7.0f, "large position clamps before integer conversion");
        near(read(audio, 0, std::numeric_limits<double>::quiet_NaN()), 0.0f, "NaN does not enter output");
        near(read(audio, 0, std::numeric_limits<double>::infinity()), 0.0f, "infinity does not enter output");
        juce::AudioBuffer<float> single(1, 1); single.setSample(0, 0, 0.75f);
        near(read(single, 0, 0.0), 0.75f, "one-sample input is not silently muted");
        juce::AudioBuffer<float> empty;
        near(read(empty, 0, 0.0), 0.0f, "empty input is silent");
        juce::AudioBuffer<float> noChannels(0, 2);
        near(read(noChannels, 0, 0.0), 0.0f, "zero-channel input is silent");
    }
    near(stemdeck::readLinear(audio, 0, 0.5), 2.0f, "valid fractional linear interpolation preserved");
    std::cout << "PASS: real-JUCE interpolation boundaries, empty/single samples, channels and nonfinite inputs\n";
}
