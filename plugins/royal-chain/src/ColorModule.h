#pragma once

#include <JuceHeader.h>
#include <vector>

// Royal Chain "Color" saturation. The tanh waveshaper is anti-aliased with
// first-order ADAA (antiderivative anti-aliasing): instead of f(x)=tanh(drive*x)
// per sample - which folds harmonics above Nyquist back down as aliasing - it
// uses the difference of the nonlinearity's antiderivative between consecutive
// samples. That suppresses aliasing meaningfully with ZERO added latency and
// only one sample of per-channel state, which suits an in-chain colour module
// better than block oversampling (no latency to report, no realloc).
class ColorModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float glow, float royal);

private:
    double sr = 44100.0;
    std::vector<float> x1;   // previous input sample per channel (ADAA state)
};
