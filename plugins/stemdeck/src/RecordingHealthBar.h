#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
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
    }

    void setState (State next)
    {
        auto differs = [] (double a, double b, double tolerance)
        { return std::abs (a - b) > tolerance; };
        bool changed = next.recording != state.recording
                    || differs (next.elapsedSeconds, state.elapsedSeconds, 0.09)
                    || differs (next.bpm, state.bpm, 0.01)
                    || next.takeNumber != state.takeNumber
                    || next.barNumber != state.barNumber
                    || next.captureCoverage != state.captureCoverage
                    || next.captureTarget != state.captureTarget
                    || next.captureEvents != state.captureEvents
                    || next.channelCount != state.channelCount
                    || differs (next.integratedLufs, state.integratedLufs, 0.1)
                    || differs (next.limiterGrDb, state.limiterGrDb, 0.1)
                    || differs (next.peakDb, state.peakDb, 0.5)
                    || next.diskMinutes != state.diskMinutes
                    || differs (next.bufferMs, state.bufferMs, 0.01)
                    || next.droppedBuffers != state.droppedBuffers
                    || next.droppedEvents != state.droppedEvents
                    || differs (next.cpuLoad, state.cpuLoad, 0.02)
                    || next.midiLinked != state.midiLinked
                    || next.countInBars != state.countInBars;
        for (int i = 0; ! changed && i < next.channelCount; ++i)
        {
            const auto& a = next.channels[(size_t) i];
            const auto& b = state.channels[(size_t) i];
            changed = a.label != b.label || a.accent != b.accent
                   || a.armed != b.armed || a.writing != b.writing;
        }
        const bool wasClipped = clipLatched;
        if (next.peakDb >= -0.1)
            clipLatched = true;
        changed = changed || wasClipped != clipLatched;
        state = std::move (next);
        if (changed)
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
        if (getHeight() < 70)
        {
            paintCompact (g);
            return;
        }

        const auto red = tokens::c (tokens::meterHot);
        const auto green = tokens::c (tokens::signal);
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
            startTimerHz (30);
            repaint (stopBounds);
            return;
        }

        // countInBounds is the lower part of recordBounds, so it must win hit
        // testing. The old order made the count-in toggle impossible to click:
        // every click there started recording instead.
        if (countInBounds.contains (e.getPosition()))
        {
            state.countInBars = state.countInBars == 0 ? 2 : 0;
            repaint (countInBounds);
            return;
        }

        if (! state.recording && recordBounds.contains (e.getPosition()))
        {
            if (onRecordRequested) onRecordRequested (state.countInBars);
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
        stopTimer();
        repaint (stopBounds);
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        repaint (recordBounds.getUnion (stopBounds).getUnion (undoBounds));
    }

    void mouseMove (const juce::MouseEvent&) override
    {
        repaint (recordBounds.getUnion (stopBounds).getUnion (undoBounds));
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        // Leaving the target cancels confirmation even while the mouse is held;
        // otherwise a press-drag away still stops and finalises the take.
        if (holdingStop)
        {
            holdingStop = false;
            stopHoldStartMs = 0.0;
            stopTimer();
            repaint (stopBounds);
        }
        repaint (recordBounds.getUnion (stopBounds).getUnion (undoBounds));
    }

private:
    static constexpr double kStopHoldMs = 900.0;

    State state;
    bool clipLatched = false;
    bool holdingStop = false;
    bool stopSent = false;
    double stopHoldStartMs = 0.0;
    juce::Rectangle<int> stopBounds, recordBounds, countInBounds, undoBounds, clipBounds;

    void paintCompact (juce::Graphics& g)
    {
        const auto red = tokens::c (tokens::meterHot);
        const auto teal = tokens::c (tokens::signal);
        const auto amber = tokens::c (tokens::warn);
        const auto primary = tokens::c (tokens::fgPrimary);
        const auto secondary = tokens::c (tokens::fgSecondary);
        const auto muted = tokens::c (tokens::fgMuted);

        auto outer = getLocalBounds();
        g.setColour (tokens::c (tokens::bgPanel));
        g.fillRoundedRectangle (outer.toFloat(), tokens::radiusPanel);
        g.setColour ((state.recording ? red : tokens::c (tokens::borderSubtle)).withAlpha (0.8f));
        g.drawRoundedRectangle (outer.toFloat().reduced (0.5f), tokens::radiusPanel,
                                state.recording ? 1.5f : 1.0f);

        auto row = outer.reduced (10, 6);
        auto transport = row.removeFromLeft (juce::jmin (190, row.getWidth() / 4));
        row.removeFromLeft (10);
        // The record action is the one control that must remain unmistakable in
        // the compact bar.  Give it enough width for a real primary button plus
        // a separate undo action instead of stacking an unreadable "Undo" under
        // the record face.
        auto controls = row.removeFromRight (juce::jmin (206, row.getWidth() / 3));
        row.removeFromRight (10);

        g.setColour (state.recording ? red : red.withAlpha (0.42f));
        g.fillEllipse ((float) transport.getX(), (float) transport.getCentreY() - 4.0f, 8.0f, 8.0f);
        transport.removeFromLeft (14);
        auto stateText = transport.removeFromTop (12);
        label (g, state.recording ? "RECORDING" : "CAPTURE READY", stateText,
               state.recording ? red : muted, tokens::tLabel);
        label (g, state.recording ? timeText (state.elapsedSeconds) : "00:00.0",
               transport, primary, 18.0f);

        const int cellW = juce::jmax (54, row.getWidth() / 5);
        auto metric = [&] (juce::String name, juce::String value, juce::Colour colour)
        {
            auto cell = row.removeFromLeft (juce::jmin (cellW, row.getWidth()));
            label (g, name, cell.removeFromTop (13), muted, tokens::tCaption);
            label (g, value, cell, colour, tokens::tControl);
        };
        metric ("ARMED", juce::String (state.channelCount) + " CH", secondary);
        metric ("LUFS-I", state.integratedLufs > -70.0 ? juce::String (state.integratedLufs, 1) : "--", primary);
        metric ("LIMIT", state.limiterGrDb > 0.05f ? "-" + juce::String (state.limiterGrDb, 1) : "0.0",
                state.limiterGrDb > 0.05f ? amber : primary);
        metric ("CPU", juce::String ((int) std::round (state.cpuLoad * 100.0f)) + "%",
                state.cpuLoad < 0.8f ? teal : amber);
        metric ("BUFFER", juce::String (state.bufferMs, 1) + "ms",
                state.droppedBuffers == 0 ? teal : amber);

        auto undo = controls.removeFromLeft (juce::jmin (44, controls.getWidth() / 4));
        controls.removeFromLeft (6);
        auto action = controls;

        undoBounds = undo;
        g.setColour (tokens::c (tokens::bgControl));
        g.fillRoundedRectangle (undo.toFloat(), tokens::radiusControl);
        g.setColour (tokens::c (tokens::borderSubtle));
        g.drawRoundedRectangle (undo.toFloat().reduced (0.5f), tokens::radiusControl, 1.0f);
        label (g, juce::String::charToString ((juce_wchar) 0x21B6), undo, secondary,
               16.0f, juce::Justification::centred);

        if (state.recording)
        {
            stopBounds = action;
            recordBounds = countInBounds = {};
            drawActionFace (g, action, red, true, action.contains (getMouseXYRelative()), holdingStop);
            auto content = action.reduced (10, 0);
            auto icon = content.removeFromLeft (18).withSizeKeepingCentre (14, 14);
            drawRecordGlyph (g, icon, true, juce::Colours::white);
            buttonLabel (g, "HOLD TO STOP", content, juce::Colours::white, tokens::tSmall);
        }
        else
        {
            // One cohesive control with two explicit hit zones: the large left
            // side starts capture, while the compact right chip changes count-in.
            // The old design placed count-in text on top of the record hit target.
            auto countIn = action.removeFromRight (juce::jmin (58, action.getWidth() / 3));
            recordBounds = action;
            stopBounds = {};
            countInBounds = countIn;
            drawActionFace (g, controls, red, false,
                            controls.contains (getMouseXYRelative()), false);
            auto content = recordBounds.reduced (9, 0);
            auto icon = content.removeFromLeft (18).withSizeKeepingCentre (14, 14);
            drawRecordGlyph (g, icon, false, juce::Colours::white);
            buttonLabel (g, "RECORD", content, juce::Colours::white, tokens::tSmall);

            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawVerticalLine (countInBounds.getX(), (float) countInBounds.getY() + 7.0f,
                                (float) countInBounds.getBottom() - 7.0f);
            const auto countText = state.countInBars > 0
                ? juce::String (state.countInBars) + " BAR"
                : juce::String ("OFF");
            label (g, countText, countInBounds, juce::Colours::white.withAlpha (0.88f),
                   tokens::tCaption, juce::Justification::centred);
        }
        clipBounds = {};
    }

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

    static void buttonLabel (juce::Graphics& g, const juce::String& text,
                             juce::Rectangle<int> r, juce::Colour colour, float size)
    {
        g.setColour (colour);
        g.setFont (tokens::ui (size, true));
        g.drawFittedText (text, r, juce::Justification::centred, 1);
    }

    static void drawActionFace (juce::Graphics& g, juce::Rectangle<int> bounds,
                                juce::Colour accent, bool active, bool hover, bool down)
    {
        auto r = bounds.toFloat().reduced (0.5f);
        if (! down)
        {
            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), tokens::radiusControl);
        }
        if (down)
            r = r.translated (0.0f, 1.0f).withTrimmedBottom (1.0f);

        const auto top = active ? accent.withAlpha (0.96f)
                                : accent.withAlpha (hover ? 0.82f : 0.68f);
        const auto bottom = active ? accent.withAlpha (0.70f)
                                   : accent.withAlpha (hover ? 0.52f : 0.38f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(),
                                                 bottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, tokens::radiusControl);
        g.setColour (accent.withAlpha (active ? 0.98f : hover ? 0.94f : 0.82f));
        g.drawRoundedRectangle (r.reduced (0.5f), tokens::radiusControl,
                                active || hover ? 1.35f : 1.0f);
        if (! down)
        {
            g.setColour (juce::Colours::white.withAlpha (active ? 0.18f : 0.08f));
            g.drawLine (r.getX() + tokens::radiusControl, r.getY() + 0.8f,
                        r.getRight() - tokens::radiusControl, r.getY() + 0.8f, 1.0f);
        }
    }

    static void drawRecordGlyph (juce::Graphics& g, juce::Rectangle<int> bounds,
                                 bool stop, juce::Colour colour)
    {
        const auto r = bounds.toFloat();
        g.setColour (colour);
        if (stop)
            g.fillRoundedRectangle (r.reduced (2.5f), 2.0f);
        else
        {
            g.fillEllipse (r.reduced (2.0f));
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.drawEllipse (r.reduced (2.5f), 1.0f);
        }
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
        // lifecycle event is converted.
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
            drawActionFace (g, top, red, true, top.contains (getMouseXYRelative()), holdingStop);
            if (progress > 0.0f)
            {
                auto progressTrack = top.toFloat().reduced (3.0f).removeFromBottom (3.0f);
                g.setColour (juce::Colours::white.withAlpha (0.24f));
                g.fillRoundedRectangle (progressTrack, 1.5f);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.fillRoundedRectangle (progressTrack.withWidth (progressTrack.getWidth() * progress), 1.5f);
            }
            auto titleRow = top.removeFromTop (25).reduced (10, 0);
            auto icon = titleRow.removeFromLeft (20).withSizeKeepingCentre (14, 14);
            drawRecordGlyph (g, icon, true, juce::Colours::white);
            buttonLabel (g, "Stop", titleRow, juce::Colours::white, tokens::tControl);
            label (g, "Hold to keep take", top, muted, tokens::tCaption,
                   juce::Justification::centred);
            countInBounds = {};
        }
        else
        {
            recordBounds = top;
            stopBounds = {};
            drawActionFace (g, top, red, false, top.contains (getMouseXYRelative()), false);
            auto titleRow = top.removeFromTop (25).reduced (10, 0);
            auto icon = titleRow.removeFromLeft (20).withSizeKeepingCentre (14, 14);
            drawRecordGlyph (g, icon, false, red);
            buttonLabel (g, "Record", titleRow, juce::Colours::white.withAlpha (0.96f), tokens::tControl);
            countInBounds = top;
            label (g, state.countInBars > 0 ? juce::String (state.countInBars) + "-bar count-in"
                                            : "Count-in off",
                   top, muted, tokens::tCaption, juce::Justification::centred);
        }

        bottom.removeFromTop (tokens::gap3);
        undoBounds = bottom;
        g.setColour (tokens::c (tokens::bgControl));
        g.fillRoundedRectangle (bottom.toFloat(), tokens::radiusControl);
        buttonLabel (g, "Undo", bottom, primary, tokens::tSmall);
    }

    void timerCallback() override
    {
        if (! holdingStop || stopSent)
        {
            stopTimer();
            return;
        }
        const double held = juce::Time::getMillisecondCounterHiRes() - stopHoldStartMs;
        repaint (stopBounds);
        if (held >= kStopHoldMs)
        {
            stopSent = true;
            holdingStop = false;
            stopTimer();
            if (onStopConfirmed) onStopConfirmed();
            stopSent = false;
        }
    }
};

} // namespace stemdeck
