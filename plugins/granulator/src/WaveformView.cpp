#include "WaveformView.h"

#include <sattari_audio_core/SattariBranding.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>

#include <cmath>

namespace
{
constexpr float kHandleHitZone = 8.0f;    // px each side of a handle
constexpr float kPanelRadius   = 10.0f;
}

// =============================================================================
//  Lifecycle
// =============================================================================
WaveformView::WaveformView (SattariGranulatorAudioProcessor& processor)
    : audioProcessor (processor),
      thumbnail (512, processor.formatManager(), thumbnailCache)
{
    setInterceptsMouseClicks (true, true);
    setMouseCursor (juce::MouseCursor::NormalCursor);
    refreshFromSource();
    startTimerHz (30);   // File-Position cursor + grain sparks
}

WaveformView::~WaveformView()
{
    stopTimer();
}

// =============================================================================
//  Source -> in-memory thumbnail + transient rail
// =============================================================================
void WaveformView::refreshFromSource()
{
    const int layerInt = (int) audioProcessor.selectedLayer();
    auto src = audioProcessor.currentSource (audioProcessor.selectedLayer());
    if (src == nullptr || src->lengthSamples <= 0)
    {
        haveSource = false;
        thumbnail.clear();
        transientMarks.clear();
        lastSourceVersion = 0xFFFFFFFFu;
        lastLayer = layerInt;
        thumbLengthSecs = 0.0;
        repaint();
        return;
    }

    // Rebuild when the immutable source changed (version bump) OR the editor
    // flipped the selected layer (so we show the new layer's buffer).
    if (haveSource && src->version == lastSourceVersion && layerInt == lastLayer)
        return;

    lastSourceVersion = src->version;
    lastLayer = layerInt;
    haveSource = true;

    const int   ch  = juce::jmax (1, src->audio.getNumChannels());
    const int   len = src->lengthSamples;
    const double sr = src->sourceSampleRate > 0.0 ? src->sourceSampleRate : 48000.0;
    thumbLengthSecs = (double) len / sr;

    // Feed the thumbnail IN-MEMORY so captured / edited (non-file) buffers render
    // exactly like a loaded file would. reset() sizes it, addBlock() fills it.
    thumbnail.reset (ch, sr, len);
    thumbnail.addBlock (0, src->audio, 0, len);

    // --- Coarse onset scan (debounced) for the top-rail transient ticks -------
    transientMarks.clear();
    {
        sattari::dsp::EnvelopeFollower follower;
        follower.prepare (sr, 2.0f, 60.0f);
        const int inCh = ch;
        float prevEnv = 0.0f;
        // Refractory window so a single hit doesn't spray a cluster of marks.
        const int refractory = juce::jmax (1, (int) (sr * 0.045));
        int sinceLast = refractory;
        const float threshRise = 0.14f;   // relative jump that counts as an onset

        for (int i = 0; i < len; ++i)
        {
            float mag = 0.0f;
            for (int c = 0; c < inCh; ++c)
                mag = juce::jmax (mag, std::fabs (src->audio.getReadPointer (c)[i]));

            const float env = follower.processRectified (mag);
            const float rise = env - prevEnv;
            prevEnv = env;

            if (sinceLast >= refractory && rise > threshRise && env > 0.06f)
            {
                transientMarks.push_back ((float) i / (float) juce::jmax (1, len - 1));
                sinceLast = 0;
                if ((int) transientMarks.size() >= 128)   // hard cap: keep the rail readable
                    break;
            }
            ++sinceLast;
        }
    }

    repaint();
}

// =============================================================================
//  Geometry
// =============================================================================
juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 10.0f);
}

float WaveformView::xForPos (float pos01) const
{
    const auto w = waveArea();
    return w.getX() + juce::jlimit (0.0f, 1.0f, pos01) * w.getWidth();
}

float WaveformView::posForX (float x) const
{
    const auto w = waveArea();
    if (w.getWidth() <= 0.0f) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / w.getWidth());
}

// =============================================================================
//  Param helpers
// =============================================================================
float WaveformView::paramValue (const char* baseId) const
{
    const auto id = SattariGranulatorAudioProcessor::layerParamId (audioProcessor.selectedLayer(), baseId);
    if (auto* p = audioProcessor.parameters().getRawParameterValue (id))
        return p->load();
    return 0.0f;
}

void WaveformView::setParamNormalising (const char* baseId, float value)
{
    const auto id = SattariGranulatorAudioProcessor::layerParamId (audioProcessor.selectedLayer(), baseId);
    if (auto* p = audioProcessor.parameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

// =============================================================================
//  paint
// =============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto accent = juce::Colour (sattari::brand::productAccent ("granulator"));
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    // Suite-dark rounded panel.
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (bounds, kPanelRadius);
    g.setColour (fileHovering ? accent.withAlpha (0.9f) : juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (bounds, kPanelRadius, fileHovering ? 2.0f : 1.0f);

    const auto wave = waveArea();

    // Faint centre line.
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) std::round (wave.getCentreY()), wave.getX(), wave.getRight());

    const float start01 = juce::jlimit (0.0f, 1.0f, paramValue ("srcStart"));
    const float end01   = juce::jlimit (0.0f, 1.0f, paramValue ("srcEnd"));
    const float sx = xForPos (start01);
    const float ex = xForPos (juce::jmax (start01, end01));

    if (! haveSource || thumbnail.getTotalLength() <= 0.0)
    {
        // Empty state: prompt to drop / load.
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (fileHovering ? "Release to load sample" : "Drop an audio file, or use Load Sample",
                    wave.toNearestInt(), juce::Justification::centred, false);
        return;
    }

    // Dimmed waveform outside the active region.
    g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (0.32f));
    thumbnail.drawChannels (g, wave.toNearestInt(), 0.0, thumbnail.getTotalLength(), 0.95f);

    // Accent-filled active region [start, end].
    juce::Rectangle<float> region (sx, wave.getY(), juce::jmax (1.0f, ex - sx), wave.getHeight());
    g.setColour (accent.withAlpha (0.08f));
    g.fillRect (region);

    // Bright waveform inside the region only (clip to the region rect).
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (region.toNearestInt());
        g.setColour (accent.withAlpha (0.9f));
        thumbnail.drawChannels (g, wave.toNearestInt(), 0.0, thumbnail.getTotalLength(), 0.95f);
    }

    // Loop sub-span bracket along the bottom when Loop is engaged.
    const bool looping = paramValue ("srcLoopMode") > 0.5f;
    if (looping)
    {
        const float by = wave.getBottom() - 3.0f;
        g.setColour (accent.withAlpha (0.85f));
        g.drawLine (sx, by, ex, by, 2.0f);
        g.drawLine (sx, by - 5.0f, sx, by, 2.0f);
        g.drawLine (ex, by - 5.0f, ex, by, 2.0f);
    }

    // Top-rail transient tick marks (coarse onsets).
    {
        const float railY = wave.getY();
        g.setColour (juce::Colours::white.withAlpha (0.24f));
        for (const float m : transientMarks)
        {
            const float tx = xForPos (m);
            g.drawLine (tx, railY, tx, railY + 6.0f, 1.0f);
        }
    }

    // Start / End handles (draggable). Highlight the hovered / active one.
    auto drawHandle = [&] (float x, Handle which)
    {
        const bool hot = (activeHandle == which) || (hoverHandle == which && activeHandle == Handle::None);
        g.setColour (hot ? accent : accent.withAlpha (0.7f));
        g.drawLine (x, wave.getY(), x, wave.getBottom(), hot ? 2.4f : 1.6f);
        // A small grab tab at the top.
        juce::Rectangle<float> tab (x - 4.0f, wave.getY() - 1.0f, 8.0f, 8.0f);
        g.fillRoundedRectangle (tab, 2.0f);
    };
    drawHandle (sx, Handle::Start);
    drawHandle (ex, Handle::End);

    // --- Live granulation overlay --------------------------------------------
    // File-Position cursor (representative playhead) mapped into [start,end].
    const float cursorNorm = juce::jlimit (0.0f, 1.0f, start01 + viz.posNorm * juce::jmax (0.0f, end01 - start01));
    const float cx = xForPos (cursorNorm);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawLine (cx, wave.getY(), cx, wave.getBottom(), 1.2f);

    // Grain "sparks": short vertical markers at each grain read position, fading
    // by window age (older grains dimmer). grainPos is 0..1 over the whole buffer.
    const auto n = juce::jmin ((uint32_t) sattari::dsp::SattariGranularEngine::kVizGrains,
                               viz.nActiveGrains);
    for (uint32_t i = 0; i < n; ++i)
    {
        const float gx = xForPos (viz.grainPos[i]);
        const float age = juce::jlimit (0.0f, 1.0f, viz.grainAge[i]);
        // Bright at birth, fading toward the end of the window.
        const float alpha = 0.15f + 0.55f * (1.0f - age);
        const float h = wave.getHeight() * (0.14f + 0.20f * (1.0f - age));
        g.setColour (accent.withAlpha (alpha));
        g.drawLine (gx, wave.getCentreY() - h * 0.5f, gx, wave.getCentreY() + h * 0.5f, 1.4f);
    }

    // Voice / grain count readout (unobtrusive, top-right).
    g.setColour (juce::Colour (sattari::brand::kTextFaint));
    g.setFont (juce::FontOptions (9.5f));
    g.drawText (juce::String ((int) viz.nActiveVoices) + " v  " + juce::String ((int) viz.nActiveGrains) + " g",
                getLocalBounds().reduced (14, 8), juce::Justification::topRight, false);
}

void WaveformView::resized() {}

// =============================================================================
//  30 Hz timer — refresh viz + pick up source swaps
// =============================================================================
void WaveformView::timerCallback()
{
    refreshFromSource();                 // cheap no-op unless the version/layer changed
    viz = audioProcessor.vizFrame (audioProcessor.selectedLayer());   // lock-free snapshot
    repaint();
}

// =============================================================================
//  Drag-drop
// =============================================================================
bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff"
            || ext == ".flac" || ext == ".mp3" || ext == ".ogg")
            return true;
    }
    return false;
}

void WaveformView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileHovering = true;
    repaint();
}

void WaveformView::fileDragExit (const juce::StringArray&)
{
    fileHovering = false;
    repaint();
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    fileHovering = false;
    if (! files.isEmpty())
        audioProcessor.loadFile (audioProcessor.selectedLayer(),
                                 juce::File (files[0]));   // atomic swap on the msg thread
    refreshFromSource();
    repaint();
}

// =============================================================================
//  Start / End handle dragging
// =============================================================================
WaveformView::Handle WaveformView::handleAt (juce::Point<float> p) const
{
    if (! haveSource) return Handle::None;
    const auto wave = waveArea();
    if (p.y < wave.getY() - 6.0f || p.y > wave.getBottom() + 2.0f)
        return Handle::None;

    const float sx = xForPos (juce::jlimit (0.0f, 1.0f, paramValue ("srcStart")));
    const float ex = xForPos (juce::jlimit (0.0f, 1.0f, paramValue ("srcEnd")));

    const float dStart = std::fabs (p.x - sx);
    const float dEnd   = std::fabs (p.x - ex);

    if (dStart <= kHandleHitZone && dStart <= dEnd) return Handle::Start;
    if (dEnd   <= kHandleHitZone)                   return Handle::End;
    return Handle::None;
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hoverHandle)
    {
        hoverHandle = h;
        setMouseCursor (h == Handle::None ? juce::MouseCursor::NormalCursor
                                          : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    activeHandle = handleAt (e.position);
    if (activeHandle != Handle::None)
        repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (activeHandle == Handle::None)
        return;

    const float pos = posForX (e.position.x);
    // Keep a minimum gap between Start and End so the region never inverts.
    constexpr float kMinGap = 0.002f;
    if (activeHandle == Handle::Start)
    {
        const float end01 = juce::jlimit (0.0f, 1.0f, paramValue ("srcEnd"));
        setParamNormalising ("srcStart", juce::jlimit (0.0f, juce::jmax (0.0f, end01 - kMinGap), pos));
    }
    else // End
    {
        const float start01 = juce::jlimit (0.0f, 1.0f, paramValue ("srcStart"));
        setParamNormalising ("srcEnd", juce::jlimit (juce::jmin (1.0f, start01 + kMinGap), 1.0f, pos));
    }
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    activeHandle = Handle::None;
    repaint();
}
