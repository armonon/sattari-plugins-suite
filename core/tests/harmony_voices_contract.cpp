// Contract for sattari::dsp::HarmonyVoicePlanner.
//
// The point of this engine is that it is NOT a fixed-interval transposer, so
// the contract tests the musical claims:
//   1. Intelligent mode picks notes FROM THE CHORD, not a constant interval;
//   2. when the chord changes under a held lead, the harmony note changes;
//   3. voice leading prefers small movement over leaps;
//   4. two voices never land on the same note;
//   5. roles respect their side of the lead (upper stays above);
//   6. with no chord known it falls back to the scale AND says so via a lower
//      confidence, rather than inventing harmony.
#include "sattari_audio_core/SattariHarmonyVoices.h"

#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;
void check (const char* label, bool ok, const char* detail = "")
{
    std::printf ("  %-38s %s %s\n", label, ok ? "ok" : "FAIL", detail);
    if (! ok) ++failures;
}
} // namespace

int main()
{
    using namespace sattari::dsp;
    std::printf ("harmony voices contract\n");

    HarmonyVoiceSettings s[4];
    s[0].enabled = true; s[0].role = HarmonyRole::upper;
    s[1].enabled = true; s[1].role = HarmonyRole::lower;

    const int cMajor[3] = { 0, 4, 7 };     // C E G
    const int aMinor[3] = { 9, 0, 4 };     // A C E
    const int lead = 60;                   // C4

    //  1. Intelligent picks a chord tone.
    {
        HarmonyVoicePlanner p;
        const auto plan = p.plan (lead, HarmonyMode::intelligent, s, 2, 0,
                                  sattari::harmony::ScaleId::Major, cMajor, 3, 0.9);
        const int n = plan.voices[0].midi;
        bool inChord = false;
        for (int pc : cMajor) if (((n % 12) + 12) % 12 == pc) inChord = true;
        char d[64]; std::snprintf (d, sizeof d, "(upper=%d)", n);
        check ("intelligent picks a chord tone", plan.voices[0].active && inChord, d);
        check ("upper stays above the lead", n > lead, d);
        check ("lower stays below the lead",
               plan.voices[1].active && plan.voices[1].midi < lead);
        check ("chord confidence propagates", plan.voices[0].confidence == 0.9);
    }

    //  2. A COMMON TONE is kept when the new chord still contains it. C major
    //     and A minor share E, and holding E is textbook voice leading — an
    //     earlier version of this test asserted the opposite and was wrong.
    {
        HarmonyVoicePlanner p;
        const auto a = p.plan (lead, HarmonyMode::intelligent, s, 2, 0,
                               sattari::harmony::ScaleId::Major, cMajor, 3, 0.9);
        const auto b = p.plan (lead, HarmonyMode::intelligent, s, 2, 9,
                               sattari::harmony::ScaleId::Minor, aMinor, 3, 0.9);
        char d[80]; std::snprintf (d, sizeof d, "(C:%d Am:%d)",
                                   a.voices[0].midi, b.voices[0].midi);
        check ("common tone is held across a chord change",
               a.voices[0].midi == b.voices[0].midi, d);
    }

    //  3. When the new chord does NOT contain the held note, the voice moves —
    //     and moves by the smallest step available, not a leap.
    {
        const int fMajor[3] = { 5, 9, 0 };   // F A C: no E
        HarmonyVoicePlanner p;
        const auto a = p.plan (lead, HarmonyMode::intelligent, s, 2, 0,
                               sattari::harmony::ScaleId::Major, cMajor, 3, 0.9);
        const auto b = p.plan (lead, HarmonyMode::intelligent, s, 2, 5,
                               sattari::harmony::ScaleId::Major, fMajor, 3, 0.9);
        char d[80]; std::snprintf (d, sizeof d, "(C:%d -> F:%d)",
                                   a.voices[0].midi, b.voices[0].midi);
        check ("chord change without a common tone moves",
               a.voices[0].midi != b.voices[0].midi, d);
        check ("and it moves by a step, not a leap",
               std::abs (b.voices[0].midi - a.voices[0].midi) <= 3, d);
    }

    //  4. No unison collisions across four voices.
    {
        HarmonyVoiceSettings four[4];
        four[0].enabled = true; four[0].role = HarmonyRole::upper;
        four[1].enabled = true; four[1].role = HarmonyRole::high;
        four[2].enabled = true; four[2].role = HarmonyRole::lower;
        four[3].enabled = true; four[3].role = HarmonyRole::low;
        HarmonyVoicePlanner p;
        const auto plan = p.plan (lead, HarmonyMode::intelligent, four, 4, 0,
                                  sattari::harmony::ScaleId::Major, cMajor, 3, 0.9);
        bool distinct = true;
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (plan.voices[i].active && plan.voices[j].active
                    && plan.voices[i].midi == plan.voices[j].midi) distinct = false;
        char d[96]; std::snprintf (d, sizeof d, "(%d %d %d %d)", plan.voices[0].midi,
            plan.voices[1].midi, plan.voices[2].midi, plan.voices[3].midi);
        check ("four voices are distinct notes", distinct, d);
    }

    //  5. Interval mode really is a fixed diatonic offset.
    {
        HarmonyVoiceSettings iv[1];
        iv[0].enabled = true; iv[0].intervalDegrees = 2;
        HarmonyVoicePlanner p;
        const auto a = p.plan (60, HarmonyMode::interval, iv, 1, 0, sattari::harmony::ScaleId::Major, nullptr, 0, 1.0);
        const auto b = p.plan (62, HarmonyMode::interval, iv, 1, 0, sattari::harmony::ScaleId::Major, nullptr, 0, 1.0);
        check ("interval mode tracks the lead",
               a.voices[0].midi == 64 && b.voices[0].midi == 65);   // C->E, D->F
    }

    //  6. No chord known: scale fallback, with honest lower confidence.
    {
        HarmonyVoicePlanner p;
        const auto plan = p.plan (lead, HarmonyMode::intelligent, s, 2, 0,
                                  sattari::harmony::ScaleId::Major, nullptr, 0, 0.0);
        check ("scale fallback when no chord", plan.voices[0].active);
        check ("fallback reports lower confidence", plan.voices[0].confidence < 0.8);
    }

    //  Unvoiced lead produces no harmony at all.
    {
        HarmonyVoicePlanner p;
        const auto plan = p.plan (-1, HarmonyMode::intelligent, s, 2, 0,
                                  sattari::harmony::ScaleId::Major, cMajor, 3, 0.9);
        check ("no lead -> no voices", ! plan.voices[0].active && ! plan.voices[1].active);
    }

    std::printf (failures == 0 ? "harmony voices contract ok\n"
                               : "harmony voices contract FAILED\n");
    return failures == 0 ? 0 : 1;
}
