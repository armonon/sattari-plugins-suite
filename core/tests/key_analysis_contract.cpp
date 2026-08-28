// Contract for KeyAnalysisEngine, the shared key-analysis router used by the
// suite. This target deliberately builds without ONNX: a neural model is an
// optional enhancement, and the chroma fallback must remain complete by itself.
#include <sattari_audio_core/SattariKeyAnalysis.h>

#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace sattari::dsp;

namespace
{
int failures = 0;

void check (const char* what, bool ok)
{
    if (! ok)
    {
        std::printf ("  FAIL %s\n", what);
        ++failures;
    }
}

bool sameBits (double a, double b) noexcept
{
    return std::memcmp (&a, &b, sizeof (a)) == 0;
}

std::array<double, 12> rotatedProfile (const double* profile, int root)
{
    std::array<double, 12> chroma {};
    for (int i = 0; i < 12; ++i)
        chroma[(size_t) ((i + root) % 12)] = profile[i];
    return chroma;
}

void checkSameDecision (const char* what,
                        const KeyAnalysisResult& actual,
                        const KeyResult& expected)
{
    check (what, actual.root == expected.root
                 && actual.minor == expected.minor
                 && sameBits (actual.confidence, expected.confidence)
                 && actual.valid == expected.valid
                 && actual.source == (expected.valid ? KeyAnalysisSource::Chroma
                                                     : KeyAnalysisSource::None));
}
} // namespace

int main()
{
    KeyAnalysisEngine engine;

    // The default is the frozen suite behaviour: Aarden, chroma-only. Lock it
    // against detectKey() across every transposition and both modes, including
    // the confidence bit pattern used by publication gates.
    const KeyAnalysisConfig defaults {};
    check ("default profile is Aarden", defaults.profile == KeyProfile::Aarden);
    check ("neural path is opt-in", ! defaults.useNeural);
    for (int root = 0; root < 12; ++root)
    {
        const auto major = rotatedProfile (detail::kAardenMajor, root);
        const auto minor = rotatedProfile (detail::kAardenMinor, root);
        const auto majorPrimary = engine.analyseChroma (major.data());
        const auto minorPrimary = engine.analyseChroma (minor.data());
        checkSameDecision ("default major is byte-equivalent to detectKey",
                           majorPrimary, detectKey (major.data()));
        checkSameDecision ("default minor is byte-equivalent to detectKey",
                           minorPrimary, detectKey (minor.data()));
        const auto majorAlternative = engine.analyseAlternativeChroma (
            major.data(), majorPrimary);
        const auto minorAlternative = engine.analyseAlternativeChroma (
            minor.data(), minorPrimary);
        check ("major alternative is a distinct ranked key",
               majorAlternative.valid
                   && (majorAlternative.root != majorPrimary.root
                       || majorAlternative.minor != majorPrimary.minor));
        check ("minor alternative is a distinct ranked key",
               minorAlternative.valid
                   && (minorAlternative.root != minorPrimary.root
                       || minorAlternative.minor != minorPrimary.minor));
    }

    // Runtime selection must route every public profile through the same
    // decision core. This prevents a consumer-specific config from silently
    // selecting the wrong pair of profile tables.
    struct ProfileCase
    {
        KeyProfile id;
        const double* major;
        const double* minor;
    };
    const ProfileCase profiles[] = {
        { KeyProfile::Krumhansl, detail::kKrumhanslMajor, detail::kKrumhanslMinor },
        { KeyProfile::Temperley, detail::kTemperleyMajor, detail::kTemperleyMinor },
        { KeyProfile::Aarden,    detail::kAardenMajor,    detail::kAardenMinor },
        { KeyProfile::Shaath,    detail::kShaathMajor,    detail::kShaathMinor },
        { KeyProfile::Bellman,   detail::kBellmanMajor,   detail::kBellmanMinor },
        { KeyProfile::Empirical, detail::kEmpiricalMajor, detail::kEmpiricalMinor },
    };
    for (const auto& profile : profiles)
    {
        KeyAnalysisConfig cfg;
        cfg.profile = profile.id;
        for (int root : { 0, 5, 11 })
        {
            const auto major = rotatedProfile (profile.major, root);
            const auto minor = rotatedProfile (profile.minor, root);
            checkSameDecision ("runtime major profile routes correctly",
                               engine.analyseChroma (major.data(), cfg),
                               detail::detectKeyWithProfiles (major.data(), profile.major, profile.minor));
            checkSameDecision ("runtime minor profile routes correctly",
                               engine.analyseChroma (minor.data(), cfg),
                               detail::detectKeyWithProfiles (minor.data(), profile.major, profile.minor));
        }
    }

    // Invalid input must stay invalid and must not be presented as a chroma or
    // neural verdict. Consumers use this distinction to decide whether to publish.
    const std::array<double, 12> silence {};
    const auto silent = engine.analyseChroma (silence.data());
    check ("silent chroma is invalid", ! silent.valid);
    check ("invalid chroma has no source", silent.source == KeyAnalysisSource::None);
    check ("invalid chroma confidence is zero", silent.confidence == 0.0);
    check ("invalid chroma has no alternative",
           ! engine.analyseAlternativeChroma (silence.data(), silent).valid);

    // No model is bundled into this contract. Asking for neural analysis must
    // therefore fall back to chroma, rather than removing basic key detection.
    check ("neural model starts unavailable", ! engine.neuralAvailable());
    check ("missing neural model does not load", ! engine.loadNeuralModel ("model-not-bundled.onnx"));
    check ("failed load leaves neural unavailable", ! engine.neuralAvailable());

    constexpr double sampleRate = 44100.0;
    std::vector<float> chord ((size_t) sampleRate);
    for (size_t i = 0; i < chord.size(); ++i)
    {
        const double t = (double) i / sampleRate;
        chord[i] = (float) (0.18 * std::sin (2.0 * juce::MathConstants<double>::pi * 261.625565 * t)
                          + 0.18 * std::sin (2.0 * juce::MathConstants<double>::pi * 329.627557 * t)
                          + 0.18 * std::sin (2.0 * juce::MathConstants<double>::pi * 391.995436 * t));
    }

    KeyAnalysisConfig neuralRequested;
    neuralRequested.useNeural = true;
    const auto chroma = engine.rollingChroma (chord.data(), (int) chord.size(), sampleRate);
    const auto expectedFallback = engine.analyseChroma (chroma.data(), neuralRequested);
    const auto fallback = engine.analyse (chord.data(), (int) chord.size(), sampleRate,
                                          neuralRequested);
    check ("neural-unavailable analysis remains valid", fallback.valid);
    check ("neural-unavailable analysis reports chroma source",
           fallback.source == KeyAnalysisSource::Chroma);
    check ("neural-unavailable analysis matches direct chroma",
           fallback.root == expectedFallback.root
               && fallback.minor == expectedFallback.minor
               && sameBits (fallback.confidence, expectedFallback.confidence)
               && fallback.valid == expectedFallback.valid);

    const auto repeated = engine.analyse (chord.data(), (int) chord.size(), sampleRate,
                                          neuralRequested);
    check ("whole-buffer fallback is deterministic",
           repeated.root == fallback.root
               && repeated.minor == fallback.minor
               && sameBits (repeated.confidence, fallback.confidence)
               && repeated.valid == fallback.valid
               && repeated.source == fallback.source);

    const auto empty = engine.analyse (nullptr, 0, sampleRate, neuralRequested);
    check ("empty buffer is invalid", ! empty.valid);
    check ("empty buffer has no source", empty.source == KeyAnalysisSource::None);

    if (failures == 0)
        std::printf ("key analysis engine contract: OK\n");
    else
        std::printf ("key analysis engine contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
