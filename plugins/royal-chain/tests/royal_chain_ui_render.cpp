// Royal Chain — "Crown Portal" headless snapshot, metrics and bench tool.
//
// Drives the REAL processor + editor with deterministic audio, advances the
// Compass frame by frame (no message loop, no VBlank), renders PNGs through the
// same CoreGraphics path the screen uses, and gates the visual/interaction
// contracts (M0-M14). With --bench it also measures thread CPU time per frame
// (M15). Exit code is non-zero only when a metric FAILS.
//
//   RoyalChainUIRender --out <dir> [--bench]

#include "RoyalChainProcessor.h"
#include "RoyalChainEditor.h"
#include <sattari_audio_core/SattariRenderHarness.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <new>
#include <pthread.h>
#include <string>
#include <vector>
#if __APPLE__
 #include <sys/qos.h>
#endif

//==============================================================================
// Allocation counter: counts operator new on the thread that armed it only, so
// background threads (session bus poller, CoreAudio, ...) cannot pollute M11.
namespace
{
thread_local bool gAllocArmed = false;
std::atomic<long> gAllocCount { 0 };

inline void* countedAlloc (std::size_t n)
{
    if (gAllocArmed)
        gAllocCount.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n != 0 ? n : 1);
}
} // namespace

void* operator new (std::size_t n)
{
    if (void* p = countedAlloc (n))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (void* p = countedAlloc (n))
        return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n, const std::nothrow_t&) noexcept   { return countedAlloc (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return countedAlloc (n); }
void operator delete (void* p) noexcept                               { std::free (p); }
void operator delete[] (void* p) noexcept                             { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                  { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                { std::free (p); }

namespace
{
constexpr double kSr = 48000.0;
constexpr int kBlock = 400;            // two blocks per 1/60 s frame
constexpr double kTwoPi = 6.283185307179586;

double threadCpuMs()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double) ts.tv_sec * 1.0e3 + (double) ts.tv_nsec * 1.0e-6;
}

double calibrationMs()
{
    const double t0 = threadCpuMs();
    volatile float acc = 0.0f;
    for (int i = 0; i < 100000000; ++i)
        acc = acc * 0.999999f + 1.0f;
    return threadCpuMs() - t0;
}

//==============================================================================
// Deterministic test signals.
struct Signal
{
    enum class Kind { Silence, Tone, Hit, Noise, Phrase };
    Kind kind = Kind::Silence;
    double hz = 220.0, phase = 0.0, t = 0.0;
    juce::uint32 lcg = 0x1234567u;
    float lp = 0.0f;
    juce::AudioBuffer<float> phrase;
    int phrasePos = 0;

    static Signal silence()             { return {}; }
    static Signal tone (double hz)      { Signal s; s.kind = Kind::Tone; s.hz = hz; return s; }
    static Signal hit()                 { Signal s; s.kind = Kind::Hit; s.hz = 220.0; return s; }
    static Signal noise()               { Signal s; s.kind = Kind::Noise; return s; }
    static Signal vocalPhrase()
    {
        Signal s;
        s.kind = Kind::Phrase;
        sattari::harness::generateVocalLike (s.phrase, kSr, 3.0);
        return s;
    }

    float white()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return ((float) lcg / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
    }

    float toneSample()
    {
        // Partials 1, .5, .3, .15, .08 at 0.25 peak; 5.5 Hz +-15 cent vibrato; 40 ms attack.
        const double vib = std::pow (2.0, (0.15 * std::sin (kTwoPi * 5.5 * t)) / 12.0);
        phase += kTwoPi * hz * vib / kSr;
        if (phase > kTwoPi * 1000.0) phase -= kTwoPi * 1000.0;
        const double s = std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)
                       + 0.15 * std::sin (4.0 * phase) + 0.08 * std::sin (5.0 * phase);
        const double env = std::min (1.0, t / 0.040);
        return (float) (0.25 / 2.03 * s * env);
    }

    float next()
    {
        float out = 0.0f;
        switch (kind)
        {
            case Kind::Silence: break;
            case Kind::Tone: out = toneSample(); break;
            case Kind::Hit:
                out = toneSample();
                if (t >= 2.0 && t < 2.025)
                    out += 0.9f * white() * (float) std::exp (-(t - 2.0) / 0.008);
                break;
            case Kind::Noise:
            {
                const float a = 1.0f - (float) std::exp (-kTwoPi * 4000.0 / kSr);
                lp += a * (0.12f * white() - lp);
                out = lp;
                break;
            }
            case Kind::Phrase:
                out = phrasePos < phrase.getNumSamples() ? phrase.getSample (0, phrasePos++) : 0.0f;
                break;
        }
        t += 1.0 / kSr;
        return out;
    }
};

//==============================================================================
struct Rig
{
    std::unique_ptr<RoyalChainAudioProcessor> p;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    RoyalChainAudioProcessorEditor* editor = nullptr;
    VocalCrownCompass* compass = nullptr;
    juce::AudioBuffer<float> block { 2, kBlock };
    juce::MidiBuffer midi;
    bool compassFoundById = false;

    explicit Rig (int style = 0, float tune = 0.6f)
    {
        p = std::make_unique<RoyalChainAudioProcessor>();
        p->setRateAndBufferSizeDetails (kSr, 2 * kBlock);
        p->prepareToPlay (kSr, 2 * kBlock);
        setParam ("tuneKey", 0.0f);
        setParam ("tuneScale", 1.0f);
        setParam ("syncFromAutoPitch", 0.0f);
        setParam ("tune", tune);
        setParam ("style", (float) style);
        openEditor();
    }

    ~Rig()
    {
        ed.reset();
        p->releaseResources();
    }

    void openEditor()
    {
        ed.reset (p->createEditor());
        ed->setSize (1100, 760);
        editor = dynamic_cast<RoyalChainAudioProcessorEditor*> (ed.get());
        compass = editor != nullptr ? &editor->viewForTesting().compass() : nullptr;
        compassFoundById = editor != nullptr && editor->viewForTesting().findChildWithID ("rc.compass") == compass;
    }

    void setParam (const char* id, float plain)
    {
        if (auto* prm = p->parameters().getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    }

    float param (const char* id) const
    {
        return p->parameters().getRawParameterValue (id)->load();
    }

    void process (Signal& sig)
    {
        for (int b = 0; b < 2; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = sig.next();
                block.setSample (0, i, v);
                block.setSample (1, i, v);
            }
            midi.clear();
            p->processBlock (block, midi);
        }
    }

    void frame (Signal& sig)
    {
        process (sig);
        if (editor != nullptr)
            editor->viewForTesting().poll();
        if (compass != nullptr)
            compass->advanceFrames (1);
    }

    void frames (Signal& sig, int n)
    {
        for (int i = 0; i < n; ++i)
            frame (sig);
    }
};

bool writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk())
        return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, os);
}

juce::Image paintComponent (juce::Component& c, float scale)
{
    juce::Image img (juce::Image::ARGB, juce::roundToInt ((float) c.getWidth() * scale),
                     juce::roundToInt ((float) c.getHeight() * scale), true);   // native (CoreGraphics)
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    c.paintEntireComponent (g, true);
    return img;
}

bool shootCompass (Rig& rig, const juce::File& dir, const juce::String& name)
{
    return writePng (paintComponent (*rig.compass, 2.0f), dir.getChildFile (name));
}

bool shootEditor (Rig& rig, const juce::File& dir, const juce::String& name, float scale)
{
    rig.compass->prepareForScale (scale * (float) rig.ed->getWidth() / 1100.0f);
    const bool ok = writePng (paintComponent (*rig.ed, scale), dir.getChildFile (name));
    rig.compass->prepareForScale (2.0f);
    return ok;
}

// Per-compute-pixel luma of the field (disc r <= 0.97), for pixelwise metrics.
std::vector<float> fieldLuma (const rc::PortalRenderer& r)
{
    std::vector<float> out;
    if (! r.isPrepared() || ! r.image().isValid())
        return out;   // the Portal renderer is retired (the aperture is the water dish)
    const int S = r.computeSize(), up = r.upsample();
    const juce::Image::BitmapData bd (r.image(), juce::Image::BitmapData::readOnly);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
        {
            const float u = (((float) x + 0.5f) / (float) S * 2.0f - 1.0f) * rc::PortalRenderer::kExtent;
            const float v = (((float) y + 0.5f) / (float) S * 2.0f - 1.0f) * rc::PortalRenderer::kExtent;
            if (u * u + v * v > 0.97f * 0.97f)
                continue;
            const auto* px = bd.getPixelPointer (x * up, y * up);
            out.push_back ((0.2126f * px[2] + 0.7152f * px[1] + 0.0722f * px[0]) / 255.0f);
        }
    return out;
}

struct GestureCounter final : juce::AudioProcessorParameter::Listener
{
    int begins = 0, ends = 0;
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends) += 1; }
};

//==============================================================================
struct Report
{
    juce::StringArray lines;
    int fails = 0;

    void add (const juce::String& id, const juce::String& status, const juce::String& detail)
    {
        if (status == "FAIL")
            ++fails;
        const auto line = id.paddedRight (' ', 22) + status.paddedRight (' ', 14) + detail;
        lines.add (line);
        std::printf ("%s\n", line.toRawUTF8());
        std::fflush (stdout);
    }
    void check (const juce::String& id, bool ok, const juce::String& detail) { add (id, ok ? "PASS" : "FAIL", detail); }
};

juce::String f3 (double v) { return juce::String (v, 3); }
juce::String f4 (double v) { return juce::String (v, 4); }

struct Stats { double median, p95, mean; };
Stats statsOf (std::vector<double> v)
{
    if (v.empty()) return { 0, 0, 0 };
    std::sort (v.begin(), v.end());
    double sum = 0; for (auto x : v) sum += x;
    return { v[v.size() / 2], v[(size_t) std::min (v.size() - 1, (size_t) std::ceil (0.95 * (double) v.size()) - 1)], sum / (double) v.size() };
}
} // namespace

//==============================================================================
int main (int argc, char** argv)
{
#if __APPLE__
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    juce::ScopedJuceInitialiser_GUI gui;

    juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("rc_snapshots");
    bool bench = false, benchOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--out" && i + 1 < argc) outDir = juce::File (juce::String (argv[++i]));
        else if (a == "--bench") bench = true;
        else if (a == "--bench-only") bench = benchOnly = true;
    }
    outDir.createDirectory();
    Report rep;
    std::printf ("Royal Chain UI render -> %s\n", outDir.getFullPathName().toRawUTF8());

    if (! benchOnly)
    {

    // --- M0: Bessel zeros + figure table -------------------------------------------
    {
        double worst = 0.0;
        for (const auto& m : rc::kModes)
            worst = std::max (worst, std::abs (rc::CymaticPlate::besselJp (m.n, (double) m.alpha)));
        static const int expected[12][4] = { {5,1,5,2}, {9,1,9,2}, {7,1,7,2}, {6,1,6,2}, {3,2,3,3}, {0,2,0,3},
                                              {1,4,1,5}, {5,2,5,3}, {2,3,2,4}, {4,2,4,3}, {8,1,8,2}, {3,3,3,4} };
        bool table = true;
        for (int d = 0; d < 12; ++d)
        {
            const auto& pm = rc::kModes[rc::kFigures[d].primary];
            const auto& om = rc::kModes[rc::kFigures[d].overtone];
            table = table && pm.n == expected[d][0] && pm.s == expected[d][1] && om.n == expected[d][2] && om.s == expected[d][3];
        }
        rep.check ("M0 bessel", worst < 2.0e-3 && table, "max |Jn'(alpha)| = " + juce::String (worst, 7) + (table ? ", table ok" : ", TABLE MISMATCH"));
    }

    std::vector<float> lumaA3, lumaE4;
    float meanA3 = 0, meanIdle = 0, meanUnvoiced = 0, meanHit = 0, coverA3 = 0, coverIdle = 0, maxBlow = 0;
    float nodalA3 = 0, nodalE4 = 0, nodalC4 = 0;
    int degA3 = -1, degE4 = -1, degC4 = -1;
    juce::String blowWhere;
    auto noteBlow = [&] (const rc::PortalRenderer::Stats& s, const char* where)
    {
        if (s.blowOut > maxBlow) { maxBlow = s.blowOut; blowWhere = where; }
    };

    // --- idle ----------------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::silence();
        rig.frames (sig, 180);
        const auto st = rig.compass->rendererForTesting().measure();
        meanIdle = st.meanLuma; coverIdle = st.coverHi; noteBlow (st, "idle");
        shootCompass (rig, outDir, "rc_idle_compass@2x.png");
        shootEditor (rig, outDir, "rc_idle_editor.png", 1.0f);
        shootEditor (rig, outDir, "rc_idle_editor@2x.png", 2.0f);
        std::printf ("idle: luma %.4f cover %.4f idle=%d calm=%d\n", st.meanLuma, st.coverHi,
                     (int) rig.compass->stateForTesting().idle, (int) rig.compass->stateForTesting().calm);
    }

    // --- A3 (degree 9, (4,2), 8 SPOKES . 1 RING) --------------------------------------
    juce::MemoryBlock pngA3;
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (220.0);
        rig.frames (sig, 240);
        const auto& plate = rig.compass->plateForTesting();
        const auto st = rig.compass->rendererForTesting().measure();
        meanA3 = st.meanLuma; coverA3 = st.coverHi; noteBlow (st, "A3");
        nodalA3 = plate.fractionNearNodal (0.10f);
        degA3 = rig.compass->stateForTesting().degree;
        lumaA3 = fieldLuma (rig.compass->rendererForTesting());
        shootCompass (rig, outDir, "rc_A3_compass@2x.png");
        outDir.getChildFile ("rc_A3_compass@2x.png").loadFileAsData (pngA3);
        const auto& s = rig.compass->stateForTesting();
        std::printf ("A3: deg %d note %d cents %.1f resp %.2f conf %.2f level %.2f | luma %.4f cover %.4f nodal %.3f dominant (%d,%d)\n",
                     s.degree, s.note, s.cents, s.resp, s.confS, s.level, st.meanLuma, st.coverHi, nodalA3,
                     rc::kModes[plate.dominantMode()].n, rc::kModes[plate.dominantMode()].s);
        { const auto& dd = rig.compass->dishForTesting(); std::printf ("DISH A3: audio %d pitch %.1f target-cents %+.1f match %.2f loud %.2f stereo %.2f resp %.3f mode (%d,%d) %.1f Hz\n", (int) dd.hasAudio(), dd.pitchHz(), dd.centsOff(), dd.tuneMatch(), dd.loudness(), dd.stereo(), dd.response(), dd.dominantMode().n, dd.dominantMode().s, dd.dominantHz()); }

        // M10: automation moves the puck within one frame.
        rig.setParam ("tune", 0.9f);
        rig.compass->advanceFrames (1);
        const float radius = rig.compass->nodePosition (0).getDistanceFrom ({ VocalCrownCompass::kCx, VocalCrownCompass::kCy });
        const float expectR = (0.10f + 0.84f * 0.9f) * VocalCrownCompass::kR;
        rep.check ("M10 automation", std::abs (radius - expectR) <= 1.0f, "puck radius " + f3 (radius) + " expected " + f3 (expectR));
        rig.setParam ("tune", 0.6f);
        rig.frames (sig, 4);

        shootEditor (rig, outDir, "rc_A3_editor.png", 1.0f);

        // M11: zero allocations per frame (after warm-up), and on the audio path.
        rig.compass->advanceFrames (30);
        gAllocCount = 0; gAllocArmed = true;
        for (int i = 0; i < 600; ++i)
            rig.compass->advanceFrames (1);
        gAllocArmed = false;
        const long frameAllocs = gAllocCount.load();

        // Pre-render the audio so the armed window only contains processBlock.
        std::vector<juce::AudioBuffer<float>> blocks;
        for (int i = 0; i < 200; ++i)
        {
            juce::AudioBuffer<float> b (2, kBlock);
            for (int k = 0; k < kBlock; ++k) { const float v = sig.next(); b.setSample (0, k, v); b.setSample (1, k, v); }
            blocks.push_back (std::move (b));
        }
        juce::MidiBuffer midi;
        midi.ensureSize (256);
        gAllocCount = 0; gAllocArmed = true;
        for (auto& b : blocks)
            rig.p->processBlock (b, midi);
        gAllocArmed = false;
        const long audioAllocs = gAllocCount.load();
        rep.check ("M11 allocations", frameAllocs == 0 && audioAllocs == 0,
                   "advanceFrames x600: " + juce::String (frameAllocs) + " | processBlock x200 (editor open): " + juce::String (audioAllocs));

        // M13: layout.
        juce::StringArray problems;
        auto& view = rig.editor->viewForTesting();
        auto checkSiblings = [&problems] (juce::Component& parent, const char* name)
        {
            for (int i = 0; i < parent.getNumChildComponents(); ++i)
                for (int j = i + 1; j < parent.getNumChildComponents(); ++j)
                {
                    auto* a = parent.getChildComponent (i);
                    auto* b = parent.getChildComponent (j);
                    if (a->isVisible() && b->isVisible() && a->getBounds().intersects (b->getBounds()))
                        problems.add (juce::String (name) + ": " + a->getTitle() + a->getName() + " x " + b->getTitle() + b->getName()
                                      + " " + a->getBounds().toString() + " / " + b->getBounds().toString());
                }
        };
        checkSiblings (view, "view");
        checkSiblings (view.spine(), "spine");
        checkSiblings (view.deck(), "deck");
        std::vector<rc::theme::CaptionCheck> captions;
        view.appendCaptionChecks (captions);
        for (const auto& c : captions)
        {
            const float w = juce::GlyphArrangement::getStringWidth (juce::Font (c.font), c.text);
            if (w > c.availableWidth + 0.01f)
                problems.add (c.where + " '" + c.text + "' " + f3 (w) + " > " + f3 (c.availableWidth));
        }
        rep.check ("M13 layout", problems.isEmpty(), problems.isEmpty() ? juce::String (captions.size()) + " captions fit, no sibling overlaps"
                                                                         : problems.joinIntoString (" | "));

        // After A3: the minimum editor size.
        rig.ed->setSize (935, 646);
        shootEditor (rig, outDir, "rc_editor_min.png", 1.0f);
        rep.check ("editor constrain", rig.ed->getWidth() == 935 && rig.ed->getHeight() == 646,
                   "editor size after setSize(935, 646): " + juce::String (rig.ed->getWidth()) + "x" + juce::String (rig.ed->getHeight()));
        rep.check ("compass id", rig.compassFoundById, "findChildWithID(\"rc.compass\")");
    }

    // --- E4 / C4 -------------------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (329.63);
        rig.frames (sig, 240);
        const auto st = rig.compass->rendererForTesting().measure();
        noteBlow (st, "E4");
        nodalE4 = rig.compass->plateForTesting().fractionNearNodal (0.10f);
        degE4 = rig.compass->stateForTesting().degree;
        lumaE4 = fieldLuma (rig.compass->rendererForTesting());
        shootCompass (rig, outDir, "rc_E4_compass@2x.png");
        std::printf ("E4: deg %d luma %.4f nodal %.3f\n", degE4, st.meanLuma, nodalE4);
    }
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        // Captured 1.5 s into the note: the tonic crown starts lobe-on-puck and
        // WIDTH then turns it slowly (it realigns when the voice goes idle).
        auto sig = Signal::tone (261.63);
        rig.frames (sig, 90);
        const auto st = rig.compass->rendererForTesting().measure();
        noteBlow (st, "C4");
        nodalC4 = rig.compass->plateForTesting().fractionNearNodal (0.10f);
        degC4 = rig.compass->stateForTesting().degree;
        shootCompass (rig, outDir, "rc_C4_compass@2x.png");
        std::printf ("C4: deg %d luma %.4f nodal %.3f psi %.3f\n", degC4, st.meanLuma, nodalC4, rig.compass->plateForTesting().rotation());
    }

    // --- phrase (mid-glide morph) ------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::vocalPhrase();
        rig.frames (sig, 96);
        const auto st = rig.compass->rendererForTesting().measure();
        noteBlow (st, "phrase");
        shootCompass (rig, outDir, "rc_phrase_compass@2x.png");
        std::printf ("phrase: deg %d note %d luma %.4f\n", rig.compass->stateForTesting().degree, rig.compass->stateForTesting().note, st.meanLuma);
    }

    // --- hit (3 frames after the burst) ---------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::hit();
        int burstFrame = -1;
        for (int f = 0; f < 240 && burstFrame < 0; ++f)
        {
            rig.frame (sig);
            if (sig.t >= 2.0 && rig.compass->stateForTesting().burst)
                burstFrame = f;
        }
        rig.frames (sig, 3);
        const auto st = rig.compass->rendererForTesting().measure();
        meanHit = st.meanLuma; noteBlow (st, "hit");
        shootCompass (rig, outDir, "rc_hit_compass@2x.png");
        std::printf ("hit: burst at frame %d, luma %.4f pulse %.2f\n", burstFrame, st.meanLuma, rig.compass->stateForTesting().pulse);
    }

    // --- drag + gestures ----------------------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (220.0);
        rig.frames (sig, 120);
        const int streaksBefore = rig.compass->rendererForTesting().liveStreaks();

        GestureCounter widthCounter;
        auto* widthParam = rig.p->parameters().getParameter ("width");
        widthParam->addListener (&widthCounter);
        rig.compass->beginNodeDrag (4);
        int streaksDuring = 0;
        for (int step = 0; step < 20; ++step)
        {
            rig.compass->dragNodeToValue (4, 0.25f + (0.85f - 0.25f) * (float) step / 19.0f);
            rig.frame (sig);
            if (step == 12)
            {
                streaksDuring = rig.compass->rendererForTesting().liveStreaks();
                const auto st = rig.compass->rendererForTesting().measure();
                noteBlow (st, "drag");
                shootCompass (rig, outDir, "rc_drag_compass@2x.png");
            }
        }
        rig.compass->endNodeDrag (4);
        widthParam->removeListener (&widthCounter);
        const float widthFinal = rig.param ("width");
        const float ratio = streaksBefore > 0 ? (float) streaksDuring / (float) streaksBefore : 0.0f;
        juce::ignoreUnused (ratio);
        rep.add ("M8 drag", "RETIRED", "Portal streaks replaced by the water dish");

        // Keyboard: Right cycles WIDTH -> TUNE, then 3 x Up on TUNE.
        rig.compass->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        GestureCounter tuneCounter;
        auto* tuneParam = rig.p->parameters().getParameter ("tune");
        tuneParam->addListener (&tuneCounter);
        const float tune0 = rig.param ("tune");
        for (int i = 0; i < 3; ++i)
            rig.compass->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        const float tune1 = rig.param ("tune");
        tuneParam->removeListener (&tuneCounter);
        const bool dragOk = widthCounter.begins == 1 && widthCounter.ends == 1 && std::abs (widthFinal - 0.85f) <= 0.002f;
        const bool keysOk = tuneCounter.begins == 3 && tuneCounter.ends == 3 && std::abs ((tune1 - tune0) - 0.03f) <= 0.002f;
        rep.check ("M9 gestures", dragOk && keysOk,
                   "drag: " + juce::String (widthCounter.begins) + " begin / " + juce::String (widthCounter.ends) + " end, width " + f3 (widthFinal)
                 + " | keys: " + juce::String (tuneCounter.begins) + "/" + juce::String (tuneCounter.ends) + ", tune " + f3 (tune0) + " -> " + f3 (tune1));
    }

    // --- unvoiced (warp + vortex) ------------------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::noise();
        rig.frames (sig, 180);
        const auto st = rig.compass->rendererForTesting().measure();
        meanUnvoiced = st.meanLuma; noteBlow (st, "unvoiced");
        shootCompass (rig, outDir, "rc_unvoiced_compass@2x.png");
        std::printf ("unvoiced: luma %.4f voiced=%d level %.2f\n", st.meanLuma, (int) rig.compass->stateForTesting().voiced, rig.compass->stateForTesting().level);
    }

    // --- styles ----------------------------------------------------------------------------------------------
    for (const auto& [style, file] : { std::pair<int, const char*> { 5, "rc_style_podcast@2x.png" }, std::pair<int, const char*> { 7, "rc_style_hook@2x.png" } })
    {
        Rig rig (style);
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (220.0);
        rig.frames (sig, 240);
        const auto st = rig.compass->rendererForTesting().measure();
        noteBlow (st, file);
        shootCompass (rig, outDir, file);
        std::printf ("style %d: overtone %.2f luma %.4f\n", style, rig.compass->stateForTesting().overtone, st.meanLuma);
    }

    float dishMatchOff = 0.0f, dishMatchOn = 0.0f, dishRespA3 = 0.0f, dishHzA3 = 0.0f;
    std::vector<std::pair<int, int>> dishNoteModes;
    // --- Dish represents the chain: off-pitch voice with Tune off vs on; Width 0 vs 1 ---
    {
        auto dishLine = [] (const char* tag, const VocalCrownCompass& c)
        {
            const auto& d = c.dishForTesting();
            std::printf ("DISH %-14s pitch %.1f cents %+.1f match %.2f loud %.2f stereo %.3f resp %.3f mode (%d,%d)\n", tag,
                         d.pitchHz(), d.centsOff(), d.tuneMatch(), d.loudness(), d.stereo(), d.response(),
                         d.dominantMode().n, d.dominantMode().s);
        };
        for (const auto& [name, hz] : { std::pair<const char*, double> { "note C4", 261.63 }, { "note D4", 293.66 },
                                        { "note E4", 329.63 }, { "note G4", 392.0 }, { "note A3", 220.0 }, { "note A4", 440.0 } })
        {
            Rig rig (0, 1.0f);
            rig.compass->prepareForScale (2.0f);
            auto sig = Signal::tone (hz);
            rig.frames (sig, 240);
            dishLine (name, *rig.compass);
            const auto& dm = rig.compass->dishForTesting().dominantMode();
            if (juce::String (name) != "note A4")
                dishNoteModes.push_back ({ dm.n, dm.s });
            if (juce::String (name) == "note A3")
            {
                dishRespA3 = rig.compass->dishForTesting().response();
                dishHzA3 = rig.compass->dishForTesting().dominantHz();
            }
            if (juce::String (name) == "note E4")
                shootCompass (rig, outDir, "rc_dish_E4@2x.png");
        }
        const double sharp = 220.0 * std::pow (2.0, 40.0 / 1200.0);   // A3, 40 cents sharp
        for (const float tune : { 0.0f, 1.0f })
        {
            Rig rig (0, tune);
            rig.compass->prepareForScale (2.0f);
            auto sig = Signal::tone (sharp);
            rig.frames (sig, 240);
            dishLine (tune > 0.5f ? "sharp tune=1" : "sharp tune=0", *rig.compass);
            (tune > 0.5f ? dishMatchOn : dishMatchOff) = rig.compass->dishForTesting().tuneMatch();
            shootCompass (rig, outDir, tune > 0.5f ? "rc_dish_sharp_tune1@2x.png" : "rc_dish_sharp_tune0@2x.png");
        }
        for (const float width : { 0.0f, 1.0f })
        {
            Rig rig (0, 0.6f);
            rig.setParam ("width", width);
            rig.compass->prepareForScale (2.0f);
            auto sig = Signal::tone (220.0);
            rig.frames (sig, 240);
            dishLine (width > 0.5f ? "width=1" : "width=0", *rig.compass);
            shootCompass (rig, outDir, width > 0.5f ? "rc_dish_width1@2x.png" : "rc_dish_width0@2x.png");
        }
    }

    // --- M12: determinism ---------------------------------------------------------------------------------------
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (220.0);
        rig.frames (sig, 240);
        const auto tmp = outDir.getChildFile ("rc_A3_repeat.png");
        shootCompass (rig, outDir, "rc_A3_repeat.png");
        juce::MemoryBlock again;
        tmp.loadFileAsData (again);
        tmp.deleteFile();
        rep.check ("M12 determinism", again.getSize() > 0 && again == pngA3, "A3 PNG bytes " + juce::String ((juce::int64) pngA3.getSize()) + (again == pngA3 ? " identical" : " DIFFER"));
    }

    // --- M14: lifecycle ----------------------------------------------------------------------------------------------
    {
        Rig rig;
        auto sig = Signal::tone (220.0);
        for (int i = 0; i < 20; ++i)
        {
            rig.ed.reset();
            rig.compass = nullptr;
            rig.editor = nullptr;
            rig.process (sig);
            rig.openEditor();
            rig.frames (sig, 3);
        }
        rep.check ("M14 lifecycle", rig.compass != nullptr, "20 editor create/destroy cycles while feeding audio");
    }

    // --- summary metrics ------------------------------------------------------------------------------------------------
    // M1-M3, M6-M8 measured the retired grain plate / Portal renderer (the aperture is
    // now the Cymatic Generator's water dish); the D-checks below gate the dish.
    rep.add ("M1 lines", "RETIRED", "grain plate replaced by the water dish (D1-D4)");
    rep.add ("M2 brightness", "RETIRED", "grain plate replaced by the water dish (D1-D4)");
    rep.add ("M3 coverage", "RETIRED", "grain plate replaced by the water dish (D1-D4)");
    rep.check ("M4 blow-out", maxBlow < 0.005f, "max blowOut " + f4 (maxBlow) + (blowWhere.isNotEmpty() ? " (" + blowWhere + ")" : juce::String()));
    rep.check ("M5 figure identity", degA3 == 9 && degE4 == 4 && degC4 == 0,
               "degrees A3 " + juce::String (degA3) + " E4 " + juce::String (degE4) + " C4 " + juce::String (degC4));
    rep.add ("M6 distinctness", "RETIRED", "see D2");
    rep.add ("M7 hit", "RETIRED", "grain plate replaced by the water dish");
    rep.check ("D1 tune shows", dishMatchOff < 0.75f && dishMatchOn > 0.95f,
               "40 c sharp voice: match Tune 0 = " + f3 (dishMatchOff) + ", Tune 1 = " + f3 (dishMatchOn));
    {
        std::vector<std::pair<int, int>> uniq (dishNoteModes);
        std::sort (uniq.begin(), uniq.end());
        uniq.erase (std::unique (uniq.begin(), uniq.end()), uniq.end());
        rep.check ("D2 notes differ", uniq.size() >= 4 && dishNoteModes.size() >= 5,
                   juce::String ((int) uniq.size()) + " distinct dish modes for C4 D4 E4 G4 A3");
    }
    rep.check ("D3 lock", dishRespA3 > 0.95f && std::abs (dishHzA3 - 220.0f) < 2.2f,
               "A3 response " + f3 (dishRespA3) + ", locked mode at " + f3 (dishHzA3) + " Hz");
    } // ! benchOnly

    // --- M15: bench ------------------------------------------------------------------------------------------------------------
    if (! bench)
    {
        rep.add ("M15 perf", "NOT VERIFIED", "run with --bench (Release) to measure");
    }
    else
    {
        Rig rig;
        rig.compass->prepareForScale (2.0f);
        auto sig = Signal::tone (220.0);
        rig.frames (sig, 120);
        juce::Image target (juce::Image::ARGB, 1488, 1016, true);
        const double cal0 = calibrationMs();
        std::vector<double> sim, paint, total, stage[5], section[4];
        for (int f = 0; f < 600; ++f)
        {
            rig.process (sig);
            rig.editor->viewForTesting().poll();
            const double t0 = threadCpuMs();
            rig.compass->advanceFrames (1);
            const double t1 = threadCpuMs();
            {
                juce::Graphics g (target);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                rig.compass->paintEntireComponent (g, true);
            }
            const double t2 = threadCpuMs();
            sim.push_back (t1 - t0);
            paint.push_back (t2 - t1);
            total.push_back (t2 - t0);
            const auto tm = rig.compass->rendererForTesting().lastTiming();
            const double parts[5] = { tm.grains, tm.streaks, tm.bloom, tm.glow, tm.composite };
            for (int k = 0; k < 5; ++k) stage[k].push_back (parts[k]);
            const auto pf = rig.compass->perf();
            const double sec[4] = { pf.paintStageMs, pf.paintFieldMs, pf.paintVectorMs, pf.paintTextMs };
            for (int k = 0; k < 4; ++k) section[k].push_back (sec[k]);
        }
        const double cal1 = calibrationMs();
        const auto s = statsOf (sim), pt = statsOf (paint), tt = statsOf (total);
        const auto plateMs = rig.compass->perf().simMs;
        juce::String detail;
        detail << "sim+render median " << f3 (s.median) << " p95 " << f3 (s.p95) << " | paint median " << f3 (pt.median) << " p95 " << f3 (pt.p95)
               << " | total median " << f3 (tt.median) << " p95 " << f3 (tt.p95) << " mean " << f3 (tt.mean)
               << " | stages (wall, median) grains " << f3 (statsOf (stage[0]).median) << " streaks " << f3 (statsOf (stage[1]).median)
               << " bloom " << f3 (statsOf (stage[2]).median) << " glow " << f3 (statsOf (stage[3]).median) << " composite " << f3 (statsOf (stage[4]).median)
               << " plate(last) " << f3 (plateMs)
               << " | paint sections (wall, median) stage " << f3 (statsOf (section[0]).median) << " field " << f3 (statsOf (section[1]).median)
               << " vectors " << f3 (statsOf (section[2]).median) << " text " << f3 (statsOf (section[3]).median)
               << " | calibration " << juce::String (cal0, 0) << "/" << juce::String (cal1, 0) << " ms";
       #if JUCE_DEBUG
        const bool trustworthy = false;
       #else
        const bool trustworthy = cal0 <= 375.0 && cal1 <= 375.0;
       #endif
        const bool ok = s.median <= 2.0 && s.p95 <= 2.6 && pt.median <= 1.0 && tt.p95 <= 3.0;
        rep.add ("M15 perf", trustworthy ? (ok ? "PASS" : "FAIL") : "NOT VERIFIED", detail);

        // Paint breakdown (informational): what CoreGraphics charges for each
        // blit, and the realistic on-screen frame (only the ring box repaints).
        auto timeIt = [] (int n, auto&& fn)
        {
            std::vector<double> v;
            for (int i = 0; i < n; ++i) { const double a = threadCpuMs(); fn(); v.push_back (threadCpuMs() - a); }
            return statsOf (v);
        };
        juce::Image nativeFull (juce::Image::ARGB, 1488, 1016, true);
        { juce::Graphics g (nativeFull); g.fillAll (juce::Colour (0xff101114)); }
        juce::Image soft (juce::Image::ARGB, 880, 880, true, juce::SoftwareImageType());
        { juce::Graphics g (soft); g.setColour (juce::Colour (0x8062f5c8)); g.fillEllipse (0.0f, 0.0f, 880.0f, 880.0f); }
        const auto nat = timeIt (200, [&] { juce::Graphics g (target); g.drawImageAt (nativeFull, 0, 0); });
        const auto sw = timeIt (200, [&] { juce::Graphics g (target); g.addTransform (juce::AffineTransform::scale (2.0f));
                                           g.drawImageTransformed (soft, juce::AffineTransform::scale (0.5f).translated (152.0f, 42.0f)); });
        const auto ring = timeIt (200, [&] { juce::Graphics g (target); g.addTransform (juce::AffineTransform::scale (2.0f));
                                             g.reduceClipRegion (juce::Rectangle<int> (142, 32, 460, 460));
                                             rig.compass->paintEntireComponent (g, true); });
        const auto full = timeIt (200, [&] { juce::Graphics g (target); g.addTransform (juce::AffineTransform::scale (2.0f));
                                             rig.compass->paintEntireComponent (g, true); });
        juce::String info;
        info << "native 1488x1016 blit " << f3 (nat.median) << " | software 880^2 1:1 blit " << f3 (sw.median)
             << " | compass paint: ring box " << f3 (ring.median) << " (p95 " << f3 (ring.p95) << "), full " << f3 (full.median)
             << " | on-screen frame estimate (sim+render + ring-box paint) median " << f3 (s.median + ring.median);
        rep.add ("M15 paint detail", "INFO", info);
    }

    outDir.getChildFile ("report.txt").replaceWithText (rep.lines.joinIntoString ("\n") + "\n");
    std::printf ("%d FAIL(s)\n", rep.fails);
    return rep.fails == 0 ? 0 : 1;
}
