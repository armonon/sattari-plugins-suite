#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariBranding.h>
#include <functional>
#include <vector>

/**
 * Piano-roll editor surface for MIDI clips.
 *
 * Phase 3 (skeleton): renders a vertical piano keyboard + a bar/beat note grid and
 * draws a list of notes. Note editing + playback land in following increments.
 */
class PianoRollView final : public juce::Component
{
public:
    struct Note
    {
        int    pitch       = 60;    // MIDI note number
        double startBeats  = 0.0;   // position from the clip start, in beats
        double lengthBeats = 1.0;
        int    velocity    = 100;   // 1..127
    };

    PianoRollView() { setOpaque (true); }

    void setBpm   (double b)            { bpm  = b > 0.0 ? b : 120.0; repaint(); }
    void setBars  (int b)               { bars = juce::jmax (1, b);   repaint(); }
    void setNotes (std::vector<Note> n) { notes = std::move (n);      repaint(); }
    const std::vector<Note>& getNotes() const noexcept { return notes; }

    std::function<void()> onClose;

    static constexpr int lowPitch  = 36;   // C2
    static constexpr int highPitch = 84;   // C6
    static constexpr int numRows   = highPitch - lowPitch + 1;

    void paint (juce::Graphics& g) override
    {
        using namespace sattari::brand;
        auto b = getLocalBounds();
        g.fillAll (juce::Colour (kPanelDeep).darker (0.2f));

        // ── Title bar with a close (X) button ──
        auto title = b.removeFromTop (titleH);
        g.setColour (juce::Colour (kPanelRaised));
        g.fillRect (title);
        g.setColour (juce::Colour (kAccent));
        g.fillRect (title.removeFromLeft (3));
        g.setColour (juce::Colour (kTextPrimary));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("PIANO ROLL", title.reduced (10, 0), juce::Justification::centredLeft);
        closeRect = title.removeFromRight (30).reduced (8);
        g.setColour (juce::Colour (kTextMuted));
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getY(),
                    (float) closeRect.getRight(), (float) closeRect.getBottom(), 1.6f);
        g.drawLine ((float) closeRect.getX(), (float) closeRect.getBottom(),
                    (float) closeRect.getRight(), (float) closeRect.getY(), 1.6f);

        auto keyCol = b.removeFromLeft (keyW);
        gridArea = b;
        const int rowH = juce::jmax (4, gridArea.getHeight() / numRows);

        // ── Pitch rows + keyboard ──
        for (int i = 0; i < numRows; ++i)
        {
            const int pitch = highPitch - i;   // high pitches at the top
            const int rowY  = gridArea.getY() + i * rowH;
            const bool black = isBlackKey (pitch);

            g.setColour (black ? juce::Colour (kPanel).withAlpha (0.55f)
                               : juce::Colour (kPanelRaised).withAlpha (0.22f));
            g.fillRect (gridArea.getX(), rowY, gridArea.getWidth(), rowH);
            g.setColour (juce::Colour (kHairline).withAlpha (0.22f));
            g.drawHorizontalLine (rowY, (float) gridArea.getX(), (float) gridArea.getRight());

            auto keyR = juce::Rectangle<int> (keyCol.getX(), rowY, keyW, rowH).withTrimmedRight (1);
            g.setColour (black ? juce::Colours::black : juce::Colours::white.withAlpha (0.92f));
            g.fillRect (keyR);
            g.setColour (juce::Colour (kHairline).withAlpha (0.4f));
            g.drawHorizontalLine (rowY, (float) keyR.getX(), (float) keyR.getRight());
            if (pitch % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (juce::FontOptions (8.5f));
                g.drawText ("C" + juce::String (pitch / 12 - 1), keyR.reduced (3, 0), juce::Justification::centredRight);
            }
        }

        // ── Vertical bar/beat grid ──
        const int beats = bars * 4;
        const double pxPerBeat = (double) gridArea.getWidth() / (double) juce::jmax (1, beats);
        for (int beat = 0; beat <= beats; ++beat)
        {
            const int x = gridArea.getX() + (int) std::round ((double) beat * pxPerBeat);
            const bool barLine = (beat % 4) == 0;
            g.setColour (juce::Colour (barLine ? kTextFaint : kHairline).withAlpha (barLine ? 0.5f : 0.22f));
            g.drawVerticalLine (x, (float) gridArea.getY(), (float) gridArea.getBottom());
        }

        // ── Notes ──
        for (const auto& n : notes)
        {
            if (n.pitch < lowPitch || n.pitch > highPitch) continue;
            const int row = highPitch - n.pitch;
            const int y   = gridArea.getY() + row * rowH;
            const int x0  = gridArea.getX() + (int) std::round (n.startBeats * pxPerBeat);
            const int x1  = gridArea.getX() + (int) std::round ((n.startBeats + n.lengthBeats) * pxPerBeat);
            auto r = juce::Rectangle<int> (x0, y, juce::jmax (3, x1 - x0), rowH).reduced (1, 1).toFloat();
            const float vel = juce::jlimit (0.2f, 1.0f, (float) n.velocity / 127.0f);
            g.setColour (juce::Colour (kAccent).withAlpha (0.40f + 0.55f * vel));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (closeRect.contains (e.getPosition()) && onClose)
            onClose();
    }

private:
    static bool isBlackKey (int pitch)
    {
        switch (((pitch % 12) + 12) % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    static constexpr int titleH = 26;
    static constexpr int keyW   = 46;
    double bpm  = 120.0;
    int    bars = 8;
    std::vector<Note> notes;
    juce::Rectangle<int> closeRect, gridArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
