#pragma once

#include <sattari_audio_core/SattariModule.h>

#include <array>
#include <cstdint>
#include <vector>

// =============================================================================
//  ChopModule — Sattari Chop's PLAYBACK engine, extracted so the standalone
//  plugin and a Stack layer run the SAME sampler voices.
//
//  Chop splits cleanly along the realtime boundary, and the split is the design:
//
//    * MESSAGE THREAD (stays in the shell): file I/O, onset analysis, slicing,
//      and the §3 editing gestures. None of it belongs in a module — a module
//      has no window, no file chooser and no business owning an
//      AudioFormatManager.
//    * AUDIO THREAD (this module): MIDI in, voices out.
//
//  The two halves meet at ONE object: an immutable `Source` (audio + slice
//  bounds) handed over by setSource(). That is the same shape as Glass Choir's
//  setAuxOutputs() — a per-plugin hook on the module itself, never a new virtual
//  on SattariModule, because "can be handed a sample" is not something every
//  module should have to carry.
//
//  REALTIME CONTRACT: the audio thread never allocates, locks or does file I/O.
//  It takes a reference to the published Source under a TRY-lock and reads it; if
//  the lock is contended it simply keeps playing what it already had. Every
//  published Source is retained while a voice may use it, so a buffer is never
//  freed on the audio thread. Unused buffers are reclaimed on the message thread.
// =============================================================================

namespace sattari::dsp
{

class ChopModule final : public SattariModule
{
public:
    // Lowest MIDI note; slice i is triggered by kBaseNote + i.
    static constexpr int kBaseNote = 36;          // C1
    static constexpr int kMaxVoices = 32;

    // The immutable hand-off between the two threads.
    struct Source : juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> audio;
        double sr = 44100.0;
        juce::String name;
        std::vector<std::pair<std::int64_t, std::int64_t>> slices;   // [start,end)
        using Ptr = juce::ReferenceCountedObjectPtr<Source>;
    };

    const char* moduleId()    const noexcept override { return "chop"; }
    const char* displayName() const noexcept override { return "Chop"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FloatP = juce::AudioParameterFloat;
        auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

        // Sensitivity is declared here so both shells share one parameter set, but
        // it is consumed on the MESSAGE thread: it re-thresholds the stored onset
        // curve and republishes a Source. The audio thread never reads it.
        out.push_back (std::make_unique<FloatP> (
            juce::ParameterID { prefix + "sensitivity", 1 }, "Sensitivity", unit(), 0.5f));
        out.push_back (std::make_unique<FloatP> (
            juce::ParameterID { prefix + "output", 1 }, "Output", unit(), 0.8f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() override
    {
        for (auto& v : voices) { v.active = false; v.releasing = false; v.env = 0.0f; v.pos = 0.0; v.src = nullptr; }
        voiceCounter = 0;
    }

    // MESSAGE THREAD ONLY. Publishes a new source for the audio thread to pick up,
    // and retains it so the audio thread can never drop the last reference to a
    // buffer it is reading. The retained list is owned by the message thread.
    void setSource (Source::Ptr s)
    {
        {
            const juce::SpinLock::ScopedLockType sl (sourceLock);
            if (s != nullptr && ! retained.contains (s)) retained.add (s);
            current = std::move (s);
        }
        // JUCE's move assignment swaps pointers; drop the previous current
        // source now, before checking whether only the retained list owns it.
        s = nullptr;
        // A count of one means only this list owns the source: neither current
        // nor any voice can acquire it now. Release it here, outside the publish
        // lock, rather than keeping every imported song until plug-in shutdown.
        for (int i = retained.size(); --i >= 0;)
            if (retained.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
                retained.remove (i);
    }

    Source::Ptr snapshot() const
    {
        const juce::SpinLock::ScopedLockType sl (sourceLock);
        return current;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (state == nullptr) return;
        const int numSamples = buffer.getNumSamples();
        if (numSamples <= 0) return;

        // An instrument owns its output: start silent and sum the voices in.
        buffer.clear();

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())       startVoice (m.getNoteNumber() - kBaseNote, m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) stopVoicesForNote (m.getNoteNumber());
        }

        const float outGain = 0.2f + 1.0f * juce::jlimit (0.0f, 1.0f, p ("output"));
        const int outCh = buffer.getNumChannels();

        for (auto& v : voices)
        {
            if (! v.active || v.src == nullptr) continue;
            const auto& src = v.src->audio;
            const auto bounds = v.src->slices[(std::size_t) v.slice];
            const int srcCh = src.getNumChannels();
            // Resample if the file's rate differs from the session's.
            const double rate = v.src->sr / (currentSampleRate > 0.0 ? currentSampleRate : 44100.0);

            for (int i = 0; i < numSamples; ++i)
            {
                const std::int64_t idx = (std::int64_t) v.pos;
                if (idx + 1 >= bounds.second) { v.active = false; break; }

                v.env += v.releasing ? -kEnvRate : kEnvRate;
                v.env = juce::jlimit (0.0f, 1.0f, v.env);
                if (v.releasing && v.env <= 0.0f) { v.active = false; break; }

                const float frac = (float) (v.pos - (double) idx);
                const float amp = v.gain * v.env * outGain;
                for (int ch = 0; ch < outCh; ++ch)
                {
                    const float* d = src.getReadPointer (juce::jmin (ch, srcCh - 1));
                    const float s0 = d[idx], s1 = d[idx + 1];
                    buffer.getWritePointer (ch)[i] += (s0 + frac * (s1 - s0)) * amp;
                }
                v.pos += rate;
            }
            // The retained list still owns this source; releasing a finished
            // voice cannot deallocate the buffer on the audio thread.
            if (! v.active) v.src = nullptr;
        }
    }

private:
    static constexpr float kEnvRate = 0.002f;   // ~1 ms at 48k: short ramp, no clicks

    struct Voice
    {
        Source::Ptr src;
        int    slice = -1;
        double pos = 0.0;
        float  gain = 1.0f;
        float  env = 0.0f;       // short attack/release ramp, avoids clicks
        bool   releasing = false;
        bool   active = false;
        int    note = -1;
    };

    void startVoice (int sliceIndex, int note, float velocity)
    {
        Source::Ptr s;
        {
            // TRY-lock: if the message thread is mid-publish we simply do not start
            // this voice. Blocking here would be a priority inversion on the audio
            // thread, which is worse than a dropped note.
            const juce::SpinLock::ScopedTryLockType sl (sourceLock);
            if (! sl.isLocked() || current == nullptr) return;
            s = current;
        }
        if (sliceIndex < 0 || sliceIndex >= (int) s->slices.size()) return;

        // Free voice, else steal the oldest.
        int idx = -1;
        for (int i = 0; i < kMaxVoices; ++i) if (! voices[(size_t) i].active) { idx = i; break; }
        if (idx < 0) { idx = voiceCounter % kMaxVoices; }
        voiceCounter = (voiceCounter + 1) % (kMaxVoices * 4);

        auto& v = voices[(size_t) idx];
        v.src = s;
        v.slice = sliceIndex;
        v.pos = (double) s->slices[(std::size_t) sliceIndex].first;
        v.gain = juce::jlimit (0.0f, 1.0f, velocity);
        v.env = 0.0f;
        v.releasing = false;
        v.active = true;
        v.note = note;
    }

    void stopVoicesForNote (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note)
                v.releasing = true;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double currentSampleRate = 44100.0;

    Source::Ptr current;
    juce::ReferenceCountedArray<Source> retained;   // never freed on the audio thread
    mutable juce::SpinLock sourceLock;

    std::array<Voice, kMaxVoices> voices;
    int voiceCounter = 0;
};

} // namespace sattari::dsp
