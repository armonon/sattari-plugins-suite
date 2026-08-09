#pragma once

#include <JuceHeader.h>

struct StemRemovalAdapter
{
    virtual ~StemRemovalAdapter() = default;
    virtual juce::String getName() const = 0;
    virtual bool isAvailable() const = 0;
};

struct MicInputAdapter
{
    virtual ~MicInputAdapter() = default;
    virtual void prepare (double sampleRate, int blockSize) = 0;
    virtual bool isArmed() const = 0;
};

struct PluginHostingAdapter
{
    virtual ~PluginHostingAdapter() = default;
    virtual juce::StringArray getSupportedPluginFormats() const = 0;
    virtual bool canHostPlugins() const = 0;
};

// ============================================================================
//  AI / ML integration points. See docs/ai-ml-roadmap.md. These are seams: the
//  app depends on the abstract adapter; a concrete ONNX / CoreML / hosted-service
//  implementation is dropped in behind each without touching the audio engine.
//  All heavy calls are expected to run OFF the audio thread.
// ============================================================================

// Real-time / offline neural source separation (upgrade path for the DSP
// heuristic in RealTimeStemSeparator). Fills four stem buffers from a full mix.
struct NeuralStemAdapter : StemRemovalAdapter
{
    // drums/bass/music/vocals write pointers; returns false if unavailable so the
    // caller falls back to the built-in heuristic split.
    virtual bool separate (const float* inL, const float* inR, int numSamples,
                           float* drumsL, float* drumsR, float* bassL, float* bassR,
                           float* musicL, float* musicR, float* vocalsL, float* vocalsR) = 0;
    virtual int  latencySamples() const = 0;
};

// Polyphonic audio→MIDI transcription (basic-pitch / MT3 class). Offline.
struct AudioToMidiAdapter
{
    struct Note { double startSeconds, lengthSeconds; int pitch; float velocity; };
    virtual ~AudioToMidiAdapter() = default;
    virtual bool isAvailable() const = 0;
    virtual void transcribeAsync (const juce::File& source,
                                  std::function<void (juce::Array<Note>)> onDone) = 0;
};

// Timbre transfer: hum→instrument, beatbox→drums (DDSP / RAVE class). Offline.
struct TimbreTransferAdapter
{
    virtual ~TimbreTransferAdapter() = default;
    virtual bool isAvailable() const = 0;
    virtual juce::StringArray availableTargets() const = 0;   // "cello", "drum-kit", …
    virtual void renderAsync (const juce::File& source, const juce::String& target,
                              std::function<void (juce::File rendered)> onDone) = 0;
};

// Cross-deck spectral un-masking + smart mastering. Returns parameter SUGGESTIONS
// (never audio) so the user confirms and it stays undo-able.
struct MasteringAdapter
{
    struct Suggestion { juce::String paramId; float value; juce::String reason; };
    virtual ~MasteringAdapter() = default;
    virtual bool isAvailable() const = 0;
    virtual juce::Array<Suggestion> analyse (const juce::AudioBuffer<float>& mix,
                                             double sampleRate,
                                             const juce::String& genreOrReference) = 0;
};

// Generative set building: prompt + candidate metadata → ordered track list.
// Metadata only — never sends audio. Service-backed (see librarian AI gateway).
struct SetBuilderAdapter
{
    struct Candidate { juce::String id; double bpm; juce::String key; juce::String path; };
    virtual ~SetBuilderAdapter() = default;
    virtual bool isAvailable() const = 0;
    virtual void buildAsync (const juce::String& prompt,
                             const juce::Array<Candidate>& library,
                             std::function<void (juce::StringArray orderedIds)> onDone) = 0;
};

// Library interop: Rekordbox / Traktor / Serato / OneLibrary cue+loop+grid I/O.
// Pure parsing/serialization — no ML.
struct LibraryInteropAdapter
{
    virtual ~LibraryInteropAdapter() = default;
    virtual juce::String formatName() const = 0;                 // "Rekordbox XML", …
    virtual bool importCollection (const juce::File& source) = 0;
    virtual bool exportCollection (const juce::File& dest) const = 0;
};
