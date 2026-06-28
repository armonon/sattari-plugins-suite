#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
 * Built-in synth + MIDI scheduler for the per-lane piano-roll clips.
 *
 * One small polyphonic sine synth per editable lane, so each lane's MIDI can be
 * scaled by its own gain (independent of note velocity) and summed into the
 * output. The editor pushes a flat lane-tagged note list via setNotes(); per-lane
 * gain is a live atomic. RT-safe schedule swap via a brief SpinLock.
 */
class MidiClipPlayer
{
public:
    static constexpr int numLanes = 5;   // matches ArrangementRecorderView::numEditableLanes

    struct SourceNote { int lane = 0; int pitch = 60; double startBeats = 0.0; double lengthBeats = 1.0; int velocity = 100; };

    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (auto& s : synths)
        {
            s.setCurrentPlaybackSampleRate (sr);
            if (s.getNumSounds() == 0)
            {
                for (int i = 0; i < 16; ++i) s.addVoice (new Voice());
                s.addSound (new Sound());
            }
        }
        for (auto& g : laneGain) g.store (1.0f, std::memory_order_relaxed);
        scratch.setSize (2, juce::jmax (16, blockSize));
    }

    void setLaneGain (int lane, float g) noexcept
    {
        if (juce::isPositiveAndBelow (lane, numLanes)) laneGain[(size_t) lane].store (g, std::memory_order_relaxed);
    }

    // Message thread. Safe any time (brief locked pointer swap per lane).
    void setNotes (const std::vector<SourceNote>& notes, double bpm)
    {
        const double secsPerBeat = 60.0 / (bpm > 0.0 ? bpm : 120.0);
        std::array<std::shared_ptr<std::vector<Evt>>, numLanes> built;
        for (auto& b : built) b = std::make_shared<std::vector<Evt>>();
        for (const auto& n : notes)
        {
            const int lane = juce::jlimit (0, numLanes - 1, n.lane);
            Evt e;
            e.pitch    = n.pitch;
            e.velocity = juce::jlimit (1, 127, n.velocity);
            e.on       = (juce::int64) std::llround (n.startBeats * secsPerBeat * sr);
            e.off      = (juce::int64) std::llround ((n.startBeats + juce::jmax (0.02, n.lengthBeats)) * secsPerBeat * sr);
            built[(size_t) lane]->push_back (e);
        }
        const juce::SpinLock::ScopedLockType l (lock);
        for (int i = 0; i < numLanes; ++i) active[(size_t) i] = built[(size_t) i];
    }

    double lengthSeconds() const
    {
        std::array<std::shared_ptr<const std::vector<Evt>>, numLanes> sched;
        { const juce::SpinLock::ScopedLockType l (lock); for (int i = 0; i < numLanes; ++i) sched[(size_t) i] = active[(size_t) i]; }
        juce::int64 maxOff = 0;
        for (const auto& s : sched)
            if (s != nullptr)
                for (const auto& e : *s) maxOff = juce::jmax (maxOff, e.off);
        return (double) maxOff / sr;
    }

    // Audio thread. Renders each lane's synth (scaled by its gain) into @p out.
    void process (juce::AudioBuffer<float>& out, juce::int64 pos, int n)
    {
        std::array<std::shared_ptr<const std::vector<Evt>>, numLanes> sched;
        { const juce::SpinLock::ScopedLockType l (lock); for (int i = 0; i < numLanes; ++i) sched[(size_t) i] = active[(size_t) i]; }

        if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);
        const int outCh = out.getNumChannels();
        const int scrCh = scratch.getNumChannels();

        for (int lane = 0; lane < numLanes; ++lane)
        {
            juce::MidiBuffer mb;
            if (sched[(size_t) lane] != nullptr)
                for (const auto& e : *sched[(size_t) lane])
                {
                    if (e.on  >= pos && e.on  < pos + n) mb.addEvent (juce::MidiMessage::noteOn  (1, e.pitch, (juce::uint8) e.velocity), (int) (e.on  - pos));
                    if (e.off >= pos && e.off < pos + n) mb.addEvent (juce::MidiMessage::noteOff (1, e.pitch), (int) (e.off - pos));
                }

            for (int ch = 0; ch < scrCh; ++ch) scratch.clear (ch, 0, n);
            synths[(size_t) lane].renderNextBlock (scratch, mb, 0, n);

            const float g = laneGain[(size_t) lane].load (std::memory_order_relaxed);
            for (int ch = 0; ch < outCh; ++ch)
                out.addFrom (ch, 0, scratch, juce::jmin (ch, scrCh - 1), 0, n, g);
        }
    }

    void allNotesOff() { for (auto& s : synths) s.allNotesOff (1, false); }

private:
    struct Evt { int pitch = 0; int velocity = 0; juce::int64 on = 0; juce::int64 off = 0; };

    struct Sound : public juce::SynthesiserSound
    {
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    struct Voice : public juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            freq  = juce::MidiMessage::getMidiNoteInHertz (midiNote);
            level = juce::jlimit (0.05f, 1.0f, velocity) * 0.16f;
            env.noteOn();
        }
        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff) env.noteOff();
            else              { env.reset(); clearCurrentNote(); }
        }
        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void setCurrentPlaybackSampleRate (double newRate) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
            if (newRate > 0.0)
            {
                env.setSampleRate (newRate);
                env.setParameters ({ 0.005f, 0.09f, 0.7f, 0.13f });
            }
        }

        void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
        {
            if (! env.isActive()) return;
            const double inc = freq / getSampleRate();
            for (int i = 0; i < numSamples; ++i)
            {
                const float e = env.getNextSample();
                const float s = (float) std::sin (juce::MathConstants<double>::twoPi * phase) * level * e;
                for (int ch = out.getNumChannels(); --ch >= 0;)
                    out.addSample (ch, startSample + i, s);
                phase += inc;
                if (phase >= 1.0) phase -= 1.0;
                if (! env.isActive()) { clearCurrentNote(); break; }
            }
        }

        double phase = 0.0, freq = 440.0;
        float  level = 0.1f;
        juce::ADSR env;
    };

    double sr = 44100.0;
    std::array<juce::Synthesiser, numLanes> synths;
    std::array<std::atomic<float>, numLanes> laneGain;
    std::array<std::shared_ptr<const std::vector<Evt>>, numLanes> active;
    juce::AudioBuffer<float> scratch;
    juce::SpinLock lock;
};
