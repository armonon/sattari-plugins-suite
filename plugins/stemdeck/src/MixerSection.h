#pragma once

#include <JuceHeader.h>
#include <array>

// The centre-console "MIXER" box. Renders a live scrolling scope of the master
// output level (fed by the editor timer via pushLevel), not a decorative fake.
class MixerSection final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
    void resized() override {}

    // Push a new stereo master-peak frame (0..1). Called from the editor timer.
    void pushLevel (float left, float right)
    {
        history[(size_t) writePos] = { juce::jlimit (0.0f, 1.0f, left),
                                       juce::jlimit (0.0f, 1.0f, right) };
        writePos = (writePos + 1) % (int) history.size();
        repaint();
    }

private:
    static constexpr int kHistory = 128;
    struct Frame { float l = 0.0f, r = 0.0f; };
    std::array<Frame, kHistory> history {};
    int writePos = 0;
};
