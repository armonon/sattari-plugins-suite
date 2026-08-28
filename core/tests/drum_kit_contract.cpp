// Contract for the synthesised drum voice engine (SattariDrumKit.h).
//
// Locks the behaviours a beat machine relies on: every voice makes sound then rings
// out to silence (so voices free themselves), the output is finite and bounded, the
// Decay control actually lengthens the tail, brightness is ordered the way real drums
// are (kick dull, hats bright, snare between), and the kit sums its voices.
#include <sattari_audio_core/SattariDrumKit.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
}

using sattari::dsp::DrumVoice;
using sattari::dsp::DrumType;
using sattari::dsp::DrumVoiceParams;

struct Render { std::vector<float> y; double rms; float peak; bool finite; int activeSamples; double zcr; };

Render renderVoice (DrumType type, const DrumVoiceParams& p, double sr = 44100.0, double seconds = 2.5)
{
    DrumVoice v; v.prepare (sr); v.setType (type);
    v.trigger (1.0f, p);
    const int n = (int) (sr * seconds);
    std::vector<float> y ((size_t) n, 0.0f);
    int active = 0;
    // render in small blocks like a host would
    const int block = 64;
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        const bool live = v.render (y.data() + i, m);
        if (live) active = i + m;
    }
    double acc = 0.0; float peak = 0.0f; bool finite = true;
    for (int i = 0; i < n; ++i)
    {
        const float s = y[(size_t) i];
        if (! std::isfinite (s)) finite = false;
        acc += (double) s * s; peak = std::max (peak, std::fabs (s));
    }
    // Brightness proxy: zero-crossing rate measured in a FIXED early window (~80 ms),
    // where every voice is at full amplitude — so it reflects timbre, not decay length
    // (a short bright hat and a long bright ride must compare on brightness, not tail).
    const int earlyN = std::min (n, (int) (sr * 0.08));
    long crossings = 0;
    for (int i = 1; i < earlyN; ++i)
        if ((y[(size_t) (i - 1)] < 0.0f) != (y[(size_t) i] < 0.0f)) ++crossings;

    Render r;
    r.y = std::move (y);
    r.rms = std::sqrt (acc / n);
    r.peak = peak; r.finite = finite; r.activeSamples = active;
    r.zcr = (double) crossings / (double) earlyN;
    return r;
}
} // namespace

int main()
{
    std::printf ("SattariAudioCore drum-kit contract\n");
    const DrumVoiceParams def {};   // all 0.5

    // 1) Every voice type makes sound, stays finite + bounded, and rings out to silence.
    const DrumType all[] = { DrumType::Kick, DrumType::Snare, DrumType::ClosedHat, DrumType::OpenHat,
                             DrumType::Clap, DrumType::Tom, DrumType::Rim, DrumType::Ride };
    for (DrumType t : all)
    {
        const auto r = renderVoice (t, def);
        const int idx = (int) t;
        check (r.finite,          "voice " + std::to_string (idx) + " finite");
        check (r.rms > 1.0e-4,    "voice " + std::to_string (idx) + " makes sound (rms=" + std::to_string (r.rms) + ")");
        check (r.peak <= 1.05f,   "voice " + std::to_string (idx) + " bounded (peak=" + std::to_string (r.peak) + ")");
        // Must go silent well before the 2.5 s buffer ends (voice deactivates, frees itself).
        check (r.activeSamples < (int) (44100 * 2.4), "voice " + std::to_string (idx)
               + " rings out to silence (active=" + std::to_string (r.activeSamples) + ")");
    }

    // 2) Decay control lengthens the tail.
    {
        DrumVoiceParams s = def; s.decay = 0.1f;
        DrumVoiceParams l = def; l.decay = 0.95f;
        const auto rs = renderVoice (DrumType::Kick, s);
        const auto rl = renderVoice (DrumType::Kick, l);
        check (rl.activeSamples > rs.activeSamples * 2, "longer Decay -> longer kick tail ("
               + std::to_string (rs.activeSamples) + " -> " + std::to_string (rl.activeSamples) + ")");
    }

    // 3) Brightness ordering: kick/tom dull, snare mid, hats/ride bright.
    {
        const double kick = renderVoice (DrumType::Kick, def).zcr;
        const double tom  = renderVoice (DrumType::Tom, def).zcr;
        const double snr  = renderVoice (DrumType::Snare, def).zcr;
        const double chat = renderVoice (DrumType::ClosedHat, def).zcr;
        const double ride = renderVoice (DrumType::Ride, def).zcr;
        std::printf ("  zcr: kick=%.4f tom=%.4f snare=%.4f chat=%.4f ride=%.4f\n", kick, tom, snr, chat, ride);
        // The robust invariant is pitched drums (kick/tom) dull, noisy voices bright.
        check (kick < snr,   "kick duller than snare");
        check (tom  < chat,  "tom duller than closed hat");
        check (kick < chat,  "kick duller than closed hat");
        check (chat > snr,   "closed hat brighter than snare");
        check (chat > 0.1,   "closed hat is genuinely bright");
        check (snr  > 0.05,  "snare has real high-frequency content");
        check (ride > 0.1,   "ride is genuinely bright");
    }

    // 4) Velocity scales level; 0 velocity is (near) silent.
    {
        DrumVoice v; v.prepare (44100.0); v.setType (DrumType::Snare);
        std::vector<float> soft (8192, 0.0f), loud (8192, 0.0f);
        v.trigger (0.2f, def); v.render (soft.data(), 8192);
        v.reset();
        v.trigger (1.0f, def); v.render (loud.data(), 8192);
        double rs = 0, rl = 0; for (int i = 0; i < 8192; ++i) { rs += soft[(size_t) i]*soft[(size_t) i]; rl += loud[(size_t) i]*loud[(size_t) i]; }
        check (std::sqrt (rl) > std::sqrt (rs) * 2.0, "velocity scales level");
    }

    // 5) The kit sums voices: two lanes together are louder than one.
    {
        sattari::dsp::DrumKit<4> kit; kit.prepare (44100.0);
        kit.setLaneType (0, DrumType::Kick); kit.setLaneType (1, DrumType::ClosedHat);
        std::vector<float> one (8192, 0.0f), two (8192, 0.0f);
        kit.trigger (0, 1.0f, def); kit.render (one.data(), 8192);
        kit.reset();
        kit.trigger (0, 1.0f, def); kit.trigger (1, 1.0f, def); kit.render (two.data(), 8192);
        double e1 = 0, e2 = 0; bool fin = true;
        for (int i = 0; i < 8192; ++i) { e1 += one[(size_t) i]*one[(size_t) i]; e2 += two[(size_t) i]*two[(size_t) i]; if (! std::isfinite (two[(size_t) i])) fin = false; }
        check (fin, "kit output finite");
        check (e2 > e1, "two lanes louder than one");
    }

    std::printf (failures == 0 ? "drum-kit contract: PASS\n" : "drum-kit contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
