// Key-detection accuracy check. Synthesize tonal material in known keys
// (diatonic triad progressions with harmonics) and confirm TrackAnalyzer recovers
// the tonic + mode. One case is detuned 40 cents off A=440 to exercise the
// tuning-compensation step; a couple use minor keys.
#include "TrackAnalysis.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Add a harmonic-rich note (fundamental + 3 decaying harmonics) for a span.
void addNote (juce::AudioBuffer<float>& b, int startSamp, int lenSamp, double freq, double sr, float amp)
{
    const int n = b.getNumSamples();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < lenSamp && startSamp + i < n; ++i)
        {
            const double t = (double) i / sr;
            const double env = std::exp (-t * 1.2);
            double s = 0.0;
            s += 1.00 * std::sin (2.0 * kPi * freq * t);
            s += 0.50 * std::sin (2.0 * kPi * freq * 2.0 * t);
            s += 0.30 * std::sin (2.0 * kPi * freq * 3.0 * t);
            s += 0.15 * std::sin (2.0 * kPi * freq * 4.0 * t);
            d[startSamp + i] += (float) (amp * env * s);
        }
    }
}

// Build a I–IV–V–vi style diatonic triad progression in the given key, detuned by
// `centsOffset`. tonicPc is the pitch class (0=C) of the tonic; minor flips the 3rd.
juce::AudioBuffer<float> makeProgression (int tonicPc, bool minor, double centsOffset, double sr, double durSec)
{
    const int n = (int) (sr * durSec);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const double detune = std::pow (2.0, centsOffset / 1200.0);

    auto pcFreq = [&] (int pc, int octave) {
        const int midi = 12 * (octave + 1) + pc;               // MIDI note
        return 440.0 * std::pow (2.0, (midi - 69) / 12.0) * detune;
    };

    // Scale degrees (semitone offsets from tonic) for major / natural minor.
    const int majSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int minSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* steps = minor ? minSteps : majSteps;

    // Triads built on degrees I, IV, V, vi (indices into the diatonic scale).
    const int degrees[4] = { 0, 3, 4, 5 };
    const int chordLen = (int) (sr * 1.0);
    int pos = 0; int c = 0;
    while (pos < n)
    {
        const int root = degrees[c % 4];
        for (int voice : { 0, 2, 4 })                          // root / third / fifth
        {
            const int deg = (root + voice) % 7;
            const int oct = 3 + ((root + voice) / 7);
            const int pc = (tonicPc + steps[deg]) % 12;
            addNote (b, pos, chordLen, pcFreq (pc, oct), sr, 0.16f);
        }
        // Bass root an octave down reinforces the tonic centre.
        addNote (b, pos, chordLen, pcFreq ((tonicPc + steps[root]) % 12, 2), sr, 0.20f);
        pos += chordLen; ++c;
    }
    return b;
}

const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

struct Case { int tonicPc; bool minor; double cents; const char* label; };
} // namespace

int main()
{
    const double sr = 44100.0;
    const std::vector<Case> cases = {
        { 0,  false, 0.0,   "C major"          },
        { 7,  false, 0.0,   "G major"          },
        { 9,  true,  0.0,   "A minor"          },
        { 2,  true,  0.0,   "D minor"          },
        { 5,  false, 0.0,   "F major"          },
        { 0,  false, +40.0, "C major (+40c)"   },   // exercises tuning comp.
        { 4,  true,  -35.0, "E minor (-35c)"   },
    };

    int fails = 0;
    for (const auto& c : cases)
    {
        const auto buf = makeProgression (c.tonicPc, c.minor, c.cents, sr, 12.0);
        const auto a = TrackAnalyzer::analyze (buf, sr);
        const bool tonicOk = a.keyPitchClass == c.tonicPc;
        const bool modeOk  = a.keyIsMinor == c.minor;
        const bool ok = tonicOk && modeOk;
        if (! ok) ++fails;
        std::cout << "  " << c.label << "  ->  "
                  << (a.keyPitchClass >= 0 ? names[a.keyPitchClass] : "?")
                  << (a.keyIsMinor ? " minor" : " major")
                  << "  conf=" << juce::String (a.keyConfidence, 2).toStdString()
                  << "   " << (ok ? "OK" : "FAIL") << "\n";
    }

    std::cout << (fails == 0 ? "KEY_DETECT_OK" : "KEY_DETECT_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
