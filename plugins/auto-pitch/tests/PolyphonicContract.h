#pragma once

#include "PluginProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace polytest
{
inline void param (SattariAutoPitchAudioProcessor& p, const char* id, float value)
{
    auto* v = p.parameters().getParameter (id);
    if (v == nullptr) throw std::runtime_error (id);
    v->setValueNotifyingHost (v->convertTo0to1 (value));
}
inline void setup (SattariAutoPitchAudioProcessor& p, float amount = 1)
{
    param (p, "followSongKey", 0); param (p, "autoKey", 0);
    param (p, "keyRoot", 0); param (p, "scaleMode", 0);
    param (p, "style", 0); param (p, "amount", amount);
    param (p, "polyphonic", 1); param (p, "enableCorrection", 1);
    param (p, "xposeMode", 0); param (p, "polyHarmony", 0);
    param (p, "tonalFocus", 0); param (p, "doNoHarm", 0);
    param (p, "graphTimeCorrection", 0);
}
inline double hz (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline juce::AudioBuffer<float> tone (double sr, double frequency, double seconds = 1.5, int harmonics = 1)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds));
    b.clear();
    for (int h = 1; h <= harmonics && frequency * h < sr * 0.45; ++h)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (0, i, (float) (0.1 / h * std::sin (juce::MathConstants<double>::twoPi * frequency * h * i / sr)));
    return b;
}
inline double mag (const juce::AudioBuffer<float>& b, double sr, double frequency, int channel = 0)
{
    const int start = b.getNumSamples() / 2;
    const double coefficient = 2 * std::cos (juce::MathConstants<double>::twoPi * frequency / sr);
    double s1 = 0, s2 = 0;
    for (int i = start; i < b.getNumSamples(); ++i)
    {
        const double s = b.getSample (channel, i) + coefficient * s1 - s2;
        s2 = s1; s1 = s;
    }
    return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2))
         / (b.getNumSamples() - start);
}
inline double centsPeak (const juce::AudioBuffer<float>& b, double sr, double target, int channel = 0)
{
    double best = -1, offset = 0;
    for (int c = -70; c <= 70; c += 2)
    {
        const double value = mag (b, sr, target * std::pow (2.0, c / 1200.0), channel);
        if (value > best) { best = value; offset = c; }
    }
    return offset;
}
inline juce::Component* find (juce::Component& c, const juce::String& id)
{
    if (c.getComponentID() == id) return &c;
    for (auto* child : c.getChildren()) if (auto* match = find (*child, id)) return match;
    return nullptr;
}
inline void tickUI()
{
    // Exercise the actual message loop: sleeping on the GUI thread leaves
    // parameter attachments and Timer messages queued. Calling pending timers
    // once does not dispatch those messages or advance their countdowns, so a
    // second mode change could be asserted against stale UI state.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
}
inline int run()
{
    int failures = 0, checks = 0, renders = 0;
    auto check = [&] (bool ok, const char* message)
    {
        ++checks;
        if (! ok) { ++failures; std::fprintf (stderr, "FAIL: %s\n", message); }
    };
    auto render = [&] (SattariAutoPitchAudioProcessor& p, const juce::AudioBuffer<float>& input,
                       double sr, int block)
    {
        ++renders;
        auto out = sattari::harness::renderThrough (p, input, sr, block);
        bool safe = true;
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                safe = safe && std::isfinite (out.getSample (ch, i)) && std::abs (out.getSample (ch, i)) <= 1;
        check (safe, "poly render must be finite and bounded");
        return out;
    };

    // Every possible pitch-class intersection, including empty. Check nearest
    // distance against a brute-force oracle and octave covariance.
    bool masksOK = true;
    for (int mask = 0; mask <= 4095; ++mask)
        for (int pc = 0; pc < 12; ++pc)
        {
            const double source = 60.2 + pc;
            int target = -1;
            const bool found = sattari::poly::nearestAllowedMidi (source, mask, target);
            if (mask == 0) { masksOK = masksOK && ! found; continue; }
            double distance = 100;
            for (int note = 36; note < 96; ++note)
                if ((mask & (1 << (note % 12))) != 0) distance = std::min (distance, std::abs (note - source));
            masksOK = masksOK && found && (mask & (1 << (target % 12))) != 0
                && std::abs (std::abs (target - source) - distance) < 1.0e-9;
        }
    check (masksOK, "all 4096 target masks must select the nearest legal note or explicitly pass through");

    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int block : {64, 257, 512})
        {
            SattariAutoPitchAudioProcessor p; setup (p, 0);
            juce::AudioBuffer<float> input (1, (int) (sr * 0.4)); input.clear();
            const int at = (int) (sr * 0.1); input.setSample (0, at, 0.1f);
            const auto out = render (p, input, sr, block);
            int peak = 0;
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peak))) peak = i;
            std::printf ("poly latency %.0f/%d reported=%d measured=%d\n", sr, block, p.getLatencySamples(), peak - at);
            check (peak - at == p.getLatencySamples() && peak - at == 4096, "poly PDC must match measured delay");
        }

    // Exercise actual processor scale AND exclusion mapping in all 12 roots.
    for (int root = 0; root < 12; ++root)
    {
        SattariAutoPitchAudioProcessor p; setup (p);
        param (p, "keyRoot", (float) root);
        param (p, "enabledNotes", (float) (4095 ^ (1 << ((root + 4) % 12))));
        const auto out = render (p, tone (48000, hz (64.2 + root)), 48000, 257);
        const double legal = mag (out, 48000, hz (65 + root));
        const double forbidden = mag (out, 48000, hz (63 + root));
        check (legal > 0.015 && legal > forbidden * 10, "excluded third must tune to legal fourth, not flat third");
    }

    // Empty scale/mask intersection must stay at unity, without a forbidden harmony.
    {
        SattariAutoPitchAudioProcessor p; setup (p);
        param (p, "enabledNotes", 2); param (p, "polyHarmony", 1);
        const double inputHz = hz (64.2);
        const auto out = render (p, tone (48000, inputHz), 48000, 512);
        check (mag (out, 48000, inputHz) > 0.045 && mag (out, 48000, hz (64)) < 0.003,
               "empty scale intersection must pass through instead of snapping or generating harmony");
    }

    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        for (double cents : {10.0, 20.0, 35.0})
        {
            auto input = tone (sr, hz (60 + cents / 100));
            const auto upper = tone (sr, hz (72 - cents / 100));
            input.addFrom (0, 0, upper, 0, 0, input.getNumSamples());
            SattariAutoPitchAudioProcessor p; setup (p);
            const auto out = render (p, input, sr, 257);
            const double c1 = centsPeak (out, sr, hz (60)), c2 = centsPeak (out, sr, hz (72));
            std::printf ("poly octave %.0f +/-%.0fc -> %.0f/%.0fc\n", sr, cents, c1, c2);
            check (std::abs (c1) <= 6 && std::abs (c2) <= 6
                && mag (out, sr, hz (60)) > 0.015 && mag (out, sr, hz (72)) > 0.015,
                "independent octave notes must both correct, not inherit opposing detune");
        }
        for (int note : {28, 33, 40})
        {
            SattariAutoPitchAudioProcessor p; setup (p);
            const auto out = render (p, tone (sr, hz (note + 0.35), 2), sr, 512);
            const double error = centsPeak (out, sr, hz (note));
            std::printf ("poly bass %.0f midi=%d -> %.0fc\n", sr, note, error);
            check (std::abs (error) <= 6 && mag (out, sr, hz (note)) > 0.01,
                   "E1/A1/E2 fundamentals must correct at supported test rates");
        }
    }

    // Retain a genuinely harmonic note's non-equal-tempered overtones: removing
    // all grouping would pass the sine-octave case while breaking timbre.
    {
        SattariAutoPitchAudioProcessor p; setup (p); param (p, "hiTrack", 1);
        const auto out = render (p, tone (48000, hz (60.35), 2, 6), 48000, 512);
        for (int h : {2, 3, 5})
            check (std::abs (centsPeak (out, 48000, hz (60) * h)) <= 6,
                   "corroborated upper harmonics must retain integer ratios to corrected fundamental");
    }

    // Preserve the five existing benchmark chords with rich, opposing detune.
    for (const std::vector<int>& notes : std::vector<std::vector<int>> {
             {60,64}, {60,64,67}, {60,64,67,71}, {60,72}, {60,67}})
    {
        auto input = tone (48000, hz (60), 2); input.clear();
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto note = tone (48000, hz (notes[i] + (i % 2 == 0 ? 0.35 : -0.35)), 2, 5);
            input.addFrom (0, 0, note, 0, 0, input.getNumSamples());
        }
        SattariAutoPitchAudioProcessor p; setup (p);
        const auto out = render (p, input, 48000, 512);
        double worst = 1;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const double correct = mag (out, 48000, hz (notes[i]));
            const double detuned = mag (out, 48000, hz (notes[i] + (i % 2 == 0 ? 0.35 : -0.35)));
            worst = std::min (worst, correct / std::max (1.0e-20, correct + detuned));
        }
        std::printf ("poly rich chord %d voices worst corrected/original magnitude score=%.3f\n", (int) notes.size(), worst);
        check (worst >= 0.80, "all existing rich chord cases must retain correction");
    }

    // Percussive-only input after a silent lead-in. Isolate the harmony layer
    // by subtracting otherwise-identical renders with harmony off/on.
    {
        juce::AudioBuffer<float> input (1, 72000); input.clear();
        unsigned rng = 91245;
        for (int i = 12000; i < input.getNumSamples(); ++i)
        {
            rng = 1664525u * rng + 1013904223u;
            const int within = i % 12000;
            if (within < 1000) input.setSample (0, i, 0.08f * ((float) (rng >> 8) / 8388607.5f - 1)
                * (1 - (float) within / 1000));
        }
        juce::AudioBuffer<float> out[4];
        for (int i = 0; i < 4; ++i)
        {
            SattariAutoPitchAudioProcessor p; setup (p);
            param (p, "tonalFocus", i >= 2 ? 1.0f : 0.0f);
            param (p, "polyHarmony", i % 2 == 1 ? 0.8f : 0.0f); param (p, "hiTrack", 1);
            out[i] = render (p, input, 48000, 257);
        }
        double unprotected = 0, protectedEnergy = 0;
        for (int i = 0; i < input.getNumSamples(); ++i)
        {
            const double a = out[1].getSample (0, i) - out[0].getSample (0, i);
            const double b = out[3].getSample (0, i) - out[2].getSample (0, i);
            unprotected += a * a; protectedEnergy += b * b;
        }
        std::printf ("poly percussive harmony energy protected/unprotected=%.6f\n", protectedEnergy / std::max (1.0e-20, unprotected));
        check (unprotected > 1.0e-6 && protectedEnergy < unprotected * 0.1,
               "Tonal Focus must suppress the added percussive harmony, not only main correction");
    }

    // Stereo proportionality, arbitrary callback boundaries, and state recall.
    {
        auto mono = tone (48000, hz (64.35));
        juce::AudioBuffer<float> stereo (2, mono.getNumSamples());
        stereo.copyFrom (0, 0, mono, 0, 0, mono.getNumSamples());
        stereo.copyFrom (1, 0, mono, 0, 0, mono.getNumSamples()); stereo.applyGain (1, 0, mono.getNumSamples(), 0.4f);
        SattariAutoPitchAudioProcessor a, b; setup (a); setup (b);
        juce::MemoryBlock state; a.getStateInformation (state); b.setStateInformation (state.getData(), (int) state.getSize());
        const auto x = render (a, stereo, 48000, 64), y = render (b, stereo, 48000, 257);
        double stereoError = 0, blockError = 0;
        for (int i = 4096; i < x.getNumSamples(); ++i)
        {
            stereoError = std::max (stereoError, std::abs ((double) x.getSample (1, i) - 0.4 * x.getSample (0, i)));
            blockError = std::max (blockError, std::abs ((double) x.getSample (0, i) - y.getSample (0, i)));
        }
        std::printf ("poly stereo max error=%.7g block/recall max error=%.7g\n", stereoError, blockError);
        check (stereoError < 0.001 && blockError < 0.0001, "poly must preserve proportional stereo and callback/recall invariance");
    }

    {
        SattariAutoPitchAudioProcessor p; setup (p); p.prepareToPlay (48000, 512);
        param (p, "liveMode", 1);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor()); tickUI();
        auto enabled = [&] (const char* id) { auto* c = find (*editor, id); return c != nullptr && c->isEnabled(); };
        for (const char* id : {"speed", "humanize", "flexTune", "doNoHarm"})
            check (! enabled (id), "mono-only controls must be disabled in poly mode");
        for (const char* id : {"amount", "polyHarmony", "hiTrack", "tonalFocus"})
            check (enabled (id), "supported poly controls including Amount must remain available");
        auto* latency = dynamic_cast<juce::Label*> (find (*editor, "engineLatency"));
        check (latency != nullptr && latency->getText().contains ("POLY"), "poly precedence must not be labelled ULTRA live mode");
        param (p, "polyphonic", 0); tickUI();
        for (const char* id : {"speed", "humanize", "flexTune", "doNoHarm"})
            check (enabled (id), "mono-only controls must return when poly is off");
        check (! enabled ("polyHarmony"), "poly controls must be disabled outside poly mode");
    }

    std::printf ("Polyphonic contract: %d checks, %d production renders, %d failures\n", checks, renders, failures);
    return failures == 0 ? 0 : 1;
}
} // namespace polytest
