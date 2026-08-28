//  Sattari Audio Suite — session HUD strip.
//
//  The cross-plugin key/tempo bus is the suite's best asset and, until now, invisible.
//  This is a drop-in strip — "A minor · 128 BPM · ●" — that any plugin puts along its
//  top edge. The instant a producer opens two Sattari plugins and sees them agree on
//  the key, the ecosystem sells itself. Pure view: it owns a SessionReader (background
//  poll) and paints the current session state; no audio, no writing.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

namespace sattari::ui
{

class SessionHud : public juce::Component, private juce::Timer
{
public:
    SessionHud() { reader_.start (400); startTimerHz (8); setInterceptsMouseClicks (false, false); }
    ~SessionHud() override { reader_.stop(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (brand::kPanelRaised));
        g.fillRoundedRectangle (r, 5.0f);

        const auto s = reader_.current();
        const float pad = 8.0f;
        auto text = r.reduced (pad, 0.0f);

        // Live dot: mint + pulsing when a fresh session is on the bus, else grey.
        const float dotR = 4.0f;
        const auto dot = juce::Rectangle<float> (text.getRight() - dotR * 2.0f,
                                                 text.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour (s.valid ? juce::Colour (brand::kAccent).withAlpha (0.5f + 0.5f * pulse_)
                             : juce::Colour (brand::kTextMuted).withAlpha (0.4f));
        g.fillEllipse (dot);
        text = text.withTrimmedRight (dotR * 2.0f + 6.0f);

        g.setFont (juce::Font (sattari::brand::fontOptions (12.0f, juce::Font::bold)));
        if (s.valid)
        {
            g.setColour (juce::Colour (brand::kAccent));
            juce::String str = keyName (s.root, s.scale);
            if (s.hasTempo && s.bpm > 0.0) str += juce::String::fromUTF8 ("   \xc2\xb7   ") + juce::String ((int) std::lround (s.bpm)) + " BPM";
            g.drawText (str, text, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (juce::Colour (brand::kTextMuted));
            g.drawText (juce::String::fromUTF8 ("no session key on the bus"), text, juce::Justification::centredLeft, true);
        }
    }

    static juce::String keyName (int root, int scale)
    {
        static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const juce::String r (N[((root % 12) + 12) % 12]);
        return scale == 2 ? r + " chromatic" : (scale == 1 ? r + " minor" : r + " major");
    }

private:
    void timerCallback() override
    {
        pulse_ = 0.5f + 0.5f * std::sin (phase_);
        phase_ += 0.6f;
        const auto s = reader_.current();
        if (s.valid != lastValid_ || s.root != lastRoot_ || s.scale != lastScale_
            || (int) s.bpm != lastBpm_ || s.valid)
        {
            lastValid_ = s.valid; lastRoot_ = s.root; lastScale_ = s.scale; lastBpm_ = (int) s.bpm;
            repaint();
        }
    }

    sattari::xplugin::SessionReader reader_;
    float pulse_ = 0.0f, phase_ = 0.0f;
    bool  lastValid_ = false;
    int   lastRoot_ = -1, lastScale_ = -1, lastBpm_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionHud)
};

} // namespace sattari::ui
