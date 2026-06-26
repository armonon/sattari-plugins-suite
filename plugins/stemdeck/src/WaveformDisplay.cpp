#include "WaveformDisplay.h"
#include <sattari_audio_core/SattariBranding.h>

namespace
{
juce::Colour accentForTitle (const juce::String& title)
{
    using namespace sattari::brand;
    if (title.containsIgnoreCase ("B")) return juce::Colour (kAccent);
    if (title.containsIgnoreCase ("C")) return juce::Colour (kAccentViolet);
    if (title.containsIgnoreCase ("D")) return juce::Colour (productAccent ("royal-chain"));
    return juce::Colour (productAccent ("stemdeck"));
}

static const juce::Colour kDrumsColour  { 0xFFE05A2B };
static const juce::Colour kBassColour   { 0xFF4A90E2 };
static const juce::Colour kMusicColour  { 0xFF5BBF7A };
static const juce::Colour kVocalsColour { 0xFFD44FC8 };

// Standard 8-cue colours (Pioneer-style)
static const std::array<juce::Colour, 8> kCueColours {{
    juce::Colour (0xFFE03030),   // 1 red
    juce::Colour (0xFF3090E0),   // 2 blue
    juce::Colour (0xFF30C050),   // 3 green
    juce::Colour (0xFFE0B020),   // 4 yellow
    juce::Colour (0xFFB040E0),   // 5 purple
    juce::Colour (0xFFE06020),   // 6 orange
    juce::Colour (0xFF20C8C8),   // 7 cyan
    juce::Colour (0xFFE040A0),   // 8 pink
}};

static juce::Colour stemColour (int lane)
{
    switch (lane)
    {
        case 1: return kDrumsColour;
        case 2: return kBassColour;
        case 3: return kMusicColour;
        case 4: return kVocalsColour;
        default: break;
    }
    return juce::Colours::white;
}

static const char* stemLabel (int lane)
{
    switch (lane)
    {
        case 1: return "DRUMS";
        case 2: return "BASS";
        case 3: return "MUSIC";
        case 4: return "VOCALS";
        default: break;
    }
    return "";
}
}

WaveformDisplay::WaveformDisplay (juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache, juce::String deckName)
    : thumbnail       (512, manager, cache),
      drumsThumbnail  (512, manager, cache),
      bassThumbnail   (512, manager, cache),
      musicThumbnail  (512, manager, cache),
      vocalsThumbnail (512, manager, cache),
      title (std::move (deckName))
{
    thumbnail.addChangeListener (this);
    drumsThumbnail.addChangeListener (this);
    bassThumbnail.addChangeListener (this);
    musicThumbnail.addChangeListener (this);
    vocalsThumbnail.addChangeListener (this);

    // Default cue colours
    for (int i = 0; i < numHotCues; ++i)
        hotCues[(size_t) i].colour = kCueColours[(size_t) i];
}

WaveformDisplay::~WaveformDisplay()
{
    thumbnail.removeChangeListener (this);
    drumsThumbnail.removeChangeListener (this);
    bassThumbnail.removeChangeListener (this);
    musicThumbnail.removeChangeListener (this);
    vocalsThumbnail.removeChangeListener (this);
}

void WaveformDisplay::setOnFileDropped (std::function<void(const juce::File&)> callback)
{
    onFileDropped = std::move (callback);
}

void WaveformDisplay::setOnSeekRequest (std::function<void(double)> callback)
{
    onSeekRequest = std::move (callback);
}

void WaveformDisplay::setPlaybackState (double positionSeconds, double totalSeconds, bool playing)
{
    const bool playingChanged = playing != isPlaying;
    const bool totalChanged   = std::abs (totalSeconds    - playbackTotalSeconds)    > 0.05;
    const bool posChanged     = std::abs (positionSeconds - playbackPositionSeconds) > 0.10;

    playbackPositionSeconds = positionSeconds;
    playbackTotalSeconds    = totalSeconds;
    isPlaying               = playing;

    if (playingChanged || totalChanged || posChanged)
        repaint();
}

void WaveformDisplay::setFile (const juce::File& file)
{
    if (file == currentFile)
        return;
    currentFile = file;
    thumbnail.clear();
    if (file.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (file));
    repaint();
}

void WaveformDisplay::setStemFile (int laneIndex, const juce::File& file)
{
    juce::AudioThumbnail* t = nullptr;
    switch (laneIndex)
    {
        case 1: t = &drumsThumbnail;  break;
        case 2: t = &bassThumbnail;   break;
        case 3: t = &musicThumbnail;  break;
        case 4: t = &vocalsThumbnail; break;
        default: return;
    }
    t->clear();
    if (file.existsAsFile())
        t->setSource (new juce::FileInputSource (file));
    repaint();
}

void WaveformDisplay::setHotCues (const std::array<HotCue, numHotCues>& cues)
{
    hotCues = cues;
    repaint();
}

void WaveformDisplay::setLoopRegion (double inSeconds, double outSeconds, bool active)
{
    loopInSeconds  = inSeconds;
    loopOutSeconds = outSeconds;
    loopActive     = active;
    repaint();
}

void WaveformDisplay::clearLoopRegion()
{
    loopInSeconds  = -1.0;
    loopOutSeconds = -1.0;
    loopActive     = false;
    repaint();
}

void WaveformDisplay::setBeatGridSeconds (std::vector<double> beatSeconds)
{
    beatGridSeconds = std::move (beatSeconds);
    repaint();
}

void WaveformDisplay::setZoom (float level, float scroll)
{
    zoomLevel      = juce::jmax (1.0f, level);
    scrollFraction = juce::jlimit (0.0f, 1.0f, scroll);
    repaint();
}

std::pair<double, double> WaveformDisplay::visibleTimeRange (double totalLength) const
{
    if (totalLength <= 0.0 || zoomLevel <= 1.001f)
        return { 0.0, totalLength };
    const double visible = totalLength / (double) zoomLevel;
    const double maxStart = totalLength - visible;
    const double start    = juce::jlimit (0.0, maxStart, scrollFraction * maxStart);
    return { start, start + visible };
}

void WaveformDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! onSeekRequest || playbackTotalSeconds <= 0.0)
        return;
    const auto wave  = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
    const float relX = juce::jlimit (0.0f, 1.0f, (e.position.x - wave.getX()) / wave.getWidth());
    const auto [t0, t1] = visibleTimeRange (playbackTotalSeconds);
    onSeekRequest (t0 + (double) relX * (t1 - t0));
}

void WaveformDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float delta = wheel.deltaY * 0.2f;
    const float newZoom = juce::jlimit (1.0f, 16.0f, zoomLevel * (1.0f + delta));
    setZoom (newZoom, scrollFraction - wheel.deltaX * 0.05f);
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    using namespace sattari::brand;
    const auto area   = getLocalBounds().toFloat();
    const auto accent = accentForTitle (title);

    g.setColour (juce::Colour (kPanelDeep));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (accent.withAlpha (0.35f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

    auto wave = area.reduced (4.0f, 4.0f);

    const bool hasStems = drumsThumbnail.getTotalLength()  > 0.0
                       || bassThumbnail.getTotalLength()   > 0.0
                       || musicThumbnail.getTotalLength()  > 0.0
                       || vocalsThumbnail.getTotalLength() > 0.0;

    // Determine zoom-adjusted time window
    const double totalLen = hasStems ? juce::jmax (juce::jmax (drumsThumbnail.getTotalLength(), bassThumbnail.getTotalLength()),
                                                    juce::jmax (musicThumbnail.getTotalLength(), vocalsThumbnail.getTotalLength()))
                                     : thumbnail.getTotalLength();
    const auto [tStart, tEnd] = visibleTimeRange (totalLen);

    // ── Loop region tint ─────────────────────────────────────────────────────
    if (loopInSeconds >= 0.0 && loopOutSeconds > loopInSeconds && totalLen > 0.0)
    {
        const float inX  = wave.getX() + wave.getWidth() * (float) ((loopInSeconds  - tStart) / (tEnd - tStart));
        const float outX = wave.getX() + wave.getWidth() * (float) ((loopOutSeconds - tStart) / (tEnd - tStart));
        const auto loopRect = juce::Rectangle<float> (inX, wave.getY(), outX - inX, wave.getHeight());
        g.setColour (accent.withAlpha (loopActive ? 0.22f : 0.10f));
        g.fillRect (loopRect);
        g.setColour (accent.brighter (0.4f).withAlpha (loopActive ? 0.9f : 0.5f));
        g.drawLine (inX,  wave.getY(), inX,  wave.getBottom(), 1.5f);
        g.drawLine (outX, wave.getY(), outX, wave.getBottom(), 1.5f);
    }

    if (hasStems)
    {
        // ── Stacked stem mode ─────────────────────────────────────────────────
        const float bandH = wave.getHeight() / 4.0f;
        juce::AudioThumbnail* stems[4] = { &drumsThumbnail, &bassThumbnail,
                                           &musicThumbnail, &vocalsThumbnail };

        for (int lane = 0; lane < 4; ++lane)
        {
            const auto band = wave.withY (wave.getY() + (float) lane * bandH).withHeight (bandH);
            const auto col  = stemColour (lane + 1);

            g.setColour (col.withAlpha (0.10f));
            g.fillRect (band);

            if (lane > 0)
            {
                g.setColour (juce::Colour (kHairline).withAlpha (0.55f));
                g.drawHorizontalLine ((int) std::round (band.getY()), band.getX(), band.getRight());
            }

            g.setColour (col.withAlpha (0.65f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (stemLabel (lane + 1),
                        band.withLeft (band.getX() + 4.0f).withWidth (50.0f).withHeight (11.0f),
                        juce::Justification::centredLeft);

            if (stems[lane]->getTotalLength() > 0.0)
            {
                const auto waveArea = band.reduced (0.0f, 2.0f);
                g.setColour (col.withAlpha (0.28f));
                stems[lane]->drawChannels (g, waveArea.toNearestInt(), tStart, tEnd, 1.0f);
                g.setColour (col.brighter (0.2f).withAlpha (0.92f));
                stems[lane]->drawChannels (g, waveArea.reduced (0, 1).toNearestInt(), tStart, tEnd, 0.85f);
            }
            else
            {
                g.setColour (col.withAlpha (0.30f));
                g.setFont (juce::FontOptions (9.0f));
                g.drawText ("drop " + juce::String (stemLabel (lane + 1)).toLowerCase(),
                            band.reduced (4.0f, 2.0f).toNearestInt(),
                            juce::Justification::centred);
            }
        }

        // Vertical grid
        g.setColour (juce::Colour (kHairline).withAlpha (0.25f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = wave.getX() + wave.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getBottom());
        }
    }
    else if (thumbnail.getTotalLength() > 0.0)
    {
        // ── Full-mix waveform ─────────────────────────────────────────────────
        g.setColour (juce::Colour (kHairline).withAlpha (0.45f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = wave.getX() + wave.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getBottom());
        }
        g.setColour (juce::Colour (kHairline).withAlpha (0.30f));
        g.drawHorizontalLine ((int) std::round (wave.getCentreY()), wave.getX(), wave.getRight());

        g.setColour (accent.withAlpha (0.30f));
        thumbnail.drawChannels (g, wave.toNearestInt(), tStart, tEnd, 1.0f);
        g.setColour (accent.brighter (0.25f).withAlpha (0.95f));
        thumbnail.drawChannels (g, wave.toNearestInt().reduced (0, 1), tStart, tEnd, 0.85f);
    }
    else
    {
        // ── Empty placeholder ─────────────────────────────────────────────────
        g.setColour (juce::Colour (kHairline).withAlpha (0.45f));
        for (int i = 1; i < 8; ++i)
        {
            const float x = wave.getX() + wave.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getBottom());
        }
        g.setColour (juce::Colour (kHairline).withAlpha (0.30f));
        g.drawHorizontalLine ((int) std::round (wave.getCentreY()), wave.getX(), wave.getRight());

        juce::Random random (title.hashCode());
        const int bars = juce::jmax (18, wave.toNearestInt().getWidth() / 8);
        for (int i = 0; i < bars; ++i)
        {
            const float x = wave.getX() + (float) i * wave.getWidth() / (float) bars;
            const float h = wave.getHeight() * (0.10f + 0.78f * random.nextFloat());
            const auto r = juce::Rectangle<float> (x + 1.0f, wave.getCentreY() - h * 0.5f,
                                                   juce::jmax (2.0f, wave.getWidth() / (float) bars - 2.0f), h);
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 1.5f);
        }
        g.setColour (juce::Colour (kTextMuted));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DROP AUDIO", wave.toNearestInt().reduced (4), juce::Justification::centred);
    }

    // ── Beat grid tick marks ─────────────────────────────────────────────────
    if (! beatGridSeconds.empty() && totalLen > 0.0)
    {
        g.setColour (juce::Colour (kTextMuted).withAlpha (0.40f));
        for (double beat : beatGridSeconds)
        {
            if (beat < tStart || beat > tEnd) continue;
            const float x = wave.getX() + wave.getWidth() * (float) ((beat - tStart) / (tEnd - tStart));
            g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getY() + 8.0f);
        }
    }

    // ── Hot cue markers ──────────────────────────────────────────────────────
    if (totalLen > 0.0)
    {
        for (int i = 0; i < numHotCues; ++i)
        {
            const auto& cue = hotCues[(size_t) i];
            if (cue.seconds < 0.0) continue;
            if (cue.seconds < tStart || cue.seconds > tEnd) continue;
            const float cx = wave.getX() + wave.getWidth() * (float) ((cue.seconds - tStart) / (tEnd - tStart));
            g.setColour (cue.colour.withAlpha (0.90f));
            g.drawLine (cx, wave.getY(), cx, wave.getBottom(), 1.5f);
            // Small triangle flag at top
            juce::Path flag;
            flag.addTriangle (cx, wave.getY(), cx + 7.0f, wave.getY(), cx, wave.getY() + 7.0f);
            g.fillPath (flag);
        }
    }

    // ── Live playhead + progress shading ────────────────────────────────────
    if (totalLen > 0.0 && playbackTotalSeconds > 0.0)
    {
        const double tLen = hasStems ? totalLen : thumbnail.getTotalLength();
        const float pos       = juce::jlimit (0.0f, 1.0f, (float) ((playbackPositionSeconds - tStart) / (tEnd - tStart)));
        const float playheadX = wave.getX() + pos * wave.getWidth();

        g.setColour (juce::Colour (kPanelDeep).withAlpha (0.32f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getY(), playheadX - wave.getX(), wave.getHeight()));

        g.setColour (juce::Colour (kTextPrimary).withAlpha (0.85f));
        g.drawLine (playheadX, wave.getY() - 1.0f, playheadX, wave.getBottom() + 1.0f, 1.4f);
        juce::ignoreUnused (tLen);
    }
    else if (totalLen <= 0.0)
    {
        g.setColour (juce::Colour (kTextPrimary).withAlpha (0.30f));
        g.drawLine (wave.getCentreX(), wave.getY() - 1.0f, wave.getCentreX(), wave.getBottom() + 1.0f, 1.0f);
    }

    // ── Play / pause glyph ───────────────────────────────────────────────────
    if (totalLen > 0.0)
    {
        const float glyphSize = juce::jmin (wave.getHeight() * 0.6f, 70.0f);
        const auto  glyphArea = juce::Rectangle<float> (glyphSize, glyphSize).withCentre (wave.getCentre());
        g.setColour (accent.withAlpha (isPlaying ? 0.18f : 0.32f));
        if (isPlaying)
        {
            juce::Path play;
            play.startNewSubPath (glyphArea.getX() + glyphArea.getWidth() * 0.18f, glyphArea.getY());
            play.lineTo (glyphArea.getRight() - glyphArea.getWidth() * 0.05f, glyphArea.getCentreY());
            play.lineTo (glyphArea.getX() + glyphArea.getWidth() * 0.18f, glyphArea.getBottom());
            play.closeSubPath();
            g.fillPath (play);
        }
        else
        {
            const float barW = glyphArea.getWidth() * 0.22f;
            const float gap  = glyphArea.getWidth() * 0.16f;
            const float yT   = glyphArea.getY();
            const float yB   = glyphArea.getBottom();
            const float cx   = glyphArea.getCentreX();
            g.fillRect (juce::Rectangle<float> (cx - gap / 2.0f - barW, yT, barW, yB - yT));
            g.fillRect (juce::Rectangle<float> (cx + gap / 2.0f,        yT, barW, yB - yT));
        }
    }

    // ── Zoom indicator (only when zoomed) ────────────────────────────────────
    if (zoomLevel > 1.05f)
    {
        g.setColour (accent.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.0f));
        g.drawText (juce::String (zoomLevel, 1) + "x",
                    wave.toNearestInt().withLeft (wave.toNearestInt().getRight() - 32).withHeight (12),
                    juce::Justification::centredRight);

        // Mini scrollbar at bottom
        const float sbH    = 3.0f;
        const float sbY    = wave.getBottom() - sbH - 1.0f;
        const float thumbW = wave.getWidth() / zoomLevel;
        const float thumbX = wave.getX() + scrollFraction * (wave.getWidth() - thumbW);
        g.setColour (juce::Colour (kHairline).withAlpha (0.4f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), sbY, wave.getWidth(), sbH));
        g.setColour (accent.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (thumbX, sbY, thumbW, sbH));
    }
}

bool WaveformDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3")
            return true;
    }
    return false;
}

void WaveformDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;
    const juce::File file (files[0]);
    if (onFileDropped)
        onFileDropped (file);
    setFile (file);
}

void WaveformDisplay::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}
