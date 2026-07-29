#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include "DesignTokens.h"

namespace stemdeck
{

// Persistent, recording-first status bar. This component contains no inferred
// state: its capture badges are driven by the processor's real EventDispatcher,
// and its drop counters come from the recorder queues/audio deadline monitor.
class RecordingHealthBar final : public juce::Component,
                                 private juce::Timer
{
public:
    static constexpr int kMaxChannels = 8;

    struct ChannelState
    {
        juce::String label;
        juce::Colour accent { tokens::c (tokens::fgMuted) };
        bool armed = false;
        bool writing = false;
    };

    struct State
    {
        bool recording = false;
        double elapsedSeconds = 0.0;
        double bpm = 120.0;
        int takeNumber = 1;
        int barNumber = 1;
        int captureCoverage = 0;
        int captureTarget = 0;
        int captureEvents = 0;
        std::array<ChannelState, kMaxChannels> channels {};
        int channelCount = 0;
        double integratedLufs = -100.0;
        float limiterGrDb = 0.0f;
        double peakDb = -100.0;
        int diskMinutes = -1;
        double bufferMs = 0.0;
        int droppedBuffers = 0;
        int droppedEvents = 0;
        float cpuLoad = 0.0f;
        bool midiLinked = false;
        int countInBars = 2;
    };

    RecordingHealthBar()
    {
        setWantsKeyboardFocus (false);
        startTimerHz (30);
    }

    void setState (State next)
    {
        if (next.peakDb >= -0.1)
            clipLatched = true;
        state = std::move (next);
        repaint();
    }

    const State& getState() const noexcept { return state; }
    bool isClipLatched() const noexcept { return clipLatched; }

    std::function<void(int countInBars)> onRecordRequested;
    std::function<void()> onStopConfirmed;
    std::function<void()> onUndo;
    std::function<void()> onClearClip;

    void paint (juce::Graphics& g) override
    {
        const auto red = tokens::c (tokens::meterHot);
        const auto green = tokens::c (tokens::stemBas);
        const auto amber = tokens::c (tokens::warn);
        const auto muted = tokens::c (tokens::fgMuted);
        const auto primary = tokens::c (tokens::fgPrimary);

        auto outer = getLocalBounds().toFloat();
        g.setColour (tokens::c (tokens::bgApp));
        g.fillRoundedRectangle (outer, tokens::radiusPanel);
        g.setColour ((state.recording ? red : tokens::c (tokens::borderSubtle))
                         .withAlpha (state.recording ? 0.72f : 0.85f));
        g.drawRoundedRectangle (outer.reduced (0.5f), tokens::radiusPanel,
                                state.recording ? 1.6f : 1.0f);

        if (state.recording)
        {
            g.setColour (red.withAlpha (0.035f));
            g.fillRoundedRectangle (outer.reduced (1.0f), tokens::radiusPanel);
        }

        auto row = getLocalBounds().reduced (tokens::gap7, tokens::gap5);
        const int gap = tokens::gap6;

        auto take = row.removeFromLeft (juce::jmin (178, row.getWidth() / 5));
        row.removeFromLeft (gap);
        drawTake (g, take, red, primary, muted);

        auto capture = row.removeFromLeft (juce::jmin (188, row.getWidth() / 4));
        row.removeFromLeft (gap);
        drawCapture (g, capture, red, amber, muted);

        auto controls = row.removeFromRight (juce::jmin (126, row.getWidth() / 5));
        row.removeFromRight (gap);
        auto telemetry = row.removeFromRight (juce::jmin (224, row.getWidth() / 2));
        row.removeFromRight (gap);

        drawMeters (g, row, green, amber, red, primary, muted);
        drawTelemetry (g, telemetry, green, amber, primary, muted);
        drawControls (g, controls, red, primary, muted);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (state.recording && stopBounds.contains (e.getPosition()))
        {
            holdingStop = true;
            stopHoldStartMs = juce::Time::getMillisecondCounterHiRes();
            repaint (stopBounds);
            return;
        }

        if (! state.recording && recordBounds.contains (e.getPosition()))
        {
            if (onRecordRequested) onRecordRequested (state.countInBars);
            return;
        }

        if (countInBounds.contains (e.getPosition()))
        {
            state.countInBars = state.countInBars == 0 ? 2 : 0;
            repaint (countInBounds);
            return;
        }

        if (undoBounds.contains (e.getPosition()))
        {
            if (onUndo) onUndo();
            return;
        }

        if (clipBounds.contains (e.getPosition()) && clipLatched)
        {
            clipLatched = false;
            if (onClearClip) onClearClip();
            repaint (clipBounds);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        holdingStop = false;
        stopHoldStartMs = 0.0;
        repaint (stopBounds);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (! isMouseButtonDownAnywhere())
        {
            holdingStop = false;
            stopHoldStartMs = 0.0;
        }
    }

private:
    static constexpr double kStopHoldMs = 900.0;

    State state;
    bool clipLatched = false;
    bool holdingStop = false;
    bool stopSent = false;
    double stopHoldStartMs = 0.0;
    juce::Rectangle<int> stopBounds, recordBounds, countInBounds, undoBounds, clipBounds;

    static juce::String timeText (double seconds)
    {
        const int whole = juce::jmax (0, (int) seconds);
        const int tenths = juce::jlimit (0, 9, (int) std::floor ((seconds - whole) * 10.0));
        return juce::String (whole / 60).paddedLeft ('0', 2) + ":"
             + juce::String (whole % 60).paddedLeft ('0', 2) + "." + juce::String (tenths);
    }

    static void fillWell (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (tokens::c (tokens::bgWell));
        g.fillRoundedRectangle (r.toFloat(), tokens::radiusWell);
    }

    static void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                       juce::Colour colour, float size = tokens::tLabel,
                       juce::Justification justification = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        auto f = tokens::numeric (size, size >= tokens::tControl);
        f.setExtraKerningFactor (size <= tokens::tLabel ? tokens::trackLabel : 0.0f);
        g.setFont (f);
        g.drawText (text, r, justification, true);
    }

    void drawTake (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour red,
                   juce::Colour primary, juce::Colour muted)
    {
        fillWell (g, r);
        auto b = r.reduced (tokens::gap8, tokens::gap3);
        auto stateRow = b.removeFromTop (14);
        g.setColour (state.recording ? red : red.withAlpha (0.38f));
        g.fillEllipse ((float) stateRow.getX(), (float) stateRow.getCentreY() - 4.0f, 8.0f, 8.0f);
        stateRow.removeFromLeft (13);
        label (g, state.recording ? "RECORDING" : "READY TO RECORD", stateRow,
               state.recording ? red : muted);
        auto clock = b.removeFromTop (27);
        label (g, state.recording ? timeText (state.elapsedSeconds) : "00:00.0",
               clock, primary, 21.0f);
        label (g, "bar " + juce::String (juce::jmax (1, state.barNumber))
                  + "  \xC2\xB7  take " + juce::String (juce::jmax (1, state.takeNumber)),
               b, muted, tokens::tSmall);
    }

    void drawCapture (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour red,
                      juce::Colour amber, juce::Colour muted)
    {
        fillWell (g, r);
        auto b = r.reduced (tokens::gap8, tokens::gap3);
        auto title = b.removeFromTop (18);
        const bool complete = state.captureTarget > 0 && state.captureCoverage == state.captureTarget;
        // Coverage is the closed ParamId vocabulary, not a claim that every future
        // lifecycle event is converted. Name the metric so 28/29 stays truthful.
        label (g, state.recording ? "PARAMS WRITING" : "PARAMS ARMED", title,
               complete ? muted : amber);
        label (g, juce::String (state.captureCoverage) + "/" + juce::String (state.captureTarget),
               title.removeFromRight (42), complete ? muted : amber, tokens::tSmall,
               juce::Justification::centredRight);

        auto badges = b.removeFromTop (25);
        const int n = juce::jlimit (0, kMaxChannels, state.channelCount);
        const int w = n > 0 ? juce::jmin (22, (badges.getWidth() - juce::jmax (0, n - 1) * 4) / n) : 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& ch = state.channels[(size_t) i];
            auto cell = badges.removeFromLeft (w);
            auto badge = cell.withHeight (17);
            g.setColour ((ch.armed ? ch.accent : tokens::c (tokens::bgControl))
                             .withAlpha (state.recording ? 0.95f : 0.42f));
            g.fillRoundedRectangle (badge.toFloat(), tokens::radiusPill);
            label (g, ch.label, badge, tokens::c (tokens::bgApp), tokens::tLabel,
                   juce::Justification::centred);
            g.setColour (ch.writing ? red : tokens::c (tokens::borderSubtle));
            g.fillRoundedRectangle ((float) cell.getX(), (float) cell.getBottom() - 3.0f,
                                    (float) cell.getWidth(), 2.0f, 1.0f);
            badges.removeFromLeft (4);
        }
        const auto eventText = juce::String (state.captureEvents) + " intent events"
                             + (state.droppedEvents > 0
                                    ? "  \xC2\xB7  " + juce::String (state.droppedEvents) + " dropped"
                                    : juce::String());
        label (g, eventText, b, state.droppedEvents > 0 ? amber : muted, tokens::tCaption);
    }

    void drawMeters (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour green,
                     juce::Colour amber, juce::Colour red, juce::Colour primary,
                     juce::Colour muted)
    {
        fillWell (g, r);
        auto b = r.reduced (tokens::gap8, tokens::gap3);
        const int w = b.getWidth() / 3;
        auto lufs = b.removeFromLeft (w);
        auto limiter = b.removeFromLeft (w);
        auto clip = b;
        clipBounds = clip;
        label (g, "LUFS-I", lufs.removeFromTop (14), muted);
        label (g, state.integratedLufs > -70.0 ? juce::String (state.integratedLufs, 1) : "--",
               lufs, primary, tokens::tReadout);
        label (g, "LIMITER", limiter.removeFromTop (14), muted);
        label (g, state.limiterGrDb > 0.05f ? "-" + juce::String (state.limiterGrDb, 1) : "0.0",
               limiter, state.limiterGrDb > 0.05f ? amber : primary, tokens::tReadout);
        label (g, "CLIP", clip.removeFromTop (14), muted);
        label (g, clipLatched ? "CLIP - clear" : "clear", clip,
               clipLatched ? red : green, tokens::tControl);
    }

    void drawTelemetry (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour green,
                        juce::Colour amber, juce::Colour primary, juce::Colour muted)
    {
        fillWell (g, r);
        auto b = r.reduced (tokens::gap8, tokens::gap3);
        const int w = b.getWidth() / 4;
        auto disk = b.removeFromLeft (w);
        auto buffer = b.removeFromLeft (w + 10);
        auto cpu = b.removeFromLeft (w - 5);
        auto midi = b;
        label (g, "DISK", disk.removeFromTop (14), muted);
        const juce::String diskText = state.diskMinutes < 0 ? "--"
            : state.diskMinutes >= 60 ? juce::String (state.diskMinutes / 60) + "h "
                                     + juce::String (state.diskMinutes % 60) + "m"
                                      : juce::String (state.diskMinutes) + "m";
        label (g, diskText, disk, state.diskMinutes >= 10 ? primary : amber, tokens::tControl);
        label (g, "BUFFER", buffer.removeFromTop (14), muted);
        label (g, juce::String (state.bufferMs, 1) + "ms", buffer.removeFromTop (16),
               state.droppedBuffers == 0 ? green : amber, tokens::tControl);
        label (g, juce::String (state.droppedBuffers) + " drops", buffer,
               state.droppedBuffers == 0 ? green : amber, tokens::tCaption);
        label (g, "CPU", cpu.removeFromTop (14), muted);
        label (g, juce::String ((int) std::round (state.cpuLoad * 100.0f)) + "%", cpu,
               state.cpuLoad < 0.8f ? primary : amber, tokens::tControl);
        label (g, "MIDI", midi.removeFromTop (14), muted);
        label (g, state.midiLinked ? "linked" : "--", midi,
               state.midiLinked ? green : muted, tokens::tSmall);
    }

    void drawControls (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour red,
                       juce::Colour primary, juce::Colour muted)
    {
        auto top = r.removeFromTop (45);
        auto bottom = r;
        if (state.recording)
        {
            stopBounds = top;
            recordBounds = {};
            const double held = holdingStop
                              ? juce::Time::getMillisecondCounterHiRes() - stopHoldStartMs : 0.0;
            const float progress = (float) juce::jlimit (0.0, 1.0, held / kStopHoldMs);
            g.setColour (red.withAlpha (0.14f));
            g.fillRoundedRectangle (top.toFloat(), tokens::radiusControl);
            g.setColour (red.withAlpha (0.45f));
            g.drawRoundedRectangle (top.toFloat().reduced (0.5f), tokens::radiusControl, 1.0f);
            if (progress > 0.0f)
            {
                g.setColour (red.withAlpha (0.35f));
                g.fillRoundedRectangle (top.toFloat().withWidth (top.getWidth() * progress),
                                        tokens::radiusControl);
            }
            label (g, "STOP", top.removeFromTop (25), red, tokens::tControl,
                   juce::Justification::centred);
            label (g, "hold - keeps take", top, muted, tokens::tCaption,
                   juce::Justification::centred);
            countInBounds = {};
        }
        else
        {
            recordBounds = top;
            stopBounds = {};
            g.setColour (red.withAlpha (0.13f));
            g.fillRoundedRectangle (top.toFloat(), tokens::radiusControl);
            g.setColour (red.withAlpha (0.48f));
            g.drawRoundedRectangle (top.toFloat().reduced (0.5f), tokens::radiusControl, 1.0f);
            label (g, "RECORD", top.removeFromTop (25), red, tokens::tControl,
                   juce::Justification::centred);
            countInBounds = top;
            label (g, state.countInBars > 0 ? juce::String (state.countInBars) + "-bar count-in"
                                            : "count-in off",
                   top, muted, tokens::tCaption, juce::Justification::centred);
        }

        bottom.removeFromTop (tokens::gap3);
        undoBounds = bottom;
        g.setColour (tokens::c (tokens::bgControl));
        g.fillRoundedRectangle (bottom.toFloat(), tokens::radiusControl);
        label (g, "undo", bottom, primary, tokens::tSmall, juce::Justification::centred);
    }

    void timerCallback() override
    {
        if (! holdingStop || stopSent)
            return;
        const double held = juce::Time::getMillisecondCounterHiRes() - stopHoldStartMs;
        repaint (stopBounds);
        if (held >= kStopHoldMs)
        {
            stopSent = true;
            holdingStop = false;
            if (onStopConfirmed) onStopConfirmed();
            stopSent = false;
        }
    }
};

} // namespace stemdeck
