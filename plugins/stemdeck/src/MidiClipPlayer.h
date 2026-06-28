#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

/**
 * Tiny built-in synth + MIDI scheduler for the piano-roll clip.
 *
 * The editor pushes a flat note list (pitch + beats) via setNotes(); process() is
 * driven from the audio thread with the arrangement playhead, emitting note on/off
 * for the current block and rendering an internal polyphonic sine synth into the
 * output. RT-safe schedule swap via a brief SpinLock (mirrors ArrangementPlayer).
 */
class MidiClipPlayer
{
public:
    struct SourceNote { int pitch = 60; double startBeats = 0.0; double lengthBeats = 1.0; int velocity = 100; };

    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        synth.setCurrentPlaybackSampleRate (sr);
        if (synth.getNumSounds() == 0)
        {
            for (int i = 0; i < 16; ++i) synth.addVoice (new Voice());
            synth.addSound (new Sound());
        }
    }

    // Message thread. Safe to call any time (brief locked pointer swap).
    void setNotes (const std::vector<SourceNote>& notes, double bpm)
    {
        const double secsPerBeat = 60.0 / (bpm > 0.0 ? bpm : 120.0);
        auto sched = std::make_shared<std::vector<Evt>>();
        sched->reserve (notes.size());
        for (const auto& n : notes)
        {
            Evt e;
            e.pitch    = n.pitch;
            e.velocity = juce::jlimit (1, 127, n.velocity);
            e.on       = (juce::int64) std::llround (n.startBeats * secsPerBeat * sr);
            e.off      = (juce::int64) std::llround ((n.startBeats + juce::jmax (0.02, n.lengthBeats)) * secsPerBeat * sr);
            sched->push_back (e);
        }
        const juce::SpinLock::ScopedLockType l (lock);
        active = sched;
    }

    double lengthSeconds() const
    {
        std::shared_ptr<const std::vector<Evt>> sched;
        { const juce::SpinLock::ScopedLockType l (lock); sched = active; }
        juce::int64 maxOff = 0;
        if (sched != nullptr)
            for (const auto& e : *sched) maxOff = juce::jmax (maxOff, e.off);
        return (double) maxOff / sr;
    }

    // Audio thread. Renders the synth for [pos, pos+n) into @p out.
    void process (juce::AudioBuffer<float>& out, juce::int64 pos, int n)
    {
        std::shared_ptr<const std::vector<Evt>> sched;
        { const juce::SpinLock::ScopedLockType l (lock); sched = active; }

        juce::MidiBuffer mb;
        if (sched != nullptr)
            for (const auto& e : *sched)
            {
                if (e.on  >= pos && e.on  < pos + n)
                    mb.addEvent (juce::MidiMessage::noteOn  (1, e.pitch, (juce::uint8) e.velocity), (int) (e.on  - pos));
                if (e.off >= pos && e.off < pos + n)
                    mb.addEvent (juce::MidiMessage::noteOff (1, e.pitch), (int) (e.off - pos));
            }
        synth.renderNextBlock (out, mb, 0, n);
    }

    void allNotesOff() { synth.allNotesOff (1, false); }

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
            if (allowTailOff) { env.noteOff(); }
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
    juce::Synthesiser synth;
    juce::SpinLock lock;
    std::shared_ptr<const std::vector<Evt>> active;
};
