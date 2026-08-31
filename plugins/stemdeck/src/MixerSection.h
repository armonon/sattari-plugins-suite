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
        const Frame next { juce::jlimit (0.0f, 1.0f, left),
                           juce::jlimit (0.0f, 1.0f, right) };
        const bool hasSignal = next.l > 0.0001f || next.r > 0.0001f;
        if (! hasSignal)
        {
            if (! historyHasSignal)
                return;

            // Audio has stopped. Clear the scope once instead of shifting 128
            // identical silent frames (and repainting the transparent console)
            // for more than a minute at the low-power idle refresh rate.
            history.fill ({});
            writePos = 0;
            historyHasSignal = false;
            repaint();
            return;
        }

        historyHasSignal = true;
        history[(size_t) writePos] = next;
        writePos = (writePos + 1) % (int) history.size();
        repaint();
    }

private:
    static constexpr int kHistory = 128;
    struct Frame { float l = 0.0f, r = 0.0f; };
    std::array<Frame, kHistory> history {};
    int writePos = 0;
    bool historyHasSignal = false;
};
