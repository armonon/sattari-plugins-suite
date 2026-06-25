#include "MainPluginEditor.h"

namespace
{
juce::Colour deckAccent (int deckIndex)
{
    static constexpr std::array<juce::uint32, 4> colours { 0xff26d9ff, 0xffff4c5f, 0xff50f06b, 0xffffcf4a };
    return juce::Colour (colours[(size_t) juce::jlimit (0, 3, deckIndex)]);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent, float corner = 12.0f)
{
    const auto r = bounds.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff080a10), r.getTopLeft(),
                                             juce::Colour (0xff151927), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (1.0f), corner, 1.0f);
    g.setColour (accent.withAlpha (0.36f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.1f);
}
}

juce::String MainPluginEditor::deckPrefix (int deckIndex)
{
    return "deck" + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, MainPluginProcessor::numDecks - 1, deckIndex)));
}

juce::String MainPluginEditor::deckLabel (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, MainPluginProcessor::numDecks - 1, deckIndex)));
}

juce::String MainPluginEditor::padKeyLabel (int padIndex)
{
    return juce::String (juce::jlimit (1, PadBank::numPads, padIndex + 1));
}

int MainPluginEditor::padIndexForKeyPress (const juce::KeyPress& key)
{
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '8')
        return (int) (c - '1');

    return -1;
}

MainPluginEditor::ArrangementRecorderView::ArrangementRecorderView() = default;

MainPluginEditor::ArrangementRecorderView::~ArrangementRecorderView()
{
    if (masterThumb != nullptr)
        masterThumb->removeChangeListener (this);
    for (auto& thumb : deckThumbs)
        if (thumb != nullptr)
            thumb->removeChangeListener (this);
}

void MainPluginEditor::ArrangementRecorderView::initialise (juce::AudioFormatManager& fm, juce::AudioThumbnailCache& cache)
{
    formatManager = &fm;
    thumbnailCache = &cache;
    masterThumb = std::make_unique<juce::AudioThumbnail> (512, fm, cache);
    masterThumb->addChangeListener (this);
    for (auto& thumb : deckThumbs)
    {
        thumb = std::make_unique<juce::AudioThumbnail> (512, fm, cache);
        thumb->addChangeListener (this);
    }
}

void MainPluginEditor::ArrangementRecorderView::setIdle (juce::String hint)
{
    mode = Mode::Idle;
    status = std::move (hint);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setRecordingProgress (double seconds, juce::String path)
{
    mode = Mode::Recording;
    elapsedSeconds = seconds;
    recordingPath = std::move (path);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setArrangement (const juce::File& masterAudio,
                                                                const std::array<juce::File, numLanes>& decks,
                                                                juce::String statusLine)
{
    mode = Mode::Finished;
    status = std::move (statusLine);
    masterFile = masterAudio;
    deckFiles = decks;
    for (int i = 0; i < numLanes; ++i)
    {
        clips[(size_t) i] = LaneClip { decks[(size_t) i], 0.0, 0.0, 0.0, 0.0, 0.0, false };
        if (deckThumbs[(size_t) i] != nullptr)
            loadThumbnail (*deckThumbs[(size_t) i], deckFiles[(size_t) i]);
    }
    clips[(size_t) masterLane] = LaneClip { masterAudio, 0.0, 0.0, 0.0, 0.0, 0.0, false };
    if (masterThumb != nullptr)
        loadThumbnail (*masterThumb, masterFile);
    selectedLane = -1;
    activeDrag = {};
    undoStack.clear();
    redoStack.clear();
    dragReadout.clear();
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::updateClipAfterBounce (int laneIndex,
                                                                       const juce::File& newFile,
                                                                       double newDuration)
{
    if (! juce::isPositiveAndBelow (laneIndex, numLanes))
        return;
    auto& clip = clips[(size_t) laneIndex];
    clip.file = newFile;
    clip.durationSeconds = newDuration;
    clip.trimStartSeconds = 0.0;
    clip.trimEndSeconds = newDuration;
    clip.fadeInSeconds = 0.0;
    clip.fadeOutSeconds = 0.0;
    deckFiles[(size_t) laneIndex] = newFile;
    if (deckThumbs[(size_t) laneIndex] != nullptr)
        loadThumbnail (*deckThumbs[(size_t) laneIndex], newFile);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setDeckAutomation (std::array<DeckAutomation, numLanes> automation)
{
    deckAutomation = std::move (automation);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::clearArrangement()
{
    mode = Mode::Idle;
    masterFile = juce::File();
    for (auto& f : deckFiles)
        f = juce::File();
    for (auto& curves : deckAutomation)
        curves.clear();
    if (masterThumb != nullptr)
        masterThumb->clear();
    for (auto& thumb : deckThumbs)
        if (thumb != nullptr)
            thumb->clear();
    repaint();
}

MainPluginEditor::ArrangementRecorderView::HitTest
MainPluginEditor::ArrangementRecorderView::hitTestAt (juce::Point<int> p) const
{
    HitTest h;
    if (mode != Mode::Finished)
        return h;
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen <= 0.0)
        return h;

    for (int i = 0; i < numEditableLanes; ++i)
    {
        auto laneRect = laneRects[(size_t) i];
        if (! laneRect.contains (p))
            continue;
        const auto& clip = clips[(size_t) i];
        if (clip.durationSeconds <= 0.0)
        {
            h.lane = i;
            h.handle = Handle::Body;
            return h;
        }
        const int labelWidth = 90;
        auto track = laneRect.withTrimmedLeft (labelWidth).reduced (4, 3);
        auto timeToX = [&] (double t) -> int
        {
            return track.getX() + (int) std::round ((double) track.getWidth() * t / maxLen);
        };
        const int clipX0 = timeToX (clip.trimStartSeconds);
        const int clipX1 = timeToX (clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
        if (p.x < clipX0 - 6 || p.x > clipX1 + 6)
        {
            h.lane = i;
            h.handle = Handle::None;
            return h;
        }
        const int edgeBand = 8;
        const int cornerH = juce::jmax (16, juce::jmin (28, track.getHeight() * 2 / 3));
        if (std::abs (p.x - clipX0) <= edgeBand)
        {
            h.lane = i;
            h.handle = (p.y - track.getY() <= cornerH) ? Handle::FadeIn : Handle::TrimStart;
            return h;
        }
        if (std::abs (p.x - clipX1) <= edgeBand)
        {
            h.lane = i;
            h.handle = (p.y - track.getY() <= cornerH) ? Handle::FadeOut : Handle::TrimEnd;
            return h;
        }
        h.lane = i;
        h.handle = Handle::Body;
        return h;
    }
    return h;
}

juce::MouseCursor MainPluginEditor::ArrangementRecorderView::cursorForHandle (Handle h) const
{
    switch (h)
    {
        case Handle::TrimStart:
        case Handle::TrimEnd:
            return juce::MouseCursor::LeftRightResizeCursor;
        case Handle::FadeIn:
        case Handle::FadeOut:
            return juce::MouseCursor::PointingHandCursor;
        case Handle::Body:
            return juce::MouseCursor::PointingHandCursor;
        case Handle::None:
        default:
            return juce::MouseCursor::NormalCursor;
    }
}

void MainPluginEditor::ArrangementRecorderView::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitTestAt (e.getPosition());
    setMouseCursor (cursorForHandle (hit.handle));
}

void MainPluginEditor::ArrangementRecorderView::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = hitTestAt (e.getPosition());

    if (e.mods.isPopupMenu())
    {
        if (hit.lane >= 0)
        {
            if (selectedLane != hit.lane) { selectedLane = hit.lane; repaint(); }
            const int lane = hit.lane;
            juce::PopupMenu menu;
            menu.addSectionHeader (lane == masterLane
                                      ? juce::String ("Master lane")
                                      : "Deck " + juce::String::charToString ((juce_wchar) ('A' + lane)));
            menu.addItem (1, "Reset edits on this clip", clips[(size_t) lane].durationSeconds > 0.0);
            menu.addItem (2, "Reveal source in Finder", clips[(size_t) lane].file.existsAsFile());
            if (lane != masterLane)
            {
                menu.addItem (3, "Clear this deck", onClearDeckRequest != nullptr);
                menu.addSeparator();
                juce::PopupMenu copyTo;
                for (int i = 0; i < numLanes; ++i)
                {
                    if (i == lane) continue;
                    copyTo.addItem (100 + i, "Deck " + juce::String::charToString ((juce_wchar) ('A' + i)));
                }
                menu.addSubMenu ("Copy clip to\xe2\x80\xa6", copyTo);
            }
            menu.addSeparator();
            menu.addItem (4, "Undo", canUndo());
            menu.addItem (5, "Redo", canRedo());

            const juce::Component::SafePointer<ArrangementRecorderView> safeThis (this);
            menu.showMenuAsync (juce::PopupMenu::Options(),
                [safeThis, lane] (int choice)
                {
                    if (safeThis == nullptr) return;
                    auto& self = *safeThis;
                    switch (choice)
                    {
                        case 1: self.resetClipEdits (lane); break;
                        case 2: if (self.clips[(size_t) lane].file.existsAsFile())
                                    self.clips[(size_t) lane].file.revealToUser();
                                break;
                        case 3: if (self.onClearDeckRequest) self.onClearDeckRequest (lane); break;
                        case 4: self.undo(); break;
                        case 5: self.redo(); break;
                        default:
                            if (choice >= 100 && choice < 100 + numLanes)
                                self.copyClip (lane, choice - 100);
                            break;
                    }
                });
        }
        activeDrag = {};
        return;
    }

    activeDrag = hit;
    if (activeDrag.lane < 0)
    {
        if (selectedLane != -1) { selectedLane = -1; repaint(); }
        return;
    }
    if (selectedLane != activeDrag.lane)
    {
        selectedLane = activeDrag.lane;
        repaint();
    }
    const auto& clip = clips[(size_t) activeDrag.lane];
    switch (activeDrag.handle)
    {
        case Handle::TrimStart: dragStartValue = clip.trimStartSeconds; break;
        case Handle::TrimEnd:   dragStartValue = clip.trimEndSeconds;   break;
        case Handle::FadeIn:    dragStartValue = clip.fadeInSeconds;    break;
        case Handle::FadeOut:   dragStartValue = clip.fadeOutSeconds;   break;
        default: dragStartValue = 0.0;
    }
    dragStartPoint = e.getPosition();
    if (activeDrag.handle == Handle::TrimStart || activeDrag.handle == Handle::TrimEnd
        || activeDrag.handle == Handle::FadeIn || activeDrag.handle == Handle::FadeOut)
        pendingEditSnapshot = captureLaneState (activeDrag.lane);
    else
        pendingEditSnapshot = {};
}

void MainPluginEditor::ArrangementRecorderView::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDrag.lane < 0 || activeDrag.handle == Handle::None || activeDrag.handle == Handle::Body)
        return;
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen <= 0.0)
        return;
    const int laneIdx = activeDrag.lane;
    const auto laneRect = laneRects[(size_t) laneIdx];
    const int labelWidth = 90;
    const int trackWidth = juce::jmax (1, laneRect.getWidth() - labelWidth - 8);
    const double secondsPerPixel = maxLen / (double) trackWidth;
    const double delta = (double) (e.getPosition().x - dragStartPoint.x) * secondsPerPixel;

    auto& clip = clips[(size_t) laneIdx];
    const double total = clip.durationSeconds;
    const double minClipWidth = 0.05;
    double readoutValue = 0.0;
    juce::String readoutLabel;
    switch (activeDrag.handle)
    {
        case Handle::TrimStart:
            clip.trimStartSeconds = juce::jlimit (0.0, juce::jmax (0.0, clip.trimEndSeconds - minClipWidth), snapTime (dragStartValue + delta));
            if (clip.fadeInSeconds > clip.trimEndSeconds - clip.trimStartSeconds)
                clip.fadeInSeconds = juce::jmax (0.0, clip.trimEndSeconds - clip.trimStartSeconds);
            readoutValue = clip.trimStartSeconds;
            readoutLabel = "Start";
            break;
        case Handle::TrimEnd:
            clip.trimEndSeconds = juce::jlimit (clip.trimStartSeconds + minClipWidth, total, snapTime (dragStartValue + delta));
            if (clip.fadeOutSeconds > clip.trimEndSeconds - clip.trimStartSeconds - clip.fadeInSeconds)
                clip.fadeOutSeconds = juce::jmax (0.0, clip.trimEndSeconds - clip.trimStartSeconds - clip.fadeInSeconds);
            readoutValue = clip.trimEndSeconds;
            readoutLabel = "End";
            break;
        case Handle::FadeIn:
            clip.fadeInSeconds = juce::jlimit (0.0, juce::jmax (0.0, clip.trimEndSeconds - clip.trimStartSeconds - clip.fadeOutSeconds), snapTime (dragStartValue + delta));
            readoutValue = clip.fadeInSeconds;
            readoutLabel = "Fade In";
            break;
        case Handle::FadeOut:
            clip.fadeOutSeconds = juce::jlimit (0.0, juce::jmax (0.0, clip.trimEndSeconds - clip.trimStartSeconds - clip.fadeInSeconds), snapTime (dragStartValue - delta));
            readoutValue = clip.fadeOutSeconds;
            readoutLabel = "Fade Out";
            break;
        default: break;
    }
    const double beats = snapBpm > 0.0 ? readoutValue * snapBpm / 60.0 : 0.0;
    dragReadout = readoutLabel + ": " + juce::String (readoutValue, 3) + " s  (" + juce::String (beats, 2) + " beats)";
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::mouseUp (const juce::MouseEvent& e)
{
    if (pendingEditSnapshot.lane >= 0)
    {
        const auto& before = pendingEditSnapshot.state;
        const auto& after = clips[(size_t) pendingEditSnapshot.lane];
        const bool changed = before.trimStartSeconds != after.trimStartSeconds
                          || before.trimEndSeconds   != after.trimEndSeconds
                          || before.fadeInSeconds    != after.fadeInSeconds
                          || before.fadeOutSeconds   != after.fadeOutSeconds;
        if (changed)
            commitEdit (std::move (pendingEditSnapshot));
    }
    pendingEditSnapshot = {};
    activeDrag = {};
    dragReadout.clear();
    setMouseCursor (cursorForHandle (hitTestAt (e.getPosition()).handle));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::drawCurve (juce::Graphics& g, juce::Rectangle<int> bounds,
                                                            const AutomationCurve& curve, double timeScale) const
{
    if (curve.points.empty() || timeScale <= 0.0 || bounds.getWidth() <= 4)
        return;

    const float range = juce::jmax (1.0e-6f, curve.maxValue - curve.minValue);
    auto valueToY = [&] (float v) -> float
    {
        const float norm = juce::jlimit (0.0f, 1.0f, (v - curve.minValue) / range);
        return (float) bounds.getBottom() - norm * (float) bounds.getHeight();
    };
    auto timeToX = [&] (double t) -> float
    {
        const float fr = (float) juce::jlimit (0.0, 1.0, t / timeScale);
        return (float) bounds.getX() + fr * (float) bounds.getWidth();
    };

    juce::Path path;
    float prevX = timeToX (curve.points.front().first);
    float prevY = valueToY (curve.points.front().second);
    path.startNewSubPath (prevX, prevY);
    for (size_t i = 1; i < curve.points.size(); ++i)
    {
        const float x = timeToX (curve.points[i].first);
        const float y = valueToY (curve.points[i].second);
        path.lineTo (x, prevY);
        path.lineTo (x, y);
        prevX = x;
        prevY = y;
    }
    path.lineTo ((float) bounds.getRight(), prevY);

    g.setColour (curve.colour.withAlpha (0.85f));
    g.strokePath (path, juce::PathStrokeType (1.3f));
}

void MainPluginEditor::ArrangementRecorderView::loadThumbnail (juce::AudioThumbnail& thumb, const juce::File& file)
{
    thumb.clear();
    if (formatManager == nullptr || ! file.existsAsFile())
        return;
    thumb.setSource (new juce::FileInputSource (file));
}

void MainPluginEditor::ArrangementRecorderView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto updateFromThumb = [this] (int laneIndex, juce::AudioThumbnail* thumb)
    {
        if (thumb == nullptr) return;
        const double len = thumb->getTotalLength();
        auto& clip = clips[(size_t) laneIndex];
        if (len > 0.0 && clip.durationSeconds <= 0.0)
        {
            clip.durationSeconds = len;
            if (clip.trimEndSeconds <= 0.0)
                clip.trimEndSeconds = len;
        }
    };
    for (int i = 0; i < numLanes; ++i)
        updateFromThumb (i, deckThumbs[(size_t) i].get());
    updateFromThumb (masterLane, masterThumb.get());
    repaint();
}

juce::AudioThumbnail* MainPluginEditor::ArrangementRecorderView::thumbForLane (int laneIndex) const
{
    if (laneIndex == masterLane) return masterThumb.get();
    if (juce::isPositiveAndBelow (laneIndex, numLanes)) return deckThumbs[(size_t) laneIndex].get();
    return nullptr;
}

double MainPluginEditor::ArrangementRecorderView::snapTime (double seconds) const
{
    if (! snapEnabled || snapBpm <= 0.0 || snapBeatGrid <= 0.0)
        return seconds;
    const double secondsPerBeat = 60.0 / snapBpm;
    const double gridSeconds = secondsPerBeat * snapBeatGrid;
    return std::round (seconds / gridSeconds) * gridSeconds;
}

void MainPluginEditor::ArrangementRecorderView::setSnap (bool enabled, double beatGrid, double bpm)
{
    snapEnabled = enabled;
    if (beatGrid > 0.0) snapBeatGrid = beatGrid;
    if (bpm > 0.0) snapBpm = bpm;
}

MainPluginEditor::ArrangementRecorderView::LaneSnapshot
MainPluginEditor::ArrangementRecorderView::captureLaneState (int laneIndex) const
{
    LaneSnapshot s;
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes))
        return s;
    s.lane = laneIndex;
    s.state = clips[(size_t) laneIndex];
    return s;
}

void MainPluginEditor::ArrangementRecorderView::commitEdit (LaneSnapshot before)
{
    if (before.lane < 0) return;
    undoStack.push_back (std::move (before));
    redoStack.clear();
}

void MainPluginEditor::ArrangementRecorderView::undo()
{
    if (undoStack.empty()) return;
    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    LaneSnapshot redoEntry;
    redoEntry.lane = entry.lane;
    redoEntry.state = clips[(size_t) entry.lane];
    redoStack.push_back (std::move (redoEntry));
    clips[(size_t) entry.lane] = entry.state;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::redo()
{
    if (redoStack.empty()) return;
    auto entry = std::move (redoStack.back());
    redoStack.pop_back();
    LaneSnapshot undoEntry;
    undoEntry.lane = entry.lane;
    undoEntry.state = clips[(size_t) entry.lane];
    undoStack.push_back (std::move (undoEntry));
    clips[(size_t) entry.lane] = entry.state;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::resetClipEdits (int laneIndex)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& c = clips[(size_t) laneIndex];
    if (c.durationSeconds <= 0.0) return;
    auto before = captureLaneState (laneIndex);
    c.trimStartSeconds = 0.0;
    c.trimEndSeconds = c.durationSeconds;
    c.fadeInSeconds = 0.0;
    c.fadeOutSeconds = 0.0;
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::resetSelectedClipEdits()
{
    if (selectedLane >= 0)
        resetClipEdits (selectedLane);
}

bool MainPluginEditor::ArrangementRecorderView::copyClip (int sourceLane, int destLane)
{
    if (! juce::isPositiveAndBelow (sourceLane, numEditableLanes)) return false;
    if (! juce::isPositiveAndBelow (destLane, numEditableLanes)) return false;
    if (sourceLane == destLane) return false;
    auto before = captureLaneState (destLane);
    auto& src = clips[(size_t) sourceLane];
    auto& dst = clips[(size_t) destLane];
    if (src.file == juce::File() || src.durationSeconds <= 0.0) return false;
    dst = src;
    dst.selected = false;
    if (destLane != masterLane)
        deckFiles[(size_t) destLane] = src.file;
    if (auto* thumb = thumbForLane (destLane))
        loadThumbnail (*thumb, src.file);
    commitEdit (std::move (before));
    repaint();
    return true;
}

double MainPluginEditor::ArrangementRecorderView::computeMaxLengthSeconds() const
{
    double maxLen = 0.0;
    if (masterThumb != nullptr)
        maxLen = juce::jmax (maxLen, masterThumb->getTotalLength());
    for (const auto& thumb : deckThumbs)
        if (thumb != nullptr)
            maxLen = juce::jmax (maxLen, thumb->getTotalLength());
    return maxLen;
}

void MainPluginEditor::ArrangementRecorderView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff080a12));
    g.fillRoundedRectangle (bounds, 18.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff151a29), bounds.getTopLeft(),
                                             juce::Colour (0xff070911), bounds.getBottomRight(), false));
    g.fillRoundedRectangle (bounds.reduced (1.0f), 17.0f);

    juce::Colour accent;
    juce::String modeLabel;
    switch (mode)
    {
        case Mode::Recording: accent = juce::Colour (0xffff4c6a); modeLabel = "RECORDING"; break;
        case Mode::Finished:  accent = juce::Colour (0xff67f6d2); modeLabel = "ARRANGEMENT"; break;
        case Mode::Idle:      accent = juce::Colour (0xff67f6d2); modeLabel = "READY"; break;
    }
    g.setColour (accent.withAlpha (0.75f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 18.0f, 1.5f);

    auto area = getLocalBounds().reduced (18, 14);
    auto header = area.removeFromTop (24);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.94f));
    g.drawText ("Set Arrangement", header.removeFromLeft (220), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (accent.withAlpha (0.95f));
    if (mode == Mode::Recording)
    {
        const int total = (int) std::floor (elapsedSeconds);
        const int mins = total / 60;
        const int secs = total % 60;
        const auto blink = ((juce::Time::currentTimeMillis() / 500) % 2) == 0;
        const juce::String dot = blink ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String ("  ");
        const juce::String elapsed = juce::String::formatted ("%02d:%02d", mins, secs);
        g.drawText (dot + modeLabel + "  " + elapsed, header, juce::Justification::centredRight);
    }
    else
    {
        g.drawText (modeLabel, header, juce::Justification::centredRight);
    }

    area.removeFromTop (6);

    const double maxLen = computeMaxLengthSeconds();
    const bool hasAudio = mode == Mode::Finished && maxLen > 0.0;

    const int rulerHeight = 18;
    const int labelWidth = 90;
    auto ruler = area.removeFromTop (rulerHeight);
    {
        auto rulerArea = ruler.withTrimmedLeft (labelWidth);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawHorizontalLine (rulerArea.getBottom(), (float) rulerArea.getX(), (float) rulerArea.getRight());
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const int divisions = 8;
        for (int t = 0; t <= divisions; ++t)
        {
            const float x = (float) rulerArea.getX() + (float) t * (float) rulerArea.getWidth() / (float) divisions;
            g.setColour (juce::Colours::white.withAlpha (t % 2 == 0 ? 0.30f : 0.14f));
            g.drawVerticalLine ((int) std::round (x), (float) rulerArea.getY(), (float) rulerArea.getBottom());
            if (t < divisions && hasAudio)
            {
                const double secs = maxLen * (double) t / (double) divisions;
                const int s = (int) std::floor (secs);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawText (juce::String::formatted ("%d:%02d", s / 60, s % 60),
                            (int) x + 4, rulerArea.getY() - 1, 56, rulerHeight, juce::Justification::centredLeft);
            }
        }
    }

    const int statusHeight = 18;
    auto statusArea = area.removeFromBottom (statusHeight);
    area.removeFromBottom (6);

    const int lanesTotal = numLanes + 1;
    const int laneGap = 4;
    const int laneHeight = juce::jmax (14, (area.getHeight() - laneGap * (lanesTotal - 1)) / lanesTotal);

    const std::array<juce::Colour, numLanes> deckColours { juce::Colour (0xff26d9ff), juce::Colour (0xffff4c5f),
                                                            juce::Colour (0xff50f06b), juce::Colour (0xffffcf4a) };

    auto drawLane = [&] (juce::Rectangle<int> lane, const juce::String& label, juce::Colour colour,
                         juce::AudioThumbnail* thumb, const juce::File& file, int deckIndex)
    {
        if (deckIndex >= 0 && deckIndex < numEditableLanes)
            laneRects[(size_t) deckIndex] = lane;

        const bool isSelected = (deckIndex >= 0 && deckIndex == selectedLane);
        g.setColour (juce::Colours::white.withAlpha (isSelected ? 0.085f : 0.045f));
        g.fillRoundedRectangle (lane.toFloat(), 6.0f);
        g.setColour (colour.withAlpha (isSelected ? 0.7f : 0.32f));
        g.drawRoundedRectangle (lane.toFloat(), 6.0f, isSelected ? 1.6f : 1.0f);

        auto labelCell = lane.removeFromLeft (labelWidth);
        g.setColour (colour.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, labelCell.reduced (8, 0), juce::Justification::centredLeft);

        auto track = lane.reduced (4, 3);
        if (mode == Mode::Finished && thumb != nullptr && thumb->getTotalLength() > 0.0 && maxLen > 0.0)
        {
            const double laneLen = thumb->getTotalLength();
            double clipStart = 0.0;
            double clipEnd = laneLen;
            double fadeIn = 0.0, fadeOut = 0.0;
            if (deckIndex >= 0 && deckIndex < numEditableLanes)
            {
                const auto& clip = clips[(size_t) deckIndex];
                if (clip.durationSeconds > 0.0)
                {
                    clipStart = juce::jlimit (0.0, laneLen, clip.trimStartSeconds);
                    clipEnd = juce::jlimit (clipStart, laneLen, clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : laneLen);
                    fadeIn = juce::jlimit (0.0, clipEnd - clipStart, clip.fadeInSeconds);
                    fadeOut = juce::jlimit (0.0, clipEnd - clipStart - fadeIn, clip.fadeOutSeconds);
                }
            }

            auto timeToX = [&] (double t) -> int
            {
                return track.getX() + (int) std::round ((double) track.getWidth() * t / maxLen);
            };
            const int clipX0 = timeToX (clipStart);
            const int clipX1 = juce::jmax (clipX0 + 4, timeToX (clipEnd));
            auto clipRect = juce::Rectangle<int> (clipX0, track.getY(), clipX1 - clipX0, track.getHeight());

            g.setColour (colour.withAlpha (isSelected ? 0.32f : 0.22f));
            g.fillRoundedRectangle (clipRect.toFloat(), 4.0f);
            g.setColour (colour.withAlpha (0.92f));
            thumb->drawChannels (g, clipRect.reduced (2), clipStart, clipEnd, 0.95f);
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (file.getFileName(), clipRect.reduced (8, 0), juce::Justification::centredLeft);

            if (fadeIn > 0.0)
            {
                const int fadeInX = timeToX (clipStart + fadeIn);
                juce::Path tri;
                tri.startNewSubPath ((float) clipRect.getX(), (float) clipRect.getY());
                tri.lineTo ((float) fadeInX, (float) clipRect.getY());
                tri.lineTo ((float) clipRect.getX(), (float) clipRect.getBottom());
                tri.closeSubPath();
                g.setColour (juce::Colour (0xff080a12).withAlpha (0.65f));
                g.fillPath (tri);
                g.setColour (colour.withAlpha (0.95f));
                g.drawLine ((float) clipRect.getX(), (float) clipRect.getBottom(),
                            (float) fadeInX, (float) clipRect.getY(), 1.2f);
            }
            if (fadeOut > 0.0)
            {
                const int fadeOutX = timeToX (clipEnd - fadeOut);
                juce::Path tri;
                tri.startNewSubPath ((float) fadeOutX, (float) clipRect.getY());
                tri.lineTo ((float) clipRect.getRight(), (float) clipRect.getY());
                tri.lineTo ((float) clipRect.getRight(), (float) clipRect.getBottom());
                tri.closeSubPath();
                g.setColour (juce::Colour (0xff080a12).withAlpha (0.65f));
                g.fillPath (tri);
                g.setColour (colour.withAlpha (0.95f));
                g.drawLine ((float) fadeOutX, (float) clipRect.getY(),
                            (float) clipRect.getRight(), (float) clipRect.getBottom(), 1.2f);
            }

            const float handleW = 4.0f;
            g.setColour (juce::Colours::white.withAlpha (isSelected ? 0.85f : 0.45f));
            g.fillRect (juce::Rectangle<float> ((float) clipRect.getX(), (float) clipRect.getY(), handleW, (float) clipRect.getHeight()));
            g.fillRect (juce::Rectangle<float> ((float) clipRect.getRight() - handleW, (float) clipRect.getY(), handleW, (float) clipRect.getHeight()));

            const float grabSize = 7.0f;
            const float grabAlpha = isSelected ? 0.95f : 0.6f;
            g.setColour (colour.withAlpha (grabAlpha));
            if (fadeIn <= 0.0)
            {
                juce::Path g0;
                g0.addTriangle ((float) clipRect.getX(),                  (float) clipRect.getY(),
                                (float) clipRect.getX() + grabSize,        (float) clipRect.getY(),
                                (float) clipRect.getX(),                   (float) clipRect.getY() + grabSize);
                g.fillPath (g0);
            }
            if (fadeOut <= 0.0)
            {
                juce::Path g1;
                g1.addTriangle ((float) clipRect.getRight() - grabSize,    (float) clipRect.getY(),
                                (float) clipRect.getRight(),               (float) clipRect.getY(),
                                (float) clipRect.getRight(),               (float) clipRect.getY() + grabSize);
                g.fillPath (g1);
            }

            if (deckIndex >= 0 && deckIndex < numLanes)
                for (const auto& curve : deckAutomation[(size_t) deckIndex])
                    drawCurve (g, clipRect.reduced (2), curve, maxLen);
        }
        else if (mode == Mode::Recording)
        {
            g.setColour (colour.withAlpha (0.18f));
            const auto rect = track.toFloat().withHeight ((float) track.getHeight() * 0.6f).withY ((float) track.getCentreY() - (float) track.getHeight() * 0.3f);
            g.fillRoundedRectangle (rect, 3.0f);
            g.setColour (colour.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("capturing\xe2\x80\xa6", track.reduced (6, 0), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("empty lane", track.reduced (6, 0), juce::Justification::centredLeft);
        }
    };

    auto masterLaneRect = area.removeFromTop (laneHeight);
    drawLane (masterLaneRect, "MASTER", juce::Colour (0xff67f6d2), masterThumb.get(), masterFile, masterLane);
    area.removeFromTop (laneGap);

    for (int i = 0; i < numLanes; ++i)
    {
        auto laneRect = area.removeFromTop (laneHeight);
        const auto laneLabel = "DECK " + juce::String::charToString ((juce_wchar) ('A' + i));
        drawLane (laneRect, laneLabel, deckColours[(size_t) i], deckThumbs[(size_t) i].get(), deckFiles[(size_t) i], i);
        if (i < numLanes - 1)
            area.removeFromTop (laneGap);
    }

    g.setFont (juce::FontOptions (10.5f));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    const auto displayStatus = dragReadout.isNotEmpty()
                                   ? dragReadout
                                   : (mode == Mode::Recording && recordingPath.isNotEmpty()
                                       ? "Writing: " + recordingPath
                                       : status);
    if (dragReadout.isNotEmpty())
        g.setColour (juce::Colour (0xff67f6d2).withAlpha (0.9f));
    g.drawText (displayStatus, statusArea, juce::Justification::centredLeft);

    if (snapEnabled)
    {
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (juce::Colour (0xff67f6d2).withAlpha (0.75f));
        g.drawText ("SNAP \xe2\x97\x86 " + juce::String (snapBeatGrid, 2) + " beat @ " + juce::String (snapBpm, 1) + " BPM",
                    statusArea, juce::Justification::centredRight);
    }
}

MainPluginEditor::MainPluginEditor (MainPluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      presetManager (p.parameters(), "stemdeck")
{
    setLookAndFeel (&lookAndFeel);
    thumbnailFormatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    thumbnailFormatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif

    setSize (1500, 940);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    setupComboBox (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    refreshUserPresets();

    setupFileBrowser();

    title.setText ("StemDeck PRO", juce::dontSendNotification);
    title.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText ("FOUR DECKS  •  STEM MIXER  •  ARRANGEMENT RECORDER", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    subtitle.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    standaloneWorkflowGuide.setText ("STANDALONE APP ALPHA QUICK START  Load or drag a full mix onto Deck A/B/C/D  •  AI Split after local Demucs setup  •  Mix stems/faders/FX/pads  •  Record WAV to capture the performed arrangement  •  Not notarized or sale-ready", juce::dontSendNotification);
    standaloneWorkflowGuide.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2).withAlpha (0.94f));
    standaloneWorkflowGuide.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    standaloneWorkflowGuide.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (standaloneWorkflowGuide);

    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (tempoLabel);

    pluginHostStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.82f));
    pluginHostStatus.setFont (juce::FontOptions (11.0f));
    pluginHostStatus.setJustificationType (juce::Justification::centredLeft);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    addAndMakeVisible (pluginHostStatus);

    auto setupSectionLabel = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.82f));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    };
    setupSectionLabel (liveMixerLabel, "LIVE MIXER / STEM DECKS");
    setupSectionLabel (arrangementLabel, "ARRANGEMENT CAPTURE");
    setupSectionLabel (samplerLabel, "PERFORMANCE PADS");

    setupButton (scanPluginsButton);
    setupButton (reloadPluginCacheButton);
    setupButton (loadPluginButton);
    setupButton (clearChainButton);
    setupComboBox (pluginChainSelector);
    setupComboBox (pluginSelector);
    pluginHostMessage.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.88f));
    pluginHostMessage.setFont (juce::FontOptions (11.0f));
    pluginHostMessage.setJustificationType (juce::Justification::centredLeft);
    pluginHostMessage.setText ("FX host cache: " + processorRef.getPluginHostCacheFile().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (scanPluginsButton);
    addAndMakeVisible (reloadPluginCacheButton);
    addAndMakeVisible (loadPluginButton);
    addAndMakeVisible (clearChainButton);
    addAndMakeVisible (pluginHostMessage);

    masterKeyLabel.setText ("MASTER KEY", juce::dontSendNotification);
    masterKeyLabel.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2).withAlpha (0.85f));
    masterKeyLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    masterKeyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (masterKeyLabel);
    setupComboBox (masterKey);
    {
        const auto keyChoices = processorRef.getKeyTargetChoices();
        for (int i = 0; i < keyChoices.size(); ++i)
            masterKey.addItem (keyChoices[i], i + 1);
        masterKey.setSelectedId (1, juce::dontSendNotification);
    }
    masterKeyAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), "masterKey", masterKey);
    setupButton (syncAllDecksButton);
    syncAllDecksButton.onClick = [this] { showSyncAllDecksMenu(); };
    addAndMakeVisible (syncAllDecksButton);
    setupButton (stemLinkToggle);
    stemLinkAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "stemLink", stemLinkToggle);
    addAndMakeVisible (stemLinkToggle);

    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto prefix = deckPrefix (d);
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            processorRef.parameters().addParameterListener (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain", this);
    }

    const auto chainNames = processorRef.getPluginHostChainNames();
    for (int i = 0; i < chainNames.size(); ++i)
        pluginChainSelector.addItem (chainNames[i], i + 1);
    pluginChainSelector.setSelectedId (1, juce::dontSendNotification);
    refreshHostPluginChoices();
    scanPluginsButton.onClick = [this] { runPluginHostScan(); };
    reloadPluginCacheButton.onClick = [this]
    {
        const bool loaded = processorRef.loadPluginHostCache();
        refreshHostPluginChoices();
        pluginHostMessage.setText (loaded ? "Reloaded FX cache" : "No FX cache found yet", juce::dontSendNotification);
    };
    loadPluginButton.onClick = [this] { loadSelectedHostPlugin(); };
    clearChainButton.onClick = [this]
    {
        const int chainIndex = pluginChainSelector.getSelectedId() - 1;
        processorRef.clearHostPluginChain (chainIndex);
        pluginHostMessage.setText ("Cleared " + pluginChainSelector.getText(), juce::dontSendNotification);
    };

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckName = deckLabel (i).toUpperCase();
        deckWaves[(size_t) i] = std::make_unique<WaveformDisplay> (thumbnailFormatManager, thumbnailCache, deckName);
        deckWaves[(size_t) i]->setOnFileDropped ([this, i] (const juce::File& f) { processorRef.loadDeckFileAsync (i, f); });
        addAndMakeVisible (*deckWaves[(size_t) i]);

        setupDeck (deckControls[(size_t) i], deckPrefix (i), juce::String::charToString ((juce_wchar) ('A' + i)) + " Play");
        deckControls[(size_t) i].load.onClick = [this, i] { openDeckChooser (i); };
        deckControls[(size_t) i].loadStem.onClick = [this, i]
        {
            const int selectedLane = deckControls[(size_t) i].stemLane.getSelectedId() - 1;
            openDeckStemChooser (i, selectedLane < 0 ? DeckPlayer::fullMixStem : selectedLane);
        };
        deckControls[(size_t) i].aiStems.onClick = [this, i] { processorRef.requestDeckAiStemsAsync (i); };
        deckControls[(size_t) i].cue.onClick = [this, i] { processorRef.rewindDeck (i); };
    }

    addAndMakeVisible (mixerPanel);
    setupSlider (crossfader, false);
    setupSlider (masterBpm, false, " BPM");
    setupSlider (master, true, " dB");
    setupSlider (limiter, true, " dB");
    hostSync.setButtonText ("Host sync");
    setupButton (hostSync);
    addAndMakeVisible (hostSync);
    beatSync.setButtonText ("Beat sync");
    setupButton (beatSync);
    addAndMakeVisible (beatSync);
    micMonitor.setButtonText ("Mic monitor");
    setupButton (micMonitor);
    addAndMakeVisible (micMonitor);
    setupSlider (micGain, false, " dB");
    setupButton (recordButton);
    recordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (recordButton);
    setupButton (revealRecordingButton);
    revealRecordingButton.setEnabled (false);
    revealRecordingButton.onClick = [this] { revealLastRecording(); };
    addAndMakeVisible (revealRecordingButton);
    setupButton (reloadIntoDecksButton);
    reloadIntoDecksButton.setEnabled (false);
    reloadIntoDecksButton.onClick = [this] { reloadLastRecordingIntoDecks(); };
    addAndMakeVisible (reloadIntoDecksButton);
    setupButton (bounceEditsButton);
    bounceEditsButton.setEnabled (false);
    bounceEditsButton.onClick = [this] { bounceArrangementEdits(); };
    addAndMakeVisible (bounceEditsButton);
    setupButton (snapToggleButton);
    snapToggleButton.setClickingTogglesState (true);
    snapToggleButton.onClick = [this]
    {
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        arrangementView.setSnap (snapToggleButton.getToggleState(), 0.25, bpm);
        arrangementView.repaint();
    };
    addAndMakeVisible (snapToggleButton);
    setupButton (undoButton);
    undoButton.onClick = [this] { arrangementView.undo(); };
    addAndMakeVisible (undoButton);
    setupButton (redoButton);
    redoButton.onClick = [this] { arrangementView.redo(); };
    addAndMakeVisible (redoButton);
    arrangementView.onClearDeckRequest = [this] (int lane)
    {
        if (lane >= 0 && lane < MainPluginProcessor::numDecks)
        {
            processorRef.clearDeck (lane);
            if (auto* wave = deckWaves[(size_t) lane].get())
                wave->setFile (juce::File());
            recordStatus.setText ("Cleared Deck " + juce::String::charToString ((juce_wchar) ('A' + lane)),
                                  juce::dontSendNotification);
        }
    };
    recordStatus.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    recordStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (recordStatus);
    arrangementView.initialise (thumbnailFormatManager, thumbnailCache);
    arrangementView.setIdle ("Hit Record to capture your set across four deck lanes.");
    addAndMakeVisible (arrangementView);

    crossfaderAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "crossfader", crossfader);
    masterBpmAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "masterBpm", masterBpm);
    masterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "master", master);
    limiterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "limiterCeiling", limiter);
    hostSyncAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "hostSync", hostSync);
    beatSyncAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "beatSync", beatSync);
    micMonitorAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), "micMonitor", micMonitor);
    micGainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "micGain", micGain);

    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto& pad = pads[(size_t) i];
        pad.trigger.setButtonText ("PAD " + juce::String (i + 1) + " [" + padKeyLabel (i) + "]");
        setupButton (pad.trigger);
        setupButton (pad.load);
        setupSlider (pad.gain, false, " dB");
        pad.trigger.onClick = [this, i] { processorRef.triggerPad (i); };
        pad.load.onClick = [this, i] { openPadChooser (i); };
        pad.gainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "pad" + juce::String (i + 1) + "Gain", pad.gain);
        addAndMakeVisible (pad.trigger);
        addAndMakeVisible (pad.load);
        addAndMakeVisible (pad.gain);
    }

    startTimerHz (8);

    juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]
    {
        if (safeThis != nullptr)
            safeThis->grabKeyboardFocus();
    });
}

bool MainPluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            arrangementView.redo();
        else
            arrangementView.undo();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y')
    {
        arrangementView.redo();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (arrangementView.getSelectedLane() >= 0)
        {
            arrangementView.resetSelectedClipEdits();
            return true;
        }
    }

    const int padIndex = padIndexForKeyPress (key);
    if (juce::isPositiveAndBelow (padIndex, PadBank::numPads))
    {
        processorRef.triggerPad (padIndex);
        return true;
    }

    return false;
}

void MainPluginEditor::setupButton (juce::Button& button)
{
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff121621));
    button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff0f5664));
    button.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.9f));
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void MainPluginEditor::setupSlider (juce::Slider& slider, bool rotary, const juce::String& suffix)
{
    slider.setWantsKeyboardFocus (false);
    slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff67f6d2));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.12f));
    slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff67f6d2));
    slider.setColour (juce::Slider::backgroundColourId, juce::Colours::white.withAlpha (0.08f));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void MainPluginEditor::setupComboBox (juce::ComboBox& comboBox)
{
    comboBox.setWantsKeyboardFocus (false);
    comboBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff10141f));
    comboBox.setColour (juce::ComboBox::textColourId, juce::Colours::white.withAlpha (0.9f));
    comboBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff67f6d2).withAlpha (0.35f));
    comboBox.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff67f6d2));
    comboBox.setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff101522));
    comboBox.setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    addAndMakeVisible (comboBox);
}

void MainPluginEditor::setupDeck (DeckControls& controls, const juce::String& prefix, const juce::String& playText)
{
    controls.play.setButtonText (playText);
    setupButton (controls.load);
    setupButton (controls.loadStem);
    setupButton (controls.aiStems);
    setupButton (controls.play);
    setupButton (controls.cue);
    setupComboBox (controls.stemLane);
    setupComboBox (controls.tempoInterpretation);
    setupComboBox (controls.syncMode);
    setupComboBox (controls.keyTarget);
    const auto stemLabels = processorRef.getStemLaneLabels();
    for (int i = 0; i < stemLabels.size(); ++i)
        controls.stemLane.addItem (stemLabels[i], i + 1);
    controls.stemLane.setSelectedId (DeckPlayer::fullMixStem + 1, juce::dontSendNotification);
    const auto tempoChoices = processorRef.getTempoInterpretationChoices();
    for (int i = 0; i < tempoChoices.size(); ++i)
        controls.tempoInterpretation.addItem (tempoChoices[i], i + 1);
    controls.tempoInterpretation.setSelectedId (1, juce::dontSendNotification);
    const auto syncChoices = processorRef.getSyncModeChoices();
    for (int i = 0; i < syncChoices.size(); ++i)
        controls.syncMode.addItem (syncChoices[i], i + 1);
    controls.syncMode.setSelectedId (1, juce::dontSendNotification);
    const auto keyTargets = processorRef.getKeyTargetChoices();
    for (int i = 0; i < keyTargets.size(); ++i)
        controls.keyTarget.addItem (keyTargets[i], i + 1);
    controls.keyTarget.setSelectedId (1, juce::dontSendNotification);
    controls.keyLock.setButtonText ("KeyLock");
    setupButton (controls.keyLock);
    controls.liveKey.setButtonText ("LiveKey");
    setupButton (controls.liveKey);
    controls.keyInfo.setColour (juce::Label::textColourId, juce::Colours::lightgrey.withAlpha (0.9f));
    controls.keyInfo.setJustificationType (juce::Justification::centredLeft);
    controls.keyInfo.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (controls.keyInfo);
    controls.stemStatus.setColour (juce::Label::textColourId, juce::Colour (0xff67f6d2).withAlpha (0.92f));
    controls.stemStatus.setJustificationType (juce::Justification::centredLeft);
    controls.stemStatus.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    controls.stemStatus.setText ("AI stems: load a full mix", juce::dontSendNotification);
    addAndMakeVisible (controls.stemStatus);
    setupSlider (controls.bpm, false, " BPM");
    setupSlider (controls.beatOffset, false, " beat");
    setupSlider (controls.downbeat, false, " beat");
    setupSlider (controls.volume, false, " dB");
    setupSlider (controls.fader, false);
    setupSlider (controls.pitch, false, " st");
    setupSlider (controls.low, true, " dB");
    setupSlider (controls.mid, true, " dB");
    setupSlider (controls.high, true, " dB");
    setupSlider (controls.filter, true);

    controls.volume.setSliderStyle (juce::Slider::LinearVertical);
    controls.fader.setSliderStyle (juce::Slider::LinearVertical);
    controls.pitch.setSliderStyle (juce::Slider::LinearVertical);
    controls.bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    controls.beatOffset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    controls.downbeat.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);

    addAndMakeVisible (controls.load);
    addAndMakeVisible (controls.loadStem);
    addAndMakeVisible (controls.aiStems);
    addAndMakeVisible (controls.play);
    addAndMakeVisible (controls.cue);
    addAndMakeVisible (controls.keyLock);
    addAndMakeVisible (controls.liveKey);

    controls.playAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "Play", controls.play);
    controls.keyLockAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "KeyLock", controls.keyLock);
    controls.liveKeyAttachment = std::make_unique<ButtonAttachment> (processorRef.parameters(), prefix + "LiveKey", controls.liveKey);
    controls.tempoInterpretationAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "TempoInterpretation", controls.tempoInterpretation);
    controls.syncModeAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "SyncMode", controls.syncMode);
    controls.keyTargetAttachment = std::make_unique<ComboBoxAttachment> (processorRef.parameters(), prefix + "KeyTarget", controls.keyTarget);
    controls.bpmAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Bpm", controls.bpm);
    controls.beatOffsetAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "BeatOffset", controls.beatOffset);
    controls.downbeatAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Downbeat", controls.downbeat);
    controls.volumeAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Volume", controls.volume);
    controls.faderAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Fader", controls.fader);
    controls.pitchAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Pitch", controls.pitch);
    controls.lowAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Low", controls.low);
    controls.midAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Mid", controls.mid);
    controls.highAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "High", controls.high);
    controls.filterAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), prefix + "Filter", controls.filter);

    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
    {
        auto& slider = controls.stemGains[(size_t) lane];
        setupSlider (slider, true, " dB");
        slider.setRange (-60.0, 12.0);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff67f6d2));
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setName (DeckPlayer::stemLaneLabel (lane));
        slider.setTooltip (DeckPlayer::stemLaneLabel (lane) + " stem gain");
        addAndMakeVisible (slider);
        controls.stemGainAttachments[(size_t) lane] = std::make_unique<SliderAttachment> (
            processorRef.parameters(),
            prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
            slider);
    }
}

void MainPluginEditor::openDeckStemChooser (int deckIndex, int stemLaneIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)
        || ! juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes))
        return;

    deckChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, deckIndex, stemLaneIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                processorRef.loadDeckStemFileAsync (deckIndex, stemLaneIndex, file);
                if (stemLaneIndex == DeckPlayer::fullMixStem)
                    if (auto* wave = deckWaves[(size_t) deckIndex].get())
                        wave->setFile (file);
            }
        });
}

juce::String MainPluginEditor::supportedPattern() const
{
    return "*.wav;*.aif;*.aiff;*.flac;*.mp3";
}

void MainPluginEditor::openDeckChooser (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;

    deckChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, deckIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
            {
                processorRef.loadDeckFileAsync (deckIndex, file);
                if (auto* wave = deckWaves[(size_t) deckIndex].get())
                    wave->setFile (file);
            }
        });
}

void MainPluginEditor::openPadChooser (int padIndex)
{
    padChooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, padIndex] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                processorRef.loadPadFileAsync (padIndex, file);
        });
}

void MainPluginEditor::toggleRecording()
{
    if (processorRef.isMasterRecording())
    {
        const auto file = processorRef.stopMasterRecording();
        const auto arrangement = processorRef.getLastArrangementFile();
        const auto savedMsg = file.existsAsFile()
                                  ? "Saved: " + file.getFileName() + " + arrangement " + arrangement.getFileName()
                                  : juce::String ("Recording stopped");
        recordStatus.setText (savedMsg, juce::dontSendNotification);
        loadLastArrangementIntoView (savedMsg);
        return;
    }

    arrangementView.clearArrangement();
    revealRecordingButton.setEnabled (false);
    reloadIntoDecksButton.setEnabled (false);
    bounceEditsButton.setEnabled (false);

    recordChooser.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
                return;

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            const bool started = processorRef.beginMasterRecording (file);
            recordStatus.setText (started ? "Recording: " + file.getFileName() : "Could not start recording", juce::dontSendNotification);
            if (started)
                arrangementView.setRecordingProgress (0.0, file.getFullPathName());
        });
}

void MainPluginEditor::revealLastRecording()
{
    const auto masterRec = processorRef.getMasterRecordingTarget();
    const auto target = masterRec.existsAsFile() ? masterRec : processorRef.getLastArrangementFile();
    if (target.existsAsFile())
        target.revealToUser();
}

void MainPluginEditor::reloadLastRecordingIntoDecks()
{
    int loaded = 0;
    const auto& clips = arrangementView.getClips();
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto file = clips[(size_t) i].file;
        if (! file.existsAsFile() || file.getSize() < 1024)
            file = processorRef.getLastDeckRecordingFile (i);
        if (! file.existsAsFile() || file.getSize() < 1024)
            continue;
        processorRef.loadDeckFileAsync (i, file);
        if (auto* wave = deckWaves[(size_t) i].get())
            wave->setFile (file);
        ++loaded;
    }
    recordStatus.setText (loaded > 0
                              ? "Reloaded " + juce::String (loaded) + " deck capture" + (loaded == 1 ? "" : "s") + " into decks A–D"
                              : juce::String ("No deck captures to reload — record a set first"),
                          juce::dontSendNotification);
}

void MainPluginEditor::bounceArrangementEdits()
{
    const auto& clips = arrangementView.getClips();
    int rendered = 0;
    juce::String errors;
    auto laneName = [] (int i) -> juce::String
    {
        return i == ArrangementRecorderView::masterLane
                   ? juce::String ("Master")
                   : "Deck " + juce::String::charToString ((juce_wchar) ('A' + i));
    };
    for (int i = 0; i < ArrangementRecorderView::numEditableLanes; ++i)
    {
        const auto& clip = clips[(size_t) i];
        if (! clip.file.existsAsFile() || clip.durationSeconds <= 0.0)
            continue;
        const double startSec = juce::jlimit (0.0, clip.durationSeconds, clip.trimStartSeconds);
        const double endSec   = juce::jlimit (startSec, clip.durationSeconds, clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
        const double regionLen = endSec - startSec;
        if (regionLen < 0.05)
            continue;
        const double fadeIn  = juce::jlimit (0.0, regionLen, clip.fadeInSeconds);
        const double fadeOut = juce::jlimit (0.0, regionLen - fadeIn, clip.fadeOutSeconds);
        const bool isEdited = startSec > 0.001
                             || endSec < clip.durationSeconds - 0.001
                             || fadeIn > 0.001
                             || fadeOut > 0.001;
        if (! isEdited)
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (thumbnailFormatManager.createReaderFor (clip.file));
        if (reader == nullptr)
        {
            errors << laneName (i) << ": read failed. ";
            continue;
        }

        const double sr = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        const auto startSample = (juce::int64) std::round (startSec * sr);
        const auto endSample   = (juce::int64) std::round (endSec   * sr);
        const juce::int64 numSamples = juce::jmax<juce::int64> (1, endSample - startSample);
        const int channels = (int) reader->numChannels;

        juce::AudioBuffer<float> buffer (channels, (int) numSamples);
        if (! reader->read (&buffer, 0, (int) numSamples, startSample, true, true))
        {
            errors << laneName (i) << ": region read failed. ";
            continue;
        }

        const int fadeInSamples  = (int) std::round (fadeIn  * sr);
        const int fadeOutSamples = (int) std::round (fadeOut * sr);
        if (fadeInSamples > 1)
            buffer.applyGainRamp (0, juce::jmin (fadeInSamples, buffer.getNumSamples()), 0.0f, 1.0f);
        if (fadeOutSamples > 1)
        {
            const int n = juce::jmin (fadeOutSamples, buffer.getNumSamples());
            buffer.applyGainRamp (buffer.getNumSamples() - n, n, 1.0f, 0.0f);
        }

        auto destFile = clip.file.getParentDirectory()
                           .getChildFile (clip.file.getFileNameWithoutExtension() + "-edited")
                           .withFileExtension ("wav");
        destFile.deleteFile();

        if (auto outStream = destFile.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (outStream.get(), sr, (unsigned int) channels, 24, {}, 0));
            if (writer != nullptr)
            {
                outStream.release();
                if (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
                {
                    writer.reset();
                    arrangementView.updateClipAfterBounce (i, destFile, regionLen);
                    ++rendered;
                    continue;
                }
            }
        }
        errors << laneName (i) << ": write failed. ";
    }

    if (rendered > 0)
        recordStatus.setText ("Bounced " + juce::String (rendered) + " edited lane" + (rendered == 1 ? "" : "s") + " — Reload to Decks uses the new versions",
                              juce::dontSendNotification);
    else
        recordStatus.setText (errors.isNotEmpty() ? errors : juce::String ("No edits to bounce — drag a clip edge or fade handle first"),
                              juce::dontSendNotification);
}

int MainPluginEditor::parseDeckIndexFromParamID (const juce::String& id)
{
    if (! id.startsWith ("deck") || id.length() < 5) return -1;
    const auto ch = id[4];
    if (ch >= 'A' && ch < 'A' + MainPluginProcessor::numDecks)
        return ch - 'A';
    return -1;
}

int MainPluginEditor::parseStemLaneFromParamID (const juce::String& id)
{
    const auto stemPos = id.indexOf ("Stem");
    if (stemPos < 0) return -1;
    const auto rest = id.substring (stemPos + 4);
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        if (rest.startsWith (DeckPlayer::stemLaneId (lane)))
            return lane;
    return -1;
}

void MainPluginEditor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (stemLinkMirroring) return;
    if (! stemLinkToggle.getToggleState()) return;
    const int srcDeck = parseDeckIndexFromParamID (parameterID);
    const int laneIdx = parseStemLaneFromParamID (parameterID);
    if (srcDeck < 0 || laneIdx < 0) return;
    const juce::ScopedValueSetter<bool> guard (stemLinkMirroring, true);
    auto& params = processorRef.parameters();
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        if (d == srcDeck) continue;
        const auto id = deckPrefix (d) + "Stem" + DeckPlayer::stemLaneId (laneIdx) + "Gain";
        if (auto* p = params.getParameter (id))
        {
            const auto range = params.getParameterRange (id);
            p->setValueNotifyingHost (range.convertTo0to1 (newValue));
        }
    }
}

void MainPluginEditor::syncAllDecksTo (int sourceDeckIndex)
{
    if (! juce::isPositiveAndBelow (sourceDeckIndex, MainPluginProcessor::numDecks))
        return;
    auto& params = processorRef.parameters();
    const auto src = deckPrefix (sourceDeckIndex);
    const std::array<const char*, 4> floatParams { "Bpm", "BeatOffset", "Downbeat", "KeyTarget" };
    for (int dst = 0; dst < MainPluginProcessor::numDecks; ++dst)
    {
        if (dst == sourceDeckIndex) continue;
        const auto dstPrefix = deckPrefix (dst);
        for (const auto* paramName : floatParams)
        {
            if (auto* srcParam = params.getParameter (src + paramName))
                if (auto* dstParam = params.getParameter (dstPrefix + paramName))
                    dstParam->setValueNotifyingHost (srcParam->getValue());
        }
    }
    recordStatus.setText ("Synced decks to Deck " + juce::String::charToString ((juce_wchar) ('A' + sourceDeckIndex))
                          + " (BPM + grid + key target)",
                          juce::dontSendNotification);
}

void MainPluginEditor::showSyncAllDecksMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Snap all other decks to:");
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto path = processorRef.getDeckPath (i);
        const auto label = "Deck " + juce::String::charToString ((juce_wchar) ('A' + i))
                         + (path.isNotEmpty() ? juce::String (" (") + juce::File (path).getFileName() + ")"
                                              : juce::String ("  (empty)"));
        menu.addItem (i + 1, label, path.isNotEmpty());
    }
    const juce::Component::SafePointer<MainPluginEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&syncAllDecksButton),
        [safeThis] (int choice)
        {
            if (safeThis != nullptr && choice > 0)
                safeThis->syncAllDecksTo (choice - 1);
        });
}

void MainPluginEditor::loadLastArrangementIntoView (juce::String statusLine)
{
    const auto masterRec = processorRef.getMasterRecordingTarget();
    std::array<juce::File, MainPluginProcessor::numDecks> decks;
    bool anyDeck = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        decks[(size_t) i] = processorRef.getLastDeckRecordingFile (i);
        if (decks[(size_t) i].existsAsFile() && decks[(size_t) i].getSize() > 1024)
            anyDeck = true;
    }

    if (masterRec.existsAsFile() || anyDeck)
    {
        arrangementView.setArrangement (masterRec, decks, std::move (statusLine));
        revealRecordingButton.setEnabled (true);
        reloadIntoDecksButton.setEnabled (anyDeck);
        bounceEditsButton.setEnabled (anyDeck);

        const auto eventLog = processorRef.getLastPerformanceEventLogFile();
        if (eventLog.existsAsFile())
        {
            std::array<ArrangementRecorderView::DeckAutomation, MainPluginProcessor::numDecks> automation;
            const auto json = juce::JSON::parse (eventLog);
            if (const auto* events = json.getProperty ("events", {}).getArray())
            {
                struct CurveSpec { const char* param; const char* label; juce::Colour colour; float lo, hi; };
                const std::array<CurveSpec, 3> specs {
                    CurveSpec { "volume", "VOL",    juce::Colour (0xff67f6d2), -60.0f,  6.0f },
                    CurveSpec { "pitch",  "PITCH",  juce::Colour (0xffff7a8d), -12.0f, 12.0f },
                    CurveSpec { "filter", "FILTER", juce::Colour (0xffffcf4a),  -1.0f,  1.0f }
                };

                for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
                {
                    const auto deckName = "Deck " + juce::String::charToString ((juce_wchar) ('A' + d));
                    std::array<ArrangementRecorderView::AutomationCurve, 3> curves;
                    for (size_t k = 0; k < specs.size(); ++k)
                    {
                        curves[k].label = specs[k].label;
                        curves[k].colour = specs[k].colour;
                        curves[k].minValue = specs[k].lo;
                        curves[k].maxValue = specs[k].hi;
                    }
                    for (const auto& v : *events)
                    {
                        if (v["deck"].toString() != deckName) continue;
                        const auto parameter = v["parameter"].toString();
                        const auto t = (double) v["timeSeconds"];
                        for (size_t k = 0; k < specs.size(); ++k)
                        {
                            if (parameter == specs[k].param)
                            {
                                const auto value = (float) v["value"].toString().getDoubleValue();
                                curves[k].points.emplace_back (t, value);
                                break;
                            }
                        }
                    }
                    for (auto& c : curves)
                        if (! c.points.empty())
                            automation[(size_t) d].push_back (std::move (c));
                }
            }
            arrangementView.setDeckAutomation (std::move (automation));
        }
    }
    else
    {
        arrangementView.setIdle ("Recording produced no files — check the destination folder.");
    }
}

void MainPluginEditor::refreshHostPluginChoices()
{
    knownHostPlugins = processorRef.getKnownHostPlugins();
    pluginSelector.clear (juce::dontSendNotification);
    for (int i = 0; i < knownHostPlugins.size(); ++i)
    {
        const auto& plugin = knownHostPlugins.getReference (i);
        pluginSelector.addItem (plugin.name + " [" + plugin.pluginFormatName + "]", i + 1);
    }
    if (knownHostPlugins.size() > 0)
        pluginSelector.setSelectedId (1, juce::dontSendNotification);
    else
        pluginSelector.setText ("No scanned FX", juce::dontSendNotification);
}

void MainPluginEditor::runPluginHostScan()
{
    pluginHostMessage.setText ("Scanning FX defaults…", juce::dontSendNotification);
    scanPluginsButton.setEnabled (false);

    juce::Thread::launch ([safeThis = juce::Component::SafePointer<MainPluginEditor> (this)]
    {
        if (safeThis == nullptr)
            return;

        auto result = safeThis->processorRef.scanPluginHostDefaultLocations (48, true);
        juce::MessageManager::callAsync ([safeThis, result]() mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->scanPluginsButton.setEnabled (true);
            safeThis->refreshHostPluginChoices();
            juce::String message = "Scanned " + result.scannedFormats.joinIntoString (", ")
                                 + " • known FX: " + juce::String (result.plugins.size());
            if (! result.blacklistedFiles.isEmpty())
                message += " • blacklisted: " + juce::String (result.blacklistedFiles.size());
            safeThis->pluginHostMessage.setText (message, juce::dontSendNotification);
        });
    });
}

void MainPluginEditor::loadSelectedHostPlugin()
{
    const int chainIndex = pluginChainSelector.getSelectedId() - 1;
    const int pluginIndex = pluginSelector.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (pluginIndex, knownHostPlugins.size()))
    {
        pluginHostMessage.setText ("Pick an FX plugin first", juce::dontSendNotification);
        return;
    }

    loadPluginButton.setEnabled (false);
    pluginHostMessage.setText ("Loading " + pluginSelector.getText() + " → " + pluginChainSelector.getText(), juce::dontSendNotification);
    processorRef.loadHostPluginToChainAsync (chainIndex, pluginIndex,
        [safeThis = juce::Component::SafePointer<MainPluginEditor> (this)] (bool success, juce::String message)
        {
            juce::MessageManager::callAsync ([safeThis, success, message]
            {
                if (safeThis == nullptr)
                    return;
                safeThis->loadPluginButton.setEnabled (true);
                safeThis->pluginHostMessage.setText ((success ? "Loaded: " : "FX load failed: ") + message, juce::dontSendNotification);
            });
        });
}

MainPluginEditor::~MainPluginEditor()
{
    stopTimer();
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto prefix = deckPrefix (d);
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            processorRef.parameters().removeParameterListener (
                prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain", this);
    }
    if (fileBrowser != nullptr)
        fileBrowser->removeListener (this);
    browserThread.stopThread (1500);
    setLookAndFeel (nullptr);
}

void MainPluginEditor::setupFileBrowser()
{
    browserThread.startThread (juce::Thread::Priority::background);

    setupButton (toggleBrowserButton);
    toggleBrowserButton.setClickingTogglesState (true);
    toggleBrowserButton.onClick = [this] { toggleFileBrowser(); };
    addAndMakeVisible (toggleBrowserButton);

    setupButton (browserUpButton);
    browserUpButton.onClick = [this] { browserNavigateUp(); };
    addChildComponent (browserUpButton);

    setupButton (browserHomeButton);
    browserHomeButton.onClick = [this] { browserNavigateTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory)); };
    addChildComponent (browserHomeButton);

    browserPathLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.78f));
    browserPathLabel.setFont (juce::FontOptions (11.0f));
    browserPathLabel.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (browserPathLabel);

    fileBrowser = std::make_unique<juce::FileTreeComponent> (browserContents);
    fileBrowser->addListener (this);
    fileBrowser->setColour (juce::TreeView::backgroundColourId, juce::Colour (0xff10141f));
    fileBrowser->setColour (juce::TreeView::linesColourId, juce::Colours::white.withAlpha (0.18f));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour (0xff0f5664));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::textColourId, juce::Colours::white.withAlpha (0.92f));
    addChildComponent (*fileBrowser);

    browserNavigateTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory));
}

void MainPluginEditor::toggleFileBrowser()
{
    browserVisible = toggleBrowserButton.getToggleState();
    if (fileBrowser != nullptr)
        fileBrowser->setVisible (browserVisible);
    browserUpButton.setVisible (browserVisible);
    browserHomeButton.setVisible (browserVisible);
    browserPathLabel.setVisible (browserVisible);
    resized();
}

void MainPluginEditor::browserNavigateTo (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    browserContents.setDirectory (folder, true, true);
    browserPathLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
}

void MainPluginEditor::browserNavigateUp()
{
    const auto current = browserContents.getDirectory();
    auto parent = current.getParentDirectory();
    if (parent == current)
        return;
    browserNavigateTo (parent);
}

void MainPluginEditor::fileDoubleClicked (const juce::File& file)
{
    if (file.isDirectory())
    {
        browserNavigateTo (file);
        return;
    }
    if (! file.existsAsFile())
        return;

    int target = -1;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (processorRef.getDeckPath (i).isEmpty()) { target = i; break; }

    if (target >= 0)
    {
        processorRef.loadDeckFileAsync (target, file);
        if (auto* wave = deckWaves[(size_t) target].get())
            wave->setFile (file);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("All decks are loaded — pick one to replace");
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto name = "Replace Deck " + juce::String::charToString ((juce_wchar) ('A' + i))
                        + " (" + juce::File (processorRef.getDeckPath (i)).getFileName() + ")";
        menu.addItem (i + 1, name);
    }
    menu.addSeparator();
    menu.addItem (99, "Cancel");
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [this, file] (int choice)
        {
            if (choice <= 0 || choice >= 99) return;
            const int idx = choice - 1;
            processorRef.loadDeckFileAsync (idx, file);
            if (auto* wave = deckWaves[(size_t) idx].get())
                wave->setFile (file);
        });
}

void MainPluginEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void MainPluginEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void MainPluginEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    g.setColour (juce::Colours::white.withAlpha (0.075f));
    g.drawHorizontalLine (title.getBounds().getBottom() + 7, 22.0f, (float) getWidth() - 22.0f);

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto deckBounds = deckWaves[(size_t) i] != nullptr ? deckWaves[(size_t) i]->getBounds().expanded (10, 12) : juce::Rectangle<int>{};
        deckBounds = deckBounds.getUnion (controls.pitch.getBounds().expanded (8, 12));
        if (! deckBounds.isEmpty())
        {
            drawPanel (g, deckBounds, deckAccent (i), 14.0f);
            g.setColour (deckAccent (i).withAlpha (0.72f));
            g.fillRoundedRectangle (deckBounds.removeFromTop (4).toFloat(), 2.0f);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.62f));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto drawControlLabel = [&g] (const juce::String& text, const juce::Component& component)
        {
            auto r = component.getBounds().withHeight (14).translated (0, -14);
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (deckAccent (i).withAlpha (0.92f));
        drawControlLabel ("GAIN", controls.volume);
        drawControlLabel ("BPM", controls.bpm);
        drawControlLabel ("GRID", controls.beatOffset);
        drawControlLabel ("DOWN", controls.downbeat);
        drawControlLabel ("FADER", controls.fader);
        drawControlLabel ("PITCH", controls.pitch);
        drawControlLabel ("LOW", controls.low);
        drawControlLabel ("MID", controls.mid);
        drawControlLabel ("HIGH", controls.high);
        drawControlLabel ("FILTER", controls.filter);
    }

    if (! crossfader.getBounds().isEmpty())
    {
        drawPanel (g, mixerPanel.getBounds().getUnion (recordStatus.getBounds()).expanded (10, 8), juce::Colour (0xff67f6d2), 14.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.68f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("Crossfader groups: A/C  ↔  B/D", crossfader.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Master BPM", masterBpm.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Master", master.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Limit", limiter.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
    g.drawText ("Mic gain", micGain.getBounds().withHeight (16).translated (0, -18), juce::Justification::centred);
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (48);
    toggleBrowserButton.setBounds (header.removeFromLeft (62).withSizeKeepingCentre (62, 30));
    header.removeFromLeft (8);
    subtitle.setBounds (header.removeFromLeft (360));
    title.setBounds (header.removeFromLeft (440));
    savePresetButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    userPresets.setBounds (header.removeFromRight (180));
    header.removeFromRight (12);
    tempoLabel.setBounds (header);

    if (browserVisible)
    {
        const int browserWidth = juce::jmin (300, area.getWidth() / 4);
        auto browserCol = area.removeFromLeft (browserWidth);
        area.removeFromLeft (10);
        auto browserHeader = browserCol.removeFromTop (28);
        browserHomeButton.setBounds (browserHeader.removeFromLeft (52));
        browserHeader.removeFromLeft (4);
        browserUpButton.setBounds (browserHeader.removeFromLeft (40));
        browserHeader.removeFromLeft (6);
        browserPathLabel.setBounds (browserHeader);
        browserCol.removeFromTop (4);
        if (fileBrowser != nullptr)
            fileBrowser->setBounds (browserCol);
    }

    standaloneWorkflowGuide.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);
    pluginHostStatus.setBounds (area.removeFromTop (16));
    area.removeFromTop (4);
    auto fxRow = area.removeFromTop (28);
    pluginChainSelector.setBounds (fxRow.removeFromLeft (148));
    fxRow.removeFromLeft (6);
    pluginSelector.setBounds (fxRow.removeFromLeft (300));
    fxRow.removeFromLeft (6);
    scanPluginsButton.setBounds (fxRow.removeFromLeft (76));
    fxRow.removeFromLeft (6);
    reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (100));
    fxRow.removeFromLeft (6);
    loadPluginButton.setBounds (fxRow.removeFromLeft (76));
    fxRow.removeFromLeft (6);
    clearChainButton.setBounds (fxRow.removeFromLeft (80));
    fxRow.removeFromLeft (8);
    pluginHostMessage.setBounds (fxRow);
    area.removeFromTop (8);

    auto keyRow = area.removeFromTop (28);
    masterKeyLabel.setBounds (keyRow.removeFromLeft (108));
    keyRow.removeFromLeft (4);
    masterKey.setBounds (keyRow.removeFromLeft (110));
    keyRow.removeFromLeft (10);
    syncAllDecksButton.setBounds (keyRow.removeFromLeft (140));
    area.removeFromTop (6);

    liveMixerLabel.setBounds (area.removeFromTop (22));
    auto topMixer = area.removeFromTop (570);
    topMixer = topMixer.reduced (0, 4);

    auto decksArea = topMixer.removeFromTop (446);
    const int deckGap = 10;
    const int deckWidth = (decksArea.getWidth() - deckGap * (MainPluginProcessor::numDecks - 1)) / MainPluginProcessor::numDecks;
    for (int deckIndex = 0; deckIndex < MainPluginProcessor::numDecks; ++deckIndex)
    {
        auto lane = decksArea.removeFromLeft (deckWidth);
        decksArea.removeFromLeft (deckGap);
        lane = lane.reduced (10, 12);

        auto& controls = deckControls[(size_t) deckIndex];
        if (auto* wave = deckWaves[(size_t) deckIndex].get())
            wave->setBounds (lane.removeFromTop (166));

        lane.removeFromTop (8);
        auto transport = lane.removeFromTop (30);
        controls.load.setBounds (transport.removeFromLeft (48));
        transport.removeFromLeft (4);
        controls.aiStems.setBounds (transport.removeFromLeft (68));
        transport.removeFromLeft (5);
        controls.play.setBounds (transport.removeFromLeft (64));
        transport.removeFromLeft (5);
        controls.cue.setBounds (transport.removeFromLeft (42));
        transport.removeFromLeft (5);
        controls.liveKey.setBounds (transport.removeFromLeft (64));

        lane.removeFromTop (8);
        auto stemRow = lane.removeFromTop (24);
        controls.stemLane.setBounds (stemRow.removeFromLeft (86));
        stemRow.removeFromLeft (5);
        controls.loadStem.setBounds (stemRow.removeFromLeft (52));
        stemRow.removeFromLeft (5);
        controls.keyTarget.setBounds (stemRow.removeFromLeft (76));
        stemRow.removeFromLeft (5);
        controls.keyLock.setBounds (stemRow);

        lane.removeFromTop (7);
        auto syncRow = lane.removeFromTop (24);
        controls.tempoInterpretation.setBounds (syncRow.removeFromLeft (112));
        syncRow.removeFromLeft (5);
        controls.syncMode.setBounds (syncRow.removeFromLeft (116));

        lane.removeFromTop (5);
        controls.keyInfo.setBounds (lane.removeFromTop (18));
        controls.stemStatus.setBounds (lane.removeFromTop (18));

        lane.removeFromTop (10);
        auto faders = lane.removeFromTop (92);
        const int faderW = 42;
        controls.volume.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        controls.fader.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        controls.pitch.setBounds (faders.removeFromLeft (faderW).reduced (4, 0));
        faders.removeFromLeft (5);
        controls.low.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.mid.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.high.setBounds (faders.removeFromLeft (44).reduced (1));
        controls.filter.setBounds (faders.removeFromLeft (44).reduced (1));

        lane.removeFromTop (6);
        auto gridRow = lane.removeFromTop (44);
        controls.bpm.setBounds (gridRow.removeFromLeft (96).reduced (2));
        gridRow.removeFromLeft (6);
        controls.beatOffset.setBounds (gridRow.removeFromLeft (88).reduced (2));
        gridRow.removeFromLeft (6);
        controls.downbeat.setBounds (gridRow.reduced (2));

        lane.removeFromTop (4);
        auto stemRow2 = lane.removeFromTop (44);
        const int stemKnobW = juce::jmax (28, stemRow2.getWidth() / DeckPlayer::numStemLanes);
        for (int lane2 = 0; lane2 < DeckPlayer::numStemLanes; ++lane2)
            controls.stemGains[(size_t) lane2].setBounds (stemRow2.removeFromLeft (stemKnobW).reduced (2));
    }

    auto mixer = topMixer.reduced (0, 5);
    mixerPanel.setBounds (mixer.removeFromLeft (150));
    auto mixControls = mixer.reduced (12, 12);
    crossfader.setBounds (mixControls.removeFromLeft (250));
    mixControls.removeFromLeft (18);
    masterBpm.setBounds (mixControls.removeFromLeft (116));
    master.setBounds (mixControls.removeFromLeft (84));
    limiter.setBounds (mixControls.removeFromLeft (84));
    hostSync.setBounds (mixControls.removeFromLeft (112).withTrimmedTop (24));
    beatSync.setBounds (mixControls.removeFromLeft (112).withTrimmedTop (24));
    micMonitor.setBounds (mixControls.removeFromLeft (120).withTrimmedTop (24));
    micGain.setBounds (mixControls.removeFromLeft (90));
    recordButton.setBounds (mixControls.removeFromLeft (96).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    revealRecordingButton.setBounds (mixControls.removeFromLeft (60).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    reloadIntoDecksButton.setBounds (mixControls.removeFromLeft (110).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    bounceEditsButton.setBounds (mixControls.removeFromLeft (94).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    snapToggleButton.setBounds (mixControls.removeFromLeft (52).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    undoButton.setBounds (mixControls.removeFromLeft (52).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (3);
    redoButton.setBounds (mixControls.removeFromLeft (52).withTrimmedTop (20).withHeight (32));
    mixControls.removeFromLeft (5);
    recordStatus.setBounds (mixControls.withTrimmedTop (20).withHeight (32));

    area.removeFromTop (8);
    arrangementLabel.setBounds (area.removeFromTop (22));
    auto bottom = area;
    auto samplerStrip = bottom.removeFromRight (270);
    bottom.removeFromRight (14);
    arrangementView.setBounds (bottom);

    samplerLabel.setBounds (samplerStrip.removeFromTop (22));
    const int padH = juce::jmax (58, samplerStrip.getHeight() / 8);
    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto cell = samplerStrip.removeFromTop (padH).reduced (4);
        pads[(size_t) i].trigger.setBounds (cell.removeFromLeft (92));
        cell.removeFromLeft (5);
        pads[(size_t) i].load.setBounds (cell.removeFromLeft (48));
        cell.removeFromLeft (5);
        pads[(size_t) i].gain.setBounds (cell);
    }
}

void MainPluginEditor::timerCallback()
{
    processorRef.refreshLiveKeyAnalysis();
    auto snapshot = processorRef.getTransportSnapshot();
    tempoLabel.setText (juce::String (snapshot.bpm, 1) + " BPM • " + (snapshot.hostPlaying ? "host playing" : "host stopped"), juce::dontSendNotification);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    const bool recording = processorRef.isMasterRecording();
    recordButton.setButtonText (recording ? "Stop Rec" : "Record WAV");

    if (snapToggleButton.getToggleState())
    {
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        arrangementView.setSnap (true, 0.25, bpm);
    }
    undoButton.setEnabled (arrangementView.canUndo());
    redoButton.setEnabled (arrangementView.canRedo());
    if (recording)
    {
        const auto samples = processorRef.getRecordedSamples();
        const auto sr = processorRef.getRecordingSampleRate();
        const double seconds = sr > 0.0 ? (double) samples / sr : 0.0;
        arrangementView.setRecordingProgress (seconds, processorRef.getMasterRecordingTarget().getFullPathName());
    }
    else if (! arrangementView.hasArrangement())
    {
        arrangementView.setIdle ("Hit Record to capture your set across four deck lanes.");
    }

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        deckControls[(size_t) i].keyInfo.setText (processorRef.getDeckKeyInfo (i) + " • " + processorRef.getDeckTimePitchStatus (i) + " • " + processorRef.getDeckBeatSyncStatus (i),
                                                   juce::dontSendNotification);
        deckControls[(size_t) i].stemStatus.setText (processorRef.getDeckStemProviderStatus (i), juce::dontSendNotification);

        const auto path = processorRef.getDeckPath (i);
        if (path != deckPaths[(size_t) i])
        {
            deckPaths[(size_t) i] = path;
            if (path.isNotEmpty())
                if (auto* wave = deckWaves[(size_t) i].get())
                    wave->setFile (juce::File (path));
        }
    }
}
