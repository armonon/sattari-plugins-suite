#pragma once

// =============================================================================
//  Sattari Granulator — immutable source-sample model
// -----------------------------------------------------------------------------
//  A SourceSample is the decoded audio the engine granulates. It is built ONCE
//  on the message thread (file load, or a pure reverse/normalize/trim/mono-fold
//  rebuild) and published to the audio thread via a std::shared_ptr<const
//  SourceSample> using the C++17 std::atomic_store / std::atomic_load free
//  functions on the shared_ptr. The engine only ever *borrows* a raw view of the
//  buffer for the duration of a block (it holds a block-local shared_ptr copy so
//  the buffer stays alive under in-flight grains even if the processor swaps in a
//  new source mid-block).
//
//  IMMUTABILITY CONTRACT: once a SourceSample is built it is never mutated. The
//  cheap, automatable playback offsets (start / end / loop / reverse-as-negative-
//  increment) are NOT baked in here — they are APVTS parameters read live by the
//  engine. Only the destructive edits (reverse-buffer, normalize, trim, mono-fold,
//  capture) produce a brand-new SourceSample; the old one's last reference drops
//  on the message thread, never on the audio thread.
//
//  The engine's own header re-declares an identical POD (sattari::dsp::
//  SourceSample) so the DSP layer has no dependency on this plugin-local file;
//  this struct is the processor/editor-facing definition and matches it field
//  for field. Keep the two in sync.
// =============================================================================

#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace sattari::granulator
{

struct SourceSample
{
    juce::AudioBuffer<float> audio;          // decoded PCM, 1..2 channels
    double   sourceSampleRate = 48000.0;     // sample rate the file was decoded at
    int      lengthSamples    = 0;           // audio.getNumSamples()
    int      rootNote         = 60;          // MIDI note the sample is tuned to (C4)
    uint32_t version          = 0;           // bumped on every rebuild; engine uses it
                                             // to invalidate grains reading an old buffer
};

} // namespace sattari::granulator
