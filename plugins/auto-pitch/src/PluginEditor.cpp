#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

namespace
{
const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::StringArray scaleNames {
    "Major", "Minor", "Chromatic", "Dorian", "Phrygian", "Lydian",
    "Mixolydian", "Locrian", "Harmonic Minor", "Melodic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Blues"
};
constexpr std::array<unsigned short, 13> uiScaleMasks {
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    0x0fffu,
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<10),
    (1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<10),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<9)|(1u<<11),
    (1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9),
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10),
    (1u<<0)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)
};
}

// ============================================================================
// NoteDisplay
// ============================================================================
void NoteDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));

    auto noteArea = bounds.removeFromTop (bounds.getHeight() * 0.65f);
    auto barArea  = bounds;

    // Big note
    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (sattari::brand::fontOptions (juce::jmin (170.0f, noteArea.getHeight() * 0.9f),
                                  juce::Font::plain).withStyle ("Italic"));
    juce::String noteText = "-";
    if (snapshot.voiced && snapshot.detectedMidi >= 0)
    {
        const int pc = ((snapshot.detectedMidi % 12) + 12) % 12;
        noteText = noteNames[pc];
    }
    g.drawText (noteText, noteArea.toNearestInt(), juce::Justification::centred, false);

    // Cents bar — wide -100..+100 with a coloured indicator at correctionCents.
    barArea = barArea.reduced (10.0f, 6.0f);
    const float trackH = 14.0f;
    auto track = juce::Rectangle<float> (barArea.getX(),
                                          barArea.getCentreY() - trackH * 0.5f,
                                          barArea.getWidth(),
                                          trackH);
    g.setColour (juce::Colour (sattari::brand::kPanelRaised));
    g.fillRoundedRectangle (track, trackH * 0.5f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (track, trackH * 0.5f, 1.0f);

    // Centre tick.
    const float centerX = track.getCentreX();
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.drawLine (centerX, track.getY() - 4.0f, centerX, track.getBottom() + 4.0f, 1.0f);

    // Tick marks every 25 cents.
    for (int c = -100; c <= 100; c += 25)
    {
        if (c == 0) continue;
        const float x = juce::jmap ((float) c, -100.0f, 100.0f, track.getX(), track.getRight());
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawLine (x, track.getY() + 1.0f, x, track.getBottom() - 1.0f, 1.0f);
    }

    // Indicator: a coloured block sliding along the track, width ~= correction magnitude.
    if (snapshot.voiced)
    {
        const float cents = juce::jlimit (-100.0f, 100.0f, snapshot.correctionCents);
        const float w = juce::jmax (8.0f, std::abs (cents) * (track.getWidth() * 0.5f / 100.0f));
        const float x = (cents >= 0.0f) ? centerX : centerX - w;
        juce::Rectangle<float> indicator (cents >= 0.0f ? centerX : centerX - w,
                                          track.getY(),
                                          (cents >= 0.0f ? w : w),
                                          track.getHeight());
        juce::ignoreUnused (x);
        g.setColour (accent.withAlpha (0.55f));
        g.fillRoundedRectangle (indicator.reduced (1.0f), trackH * 0.5f - 1.0f);
    }

    // Scale labels
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (sattari::brand::fontOptions (10.0f));
    auto labelArea = juce::Rectangle<float> (track.getX(), track.getBottom() + 4.0f, track.getWidth(), 14.0f);
    g.drawText ("-100", labelArea.removeFromLeft (40.0f).toNearestInt(), juce::Justification::centredLeft, false);
    g.drawText ("+100", labelArea.removeFromRight (40.0f).toNearestInt(), juce::Justification::centredRight, false);
    g.drawText ("0", labelArea.toNearestInt(), juce::Justification::centred, false);
}

// ============================================================================
// KeyPillRow
// ============================================================================
void KeyPillRow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));

    // Two rows in the screenshot: top row has the 5 sharps/flats (C# D# F# G# A#),
    // bottom row has the 7 naturals (C D E F G A B). Render the same way.
    const float pillH = bounds.getHeight() * 0.42f;
    auto topRow = bounds.removeFromTop (pillH);
    bounds.removeFromTop (6.0f);
    auto botRow = bounds.removeFromTop (pillH);

    // Scale intervals mirror the processor's stable scale-index table.
    auto inScale = [&] (int pc) -> bool
    {
        const int degree = ((pc - keyRootIdx) % 12 + 12) % 12;
        const int idx = juce::jlimit (0, (int) uiScaleMasks.size() - 1, scaleIdx);
        return (uiScaleMasks[(size_t) idx] & (1u << degree)) != 0;
    };

    auto drawPill = [&] (juce::Rectangle<float> r, int pc)
    {
        const bool lit        = inScale (pc);
        const bool enabled    = (enabledMask >> pc) & 1;
        const bool isDetected = (detectedMidi >= 0)
                                && (((detectedMidi % 12) + 12) % 12) == pc;
        const auto pillBg = (lit && enabled) ? accent.withAlpha (isDetected ? 0.95f : 0.50f)
                                              : juce::Colour (sattari::brand::kPanelRaised);
        const auto stroke = (lit && enabled) ? accent.withAlpha (0.85f)
                                              : juce::Colour (sattari::brand::kHairline);
        const auto textCol = (! enabled) ? juce::Colour (sattari::brand::kTextFaint)
                                          : ((lit) ? juce::Colour (sattari::brand::kBackground)
                                                   : juce::Colour (sattari::brand::kTextMuted));
        g.setColour (pillBg);
        g.fillRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f);
        g.setColour (stroke);
        g.drawRoundedRectangle (r.reduced (2.0f), r.getHeight() * 0.5f, 1.0f);
        g.setColour (textCol);
        g.setFont (sattari::brand::fontOptions (r.getHeight() * 0.42f, juce::Font::plain));
        g.drawText (noteNames[pc], r.toNearestInt(), juce::Justification::centred, false);
        // Strike-through for blocked notes.
        if (! enabled)
        {
            g.setColour (juce::Colour (sattari::brand::kDanger).withAlpha (0.7f));
            const auto centre = r.getCentre();
            g.drawLine (r.getX() + 6.0f, centre.y, r.getRight() - 6.0f, centre.y, 1.4f);
        }
    };

    // Top row: positions match the black-key positions on a keyboard.
    // C#=1, D#=3, F#=6, G#=8, A#=10 → 5 pills, but laid out under the naturals
    // they're between. Simpler: just space the 5 sharps evenly.
    const int sharps[5] = { 1, 3, 6, 8, 10 };
    const float gapTop = 8.0f;
    const float pillWTop = (topRow.getWidth() - gapTop * 4.0f) / 5.0f;
    auto cursor = topRow;
    for (int i = 0; i < 5; ++i)
    {
        auto cell = cursor.removeFromLeft (pillWTop);
        sharpRects[i] = cell;       // cache hit-test rect
        drawPill (cell, sharps[i]);
        cursor.removeFromLeft (gapTop);
    }

    // Bottom row: 7 naturals
    const int naturals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const float gapBot = 6.0f;
    const float pillWBot = (botRow.getWidth() - gapBot * 6.0f) / 7.0f;
    cursor = botRow;
    for (int i = 0; i < 7; ++i)
    {
        auto cell = cursor.removeFromLeft (pillWBot);
        naturalRects[i] = cell;     // cache hit-test rect
        drawPill (cell, naturals[i]);
        cursor.removeFromLeft (gapBot);
    }
}

int KeyPillRow::hitTestPill (juce::Point<float> p) const
{
    static const int sharps[5] = { 1, 3, 6, 8, 10 };
    for (int i = 0; i < 5; ++i)
        if (sharpRects[i].contains (p)) return sharps[i];
    static const int naturals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        if (naturalRects[i].contains (p)) return naturals[i];
    return -1;
}

void KeyPillRow::mouseDown (const juce::MouseEvent& e)
{
    const int pc = hitTestPill (e.position);
    if (pc < 0) return;
    if (e.mods.isShiftDown())
    {
        if (onPillShiftClicked) onPillShiftClicked (pc);
    }
    else
    {
        if (onPillClicked) onPillClicked (pc);
    }
}

// ============================================================================
// KeyButton — one tactile control for the whole key story
// ============================================================================
void KeyButton::paint (juce::Graphics& g)
{
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));
    const bool linked = (mode != Mode::Manual);
    auto r = getLocalBounds().toFloat().reduced (1.5f);

    // Tactile body: hover brightens, press darkens; accent wash + border when a
    // link (AutoKey / Follow) is driving the key.
    auto body = juce::Colour (sattari::brand::kPanelRaised);
    if (pressed)     body = body.darker (0.30f);
    else if (hover)  body = body.brighter (0.14f);
    g.setColour (linked ? accent.withAlpha (pressed ? 0.30f : 0.16f) : body);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (linked ? accent.withAlpha (0.9f) : juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r, 10.0f, linked ? 1.8f : 1.0f);

    auto inner = r.reduced (14.0f, 10.0f);

    // Top row: mode chip (left) + confidence lock meter (right, only while auto).
    auto topRow = inner.removeFromTop (14.0f);
    const char* chip = mode == Mode::Auto ? "AUTO-DETECT"
                     : mode == Mode::Follow ? "FOLLOWING SONG KEY"
                     : mode == Mode::Map ? "KEY MAP" : "MANUAL KEY";
    g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    g.setColour (linked ? accent : juce::Colour (sattari::brand::kTextMuted));
    const juce::String chipText = (linked ? juce::String::fromUTF8 ("\xe2\x9b\x93  ") : juce::String())
                                + chip;   // U+26D3 chains glyph when linked
    g.drawText (chipText, topRow, juce::Justification::centredLeft, false);

    // Confidence lock meter: 5 dots that fill as detection settles. Shown for the
    // live detectors (Auto / Follow / Map); hidden for a hand-set key (no detection).
    if (linked)
    {
        auto meter = topRow.removeFromRight (78.0f);
        g.setFont (sattari::brand::fontOptions (8.5f, juce::Font::bold));
        g.setColour (juce::Colour (sattari::brand::kTextFaint));
        g.drawText ("LOCK", meter.removeFromLeft (26.0f), juce::Justification::centredRight, false);
        meter.removeFromLeft (4.0f);
        const int lit = juce::roundToInt (confidence * 5.0f);
        const float d = 6.0f, gap = 3.0f;
        float x = meter.getX();
        const float cy = meter.getCentreY();
        for (int i = 0; i < 5; ++i)
        {
            juce::Rectangle<float> dot (x, cy - d * 0.5f, d, d);
            g.setColour (i < lit ? accent : accent.withAlpha (0.22f));
            g.fillEllipse (dot);
            x += d + gap;
        }
    }

    // Big key name (centre) — the headline the whole plugin is tuning to.
    static const char* names[]   = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const juce::String keyText = juce::String (names[juce::jlimit (0, 11, rootIdx)])
                               + "  " + scaleNames[juce::jlimit (0, scaleNames.size() - 1, scaleIndex)].toLowerCase();
    g.setColour (juce::Colour (sattari::brand::kTextPrimary));
    g.setFont (sattari::brand::fontOptions (juce::jmin (32.0f, inner.getHeight() * 0.9f), juce::Font::bold));
    g.drawText (keyText, inner, juce::Justification::centredLeft, false);
}

void KeyButton::mouseDown (const juce::MouseEvent&)
{
    pressed = true;
    repaint();

    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    juce::PopupMenu menu;
    menu.addItem (1, "Link AutoKey  (auto-detect from this track)", true, mode == Mode::Auto);
    menu.addItem (2, "Follow Song Key  (from Sattari Key on master)", true, mode == Mode::Follow);
    menu.addSeparator();

    juce::PopupMenu manual;
    for (int root = 0; root < 12; ++root)
    {
        juce::PopupMenu sub;
        for (int scale = 0; scale < scaleNames.size(); ++scale)
            sub.addItem (100 + root * 20 + scale, scaleNames[scale], true,
                         mode == Mode::Manual && rootIdx == root && scaleIndex == scale);
        manual.addSubMenu (names[root], sub);
    }
    menu.addSubMenu ("Set key manually", manual);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int id)
        {
            pressed = false;
            repaint();
            if (id == 1)       { if (onLinkAuto)   onLinkAuto(); }
            else if (id == 2)  { if (onLinkFollow) onLinkFollow(); }
            else if (id >= 100){ const int root = (id - 100) / 20, sc = (id - 100) % 20;
                                 if (onManualKey) onManualKey (root, sc); }
        });
}

// ============================================================================
// PitchEditTimeline
// ============================================================================
void PitchEditTimeline::setData (std::vector<Entry> newEntries, double newPlayhead)
{
    entries = std::move (newEntries);
    playheadSec = juce::jmax (0.0, newPlayhead);
    timelineDurationSec = 10.0;
    for (const auto& entry : entries)
        timelineDurationSec = juce::jmax (timelineDurationSec, entry.endSec + entry.timeOffsetSec + 1.0);
    selected = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].id == selectedEntryId) { selected = i; break; }
    if (selected < 0) selectedEntryId = 0;
    if (fitToContent)
    {
        viewStartSec = 0.0;
        viewDurationSec = timelineDurationSec;
    }
    viewDurationSec = juce::jlimit (0.25, timelineDurationSec, viewDurationSec);
    viewStartSec = juce::jlimit (0.0, juce::jmax (0.0, timelineDurationSec - viewDurationSec), viewStartSec);
    repaint();
}

void PitchEditTimeline::setHarmonyOverlays (std::vector<HarmonyOverlay> newOverlays)
{
    harmonyOverlays = std::move (newOverlays);
    for (const auto& voice : harmonyOverlays)
        timelineDurationSec = juce::jmax (timelineDurationSec, voice.endSec + 1.0);
    repaint();
}

void PitchEditTimeline::zoomIn()
{
    fitToContent = false;
    const double centre = viewStartSec + 0.5 * viewDurationSec;
    viewDurationSec = juce::jmax (0.25, viewDurationSec * 0.65);
    viewStartSec = juce::jlimit (0.0, juce::jmax (0.0, timelineDurationSec - viewDurationSec), centre - 0.5 * viewDurationSec);
    repaint();
}

void PitchEditTimeline::zoomOut()
{
    fitToContent = false;
    const double centre = viewStartSec + 0.5 * viewDurationSec;
    viewDurationSec = juce::jmin (timelineDurationSec, viewDurationSec / 0.65);
    viewStartSec = juce::jlimit (0.0, juce::jmax (0.0, timelineDurationSec - viewDurationSec), centre - 0.5 * viewDurationSec);
    repaint();
}

void PitchEditTimeline::fitAll()
{
    fitToContent = true;
    viewStartSec = 0.0;
    viewDurationSec = timelineDurationSec;
    repaint();
}

int PitchEditTimeline::minMidi() const
{
    int result = 60;
    if (! entries.empty() || ! harmonyOverlays.empty())
    {
        result = 127;
        for (const auto& e : entries) result = juce::jmin (result, e.sourceMidi, e.targetMidi);
        for (const auto& voice : harmonyOverlays) result = juce::jmin (result, voice.midi);
    }
    return juce::jlimit (0, 120, result - 2);
}

int PitchEditTimeline::maxMidi() const
{
    int result = 72;
    if (! entries.empty() || ! harmonyOverlays.empty())
    {
        result = 0;
        for (const auto& e : entries) result = juce::jmax (result, e.sourceMidi, e.targetMidi);
        for (const auto& voice : harmonyOverlays) result = juce::jmax (result, voice.midi);
    }
    return juce::jlimit (7, 127, result + 2);
}

juce::Rectangle<float> PitchEditTimeline::entryRect (int index) const
{
    if (index < 0 || index >= (int) entries.size()) return {};
    const auto plot = getLocalBounds().toFloat().reduced (44.0f, 20.0f);
    const int lo = minMidi(), hi = maxMidi();
    const auto& e = entries[(size_t) index];
    const double visualStart = e.startSec + e.timeOffsetSec;
    const double visualEnd = e.endSec + e.timeOffsetSec;
    const float x0 = juce::jmap ((float) visualStart, (float) viewStartSec, (float) (viewStartSec + viewDurationSec), plot.getX(), plot.getRight());
    const float x1 = juce::jmap ((float) visualEnd,   (float) viewStartSec, (float) (viewStartSec + viewDurationSec), plot.getX(), plot.getRight());
    const float row = plot.getHeight() / (float) juce::jmax (1, hi - lo + 1);
    const float y = plot.getBottom() - ((float) (e.targetMidi - lo) + 1.0f) * row;
    return { x0, y, juce::jmax (5.0f, x1 - x0), juce::jmax (5.0f, row - 1.0f) };
}

int PitchEditTimeline::hitEntry (juce::Point<float> point) const
{
    for (int i = (int) entries.size() - 1; i >= 0; --i)
        if (entryRect (i).expanded (2.0f).contains (point)) return i;
    return -1;
}

void PitchEditTimeline::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = bounds.reduced (44.0f, 20.0f);
    const auto accent = juce::Colour (sattari::brand::productAccent ("auto-pitch"));
    g.setColour (juce::Colour (sattari::brand::kPanel));
    g.fillRoundedRectangle (bounds, 14.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 14.0f, 1.0f);

    const int lo = minMidi(), hi = maxMidi();
    const float row = plot.getHeight() / (float) juce::jmax (1, hi - lo + 1);
    g.setFont (sattari::brand::fontOptions (9.0f));
    for (int midi = lo; midi <= hi; ++midi)
    {
        const float y = plot.getBottom() - ((float) (midi - lo) + 0.5f) * row;
        const bool cLine = midi % 12 == 0;
        g.setColour (juce::Colour (cLine ? sattari::brand::kHairline : sattari::brand::kPanelRaised));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        if (cLine)
        {
            g.setColour (juce::Colour (sattari::brand::kTextMuted));
            g.drawText ("C" + juce::String (midi / 12 - 1), 4, (int) y - 7, 36, 14,
                        juce::Justification::centredRight, false);
        }
    }

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    // Generated voices are read-only objects behind the lead notes. Their stable
    // colour/lane labels make the audible arrangement inspectable without
    // pretending they are independent source recordings.
    const std::array<juce::Colour, 4> voiceColours {
        juce::Colour (0xff60a5fa), juce::Colour (0xffc084fc),
        juce::Colour (0xff34d399), juce::Colour (0xfff59e0b)
    };
    for (const auto& voice : harmonyOverlays)
    {
        const float x0 = juce::jmap ((float) voice.startSec, (float) viewStartSec,
            (float) (viewStartSec + viewDurationSec), plot.getX(), plot.getRight());
        const float x1 = juce::jmap ((float) voice.endSec, (float) viewStartSec,
            (float) (viewStartSec + viewDurationSec), plot.getX(), plot.getRight());
        const float y = plot.getBottom() - ((float) (voice.midi - lo) + 1.0f) * row;
        juce::Rectangle<float> r (x0, y, juce::jmax (5.0f, x1 - x0), juce::jmax (4.0f, row - 2.0f));
        const auto colour = voiceColours[(size_t) juce::jlimit (0, 3, voice.voice)];
        g.setColour (colour.withAlpha (0.24f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colour.withAlpha (0.78f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        if (r.getWidth() > 44.0f)
        {
            g.setFont (sattari::brand::fontOptions (8.0f, juce::Font::bold));
            g.drawText ("H" + juce::String (voice.voice + 1), r.toNearestInt().reduced (3, 0),
                        juce::Justification::centredLeft, false);
        }
    }
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        auto r = entryRect (i);
        const auto colour = e.enabled ? accent : juce::Colour (sattari::brand::kTextFaint);
        g.setColour (colour.withAlpha (i == selected ? 0.95f : 0.62f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colour.brighter (0.2f));
        g.drawRoundedRectangle (r, 4.0f, i == selected ? 2.0f : 1.0f);
        if (r.getWidth() > 30.0f)
        {
            const int pc = ((e.targetMidi % 12) + 12) % 12;
            g.setColour (juce::Colour (sattari::brand::kBackground));
            g.setFont (sattari::brand::fontOptions (9.0f, juce::Font::bold));
            g.drawText (names[pc], r.toNearestInt(), juce::Justification::centred, false);
        }
    }

    if (playheadSec >= viewStartSec && playheadSec <= viewStartSec + viewDurationSec)
    {
        const float x = juce::jmap ((float) playheadSec, (float) viewStartSec,
                                    (float) (viewStartSec + viewDurationSec), plot.getX(), plot.getRight());
        g.setColour (accent.withAlpha (0.9f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    if (entries.empty())
    {
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.setFont (sattari::brand::fontOptions (14.0f));
        g.drawText ("Arm Capture notes and play the vocal", plot.toNearestInt(),
                    juce::Justification::centred, false);
    }
}

void PitchEditTimeline::mouseDown (const juce::MouseEvent& e)
{
    selected = hitEntry (e.position);
    selectedEntryId = selected >= 0 ? entries[(size_t) selected].id : 0;
    if (onSelectionChanged) onSelectionChanged (selectedEntryId);
    if (selected < 0) { repaint(); return; }
    dragStartY = e.position.y;
    dragStartTarget = entries[(size_t) selected].targetMidi;
    if (e.mods.isPopupMenu())
    {
        const bool next = ! entries[(size_t) selected].enabled;
        entries[(size_t) selected].enabled = next;
        if (onEntryChanged) onEntryChanged (entries[(size_t) selected]);
    }
    else if (onGestureBegin) onGestureBegin();
    repaint();
}

void PitchEditTimeline::mouseDrag (const juce::MouseEvent& e)
{
    if (selected < 0 || selected >= (int) entries.size()) return;
    const auto plot = getLocalBounds().toFloat().reduced (44.0f, 20.0f);
    const float row = plot.getHeight() / (float) juce::jmax (1, maxMidi() - minMidi() + 1);
    const int delta = juce::roundToInt ((dragStartY - e.position.y) / juce::jmax (1.0f, row));
    const int target = juce::jlimit (0, 127, dragStartTarget + delta);
    if (entries[(size_t) selected].targetMidi != target)
    {
        entries[(size_t) selected].targetMidi = target;
        if (onEntryChanged) onEntryChanged (entries[(size_t) selected]);
        repaint();
    }
}

void PitchEditTimeline::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && selected >= 0 && onGestureEnd) onGestureEnd();
}

void PitchEditTimeline::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    fitToContent = false;
    if (std::abs (wheel.deltaY) >= std::abs (wheel.deltaX))
    {
        const auto plot = getLocalBounds().toFloat().reduced (44.0f, 20.0f);
        const double anchor = juce::jmap ((double) juce::jlimit (plot.getX(), plot.getRight(), e.position.x),
                                          (double) plot.getX(), (double) plot.getRight(),
                                          viewStartSec, viewStartSec + viewDurationSec);
        const double scale = wheel.deltaY > 0.0f ? 0.78 : 1.0 / 0.78;
        const double nextDuration = juce::jlimit (0.25, timelineDurationSec, viewDurationSec * scale);
        const double fraction = plot.getWidth() > 0.0f ? (e.position.x - plot.getX()) / plot.getWidth() : 0.5;
        viewStartSec = juce::jlimit (0.0, juce::jmax (0.0, timelineDurationSec - nextDuration),
                                     anchor - fraction * nextDuration);
        viewDurationSec = nextDuration;
    }
    else
    {
        viewStartSec = juce::jlimit (0.0, juce::jmax (0.0, timelineDurationSec - viewDurationSec),
                                     viewStartSec - (double) wheel.deltaX * viewDurationSec);
    }
    repaint();
}

// ============================================================================
// Editor
// ============================================================================
SattariAutoPitchAudioProcessorEditor::SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "auto-pitch")
{
    setLookAndFeel (&lookAndFeel);
    setResizable (true, true);
    setResizeLimits (960, 760, 1600, 1100);
    setSize (1120, 820);

    // Header
    // The shared title treatment already paints the full Sattari mark. Keep the
    // adjacent wordmark product-only so the brand is not repeated or clipped.
    sattari::ui::SattariLookAndFeel::styleTitle (title, "Auto Pitch");
    addAndMakeVisible (title);

    vocalRange.addItemList ({
        "Wide",
        "Bass Voice", "Baritone", "Tenor", "Alto / Tenor", "Alto", "Soprano",
        "Instrument: Bass", "Instrument: Mid", "Instrument: High"
    }, 1);
    // keyRoot / scale are still populated + attached (they hold the manual key),
    // but the header dropdowns are gone — the single KeyButton is now the only
    // key control, so we don't add keyRoot/scale/keyScaleCaption to the view.
    keyRoot.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
    scale.addItemList ({ "Major", "Minor", "Chromatic" }, 1);
    scaleMode.addItemList (scaleNames, 1);
    for (auto* c : { &vocalRange, &factoryPresets, &userPresets })
        addAndMakeVisible (*c);

    styleCaption (vocalRangeCaption, "SOUND SOURCE");
    addAndMakeVisible (vocalRangeCaption);

    factoryPresets.setTextWhenNothingSelected ("Factory");
    registerFactoryPresets();
    factoryPresets.addItemList (presetManager.factoryPresetNames(), 1);
    factoryPresets.onChange = [this] {
        const int i = factoryPresets.getSelectedItemIndex();
        if (i >= 0) presetManager.loadFactoryPreset (i);
    };
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this] {
        const auto n = userPresets.getText();
        if (n.isNotEmpty()) presetManager.loadUserPreset (n);
    };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    refreshUserPresets();

    // Tabs
    //  Single view: all three former tab panels are bands of one page. The
    //  ADVANCED drawer (the old PRO tab) starts collapsed — the everyday controls
    //  are the whole first impression, which is the "minimal" in minimal UI.
    liveModeButton.setClickingTogglesState (true);
    liveModeButton.setTooltip ("Live Mode: causal tracking and monitoring. Enable 2MS for "
                               "the Extreme renderer; disable it for waveform-locked Ultra. "
                               "Extended Range reaches 70 Hz at about 22 ms.");
    liveModeAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "liveMode", liveModeButton);
    addAndMakeVisible (liveModeButton);

    extremeLiveButton.setClickingTogglesState (true);
    extremeLiveButton.setTooltip ("Extreme Live: fixed 96-sample / 2.0 ms processing latency "
                                  "at 48 kHz. Available with the Ultra profile.");
    extremeLiveAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "extremeLive", extremeLiveButton);
    addAndMakeVisible (extremeLiveButton);

    liveProfile.addItemList ({ "ULTRA C3+", "RANGE 70HZ" }, 1);
    liveProfile.setTooltip ("Ultra + 2MS: 96 samples at 48 kHz. Ultra without 2MS: "
                            "571 samples, waveform-locked and optimized for C3 and above. "
                            "Range: 1,037 samples, tracking down to 70 Hz.");
    liveProfileAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "liveProfile", liveProfile);
    addAndMakeVisible (liveProfile);

    latencyReadout.setJustificationType (juce::Justification::centredLeft);
    latencyReadout.setInterceptsMouseClicks (false, false);
    latencyReadout.setText ("60.1 ms STUDIO", juce::dontSendNotification);   // real value on the first tick
    addAndMakeVisible (latencyReadout);

    advancedToggle.setClickingTogglesState (true);
    advancedToggle.onClick = [this]
    {
        advancedOpen = advancedToggle.getToggleState();
        resized();
    };
    addAndMakeVisible (advancedToggle);

    //  Workspace navigation. Radio-style; TUNE is the everyday default.
    for (auto* b : { &wsKeyButton, &wsTuneButton, &wsHarmonyButton, &wsEditButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (77);
        addAndMakeVisible (*b);
    }
    wsTuneButton.setToggleState (true, juce::dontSendNotification);
    wsKeyButton.onClick     = [this] { setWorkspace (Workspace::key); };
    wsTuneButton.onClick    = [this] { setWorkspace (Workspace::tune); };
    wsEditButton.onClick    = [this] { setWorkspace (Workspace::edit); };

    headerSummary.setJustificationType (juce::Justification::centredLeft);
    headerSummary.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (headerSummary);

    //  KEY workspace. Pure view over the shared song model.
    addAndMakeVisible (keyPanel);
    keyPanel.setVisible (false);
    auto initKeyLabel = [this] (juce::Label& l, float size, bool bold)
    {
        l.setJustificationType (juce::Justification::centredLeft);
        l.setFont (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
        keyPanel.addAndMakeVisible (l);
    };
    initKeyLabel (keyBigLabel, 44.0f, true);
    initKeyLabel (keyMetaLabel, 18.0f, false);
    initKeyLabel (keyAltLabel, 14.0f, false);
    initKeyLabel (keySectionsLabel, 14.0f, false);
    initKeyLabel (keyInspectorLabel, 14.0f, false);
    initKeyLabel (keyHintLabel, 16.0f, false);
    keyHintLabel.setJustificationType (juce::Justification::centred);
    keyHintLabel.setText ("Drop a song here to map its keys, sections, and tempo",
                          juce::dontSendNotification);
    keyPanel.addAndMakeVisible (songMapView);
    songMapView.onSegmentClicked = [this] (double sec)
    {
        const auto a = audioProcessor.songAnalysisCopy();
        static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        for (size_t ri = 0; ri < a.scaleRegions.size(); ++ri)
        {
            const auto& r = a.scaleRegions[ri];
            if (sec < r.startSec || sec >= r.endSec) continue;
            selectedRegionIndex = (int) ri;
            regionRootBox.setSelectedId (r.root + 1, juce::dontSendNotification);
            regionScaleBox.setSelectedId (r.minor ? 2 : 1, juce::dontSendNotification);
            auto fmt = [] (double t) { return juce::String ((int) t / 60) + ":"
                + juce::String ((int) t % 60).paddedLeft ('0', 2); };
            keyInspectorLabel.setText (
                "REGION  " + fmt (r.startSec) + " - " + fmt (r.endSec)
                + "   " + juce::String (nm[juce::jlimit (0, 11, r.root)])
                + (r.minor ? " minor" : " major")
                + "   margin " + juce::String (r.confidence, 3)
                + (r.userSet ? "   USER SET"
                   : r.confidence >= SattariAutoPitchAudioProcessor::kMinBusKeyConfidence
                       ? "" : "   (below trust gate - shown, not applied)"),
                juce::dontSendNotification);
            break;
        }
    };

    //  KEY: chord + modulation readouts, Follow Song Map, and region editing.
    initKeyLabel (keyChordsLabel, 13.0f, false);
    initKeyLabel (keyModsLabel, 13.0f, false);
    keyChordsLabel.setText ("CHORDS   -", juce::dontSendNotification);
    keyModsLabel.setText ("CHANGES  -", juce::dontSendNotification);
    followSongMapButton.setClickingTogglesState (true);
    followSongMapButton.setTooltip ("Correction follows the analysed song's scale regions "
                                    "as the transport moves, so a verse and a chorus in "
                                    "different modes are both legal in their own places.");
    followSongMapAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "followSongMap", followSongMapButton);
    keyPanel.addAndMakeVisible (followSongMapButton);

    static const char* kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    for (int i = 0; i < 12; ++i) regionRootBox.addItem (kNoteNames[i], i + 1);
    regionScaleBox.addItem ("major", 1);
    regionScaleBox.addItem ("minor", 2);
    regionRootBox.setSelectedId (1, juce::dontSendNotification);
    regionScaleBox.setSelectedId (1, juce::dontSendNotification);
    regionApplyButton.setTooltip ("Your key for this region outranks detection permanently, "
                                  "and reports full confidence: you asserting the key IS the evidence.");
    regionApplyButton.onClick = [this]
    {
        //  Say so rather than doing nothing. After every editor reopen no region
        //  is selected, and a silent no-op reads as a broken button.
        if (selectedRegionIndex < 0)
        {
            keyInspectorLabel.setText ("Click a region on the map first, then set its key",
                                       juce::dontSendNotification);
            return;
        }
        //  The index is only meaningful for the model it came from: a re-analysis
        //  between click and apply would stamp a permanent user key onto a region
        //  the user never selected.
        const auto model = audioProcessor.songAnalysisCopy();
        if (selectedRegionIndex >= (int) model.scaleRegions.size()
            || std::abs (model.durationSec - songModelSeenDuration) > 1.0e-6)
        {
            selectedRegionIndex = -1;
            keyInspectorLabel.setText ("The analysis changed - click the region again",
                                       juce::dontSendNotification);
            return;
        }
        audioProcessor.setRegionKey (selectedRegionIndex,
                                     regionRootBox.getSelectedId() - 1,
                                     regionScaleBox.getSelectedId() == 2);
        songModelSeenRegions = -1;   // force the page to rebuild from the model
        pendingRegionConfirm = "Region key set to "
            + regionRootBox.getText() + " " + regionScaleBox.getText() + "   USER SET";
    };
    keyPanel.addAndMakeVisible (regionRootBox);
    keyPanel.addAndMakeVisible (regionScaleBox);
    keyPanel.addAndMakeVisible (regionApplyButton);

    //  HARMONY workspace.
    addAndMakeVisible (harmonyPanel);
    harmonyPanel.setVisible (false);
    harmonyHeaderLabel.setJustificationType (juce::Justification::centredLeft);
    harmonyHeaderLabel.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    harmonyHeaderLabel.setText ("HARMONY", juce::dontSendNotification);
    harmonyPanel.addAndMakeVisible (harmonyHeaderLabel);
    harmonyStatusLabel.setJustificationType (juce::Justification::centredLeft);
    harmonyStatusLabel.setText ("Play a vocal to hear and inspect the generated voices",
                                juce::dontSendNotification);
    harmonyPanel.addAndMakeVisible (harmonyStatusLabel);
    harmonyModeBox.addItem ("Intelligent  (chord-aware)", 1);
    harmonyModeBox.addItem ("Interval  (fixed diatonic)", 2);
    harmonyModeBox.addItem ("MIDI  (held notes)", 3);
    harmonyModeAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "harmonyMode", harmonyModeBox);
    harmonyPanel.addAndMakeVisible (harmonyModeBox);

    const std::array<juce::Slider*, 12> macroSliders {
        &harmonyMix, &harmonyLead, &harmonyGlide, &harmonyHumanPitch,
        &harmonyHumanTime, &harmonyVibratoRate, &harmonyEnsemble, &harmonyEnsembleDepth,
        &harmonyEnsembleRate, &harmonyEnsembleSpread, &harmonyLowCut,
        &harmonyHighCut
    };
    const std::array<const char*, 12> macroIds {
        "harmonyMix", "harmonyLead", "harmonyGlide", "harmonyHumanPitch",
        "harmonyHumanTime", "harmonyVibratoRate", "harmonyEnsemble", "harmonyEnsembleDepth",
        "harmonyEnsembleRate", "harmonyEnsembleSpread", "harmonyLowCut",
        "harmonyHighCut"
    };
    const std::array<const char*, 12> macroNames {
        "HARMONY", "LEAD", "GLIDE MS", "PITCH HUMAN", "TIME HUMAN", "VIB RATE",
        "CHOIR", "CHOIR DEPTH", "CHOIR RATE", "CHOIR WIDTH", "LOW CUT", "HIGH CUT"
    };
    for (int i = 0; i < (int) macroSliders.size(); ++i)
    {
        auto& slider = *macroSliders[(size_t) i];
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        harmonyPanel.addAndMakeVisible (slider);
        auto& label = harmonyMacroLabels[(size_t) i];
        label.setText (macroNames[(size_t) i], juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredLeft);
        label.setFont (juce::Font (sattari::brand::fontOptions (9.0f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
        harmonyPanel.addAndMakeVisible (label);
        harmonyMacroAttachments[(size_t) i] = std::make_unique<SliderAttachment> (
            audioProcessor.parameters(), macroIds[(size_t) i], slider);
    }
    harmonyVoiceOnly.setClickingTogglesState (true);
    harmonyVoiceOnly.setTooltip ("Audition generated singers without the lead vocal.");
    harmonyVoiceOnlyAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "harmonyVoiceOnly", harmonyVoiceOnly);
    harmonyPanel.addAndMakeVisible (harmonyVoiceOnly);

    for (int v = 0; v < 4; ++v)
    {
        auto& strip = harmonyVoices[(size_t) v];
        const auto id = juce::String ("h") + juce::String (v + 1);
        strip.caption.setText ("H" + juce::String (v + 1), juce::dontSendNotification);
        strip.caption.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        harmonyPanel.addAndMakeVisible (strip.caption);

        strip.enable.setButtonText ("ON");
        strip.enable.setClickingTogglesState (true);
        strip.enableAttach = std::make_unique<ButtonAttachment> (
            audioProcessor.parameters(), id + "Enable", strip.enable);
        harmonyPanel.addAndMakeVisible (strip.enable);
        strip.solo.setButtonText ("S");
        strip.solo.setClickingTogglesState (true);
        strip.soloAttach = std::make_unique<ButtonAttachment> (
            audioProcessor.parameters(), id + "Solo", strip.solo);
        harmonyPanel.addAndMakeVisible (strip.solo);

        for (const char* r : { "Upper", "Lower", "High", "Low",
                               "Octave Up", "Octave Down", "Choir" })
            strip.role.addItem (r, strip.role.getNumItems() + 1);
        strip.roleAttach = std::make_unique<ComboAttachment> (
            audioProcessor.parameters(), id + "Role", strip.role);
        harmonyPanel.addAndMakeVisible (strip.role);

        strip.degrees.setSliderStyle (juce::Slider::IncDecButtons);
        strip.degrees.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 54, 20);
        strip.degrees.setTooltip ("Interval mode: diatonic degrees from the lead "
                                  "(2 = a third in the current key).");
        strip.degreesAttach = std::make_unique<SliderAttachment> (
            audioProcessor.parameters(), id + "Degrees", strip.degrees);
        harmonyPanel.addAndMakeVisible (strip.degrees);

        strip.octave.setSliderStyle (juce::Slider::IncDecButtons);
        strip.octave.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 54, 20);
        strip.octaveAttach = std::make_unique<SliderAttachment> (
            audioProcessor.parameters(), id + "Octave", strip.octave);
        harmonyPanel.addAndMakeVisible (strip.octave);

        strip.readout.setJustificationType (juce::Justification::centredLeft);
        strip.readout.setText ("-", juce::dontSendNotification);
        harmonyPanel.addAndMakeVisible (strip.readout);

        const std::array<juce::Slider*, 5> voiceSliders {
            &strip.level, &strip.pan, &strip.delay, &strip.detune, &strip.vibrato
        };
        const std::array<juce::Label*, 5> voiceLabels {
            &strip.levelLabel, &strip.panLabel, &strip.delayLabel,
            &strip.detuneLabel, &strip.vibratoLabel
        };
        const std::array<const char*, 5> suffixes { " dB", "", " ms", " ct", " ct" };
        const std::array<const char*, 5> names { "LEVEL", "PAN", "DELAY", "DETUNE", "VIBRATO" };
        const std::array<juce::String, 5> ids {
            id + "Level", id + "Pan", id + "Delay", id + "Detune", id + "Vibrato"
        };
        for (int i = 0; i < 5; ++i)
        {
            auto& slider = *voiceSliders[(size_t) i];
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 17);
            slider.setTextValueSuffix (suffixes[(size_t) i]);
            harmonyPanel.addAndMakeVisible (slider);
            auto& label = *voiceLabels[(size_t) i];
            label.setText (names[(size_t) i], juce::dontSendNotification);
            label.setFont (juce::Font (sattari::brand::fontOptions (8.5f, juce::Font::bold)));
            label.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
            harmonyPanel.addAndMakeVisible (label);
        }
        strip.levelAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), ids[0], strip.level);
        strip.panAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), ids[1], strip.pan);
        strip.delayAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), ids[2], strip.delay);
        strip.detuneAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), ids[3], strip.detune);
        strip.vibratoAttach = std::make_unique<SliderAttachment> (audioProcessor.parameters(), ids[4], strip.vibrato);
    }
    wsHarmonyButton.setEnabled (true);
    wsHarmonyButton.onClick = [this] { setWorkspace (Workspace::harmony); };
    wsHarmonyButton.setTooltip ("Four rendered voices with chord-aware planning and choir controls.");

    songStatusLabel.setJustificationType (juce::Justification::centredRight);
    songStatusLabel.setInterceptsMouseClicks (false, false);
    songStatusLabel.setText ("Drop a song anywhere to detect its key + tempo",
                             juce::dontSendNotification);
    addAndMakeVisible (songStatusLabel);

    // Panels
    addAndMakeVisible (basicPanel);
    addAndMakeVisible (proPanel);
    addAndMakeVisible (editPanel);

    // ----- Basic -----
    // One unified tactile key control (replaces the AutoKey / Follow toggles +
    // status label). Its menu links AutoKey / Follow or sets the key by hand;
    // Auto and Follow are mutually exclusive, and a manual pick drops both.
    basicPanel.addAndMakeVisible (keyButton);
    keyButton.onLinkAuto = [this]
    {
        const bool on = audioProcessor.parameters().getRawParameterValue ("autoKey")->load() > 0.5f;
        setKeyParam ("followSongKey", 0.0f);
        setKeyParam ("autoKey", on ? 0.0f : 1.0f);
    };
    keyButton.onLinkFollow = [this]
    {
        const bool on = audioProcessor.parameters().getRawParameterValue ("followSongKey")->load() > 0.5f;
        setKeyParam ("autoKey", 0.0f);
        setKeyParam ("followSongKey", on ? 0.0f : 1.0f);
    };
    keyButton.onManualKey = [this] (int root, int scaleIdx)
    {
        setKeyParam ("autoKey", 0.0f);
        setKeyParam ("followSongKey", 0.0f);
        setKeyParam ("keyRoot", (float) root);
        const int legacy = scaleIdx == 2 ? 2
                         : (scaleIdx == 1 || scaleIdx == 3 || scaleIdx == 4
                            || scaleIdx == 7 || scaleIdx == 8 || scaleIdx == 9
                            || scaleIdx == 11 || scaleIdx == 12) ? 1 : 0;
        setKeyParam ("scale", (float) legacy);
        setKeyParam ("scaleMode", (float) scaleIdx);
    };

    // Three explicit action buttons under the readout — the discoverable controls.
    // AUTO-DETECT / FOLLOW toggle the live sources; SET KEY opens a direct key picker.
    // (The KeyButton readout itself still opens the same combined menu as a shortcut.)
    for (auto* b : { &autoDetectBtn, &followSongBtn, &setKeyBtn })
        basicPanel.addAndMakeVisible (*b);
    autoDetectBtn.onClick = [this] { if (keyButton.onLinkAuto)   keyButton.onLinkAuto(); };
    followSongBtn.onClick = [this] { if (keyButton.onLinkFollow) keyButton.onLinkFollow(); };
    setKeyBtn.onClick = [this]
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        auto& a = audioProcessor.parameters();
        const int curRoot  = juce::jlimit (0, 11, (int) std::round (a.getRawParameterValue ("keyRoot")->load()));
        const int curScale = juce::jlimit (0, scaleNames.size() - 1,
            (int) std::round (a.getRawParameterValue ("scaleMode")->load()));
        const bool manual  = a.getRawParameterValue ("autoKey")->load() < 0.5f
                          && a.getRawParameterValue ("followSongKey")->load() < 0.5f;
        juce::PopupMenu menu;
        for (int root = 0; root < 12; ++root)
        {
            juce::PopupMenu sub;
            for (int scaleIdx = 0; scaleIdx < scaleNames.size(); ++scaleIdx)
                sub.addItem (100 + root * 20 + scaleIdx, scaleNames[scaleIdx], true,
                             manual && curRoot == root && curScale == scaleIdx);
            menu.addSubMenu (names[root], sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setKeyBtn),
            [this, curRoot] (int id)
            {
                juce::ignoreUnused (curRoot);
                if (id >= 100)  { const int root = (id - 100) / 20, sc = (id - 100) % 20;
                                       if (keyButton.onManualKey) keyButton.onManualKey (root, sc); }
            });
    };

    basicPanel.addAndMakeVisible (noteDisplay);
    basicPanel.addAndMakeVisible (pillRow);
    // Plain click toggles whether the note is a valid correction target.
    pillRow.onPillClicked = [this] (int pc) {
        if (auto* param = audioProcessor.parameters().getParameter ("enabledNotes"))
        {
            const auto& range = param->getNormalisableRange();
            const int current = (int) std::round (range.convertFrom0to1 (param->getValue()));
            const int flipped = current ^ (1 << pc);
            param->beginChangeGesture();
            param->setValueNotifyingHost (range.convertTo0to1 ((float) flipped));
            param->endChangeGesture();
        }
    };
    // Shift-click sets the key root (the previous behaviour).
    pillRow.onPillShiftClicked = [this] (int pc) {
        if (auto* param = audioProcessor.parameters().getParameter ("keyRoot"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) pc));
            param->endChangeGesture();
        }
    };
    styleCaption (editNotesLabel, "EDIT NOTES");
    basicPanel.addAndMakeVisible (editNotesLabel);


    styleKnob (retuneSpeed, true);
    styleKnob (flexTune);
    styleKnob (humanize);
    humanize.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    flexTune.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    retuneSpeed.textFromValueFunction = [] (double v) {
        return juce::String ((int) std::round (v * 100.0));
    };
    styleCaption (retuneSpeedCaption, "RETUNE SPEED");
    // This Basic knob is attached to the `amount` param (correction strength), NOT the
    // Flex-Tune param — label it accordingly so it doesn't collide with the Pro Flex-Tune.
    styleCaption (flexTuneCaption, "AMOUNT");
    styleCaption (humanizeCaption, "HUMANIZE");
    basicPanel.addAndMakeVisible (retuneSpeed);
    basicPanel.addAndMakeVisible (flexTune);
    basicPanel.addAndMakeVisible (humanize);
    basicPanel.addAndMakeVisible (retuneSpeedCaption);
    basicPanel.addAndMakeVisible (flexTuneCaption);
    basicPanel.addAndMakeVisible (humanizeCaption);

    styleCaption (styleCaptionLabel, "STYLE");
    styleSelect.addItemList ({ "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" }, 1);
    basicPanel.addAndMakeVisible (styleCaptionLabel);
    basicPanel.addAndMakeVisible (styleSelect);

    styleCaption (detuneLabel, "Detune live");
    basicPanel.addAndMakeVisible (detuneLabel);

    // ----- Pro -----
    proPanel.addAndMakeVisible (enableCorrection);
    proPanel.addAndMakeVisible (doNoHarm);
    proPanel.addAndMakeVisible (formantPreserve);
    // Polyphonic tuning: shipping feature (benchmark: 96-98% per-note on separated
    // chords). Retunes each note of a chord/pad independently to the scale.
    proPanel.addAndMakeVisible (polyphonic);
    // Scale Lock macro — arm Polyphonic + AutoKey in one click (and clear the mutually
    // exclusive Follow-Song-Key), so a whole instrumental snaps into its own detected
    // key. Releasing just drops Polyphonic; the key mode is left as the user had it.
    proPanel.addAndMakeVisible (scaleLock);
    scaleLock.onClick = [this]
    {
        const bool lock = scaleLock.getToggleState();
        setKeyParam ("polyphonic", lock ? 1.0f : 0.0f);
        if (lock) { setKeyParam ("autoKey", 1.0f); setKeyParam ("followSongKey", 0.0f); }
        // Full poly high-end: follow each corrected note's tonal overtones up into the top
        // end so the HIGHS land in key too (legacy stopped at 2.5 kHz, leaving them frozen),
        // and protect percussive/noise content while doing it. Both release with the lock.
        // Measured +4.5 pts average high-band in-key across the GiantSteps corpus, 16/16 up.
        setKeyParam ("hiTrack",    lock ? 1.0f : 0.0f);
        setKeyParam ("tonalFocus", lock ? 1.0f : 0.0f);
    };
    // Poly Harmony amount — an in-key diatonic-3rd voice added on top of polyphonic
    // correction (best-effort on dense material; 0 = off).
    polyHarmonyCaption.setText (juce::String::fromUTF8 ("POLY HARMONY  \xc2\xb7  in-key 3rd (poly)"), juce::dontSendNotification);
    polyHarmonyCaption.setFont (juce::Font (sattari::brand::fontOptions (11.0f)));
    polyHarmonyCaption.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (polyHarmonyCaption);
    polyHarmonyKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    polyHarmonyKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    proPanel.addAndMakeVisible (polyHarmonyKnob);
    // Poly high-end (instrumentals). High Tracking follows a corrected note's tonal
    // overtones up into the top end so the highs land in key; Tonal Focus protects
    // percussive/noise content (hats/air) from being pitched while it does. The Scale
    // Lock macro arms both at full; these knobs let you dial them independently.
    highTrackCaption.setText (juce::String::fromUTF8 ("HIGH TRACKING  \xc2\xb7  tune the highs (poly)"), juce::dontSendNotification);
    highTrackCaption.setFont (juce::Font (sattari::brand::fontOptions (11.0f)));
    highTrackCaption.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (highTrackCaption);
    highTrackKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    highTrackKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    proPanel.addAndMakeVisible (highTrackKnob);
    tonalFocusCaption.setText (juce::String::fromUTF8 ("TONAL FOCUS  \xc2\xb7  spare the noise (poly)"), juce::dontSendNotification);
    tonalFocusCaption.setFont (juce::Font (sattari::brand::fontOptions (11.0f)));
    tonalFocusCaption.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (tonalFocusCaption);
    tonalFocusKnob.setSliderStyle (juce::Slider::LinearHorizontal);
    tonalFocusKnob.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    proPanel.addAndMakeVisible (tonalFocusKnob);
    styleKnob (inputGain);
    styleKnob (outputGain);
    styleKnob (formantShift);
    inputGain.setTextValueSuffix (" dB");
    outputGain.setTextValueSuffix (" dB");
    formantShift.setTextValueSuffix (" st");
    styleCaption (inputCaption, "INPUT");
    styleCaption (outputCaption, "OUTPUT");
    styleCaption (formantCaption, "FORMANT");
    proPanel.addAndMakeVisible (inputGain);
    proPanel.addAndMakeVisible (outputGain);
    proPanel.addAndMakeVisible (formantShift);
    proPanel.addAndMakeVisible (inputCaption);
    proPanel.addAndMakeVisible (outputCaption);
    proPanel.addAndMakeVisible (formantCaption);

    // Antares-class behaviour controls.
    proPanel.addAndMakeVisible (classicMode);
    proPanel.addAndMakeVisible (neuralPitch);

    // Transpose to Key: whole-mix musical transpose for beats/instruments. The semitone
    // slider is the shift; "Match Key" reads AutoKey's detected key and sets the shift
    // to land on the SET KEY target; enabling the mode auto-runs a Match once.
    proPanel.addAndMakeVisible (transposeMode);
    styleCaption (transposeCaption, "TRANSPOSE TO KEY  (beats / instruments)");
    proPanel.addAndMakeVisible (transposeCaption);
    transposeSemis.setSliderStyle (juce::Slider::LinearHorizontal);
    transposeSemis.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    transposeSemis.setRange (-12.0, 12.0, 1.0);
    transposeSemis.textFromValueFunction = [] (double v) {
        const int s = juce::roundToInt (v);
        return (s > 0 ? "+" : "") + juce::String (s) + " st"; };
    proPanel.addAndMakeVisible (transposeSemis);
    matchKeyButton.onClick = [this]
    {
        // source = AutoKey's detected root; target = the SET KEY root. Shift by the
        // nearest signed semitone delta so the whole mix lands on the target key.
        const auto snap = audioProcessor.getLiveSnapshot();
        auto& a = audioProcessor.parameters();
        const int src = juce::jlimit (0, 11, snap.detectedRoot);
        const int tgt = juce::jlimit (0, 11, (int) std::round (a.getRawParameterValue ("keyRoot")->load()));
        int d = ((tgt - src) % 12 + 12) % 12;   // 0..11
        if (d > 6) d -= 12;                      // nearest signed: -5..+6
        setKeyParam ("xposeSemis", (float) d);
    };
    proPanel.addAndMakeVisible (matchKeyButton);
    // Enabling the mode auto-runs Match once so it "just works" on drop-in.
    transposeMode.onClick = [this] { if (transposeMode.getToggleState()) matchKeyButton.onClick(); };
    transposeReadout.setJustificationType (juce::Justification::centredLeft);
    transposeReadout.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (transposeReadout);
    styleKnob (flexAmt);
    styleKnob (transition);
    styleKnob (throat);
    styleKnob (vibratoKnob);
    vibratoKnob.textFromValueFunction = [] (double v) {
        return v < 0.005 ? juce::String ("Off") : juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    styleCaption (vibratoCaption, "VIBRATO");
    proPanel.addAndMakeVisible (vibratoKnob);
    proPanel.addAndMakeVisible (vibratoCaption);
    flexAmt.textFromValueFunction    = [] (double v) { return v < 0.005 ? juce::String ("Rigid")
                                                                        : juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    transition.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    throat.textFromValueFunction     = [] (double v) { return juce::String (v, 2) + "x"; };
    styleCaption (flexAmtCaption, "FLEX-TUNE");
    styleCaption (transitionCaption, "TRANSITION");
    styleCaption (throatCaption, "THROAT");
    proPanel.addAndMakeVisible (flexAmt);
    proPanel.addAndMakeVisible (transition);
    proPanel.addAndMakeVisible (throat);
    proPanel.addAndMakeVisible (flexAmtCaption);
    proPanel.addAndMakeVisible (transitionCaption);
    proPanel.addAndMakeVisible (throatCaption);

    // Full vibrato creation controls: preserve the singer's vibrato above, or
    // generate a new one with independent depth, rate, and onset.
    for (auto* s : { &addVibrato, &vibratoRate, &vibratoOnset })
        styleKnob (*s);
    addVibrato.setTextValueSuffix (" ct");
    vibratoRate.setTextValueSuffix (" Hz");
    vibratoOnset.setTextValueSuffix (" ms");
    styleCaption (addVibratoCaption, "ADD VIBRATO");
    styleCaption (vibratoRateCaption, "RATE");
    styleCaption (vibratoOnsetCaption, "ONSET");
    for (auto* c : { &addVibratoCaption, &vibratoRateCaption, &vibratoOnsetCaption })
        proPanel.addAndMakeVisible (*c);
    for (auto* s : { &addVibrato, &vibratoRate, &vibratoOnset })
        proPanel.addAndMakeVisible (*s);

    referenceDetune.setSliderStyle (juce::Slider::LinearHorizontal);
    referenceDetune.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 20);
    referenceDetune.setTextValueSuffix (" ct");
    styleCaption (referenceDetuneCaption, "REFERENCE TUNING");
    proPanel.addAndMakeVisible (referenceDetuneCaption);
    proPanel.addAndMakeVisible (referenceDetune);
    proPanel.addAndMakeVisible (followReference);
    proPanel.addAndMakeVisible (midiTarget);
    // loadTraceButton is a dev-only JSON trace import. Plumbing kept for the
    // contract test (loadTraceFromClipboard) but the button stays off-UI in
    // shipping builds - it's not a user feature.
    loadTraceButton.onClick = [this] { loadTraceFromClipboard(); };
    traceStatus.setJustificationType (juce::Justification::topLeft);
    keyDetectionStatus.setJustificationType (juce::Justification::topLeft);
    traceStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    keyDetectionStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    proPanel.addAndMakeVisible (traceStatus);
    proPanel.addAndMakeVisible (keyDetectionStatus);

    // Track Key Map (Pro tab) — scan a song -> key timeline, applied at playback.
    proPanel.addAndMakeVisible (scanTrackButton);
    proPanel.addAndMakeVisible (clearMapButton);
    proPanel.addAndMakeVisible (useKeyMap);
    keyMapStatusLabel.setJustificationType (juce::Justification::topLeft);
    keyMapStatusLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    keyMapStatusLabel.setText (juce::String::fromUTF8 ("No key map \xe2\x80\x94 scan a track to follow key changes across a song."),
                               juce::dontSendNotification);
    proPanel.addAndMakeVisible (keyMapStatusLabel);
    // Visual key map. Clicking a section reports its start time; we surface it in the
    // status line so the producer can find the modulation in their DAW (a plugin can't
    // seek the host transport).
    proPanel.addAndMakeVisible (keyMapTimeline);
    keyMapTimeline.onSegmentClicked = [this] (double sec)
    {
        const int mm = (int) (sec / 60.0), ss = (int) sec % 60;
        keyMapStatusLabel.setText ("Section starts at " + juce::String (mm) + ":"
                                       + juce::String (ss).paddedLeft ('0', 2),
                                   juce::dontSendNotification);
    };
    scanTrackButton.onClick = [this] { onScanTrackClicked(); };
    clearMapButton.onClick  = [this] { audioProcessor.clearKeyMap(); };
    useKeyMapAttach = std::make_unique<ButtonAttachment> (audioProcessor.parameters(), "useKeyMap", useKeyMap);

    proPanel.addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };

    // ----- Edit -----
    editPanel.addAndMakeVisible (capturePitchEdits);
    editPanel.addAndMakeVisible (usePitchEdits);
    editPanel.addAndMakeVisible (graphTimeCorrection);
    editPanel.addAndMakeVisible (clearPitchEdits);
    for (auto* button : { &undoPitchEdit, &redoPitchEdit, &splitPitchEdit, &joinPitchEdit,
                          &deletePitchEdit, &zoomInPitchEdit, &zoomOutPitchEdit, &fitPitchEdit })
        editPanel.addAndMakeVisible (*button);
    editPanel.addAndMakeVisible (pitchEditTimeline);
    editPanel.addAndMakeVisible (pitchEditHelp);
    editPanel.addAndMakeVisible (pitchEditStatus);
    pitchEditHelp.setText (
        "Select a note, then tune its pitch, feel, formant, level or timing. Drag vertically to retarget; wheel to zoom.",
        juce::dontSendNotification);
    pitchEditHelp.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pitchEditHelp.setJustificationType (juce::Justification::centredLeft);
    pitchEditStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    clearPitchEdits.onClick = [this] { audioProcessor.clearPitchEdits(); };
    undoPitchEdit.onClick = [this] { audioProcessor.undoPitchEdit(); };
    redoPitchEdit.onClick = [this] { audioProcessor.redoPitchEdit(); };
    splitPitchEdit.onClick = [this] { if (selectedPitchEditId != 0) audioProcessor.splitPitchEdit (selectedPitchEditId, audioProcessor.getKeyMapPlayheadSec()); };
    joinPitchEdit.onClick = [this] { if (selectedPitchEditId != 0) audioProcessor.joinPitchEdit (selectedPitchEditId); };
    deletePitchEdit.onClick = [this] { if (selectedPitchEditId != 0) audioProcessor.deletePitchEdit (selectedPitchEditId); };
    zoomInPitchEdit.onClick = [this] { pitchEditTimeline.zoomIn(); };
    zoomOutPitchEdit.onClick = [this] { pitchEditTimeline.zoomOut(); };
    fitPitchEdit.onClick = [this] { pitchEditTimeline.fitAll(); };
    pitchEditTimeline.onEntryChanged = [this] (const auto& entry) { audioProcessor.setPitchEditEntry (entry.id, entry); };
    pitchEditTimeline.onSelectionChanged = [this] (juce::int64 id) { selectPitchEdit (id); };
    pitchEditTimeline.onGestureBegin = [this] { audioProcessor.beginPitchEditGesture(); };
    pitchEditTimeline.onGestureEnd = [this] { audioProcessor.endPitchEditGesture(); };

    struct ObjectControl { juce::Slider* slider; juce::Label* label; const char* name; double lo, hi, step; const char* suffix; };
    const ObjectControl objectControls[] {
        { &objectAmount, &objectAmountLabel, "Amount", 0.0, 1.0, 0.01, "" },
        { &objectRetune, &objectRetuneLabel, "Retune", 0.05, 1.0, 0.01, "" },
        { &objectTransition, &objectTransitionLabel, "Transition", 0.0, 1.0, 0.01, "" },
        { &objectHumanize, &objectHumanizeLabel, "Humanize", 0.0, 1.0, 0.01, "" },
        { &objectVibrato, &objectVibratoLabel, "Vibrato", 0.0, 1.0, 0.01, "" },
        { &objectFormant, &objectFormantLabel, "Formant", -12.0, 12.0, 0.1, " st" },
        { &objectTime, &objectTimeLabel, "Time", -500.0, 500.0, 1.0, " ms" },
        { &objectGain, &objectGainLabel, "Gain", -24.0, 12.0, 0.1, " dB" }
    };
    for (const auto& control : objectControls)
    {
        editPanel.addAndMakeVisible (*control.slider);
        editPanel.addAndMakeVisible (*control.label);
        control.slider->setSliderStyle (juce::Slider::LinearHorizontal);
        control.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 20);
        control.slider->setRange (control.lo, control.hi, control.step);
        control.slider->setTextValueSuffix (control.suffix);
        styleCaption (*control.label, control.name);
        control.slider->setEnabled (false);
        control.slider->onDragStart = [this] { audioProcessor.beginPitchEditGesture(); };
        control.slider->onDragEnd = [this] { audioProcessor.endPitchEditGesture(); };
        control.slider->onValueChange = [this] { updateSelectedPitchEdit(); };
    }

    // Attachments
    auto& params = audioProcessor.parameters();
    vocalRangeAttach     = std::make_unique<ComboAttachment> (params, "vocalRange", vocalRange);
    keyRootAttach        = std::make_unique<ComboAttachment> (params, "keyRoot", keyRoot);
    scaleAttach          = std::make_unique<ComboAttachment> (params, "scale", scale);
    scaleModeAttach      = std::make_unique<ComboAttachment> (params, "scaleMode", scaleMode);

    autoKeyAttach        = std::make_unique<ButtonAttachment> (params, "autoKey", autoKey);
    followKeyAttach      = std::make_unique<ButtonAttachment> (params, "followSongKey", followKey);

    styleAttach           = std::make_unique<ComboAttachment> (params, "style", styleSelect);

    retuneSpeedAttach    = std::make_unique<SliderAttachment> (params, "speed", retuneSpeed);
    flexTuneAttach       = std::make_unique<SliderAttachment> (params, "amount", flexTune);
    humanizeAttach       = std::make_unique<SliderAttachment> (params, "humanize", humanize);
    inputGainAttach      = std::make_unique<SliderAttachment> (params, "inputGain", inputGain);
    outputGainAttach     = std::make_unique<SliderAttachment> (params, "outputGain", outputGain);
    formantShiftAttach   = std::make_unique<SliderAttachment> (params, "formantShift", formantShift);
    polyHarmonyAttach    = std::make_unique<SliderAttachment> (params, "polyHarmony", polyHarmonyKnob);
    highTrackAttach      = std::make_unique<SliderAttachment> (params, "hiTrack", highTrackKnob);
    tonalFocusAttach     = std::make_unique<SliderAttachment> (params, "tonalFocus", tonalFocusKnob);

    enableCorrectionAttach = std::make_unique<ButtonAttachment> (params, "enableCorrection", enableCorrection);
    doNoHarmAttach         = std::make_unique<ButtonAttachment> (params, "doNoHarm", doNoHarm);
    formantPreserveAttach  = std::make_unique<ButtonAttachment> (params, "formantPreserve", formantPreserve);
    polyphonicAttach       = std::make_unique<ButtonAttachment> (params, "polyphonic", polyphonic);
    classicModeAttach      = std::make_unique<ButtonAttachment> (params, "classicMode", classicMode);
    flexAmtAttach          = std::make_unique<SliderAttachment> (params, "flexTune", flexAmt);
    transitionAttach       = std::make_unique<SliderAttachment> (params, "transition", transition);
    throatAttach           = std::make_unique<SliderAttachment> (params, "throat", throat);
    vibratoAttach          = std::make_unique<SliderAttachment> (params, "vibrato", vibratoKnob);
    addVibratoAttach       = std::make_unique<SliderAttachment> (params, "addVibrato", addVibrato);
    vibratoRateAttach      = std::make_unique<SliderAttachment> (params, "vibratoRate", vibratoRate);
    vibratoOnsetAttach     = std::make_unique<SliderAttachment> (params, "vibratoOnset", vibratoOnset);
    referenceDetuneAttach  = std::make_unique<SliderAttachment> (params, "referenceDetune", referenceDetune);
    neuralPitchAttach      = std::make_unique<ButtonAttachment> (params, "neuralPitch", neuralPitch);
    transposeModeAttach    = std::make_unique<ButtonAttachment> (params, "xposeMode", transposeMode);
    followReferenceAttach  = std::make_unique<ButtonAttachment> (params, "followReference", followReference);
    midiTargetAttach       = std::make_unique<ButtonAttachment> (params, "midiTarget", midiTarget);
    capturePitchEditsAttach = std::make_unique<ButtonAttachment> (params, "capturePitchEdits", capturePitchEdits);
    usePitchEditsAttach     = std::make_unique<ButtonAttachment> (params, "usePitchEdits", usePitchEdits);
    graphTimeCorrectionAttach = std::make_unique<ButtonAttachment> (params, "graphTimeCorrection", graphTimeCorrection);
    transposeSemisAttach   = std::make_unique<SliderAttachment> (params, "xposeSemis", transposeSemis);

    basicPanel.setVisible (true);
    proPanel.setVisible (true);    // the ADVANCED drawer band; zero-height while collapsed
    editPanel.setVisible (true);
    startTimerHz (15);
}

SattariAutoPitchAudioProcessorEditor::~SattariAutoPitchAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariAutoPitchAudioProcessorEditor::selectPitchEdit (juce::int64 id)
{
    const auto edits = audioProcessor.getPitchEdits();
    const auto it = std::find_if (edits.begin(), edits.end(), [id] (const auto& entry) { return entry.id == id; });
    const bool valid = it != edits.end() && id != 0;
    selectedPitchEditId = valid ? id : 0;
    if (valid) selectedPitchEdit = *it;

    syncingPitchEditControls = true;
    for (auto* slider : { &objectAmount, &objectRetune, &objectTransition, &objectHumanize, &objectVibrato,
                          &objectFormant, &objectTime, &objectGain })
        slider->setEnabled (valid);
    splitPitchEdit.setEnabled (valid);
    joinPitchEdit.setEnabled (valid);
    deletePitchEdit.setEnabled (valid);
    if (valid)
    {
        const auto global = [this] (const char* idToRead) { return audioProcessor.parameters().getRawParameterValue (idToRead)->load(); };
        objectAmount.setValue (selectedPitchEdit.amount >= 0.0f ? selectedPitchEdit.amount : global ("amount"), juce::dontSendNotification);
        objectRetune.setValue (selectedPitchEdit.retune >= 0.0f ? selectedPitchEdit.retune : global ("speed"), juce::dontSendNotification);
        objectTransition.setValue (selectedPitchEdit.transition >= 0.0f ? selectedPitchEdit.transition : global ("transition"), juce::dontSendNotification);
        objectHumanize.setValue (selectedPitchEdit.humanize >= 0.0f ? selectedPitchEdit.humanize : global ("humanize"), juce::dontSendNotification);
        objectVibrato.setValue (selectedPitchEdit.vibrato >= 0.0f ? selectedPitchEdit.vibrato : global ("vibrato"), juce::dontSendNotification);
        objectFormant.setValue (selectedPitchEdit.formantSemitones >= -12.0f ? selectedPitchEdit.formantSemitones : global ("formantShift"), juce::dontSendNotification);
        objectTime.setValue (selectedPitchEdit.timeOffsetSec * 1000.0, juce::dontSendNotification);
        objectGain.setValue (selectedPitchEdit.gainDb, juce::dontSendNotification);
    }
    syncingPitchEditControls = false;
}

void SattariAutoPitchAudioProcessorEditor::updateSelectedPitchEdit()
{
    if (syncingPitchEditControls || selectedPitchEditId == 0) return;
    selectedPitchEdit.amount = (float) objectAmount.getValue();
    selectedPitchEdit.retune = (float) objectRetune.getValue();
    selectedPitchEdit.transition = (float) objectTransition.getValue();
    selectedPitchEdit.humanize = (float) objectHumanize.getValue();
    selectedPitchEdit.vibrato = (float) objectVibrato.getValue();
    selectedPitchEdit.formantSemitones = (float) objectFormant.getValue();
    selectedPitchEdit.timeOffsetSec = objectTime.getValue() * 0.001;
    selectedPitchEdit.gainDb = (float) objectGain.getValue();
    audioProcessor.setPitchEditEntry (selectedPitchEditId, selectedPitchEdit);
}

void SattariAutoPitchAudioProcessorEditor::styleKnob (juce::Slider& s, bool feature)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, feature ? 90 : 70, feature ? 24 : 18);
}

void SattariAutoPitchAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariAutoPitchAudioProcessorEditor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // Factory workflows must be deterministic. The shared preset manager is
    // intentionally sparse, so loading MIDI Directed/Graph Editor and then a
    // normal preset used to leave MIDI targeting, graph correction, generated
    // vibrato, or other advanced modes silently active. Start every workflow
    // from a neutral tuning engine, then layer its intentional differences.
    const PV neutral {
        {"style",0},{"enableCorrection",1},{"amount",1.0f},{"speed",0.85f},
        {"doNoHarm",1},{"formantPreserve",1},{"formantShift",0},{"classicMode",0},
        {"flexTune",0},{"transition",0.5f},{"throat",1.0f},{"humanize",0},
        {"vibrato",0},{"addVibrato",0},{"midiTarget",0},{"capturePitchEdits",0},
        {"usePitchEdits",0},{"graphTimeCorrection",0},{"xposeMode",0},{"xposeSemis",0},
        {"polyphonic",0},{"polyHarmony",0},{"tonalFocus",0},{"hiTrack",0},
        {"enabledNotes",4095},{"neuralPitch",0},{"lowLatency",0},{"liveEngine",0},
        {"liveMode",0},{"extremeLive",1},
        {"liveProfile",0},
        {"inputGain",0},{"outputGain",0},
        {"harmonyMode",0},{"harmonyMix",0.72f},{"harmonyLead",1.0f},
        {"harmonyVoiceOnly",0},{"harmonyGlide",28.0f},
        {"harmonyHumanPitch",5.0f},{"harmonyHumanTime",7.0f},
        {"harmonyVibratoRate",5.2f},{"harmonyEnsemble",0.2f},
        {"harmonyEnsembleDepth",7.0f},{"harmonyEnsembleRate",0.35f},
        {"harmonyEnsembleSpread",0.75f},{"harmonyLowCut",90.0f},
        {"harmonyHighCut",16000.0f},
        {"h1Enable",0},{"h2Enable",0},{"h3Enable",0},{"h4Enable",0},
        {"h1Solo",0},{"h2Solo",0},{"h3Solo",0},{"h4Solo",0}
    };
    auto addWorkflow = [this, &neutral] (const juce::String& name, PV values)
    {
        PV complete = neutral;
        complete.insert (complete.end(), values.begin(), values.end());
        presetManager.addFactoryPreset (name, std::move (complete));
    };

    addWorkflow ("Natural", PV {
        {"style",0},{"amount",0.86f},{"speed",0.58f},{"doNoHarm",1},
        {"formantPreserve",1},{"flexTune",0.22f},{"transition",0.65f},
        {"humanize",0.16f},{"vibrato",0.4f} });
    addWorkflow ("Pitch Center", PV {
        {"style",0},{"amount",1.0f},{"speed",0.72f},{"doNoHarm",1},
        {"formantPreserve",1},{"flexTune",0.0f},{"transition",0.55f} });
    addWorkflow ("Hard Tune", PV {
        {"style",1},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},
        {"formantPreserve",1},{"formantShift",0},{"classicMode",1},{"flexTune",0.0f},
        {"humanize",0.0f},{"transition",0.0f},{"lowLatency",0} });
    addWorkflow ("Modern Rap", PV {
        {"style",2},{"enableCorrection",1},{"amount",0.95f},{"speed",0.9f},{"doNoHarm",0},
        {"formantPreserve",1},{"formantShift",0},{"classicMode",0},{"flexTune",0.08f},
        {"humanize",0.08f},{"transition",0.08f},{"lowLatency",0},{"liveEngine",0} });
    addWorkflow ("Robot", PV {
        {"style",3},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},
        {"formantPreserve",0},{"formantShift",0},{"classicMode",1},{"flexTune",0.0f},
        {"humanize",0.0f},{"vibrato",0.0f},{"lowLatency",0},{"liveEngine",0} });
    addWorkflow ("Fast Natural", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.9f},{"speed",0.78f},{"doNoHarm",1},
        {"formantPreserve",1},{"classicMode",0},{"flexTune",0.14f},{"humanize",0.1f},
        {"transition",0.72f} });
    addWorkflow ("MIDI Directed", PV {
        {"style",0},{"enableCorrection",1},{"amount",1.0f},{"speed",0.82f},{"doNoHarm",1},
        {"formantPreserve",1},{"midiTarget",1},{"classicMode",0},{"liveEngine",0},{"lowLatency",0} });
    addWorkflow ("Graph Editor", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.9f},{"speed",0.72f},{"doNoHarm",1},
        {"formantPreserve",1},{"capturePitchEdits",1},{"usePitchEdits",1},{"classicMode",0} });
    addWorkflow ("Natural Vibrato", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.84f},{"speed",0.62f},{"doNoHarm",1},
        {"formantPreserve",1},{"flexTune",0.18f},{"humanize",0.1f},{"vibrato",0.8f},
        {"addVibrato",0.0f},{"classicMode",0} });
    addWorkflow ("Deeper Throat", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.84f},{"speed",0.58f},{"doNoHarm",1},
        {"formantPreserve",1},{"flexTune",0.2f},{"humanize",0.1f},{"formantShift",-3} });
    addWorkflow ("Bright Lift", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.84f},{"speed",0.58f},{"doNoHarm",1},
        {"formantPreserve",1},{"flexTune",0.2f},{"humanize",0.1f},{"formantShift",4} });
    addWorkflow ("Live Extreme", PV {
        {"style",0},{"liveMode",1},{"liveProfile",0},{"extremeLive",1},
        {"amount",0.9f},{"speed",0.78f},
        {"doNoHarm",1},{"formantPreserve",1},{"flexTune",0.12f},
        {"transition",0.7f},{"humanize",0.08f},{"vibrato",0.55f} });
    addWorkflow ("Live Ultra Quality", PV {
        {"style",0},{"liveMode",1},{"liveProfile",0},{"extremeLive",0},
        {"amount",0.9f},{"speed",0.78f},
        {"doNoHarm",1},{"formantPreserve",1},{"flexTune",0.12f},
        {"transition",0.7f},{"humanize",0.08f},{"vibrato",0.55f} });
    addWorkflow ("Live Extended", PV {
        {"style",0},{"liveMode",1},{"liveProfile",1},{"extremeLive",0},
        {"amount",0.9f},{"speed",0.75f},
        {"doNoHarm",1},{"formantPreserve",1},{"flexTune",0.12f},
        {"transition",0.7f},{"humanize",0.08f},{"vibrato",0.55f} });
    addWorkflow ("Harmony · Pop Stack", PV {
        {"harmonyMode",0},{"h1Enable",1},{"h2Enable",1},
        {"h1Role",0},{"h2Role",1},{"h1Level",-8.0f},{"h2Level",-9.0f},
        {"h1Pan",-0.32f},{"h2Pan",0.32f},{"harmonyMix",0.72f},
        {"harmonyHumanPitch",4.0f},{"harmonyHumanTime",6.0f},
        {"harmonyEnsemble",0.16f},{"harmonyEnsembleSpread",0.62f} });
    addWorkflow ("Harmony · Wide Choir", PV {
        {"harmonyMode",0},{"h1Enable",1},{"h2Enable",1},{"h3Enable",1},{"h4Enable",1},
        {"h1Role",0},{"h2Role",1},{"h3Role",2},{"h4Role",3},
        {"h1Level",-10.0f},{"h2Level",-10.0f},{"h3Level",-13.0f},{"h4Level",-13.0f},
        {"harmonyMix",0.8f},{"harmonyHumanPitch",8.0f},{"harmonyHumanTime",14.0f},
        {"harmonyEnsemble",0.72f},{"harmonyEnsembleDepth",11.0f},
        {"harmonyEnsembleRate",0.28f},{"harmonyEnsembleSpread",1.0f},
        {"harmonyLowCut",120.0f},{"harmonyHighCut",13500.0f} });
    addWorkflow ("Harmony · MIDI Choir", PV {
        {"harmonyMode",2},{"h1Enable",1},{"h2Enable",1},{"h3Enable",1},{"h4Enable",1},
        {"h1Level",-9.0f},{"h2Level",-9.0f},{"h3Level",-9.0f},{"h4Level",-9.0f},
        {"harmonyMix",0.76f},{"harmonyHumanPitch",3.0f},{"harmonyHumanTime",5.0f},
        {"harmonyEnsemble",0.3f},{"harmonyEnsembleSpread",0.85f} });
}

void SattariAutoPitchAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& n : presetManager.userPresetNames())
        userPresets.addItem (n, id++);
}

void SattariAutoPitchAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariAutoPitchAudioProcessorEditor::loadTraceFromClipboard()
{
    audioProcessor.applyTraceContractJson (juce::SystemClipboard::getTextFromClipboard());
}

void SattariAutoPitchAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* p = audioProcessor.parameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

void SattariAutoPitchAudioProcessorEditor::onScanTrackClicked()
{
    trackFileChooser = std::make_unique<juce::FileChooser> (
        "Pick a track to scan for the Key Map",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    trackFileChooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.startKeyMapScan (file);
    });
}

void SattariAutoPitchAudioProcessorEditor::timerCallback()
{
    const auto snap = audioProcessor.getLiveSnapshot();
    noteDisplay.setSnapshot (snap);

    {   // graph widgets are always visible now; update them every tick
        auto edits = audioProcessor.getPitchEdits();
        const int revision = audioProcessor.getPitchEditRevision();
        pitchEditTimeline.setData (edits, audioProcessor.getKeyMapPlayheadSec());
        pitchEditTimeline.setHarmonyOverlays (audioProcessor.getHarmonyEditOverlays());
        if (revision != pitchEditShownRevision && selectedPitchEditId != 0
            && ! objectAmount.isMouseButtonDown() && ! objectRetune.isMouseButtonDown()
            && ! objectTransition.isMouseButtonDown() && ! objectHumanize.isMouseButtonDown()
            && ! objectVibrato.isMouseButtonDown()
            && ! objectFormant.isMouseButtonDown() && ! objectTime.isMouseButtonDown()
            && ! objectGain.isMouseButtonDown())
            selectPitchEdit (selectedPitchEditId);
        pitchEditShownRevision = revision;
        undoPitchEdit.setEnabled (audioProcessor.canUndoPitchEdit());
        redoPitchEdit.setEnabled (audioProcessor.canRedoPitchEdit());
        pitchEditStatus.setText (juce::String ((int) edits.size()) + " note objects"
            + juce::String::fromUTF8 ("   \xc2\xb7   ")
            + juce::String (audioProcessor.parameters().getRawParameterValue ("usePitchEdits")->load() > 0.5f
                                ? "edited playback active" : "edited playback off"),
            juce::dontSendNotification);
    }

    // Keep the Scale Lock macro lit only while it is actually armed (Polyphonic + AutoKey),
    // so toggling either underlying control directly stays reflected.
    if (auto* pv = audioProcessor.parameters().getRawParameterValue ("polyphonic"))
        if (auto* ak = audioProcessor.parameters().getRawParameterValue ("autoKey"))
        {
            const bool locked = pv->load() > 0.5f && ak->load() > 0.5f;
            if (scaleLock.getToggleState() != locked)
                scaleLock.setToggleState (locked, juce::dontSendNotification);
        }

    // Publish the current correction key onto the cross-plugin bus so other
    // Sattari instances (e.g. Royal Chain's Tune) can follow. Throttled to
    // ~1 Hz inside publishCurrentKeyToBus() so the JSON file never thrashes.
    audioProcessor.publishCurrentKeyToBus();

    // Drive the single KeyButton: it shows the live key + how it's driven
    // (manual / AutoKey / Follow). This is now the only key control.
    const auto origin = audioProcessor.currentKeyOrigin();
    using KBMode = KeyButton::Mode;
    const KBMode kbMode = origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus ? KBMode::Follow
                        : origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::KeyMap    ? KBMode::Map
                        : origin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::AutoKey  ? KBMode::Auto
                        : KBMode::Manual;
    keyButton.setState (kbMode, origin.root, origin.scale);

    // Smoothed "lock" meter: track pitch confidence while voiced, hold between notes
    // so the meter reflects how settled detection is rather than flickering per-note.
    const float lockTarget = snap.voiced ? snap.confidence : keyLock;
    keyLock += 0.12f * (lockTarget - keyLock);
    keyButton.setConfidence (keyLock);

    // Highlight the button matching the active source so the current mode is obvious.
    const auto acc = juce::Colour (sattari::brand::productAccent ("auto-pitch"));
    const auto idle = juce::Colour (sattari::brand::kPanelRaised);
    autoDetectBtn.setColour (juce::TextButton::buttonColourId, kbMode == KBMode::Auto   ? acc.withAlpha (0.85f) : idle);
    followSongBtn.setColour (juce::TextButton::buttonColourId, kbMode == KBMode::Follow ? acc.withAlpha (0.85f) : idle);
    setKeyBtn.setColour     (juce::TextButton::buttonColourId, kbMode == KBMode::Manual ? acc.withAlpha (0.85f) : idle);

    // Pills light for the key actually in use (the followed/detected key when
    // overriding, the manual key otherwise) — not the stale dropdown value.
    pillRow.setScale (origin.root, origin.scale);
    pillRow.setDetectedMidi (snap.detectedMidi);
    pillRow.setEnabledMask (juce::jlimit (0, 4095,
        juce::roundToInt (audioProcessor.parameters().getRawParameterValue ("enabledNotes")->load())));

    detuneLabel.setText (snap.voiced
        ? "Detune " + juce::String (snap.correctionCents, 1) + " ct"
        : "Detune -", juce::dontSendNotification);

    //  Persistent musical summary. The song model when it exists (it is the
    //  richer truth), otherwise the live key state — ALWAYS labelled with its
    //  source, never a bare assertion.
    {
        static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const auto model = audioProcessor.songAnalysisCopy();
        juce::String text;
        if (model.valid && model.globalKey.valid)
        {
            text << "SONG  " << nm[juce::jlimit (0, 11, model.globalKey.root)]
                 << (model.globalKey.minor ? " MINOR" : " MAJOR");
            if (model.bpm > 0.0) text << "  -  " << juce::String (model.bpm, 1) << " BPM";
            text << "  -  A=" << juce::String (model.tuningHz(), 1) << " Hz"
                 << "  -  margin " << juce::String (model.globalKey.confidence, 2);
        }
        else
        {
            const auto summaryOrigin = audioProcessor.currentKeyOrigin();
            const char* src = summaryOrigin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus
                                  ? (summaryOrigin.dynamic ? "DYNAMIC BUS" : "BUS")
                            : summaryOrigin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::KeyMap    ? "KEY MAP"
                            : summaryOrigin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::AutoKey   ? "AUTO"
                                                                                                                   : "MANUAL";
            text << src << "  " << nm[juce::jlimit (0, 11, summaryOrigin.root)]
                 << (summaryOrigin.scale == 1 ? " minor" : summaryOrigin.scale == 2 ? "" : " major");
            if (summaryOrigin.dynamic)
                text << "  -  rev " << summaryOrigin.revision;
            if (summaryOrigin.source == SattariAutoPitchAudioProcessor::KeyOrigin::Source::AutoKey
                || summaryOrigin.dynamic)
                text << "  -  margin " << juce::String (summaryOrigin.confidence, 2);
        }
        headerSummary.setText (text, juce::dontSendNotification);

        //  KEY page: rebuild only when the model actually changed shape.
        if (model.valid
            && (std::abs (model.durationSec - songModelSeenDuration) > 1.0e-6
                || (int) model.scaleRegions.size() != songModelSeenRegions))
        {
            songModelSeenDuration = model.durationSec;
            songModelSeenRegions  = (int) model.scaleRegions.size();

            keyBigLabel.setText (juce::String (nm[juce::jlimit (0, 11, model.globalKey.root)])
                                     + (model.globalKey.minor ? " MINOR" : " MAJOR"),
                                 juce::dontSendNotification);
            juce::String meta;
            if (model.bpm > 0.0)
                meta << juce::String (model.bpm, 1) << " BPM"
                     << (model.tempoOctaveAmbiguous ? " (octave ambiguous)" : "") << "   -   ";
            meta << "A = " << juce::String (model.tuningHz(), 1) << " Hz   -   "
                 << juce::String (model.detuneCents, 1) << " cents   -   "
                 << juce::String (model.durationSec / 60.0, 0) << ":"
                 << juce::String ((int) model.durationSec % 60).paddedLeft ('0', 2);
            keyMetaLabel.setText (meta, juce::dontSendNotification);

            juce::String alt ("margin " + juce::String (model.globalKey.confidence, 3));
            if (model.alternative.valid)
                alt << "   -   alternative: " << nm[juce::jlimit (0, 11, model.alternative.root)]
                    << (model.alternative.minor ? " minor" : " major");
            if (model.globalKey.confidence < SattariAutoPitchAudioProcessor::kMinBusKeyConfidence)
                alt << "   -   BELOW TRUST GATE: shown, not applied";
            keyAltLabel.setText (alt, juce::dontSendNotification);

            std::vector<sattari::ui::KeyMapSegment> segs;
            for (const auto& r : model.scaleRegions)
                segs.push_back ({ r.startSec, r.root, r.minor ? 1 : 0, (float) r.confidence });
            songMapView.setData (model.waveform, std::move (segs), model.durationSec);

            //  Chord + modulation summaries. Both say plainly when empty rather
            //  than leaving a blank the user has to interpret.
            {
                juce::String chords;
                for (size_t i = 0; i < model.chordRegions.size() && i < 10; ++i)
                {
                    const auto& cr = model.chordRegions[i];
                    if (chords.isNotEmpty()) chords << "  ";
                    chords << "| " << nm[juce::jlimit (0, 11, cr.chord.root)]
                           << sattari::dsp::chordQualityName (cr.chord.quality);
                    if (! cr.diatonic) chords << "*";
                }
                if (model.chordRegions.empty())
                    chords = "CHORDS   none confident enough to show";
                else
                    chords = "CHORDS   " + chords
                           + (model.chordRegions.size() > 10 ? "  ..." : "")
                           + "     (* = borrowed)";
                keyChordsLabel.setText (chords, juce::dontSendNotification);

                juce::String mods;
                for (const auto& m : model.modulations)
                {
                    if (mods.isNotEmpty()) mods << "   ";
                    mods << juce::String ((int) m.atSec / 60) << ":"
                         << juce::String ((int) m.atSec % 60).paddedLeft ('0', 2)
                         << " " << nm[juce::jlimit (0, 11, m.toRoot)]
                         << (m.toMinor ? " min" : " maj")
                         << " (" << sattari::dsp::modulationTypeName (m.type)
                         //  Mark the uncertain ones. Presenting a 0.03-margin
                         //  boundary in the same voice as a 0.4-margin one states
                         //  a guess as a fact.
                         << (m.confidence < SattariAutoPitchAudioProcessor::kMinBusKeyConfidence
                                 ? ", uncertain)" : ")");
                }
                //  "None detected" is not the same claim as "one key throughout":
                //  the second asserts something the analysis never established.
                keyModsLabel.setText (model.modulations.empty()
                                          ? "CHANGES  no key changes detected"
                                          : "CHANGES  " + mods,
                                      juce::dontSendNotification);
            }

            juce::String sections;
            for (const auto& sec : model.sections)
            {
                auto fmt = [] (double t) { return juce::String ((int) t / 60) + ":"
                    + juce::String ((int) t % 60).paddedLeft ('0', 2); };
                if (sections.isNotEmpty()) sections << "    ";
                sections << sec.name << " " << fmt (sec.startSec) << "-" << fmt (sec.endSec);
            }
            keySectionsLabel.setText (sections, juce::dontSendNotification);
            keyInspectorLabel.setText (pendingRegionConfirm.isNotEmpty()
                                           ? pendingRegionConfirm
                                           : juce::String ("Click a region for detail"),
                                       juce::dontSendNotification);
            pendingRegionConfirm.clear();
            if (workspace == Workspace::key)
                resized();   // swap the hint for the populated page
        }
        if (! model.valid && songModelSeenRegions > 0)
        {
            songModelSeenDuration = -1.0; songModelSeenRegions = -1;
            songMapView.clear();
            if (workspace == Workspace::key) resized();
        }
    }

    //  Engine + latency readout: the ACTIVE engine's reported delay, which is what
    //  the host compensates by. Reading it live means the label can never disagree
    //  with the number the host sees.
    {
        const int lat = audioProcessor.getLatencySamples();
        const double srNow = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 48000.0;
        const bool live = liveModeButton.getToggleState();
        const auto mode = live ? (liveProfile.getSelectedItemIndex() == 1 ? "RANGE"
                                : extremeLiveButton.getToggleState() ? "EXTREME" : "ULTRA")
                               : "STUDIO";
        latencyReadout.setText (juce::String (lat > 0 ? 1000.0 * lat / srNow : 0.0, 1)
                                    + " ms " + mode,
                                juce::dontSendNotification);
        setControlActive (extremeLiveButton,
                          live && liveProfile.getSelectedItemIndex() == 0);
    }

    //  Song drop analysis: progress while running, the verdict once done, and the
    //  No Result policy spelled out when the margin is below the trust gate.
    {
        const float prog = audioProcessor.fileProgress.load (std::memory_order_acquire);
        if (prog < 0.0f)
            songStatusLabel.setText ("Song analysis failed - unreadable or unpitched audio",
                                     juce::dontSendNotification);
        else if (prog > 0.0f && prog < 1.0f)
            songStatusLabel.setText ("Analysing song... " + juce::String ((int) (prog * 100.0f)) + "%",
                                     juce::dontSendNotification);
        else if (audioProcessor.fileKeyValid.load (std::memory_order_acquire))
        {
            static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            const float conf = audioProcessor.fileKeyConf.load();
            const float bpm  = audioProcessor.fileBpm.load();
            juce::String text = "Song: "
                + juce::String (nm[juce::jlimit (0, 11, audioProcessor.fileKeyRoot.load())])
                + (audioProcessor.fileKeyMinor.load() != 0 ? " minor" : " major");
            if (bpm > 0.0f) text += "  -  " + juce::String (bpm, 1) + " BPM";
            text += conf >= SattariAutoPitchAudioProcessor::kMinBusKeyConfidence
                        ? "  (driving AutoKey)"
                        : "  (low confidence - shown, not applied)";
            songStatusLabel.setText (text, juce::dontSendNotification);
        }
        else
            songStatusLabel.setText ("Drop a song anywhere to detect its key + tempo",
                                     juce::dontSendNotification);
    }

    // HARMONY: the readout is the same planner that drives the audible renderer.
    if (workspace == Workspace::harmony)
    {
        static const char* hn[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const auto h = audioProcessor.planHarmonyNow();
        auto noteName = [&] (int midi)
        {
            return midi < 0 ? juce::String ("-")
                            : juce::String (hn[((midi % 12) + 12) % 12])
                                  + juce::String (midi / 12 - 1);
        };
        const bool midiMode = harmonyModeBox.getSelectedId() == 3;
        juce::String status;
        if (! h.valid)
            status = "Play a vocal to see the planned harmony";
        else if (midiMode && h.heldNotes == 0)
            status = "Lead " + noteName (h.leadMidi)
                   + "   -   MIDI mode: no notes held, so no harmony is planned";
        else
            status = "Lead " + noteName (h.leadMidi)
                   + (h.fromChord ? "   -   planning from the detected chord"
                                  : "   -   no chord known here, planning from the scale");
        int activeVoices = 0;
        for (bool active : h.active) if (active) ++activeVoices;
        harmonyStatusLabel.setText (
            status + "   -   " + juce::String (activeVoices) + " voices rendering"
                   + "   -   generated notes are visible in Edit",
            juce::dontSendNotification);
        for (int v = 0; v < 4; ++v)
            harmonyVoices[(size_t) v].readout.setText (
                h.valid && h.active[(size_t) v]
                    ? noteName (h.midi[(size_t) v]) : juce::String ("-"),
                juce::dontSendNotification);
    }

    // Show only the live engine status; the JSON-trace summary is dev-only.
    traceStatus.setText (audioProcessor.getLiveStatusSummary(), juce::dontSendNotification);
    keyDetectionStatus.setText (audioProcessor.getKeyDetectionTabSummary(), juce::dontSendNotification);

    // Transpose-to-Key readout: detected source key -> shift -> resulting key.
    {
        static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        const int src   = juce::jlimit (0, 11, snap.detectedRoot);
        const int semis = juce::jlimit (-12, 12,
            (int) std::round (audioProcessor.parameters().getRawParameterValue ("xposeSemis")->load()));
        const int res   = ((src + semis) % 12 + 12) % 12;
        const bool on   = audioProcessor.parameters().getRawParameterValue ("xposeMode")->load() > 0.5f;
        transposeReadout.setText (on
            ? juce::String ("detected ") + nm[src] + juce::String::fromUTF8 ("  \xe2\x86\x92  ")
                + (semis > 0 ? "+" : "") + juce::String (semis) + juce::String::fromUTF8 (" st  \xe2\x86\x92  ") + nm[res]
            : juce::String::fromUTF8 ("off \xe2\x80\x94 per-note correction active"),
            juce::dontSendNotification);
    }

    // Track Key Map status panel.
    const auto km = audioProcessor.getKeyMapStatus();
    juce::String s;
    if (km.scanProgress >= 0 && km.scanProgress < 100)
        s << "Scanning: " << km.scanProgress << "%\n";
    if (km.hasMap)
    {
        s << km.loadedFileName << juce::String::fromUTF8 ("   \xc2\xb7   ") << km.numSegments << " key segments";
        if (km.currentSegment >= 0)
            s << juce::String::fromUTF8 ("   \xc2\xb7   segment ") << (km.currentSegment + 1);
    }
    else if (km.lastError.isNotEmpty())
        s << "Error: " << km.lastError;
    else if (km.scanProgress < 0)
        s << juce::String::fromUTF8 ("No key map \xe2\x80\x94 scan a track to follow key changes across a song.");
    keyMapStatusLabel.setText (s, juce::dontSendNotification);

    // Key Map timeline: refetch the (potentially large) waveform + segments only when
    // the map actually changes, then just move the playhead marker each tick.
    if (km.hasMap != keyMapShown || km.numSegments != keyMapShownSegs || km.loadedFileName != keyMapShownFile)
    {
        keyMapShown = km.hasMap; keyMapShownSegs = km.numSegments; keyMapShownFile = km.loadedFileName;
        if (km.hasMap)
        {
            std::vector<sattari::ui::KeyMapSegment> segs;
            for (const auto& e : audioProcessor.getKeyMapSegments())
                segs.push_back ({ e.timeSec, e.rootIdx, e.scaleIdx, e.confidence });
            keyMapTimeline.setData (audioProcessor.getKeyMapWaveform(), std::move (segs),
                                    audioProcessor.getKeyMapDurationSec());
        }
        else
            keyMapTimeline.clear();
    }
    keyMapTimeline.setPlayhead (audioProcessor.getKeyMapPlayheadSec());

    // Honest Neural Pitch label + state: the toggle silently falls back to YIN when no
    // crepe.onnx model is installed — say so AND disable it, so it never looks functional.
    const bool neuralOk = audioProcessor.neuralModelPresent();
    const juce::String wantedNeuralText = neuralOk ? "Neural Pitch" : "Neural Pitch (no model installed)";
    if (neuralPitch.getButtonText() != wantedNeuralText)
        neuralPitch.setButtonText (wantedNeuralText);
    setControlActive (neuralPitch, neuralOk);

    // ── Mode-aware enabled states ────────────────────────────────────────────────
    // Don't let a control look interactive when the current mode makes it do nothing.
    // Per-note correction is bypassed both when Correction is Off and when Transpose to
    // Key is On (a whole-mix shift replaces correction), so dim every correction control.
    auto& ap = audioProcessor.parameters();
    const bool corrOn  = ap.getRawParameterValue ("enableCorrection")->load() > 0.5f;
    const bool xposeOn = ap.getRawParameterValue ("xposeMode")->load() > 0.5f;
    const bool correctionActive = corrOn && ! xposeOn;
    juce::Component* correctionControls[] = {
        &retuneSpeed, &flexTune, &humanize, &styleSelect, &styleCaptionLabel,
        &retuneSpeedCaption, &flexTuneCaption, &humanizeCaption, &pillRow, &editNotesLabel,
        &flexAmt, &transition, &throat, &vibratoKnob, &classicMode, &doNoHarm,
        &formantPreserve, &polyphonic, &formantShift, &flexAmtCaption, &transitionCaption,
        &throatCaption, &vibratoCaption, &formantCaption, &addVibrato, &vibratoRate,
        &vibratoOnset, &addVibratoCaption, &vibratoRateCaption, &vibratoOnsetCaption,
        &midiTarget
    };
    for (auto* c : correctionControls)
        setControlActive (*c, correctionActive);

    // The transpose amount + Match Key are inert until Transpose to Key is engaged.
    setControlActive (transposeSemis, xposeOn);
    setControlActive (matchKeyButton, xposeOn);
}

void SattariAutoPitchAudioProcessorEditor::setControlActive (juce::Component& c, bool on)
{
    if (c.isEnabled() != on)       c.setEnabled (on);
    const float a = on ? 1.0f : 0.4f;
    if (std::abs (c.getAlpha() - a) > 1.0e-6f) c.setAlpha (a);
}

void SattariAutoPitchAudioProcessorEditor::setWorkspace (Workspace w)
{
    workspace = w;
    resized();
}

void SattariAutoPitchAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariAutoPitchAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    //  The drop invite must go OVER the children: paint() renders beneath them,
    //  and the graph timeline fills the editor centre with an opaque panel — the
    //  first version drew the banner exactly where nobody could see it.
    if (dragActive)
    {
        //  A song is being dragged over the editor: invite the drop.
        const auto accent = lookAndFeel.findColour (juce::Slider::thumbColourId);
        g.setColour (accent.withAlpha (0.10f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 12.0f);
        g.setColour (accent.withAlpha (0.85f));
        juce::Path outline;
        outline.addRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 12.0f);
        const float dashes[] = { 8.0f, 6.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
        g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        g.drawText ("DROP TO ANALYSE SONG  -  KEY + TEMPO",
                    getLocalBounds(), juce::Justification::centred);
    }
}

bool SattariAutoPitchAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1
        && audioProcessor.isSupportedAudioFile (juce::File (files[0]));
}

void SattariAutoPitchAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint();
}

void SattariAutoPitchAudioProcessorEditor::fileDragExit (const juce::StringArray&)
{
    dragActive = false;
    repaint();
}

void SattariAutoPitchAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragActive = false;
    repaint();
    if (files.size() == 1)
    {
        audioProcessor.analyseSongFileAsync (juce::File (files[0]));
        //  The analysis lands in the KEY workspace — take the user there.
        wsKeyButton.setToggleState (true, juce::dontSendNotification);
        setWorkspace (Workspace::key);
    }
}

void SattariAutoPitchAudioProcessorEditor::resized()
{
    //  One page, four bands: header / everyday controls / graph / ADVANCED drawer.
    //  The former tab panels survive as the band containers so that none of their
    //  ~60 children needed reparenting — only this function changed shape.
    auto area = getLocalBounds().reduced (16);

    // ---- Header band -----------------------------------------------------------
    auto header = area.removeFromTop (50);
    title.setBounds (header.removeFromLeft (240));
    header.removeFromLeft (10);

    auto vocalBlock = header.removeFromLeft (150);
    vocalRangeCaption.setBounds (vocalBlock.removeFromTop (14));
    vocalRange.setBounds (vocalBlock);
    header.removeFromLeft (12);

    //  Engine mode: one pill, lit while Live Mode runs, with the ACTIVE engine's
    //  reported delay beside it. The old three-item combo pretending to offer
    //  locked-off engines is gone.
    auto engineBlock = header.removeFromLeft (310);
    engineBlock.removeFromTop (10);
    liveModeButton.setBounds (engineBlock.removeFromLeft (58).reduced (0, 2));
    engineBlock.removeFromLeft (6);
    extremeLiveButton.setBounds (engineBlock.removeFromLeft (48).reduced (0, 2));
    engineBlock.removeFromLeft (6);
    liveProfile.setBounds (engineBlock.removeFromLeft (102).reduced (0, 2));
    engineBlock.removeFromLeft (6);
    latencyReadout.setBounds (engineBlock);

    auto presetCol = header.removeFromRight (300);
    auto factoryRow = presetCol.removeFromTop (24);
    abRow.setBounds (factoryRow.removeFromRight (84));
    factoryRow.removeFromRight (6);
    factoryPresets.setBounds (factoryRow);
    presetCol.removeFromTop (2);
    auto userRow = presetCol;
    savePresetButton.setBounds (userRow.removeFromRight (60));
    userRow.removeFromRight (4);
    userPresets.setBounds (userRow);

    area.removeFromTop (4);

    // ---- Workspace bar: KEY | TUNE | HARMONY | EDIT + the persistent summary ----
    {
        auto wsRow = area.removeFromTop (26);
        for (auto* b : { &wsKeyButton, &wsTuneButton, &wsHarmonyButton, &wsEditButton })
        {
            b->setBounds (wsRow.removeFromLeft (86));
            wsRow.removeFromLeft (6);
        }
        wsRow.removeFromLeft (10);
        headerSummary.setBounds (wsRow);
    }
    area.removeFromTop (8);

    const bool inKey  = workspace == Workspace::key;
    const bool inTune = workspace == Workspace::tune;
    const bool inEdit = workspace == Workspace::edit;
    const bool inHarm = workspace == Workspace::harmony;
    harmonyPanel.setVisible (inHarm);
    keyPanel.setVisible (inKey);
    basicPanel.setVisible (inTune);
    editPanel.setVisible (inTune || inEdit);
    advancedToggle.setVisible (inTune);
    songStatusLabel.setVisible (inTune);    // KEY and EDIT have their own status surfaces

    // keyPanel and harmonyPanel are added after the shared header in the
    // constructor. Some JUCE host repaint paths therefore promoted a workspace
    // panel over the header when it became visible, producing a completely blank
    // top band in KEY/HARMONY. Reassert the persistent chrome's z-order whenever
    // the workspace changes.
    for (auto* c : { (juce::Component*) &title, (juce::Component*) &vocalRangeCaption,
                     (juce::Component*) &vocalRange, (juce::Component*) &liveModeButton,
                     (juce::Component*) &extremeLiveButton, (juce::Component*) &liveProfile,
                     (juce::Component*) &latencyReadout, (juce::Component*) &factoryPresets,
                     (juce::Component*) &userPresets, (juce::Component*) &savePresetButton,
                     (juce::Component*) &abRow, (juce::Component*) &wsKeyButton,
                     (juce::Component*) &wsTuneButton, (juce::Component*) &wsHarmonyButton,
                     (juce::Component*) &wsEditButton, (juce::Component*) &headerSummary })
        c->toFront (false);

    // ---- HARMONY workspace -------------------------------------------------------
    if (inHarm)
    {
        basicPanel.setBounds ({}); proPanel.setBounds ({}); editPanel.setBounds ({});
        harmonyPanel.setBounds (area);
        auto h = juce::Rectangle<int> (0, 0, harmonyPanel.getWidth(),
                                       harmonyPanel.getHeight()).reduced (8);
        auto top = h.removeFromTop (30);
        harmonyHeaderLabel.setBounds (top.removeFromLeft (140));
        harmonyModeBox.setBounds (top.removeFromLeft (260).reduced (0, 2));
        top.removeFromLeft (10);
        harmonyVoiceOnly.setBounds (top.removeFromLeft (126).reduced (0, 3));
        h.removeFromTop (4);
        harmonyStatusLabel.setBounds (h.removeFromTop (22));
        h.removeFromTop (6);

        const std::array<juce::Slider*, 12> macroSliders {
            &harmonyMix, &harmonyLead, &harmonyGlide, &harmonyHumanPitch,
            &harmonyHumanTime, &harmonyVibratoRate, &harmonyEnsemble, &harmonyEnsembleDepth,
            &harmonyEnsembleRate, &harmonyEnsembleSpread, &harmonyLowCut,
            &harmonyHighCut
        };
        for (int rowIndex = 0; rowIndex < 2; ++rowIndex)
        {
            auto macroRow = h.removeFromTop (48);
            const int first = rowIndex == 0 ? 0 : 6;
            const int count = 6;
            const int cells = 6;
            const int cellWidth = macroRow.getWidth() / cells;
            for (int i = 0; i < count; ++i)
            {
                auto cell = macroRow.removeFromLeft (cellWidth).reduced (3, 0);
                harmonyMacroLabels[(size_t) (first + i)].setBounds (cell.removeFromTop (14));
                macroSliders[(size_t) (first + i)]->setBounds (cell.reduced (0, 2));
            }
        }
        h.removeFromTop (6);

        for (int v = 0; v < 4; ++v)
        {
            auto& strip = harmonyVoices[(size_t) v];
            auto voiceArea = h.removeFromTop (76);
            auto row = voiceArea.removeFromTop (32);
            strip.caption.setBounds (row.removeFromLeft (36));
            strip.enable.setBounds (row.removeFromLeft (50).reduced (2, 5));
            strip.solo.setBounds (row.removeFromLeft (34).reduced (2, 5));
            row.removeFromLeft (6);
            strip.role.setBounds (row.removeFromLeft (138).reduced (0, 4));
            row.removeFromLeft (8);
            strip.degrees.setBounds (row.removeFromLeft (112).reduced (0, 6));
            row.removeFromLeft (6);
            strip.octave.setBounds (row.removeFromLeft (112).reduced (0, 6));
            row.removeFromLeft (10);
            strip.readout.setBounds (row);

            const std::array<juce::Slider*, 5> sliders {
                &strip.level, &strip.pan, &strip.delay, &strip.detune, &strip.vibrato
            };
            const std::array<juce::Label*, 5> labels {
                &strip.levelLabel, &strip.panLabel, &strip.delayLabel,
                &strip.detuneLabel, &strip.vibratoLabel
            };
            const int cellWidth = voiceArea.getWidth() / 5;
            for (int i = 0; i < 5; ++i)
            {
                auto cell = voiceArea.removeFromLeft (cellWidth).reduced (4, 0);
                labels[(size_t) i]->setBounds (cell.removeFromTop (13));
                sliders[(size_t) i]->setBounds (cell.reduced (0, 1));
            }
            h.removeFromTop (3);
        }
        return;
    }

    // ---- KEY workspace -----------------------------------------------------------
    if (inKey)
    {
        keyPanel.setBounds (area);
        auto k = juce::Rectangle<int> (0, 0, keyPanel.getWidth(), keyPanel.getHeight()).reduced (8);
        const bool haveModel = songModelSeenRegions > 0;
        keyHintLabel.setVisible (! haveModel);
        if (! haveModel)
        {
            keyHintLabel.setBounds (k);
            //  Zero EVERY populated-state child, not just some: going populated
            //  -> empty (clearSongFileResult, or loading a project) otherwise
            //  leaves these floating at their last bounds over the drop hint.
            for (auto* c : { (juce::Component*) &keyBigLabel, (juce::Component*) &keyMetaLabel,
                             (juce::Component*) &keyAltLabel, (juce::Component*) &songMapView,
                             (juce::Component*) &keySectionsLabel,
                             (juce::Component*) &keyChordsLabel, (juce::Component*) &keyModsLabel,
                             (juce::Component*) &keyInspectorLabel,
                             (juce::Component*) &followSongMapButton,
                             (juce::Component*) &regionRootBox, (juce::Component*) &regionScaleBox,
                             (juce::Component*) &regionApplyButton })
                c->setBounds ({});
        }
        else
        {
            auto head = k.removeFromTop (52);
            keyBigLabel.setBounds (head.removeFromLeft (juce::jmax (260, head.getWidth() / 2)));
            //  Follow Song Map sits WITH the map it follows, not in a drawer.
            followSongMapButton.setBounds (head.removeFromRight (190).reduced (0, 12));
            keyMetaLabel.setBounds (k.removeFromTop (24));
            keyAltLabel.setBounds (k.removeFromTop (20));
            k.removeFromTop (6);

            //  Region editing row: pick a region on the map, set its key.
            auto editRow = k.removeFromBottom (26);
            regionApplyButton.setBounds (editRow.removeFromLeft (150));
            editRow.removeFromLeft (8);
            regionRootBox.setBounds (editRow.removeFromLeft (80));
            editRow.removeFromLeft (6);
            regionScaleBox.setBounds (editRow.removeFromLeft (90));
            editRow.removeFromLeft (12);
            keyInspectorLabel.setBounds (editRow);
            k.removeFromBottom (4);
            keyModsLabel.setBounds (k.removeFromBottom (20));
            keyChordsLabel.setBounds (k.removeFromBottom (20));
            keySectionsLabel.setBounds (k.removeFromBottom (20));
            k.removeFromBottom (6);
            songMapView.setBounds (k);
        }
        proPanel.setBounds ({});
        harmonyPanel.setBounds ({});
        return;   // KEY owns the whole body; the other bands are hidden
    }

    // ---- EDIT workspace: the graph gets everything ------------------------------
    if (inEdit)
    {
        basicPanel.setBounds ({});
        proPanel.setBounds ({});
        editPanel.setBounds (area);
        layoutEditPanel (true);
        return;
    }

    // ---- Everyday band (the old BASIC panel, compacted) -------------------------
    const int basicH = 236;
    basicPanel.setBounds (area.removeFromTop (basicH));
    {
        auto b = juce::Rectangle<int> (0, 0, basicPanel.getWidth(), basicH);
        auto left = b.removeFromLeft ((int) (b.getWidth() * 0.56f));
        b.removeFromLeft (12);
        auto right = b;

        keyButton.setBounds (left.removeFromTop (46));
        left.removeFromTop (4);
        auto keyBtnRow = left.removeFromTop (26);
        const int keyBw = (keyBtnRow.getWidth() - 12) / 3;
        autoDetectBtn.setBounds (keyBtnRow.removeFromLeft (keyBw));
        keyBtnRow.removeFromLeft (6);
        followSongBtn.setBounds (keyBtnRow.removeFromLeft (keyBw));
        keyBtnRow.removeFromLeft (6);
        setKeyBtn.setBounds (keyBtnRow);
        left.removeFromTop (6);

        auto meterRow = left;
        noteDisplay.setBounds (meterRow.removeFromLeft ((int) (meterRow.getWidth() * 0.46f)));
        meterRow.removeFromLeft (10);
        editNotesLabel.setBounds (meterRow.removeFromTop (14));
        pillRow.setBounds (meterRow.removeFromTop (72));
        meterRow.removeFromTop (4);
        detuneLabel.setBounds (meterRow.removeFromTop (22));

        //  Right: the everyday knobs in one row — the big retune plus two smalls —
        //  and the style selector under them.
        auto knobRow = right.removeFromTop (170);
        auto bigCell = knobRow.removeFromLeft ((int) (knobRow.getWidth() * 0.44f));
        retuneSpeedCaption.setBounds (bigCell.removeFromTop (14));
        const int bigSide = juce::jmin (bigCell.getWidth(), bigCell.getHeight());
        retuneSpeed.setBounds (bigCell.withSizeKeepingCentre (bigSide, bigSide));
        const int kw = knobRow.getWidth() / 2;
        auto flexCell = knobRow.removeFromLeft (kw);
        flexTuneCaption.setBounds (flexCell.removeFromTop (14));
        flexTune.setBounds (flexCell.reduced (6));
        auto humanCell = knobRow;
        humanizeCaption.setBounds (humanCell.removeFromTop (14));
        humanize.setBounds (humanCell.reduced (6));

        right.removeFromTop (4);
        auto styleRow = right.removeFromTop (44);
        styleCaptionLabel.setBounds (styleRow.removeFromTop (14));
        styleSelect.setBounds (styleRow.removeFromTop (28).removeFromLeft (juce::jmin (280, styleRow.getWidth())));
    }

    area.removeFromTop (6);

    // ---- ADVANCED drawer (bottom): toggle bar + the old PRO panel ---------------
    auto drawerBar = area.removeFromBottom (24);
    advancedToggle.setBounds (drawerBar.removeFromLeft (110));
    drawerBar.removeFromLeft (10);
    songStatusLabel.setBounds (drawerBar);

    //  An open drawer gets its full height before the graph does: its content is
    //  fixed-size rows that clip, while the timeline shrinks gracefully. 100 px of
    //  graph stays as a floor so the playhead never vanishes entirely.
    const int drawerH = advancedOpen ? juce::jmin (280, area.getHeight() - 124) : 0;
    if (drawerH > 0)
    {
        area.removeFromBottom (6);
        proPanel.setBounds (area.removeFromBottom (drawerH));
    }
    else
    {
        proPanel.setBounds (area.removeFromBottom (0));
    }

    area.removeFromBottom (6);

    // ---- Graph band (the old EDIT panel) — everything that remains --------------
    editPanel.setBounds (area);

    // ---- Drawer internals: the PRO content reflowed into three columns ----------
    if (drawerH > 0)
    {
        auto pArea = juce::Rectangle<int> (0, 0, proPanel.getWidth(), drawerH).reduced (8, 4);
        const int colW = (pArea.getWidth() - 24) / 3;

        auto c1 = pArea.removeFromLeft (colW);
        pArea.removeFromLeft (12);
        auto c2 = pArea.removeFromLeft (colW);
        pArea.removeFromLeft (12);
        auto c3 = pArea;

        //  Column 1: switches + gain/formant knobs.
        auto t1 = c1.removeFromTop (22);
        enableCorrection.setBounds (t1.removeFromLeft (t1.getWidth() / 2));
        doNoHarm.setBounds (t1);
        auto t2 = c1.removeFromTop (22);
        formantPreserve.setBounds (t2.removeFromLeft (t2.getWidth() / 2));
        polyphonic.setBounds (t2);
        auto t3 = c1.removeFromTop (22);
        scaleLock.setBounds (t3.removeFromLeft (t3.getWidth() / 2));
        neuralPitch.setBounds (t3);
        auto t4 = c1.removeFromTop (22);
        classicMode.setBounds (t4.removeFromLeft (t4.getWidth() / 2));
        midiTarget.setBounds (t4);
        c1.removeFromTop (6);
        auto gains = c1.removeFromTop (82);
        const int gw = gains.getWidth() / 3;
        auto iCell = gains.removeFromLeft (gw);
        inputCaption.setBounds (iCell.removeFromTop (14));
        inputGain.setBounds (iCell);
        auto oCell = gains.removeFromLeft (gw);
        outputCaption.setBounds (oCell.removeFromTop (14));
        outputGain.setBounds (oCell);
        auto fCell = gains;
        formantCaption.setBounds (fCell.removeFromTop (14));
        formantShift.setBounds (fCell);
        c1.removeFromTop (4);
        referenceDetuneCaption.setBounds (c1.removeFromTop (14));
        referenceDetune.setBounds (c1.removeFromTop (22));
        followReference.setBounds (c1.removeFromTop (22));

        //  Column 2: behaviour + generated vibrato.
        auto beh = c2.removeFromTop (78);
        const int bw = beh.getWidth() / 4;
        auto flexAmtCell = beh.removeFromLeft (bw);
        flexAmtCaption.setBounds (flexAmtCell.removeFromTop (14));
        flexAmt.setBounds (flexAmtCell);
        auto transCell = beh.removeFromLeft (bw);
        transitionCaption.setBounds (transCell.removeFromTop (14));
        transition.setBounds (transCell);
        auto throatCell = beh.removeFromLeft (bw);
        throatCaption.setBounds (throatCell.removeFromTop (14));
        throat.setBounds (throatCell);
        auto vibCell = beh;
        vibratoCaption.setBounds (vibCell.removeFromTop (14));
        vibratoKnob.setBounds (vibCell);
        c2.removeFromTop (6);
        auto gen = c2.removeFromTop (72);
        const int gw2 = gen.getWidth() / 3;
        auto depthCell = gen.removeFromLeft (gw2);
        addVibratoCaption.setBounds (depthCell.removeFromTop (14));
        addVibrato.setBounds (depthCell);
        auto rateCell = gen.removeFromLeft (gw2);
        vibratoRateCaption.setBounds (rateCell.removeFromTop (14));
        vibratoRate.setBounds (rateCell);
        auto onsetCell = gen;
        vibratoOnsetCaption.setBounds (onsetCell.removeFromTop (14));
        vibratoOnset.setBounds (onsetCell);
        c2.removeFromTop (6);
        transposeCaption.setBounds (c2.removeFromTop (14));
        auto xRow = c2.removeFromTop (24);
        transposeMode.setBounds (xRow.removeFromLeft ((int) (xRow.getWidth() * 0.62f)));
        xRow.removeFromLeft (8);
        matchKeyButton.setBounds (xRow);
        transposeSemis.setBounds (c2.removeFromTop (24));
        transposeReadout.setBounds (c2.removeFromTop (16));

        //  Column 3: fine tracking, status, and the Track Key Map.
        auto ph = c3.removeFromTop (18);
        polyHarmonyCaption.setBounds (ph.removeFromLeft (ph.getWidth() / 2));
        polyHarmonyKnob.setBounds (ph);
        auto ht = c3.removeFromTop (18);
        highTrackCaption.setBounds (ht.removeFromLeft (ht.getWidth() / 2));
        highTrackKnob.setBounds (ht);
        auto tf = c3.removeFromTop (18);
        tonalFocusCaption.setBounds (tf.removeFromLeft (tf.getWidth() / 2));
        tonalFocusKnob.setBounds (tf);
        c3.removeFromTop (4);
        traceStatus.setBounds (c3.removeFromTop (30));
        keyDetectionStatus.setBounds (c3.removeFromTop (30));
        c3.removeFromTop (4);
        auto kmHeader = c3.removeFromTop (24);
        //  Proportional, not fixed: at the editor's minimum width this column is
        //  296 px, and 296 px of fixed widths left MIDI Learn a zero-width button.
        const int kmW = kmHeader.getWidth() - 12;
        scanTrackButton.setBounds (kmHeader.removeFromLeft (kmW * 30 / 100));
        kmHeader.removeFromLeft (4);
        clearMapButton.setBounds (kmHeader.removeFromLeft (kmW * 20 / 100));
        kmHeader.removeFromLeft (4);
        useKeyMap.setBounds (kmHeader.removeFromLeft (kmW * 32 / 100));
        kmHeader.removeFromLeft (4);
        midiLearnButton.setBounds (kmHeader);
        c3.removeFromTop (4);
        keyMapTimeline.setBounds (c3.removeFromTop (juce::jmax (40, c3.getHeight() - 22)));
        c3.removeFromTop (2);
        keyMapStatusLabel.setBounds (c3);
    }

    layoutEditPanel (false);
}

void SattariAutoPitchAudioProcessorEditor::layoutEditPanel (bool editMode)
{
    auto eArea = juce::Rectangle<int> (0, 0, editPanel.getWidth(), editPanel.getHeight()).reduced (4);
    auto editToolbar = eArea.removeFromTop (28);
    capturePitchEdits.setBounds (editToolbar.removeFromLeft (132));
    editToolbar.removeFromLeft (8);
    usePitchEdits.setBounds (editToolbar.removeFromLeft (152));
    editToolbar.removeFromLeft (8);
    graphTimeCorrection.setBounds (editToolbar.removeFromLeft (158));
    editToolbar.removeFromLeft (14);
    pitchEditStatus.setBounds (editToolbar);

    eArea.removeFromTop (4);
    auto commandBar = editMode ? eArea.removeFromTop (26) : juce::Rectangle<int>();
    for (auto* button : { &undoPitchEdit, &redoPitchEdit, &splitPitchEdit, &joinPitchEdit, &deletePitchEdit, &clearPitchEdits })
    {
        button->setBounds (commandBar.removeFromLeft (70));
        commandBar.removeFromLeft (5);
    }
    commandBar.removeFromLeft (10);
    zoomOutPitchEdit.setBounds (commandBar.removeFromLeft (34));
    commandBar.removeFromLeft (3);
    zoomInPitchEdit.setBounds (commandBar.removeFromLeft (34));
    commandBar.removeFromLeft (3);
    fitPitchEdit.setBounds (commandBar.removeFromLeft (52));

    //  On a tight layout (drawer open on a short window) the help line and the
    //  per-object slider rows yield to the timeline: editing objects is a
    //  drawer-closed activity, and a graph you cannot see is worth less than
    //  eight sliders you are not touching.
    //  TUNE keeps the graph as a monitor: toggles + timeline. The surgical
    //  chrome — command bar and per-object sliders — is the EDIT workspace.
    const bool roomy = editMode && eArea.getHeight() >= 260;
    for (auto* b : { &undoPitchEdit, &redoPitchEdit, &splitPitchEdit, &joinPitchEdit,
                     &deletePitchEdit, &clearPitchEdits, &zoomOutPitchEdit,
                     &zoomInPitchEdit, &fitPitchEdit })
        b->setVisible (editMode);
    pitchEditHelp.setVisible (roomy);
    if (roomy)
    {
        eArea.removeFromTop (4);
        pitchEditHelp.setBounds (eArea.removeFromTop (22));
    }

    auto layoutControlRow = [] (juce::Rectangle<int> row,
                                std::initializer_list<std::pair<juce::Label*, juce::Slider*>> controls)
    {
        const int gap = 8;
        const int width = (row.getWidth() - gap * ((int) controls.size() - 1)) / (int) controls.size();
        for (const auto& control : controls)
        {
            auto cell = row.removeFromLeft (width);
            control.first->setBounds (cell.removeFromTop (16));
            control.second->setBounds (cell);
            row.removeFromLeft (gap);
        }
    };
    for (auto* c : { (juce::Component*) &objectAmount, (juce::Component*) &objectRetune,
                     (juce::Component*) &objectTransition, (juce::Component*) &objectHumanize,
                     (juce::Component*) &objectVibrato, (juce::Component*) &objectFormant,
                     (juce::Component*) &objectTime, (juce::Component*) &objectGain,
                     (juce::Component*) &objectAmountLabel, (juce::Component*) &objectRetuneLabel,
                     (juce::Component*) &objectTransitionLabel, (juce::Component*) &objectHumanizeLabel,
                     (juce::Component*) &objectVibratoLabel, (juce::Component*) &objectFormantLabel,
                     (juce::Component*) &objectTimeLabel, (juce::Component*) &objectGainLabel })
        c->setVisible (roomy);
    if (roomy)
    {
        eArea.removeFromTop (4);
        auto controlsArea = eArea.removeFromBottom (100);
        layoutControlRow (controlsArea.removeFromTop (47), {
            { &objectAmountLabel, &objectAmount }, { &objectRetuneLabel, &objectRetune },
            { &objectTransitionLabel, &objectTransition }, { &objectHumanizeLabel, &objectHumanize } });
        controlsArea.removeFromTop (6);
        layoutControlRow (controlsArea.removeFromTop (47), {
            { &objectVibratoLabel, &objectVibrato }, { &objectFormantLabel, &objectFormant },
            { &objectTimeLabel, &objectTime }, { &objectGainLabel, &objectGain } });
        eArea.removeFromBottom (6);
    }
    pitchEditTimeline.setBounds (eArea);
}
