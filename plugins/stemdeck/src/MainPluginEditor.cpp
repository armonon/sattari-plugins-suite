#include "MainPluginEditor.h"

namespace
{
juce::Colour deckAccent (int deckIndex)
{
    // Pulled from the suite brand registry so deck accents read as one family.
    static const std::array<juce::uint32, 4> colours {
        sattari::brand::productAccent ("stemdeck"),     // cyan
        sattari::brand::kAccent,                         // mint
        sattari::brand::kAccentViolet,                   // violet
        sattari::brand::productAccent ("royal-chain"),   // gold
    };
    return juce::Colour (colours[(size_t) juce::jlimit (0, 3, deckIndex)]);
}

// This build decodes bare `char*` string literals as Latin-1, so any literal with
// non-ASCII bytes must be wrapped to be interpreted as UTF-8 or it renders as
// mojibake ("—" → "â€"). u8() forces correct UTF-8 decoding.
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent,
                float corner = 10.0f, bool active = false)
{
    using namespace sattari::brand;
    const auto r = bounds.toFloat();

    // Flat solid card — no gradient, no shadow.
    g.setColour (juce::Colour (kPanel));
    g.fillRoundedRectangle (r, corner);

    // Hairline frame; the accent border firms up when the deck is active.
    g.setColour (juce::Colour (kHairline).withAlpha (0.9f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (accent.withAlpha (active ? 0.85f : 0.35f));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, active ? 1.5f : 1.0f);
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
    if (mode == Mode::Idle && hint == status)
        return;
    mode = Mode::Idle;
    status = std::move (hint);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setRecordingProgress (double seconds, juce::String path)
{
    const int newSecond = (int) seconds;
    const bool changed = mode != Mode::Recording
                      || newSecond != (int) elapsedSeconds
                      || path != recordingPath;
    mode = Mode::Recording;
    elapsedSeconds = seconds;
    recordingPath = std::move (path);
    if (changed)
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
        clips[(size_t) i].assign (1, LaneClip { decks[(size_t) i], 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false });
        if (deckThumbs[(size_t) i] != nullptr)
            loadThumbnail (*deckThumbs[(size_t) i], deckFiles[(size_t) i]);
    }
    clips[(size_t) masterLane].assign (1, LaneClip { masterAudio, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false });
    if (masterThumb != nullptr)
        loadThumbnail (*masterThumb, masterFile);
    selectedLane = -1;
    selectedClip = -1;
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
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes))
        return;
    // Bounce flattens the lane's clips into one rendered file → collapse to a
    // single clip spanning the whole new file.
    clips[(size_t) laneIndex].assign (1, LaneClip { newFile, newDuration, 0.0, newDuration, 0.0, 0.0, 0.0, false });
    if (laneIndex == masterLane)
    {
        masterFile = newFile;
        if (masterThumb != nullptr)
            loadThumbnail (*masterThumb, newFile);
    }
    else
    {
        deckFiles[(size_t) laneIndex] = newFile;
        if (deckThumbs[(size_t) laneIndex] != nullptr)
            loadThumbnail (*deckThumbs[(size_t) laneIndex], newFile);
    }
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

        h.lane = i;
        const auto& laneClips = clips[(size_t) i];
        if (laneClips.empty())
        {
            h.handle = Handle::Body;
            return h;
        }

        const int labelWidth = 90;
        auto track = laneRect.withTrimmedLeft (labelWidth).reduced (4, 3);
        auto timeToX = [&] (double t) -> int
        {
            return track.getX() + (int) std::round ((double) track.getWidth() * t / maxLen);
        };
        const int edgeBand = 8;
        const int cornerH = juce::jmax (16, juce::jmin (28, track.getHeight() * 2 / 3));

        for (int c = 0; c < (int) laneClips.size(); ++c)
        {
            const auto& clip = laneClips[(size_t) c];
            if (clip.durationSeconds <= 0.0)
            {
                h.clip = c;
                h.handle = Handle::Body;
                return h;
            }
            const int clipX0 = timeToX (clip.timelineStartSeconds);
            const int clipX1 = timeToX (clip.timelineEndSeconds());
            if (p.x < clipX0 - 6 || p.x > clipX1 + 6)
                continue;

            h.clip = c;
            if (std::abs (p.x - clipX0) <= edgeBand)
                h.handle = (p.y - track.getY() <= cornerH) ? Handle::FadeIn : Handle::TrimStart;
            else if (std::abs (p.x - clipX1) <= edgeBand)
                h.handle = (p.y - track.getY() <= cornerH) ? Handle::FadeOut : Handle::TrimEnd;
            else
                h.handle = Handle::Body;
            return h;
        }

        // Inside the lane but not over a clip.
        h.handle = Handle::None;
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

double MainPluginEditor::ArrangementRecorderView::timeAtX (int laneIndex, int x) const
{
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen <= 0.0 || ! juce::isPositiveAndBelow (laneIndex, numEditableLanes))
        return 0.0;
    const int labelWidth = 90;
    const auto track = laneRects[(size_t) laneIndex].withTrimmedLeft (labelWidth).reduced (4, 3);
    if (track.getWidth() <= 0) return 0.0;
    const double frac = juce::jlimit (0.0, 1.0, (double) (x - track.getX()) / (double) track.getWidth());
    return frac * maxLen;
}

void MainPluginEditor::ArrangementRecorderView::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = hitTestAt (e.getPosition());

    if (e.mods.isPopupMenu())
    {
        if (hit.lane >= 0)
        {
            if (selectedLane != hit.lane || selectedClip != hit.clip)
            {
                selectedLane = hit.lane;
                selectedClip = hit.clip;
                repaint();
            }
            const int lane = hit.lane;
            const int clipIdx = hit.clip;
            lastContextPos = e.getPosition();
            const bool overClip = clipIdx >= 0 && clipIdx < (int) clips[(size_t) lane].size();
            const bool hasClip = overClip && clips[(size_t) lane][(size_t) clipIdx].durationSeconds > 0.0;
            const juce::File sourceFile = overClip ? clips[(size_t) lane][(size_t) clipIdx].file : juce::File();

            juce::PopupMenu menu;
            menu.addSectionHeader (lane == masterLane
                                      ? juce::String ("Master lane")
                                      : "Deck " + juce::String::charToString ((juce_wchar) ('A' + lane)));
            menu.addItem (6, "Split clip here", hasClip);
            menu.addItem (7, "Delete this clip", overClip && clips[(size_t) lane].size() > 1);
            menu.addSeparator();
            menu.addItem (1, "Reset edits on this lane", ! clips[(size_t) lane].empty());
            menu.addItem (2, "Reveal source in Finder", sourceFile.existsAsFile());
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
                menu.addSubMenu (u8 ("Copy lane to\xe2\x80\xa6"), copyTo);
            }
            menu.addSeparator();
            menu.addItem (4, "Undo", canUndo());
            menu.addItem (5, "Redo", canRedo());

            const juce::Component::SafePointer<ArrangementRecorderView> safeThis (this);
            const double splitTime = timeAtX (lane, e.getPosition().x);
            menu.showMenuAsync (juce::PopupMenu::Options(),
                [safeThis, lane, clipIdx, splitTime, sourceFile] (int choice)
                {
                    if (safeThis == nullptr) return;
                    auto& self = *safeThis;
                    switch (choice)
                    {
                        case 6: self.splitClipAt (lane, clipIdx, splitTime); break;
                        case 7: self.deleteClip (lane, clipIdx); break;
                        case 1: self.resetClipEdits (lane); break;
                        case 2: if (sourceFile.existsAsFile()) sourceFile.revealToUser(); break;
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

    // Razor mode: a plain click splits the clip under the cursor.
    if (razorMode && hit.lane >= 0 && hit.clip >= 0)
    {
        splitClipAt (hit.lane, hit.clip, timeAtX (hit.lane, e.getPosition().x));
        activeDrag = {};
        return;
    }

    activeDrag = hit;
    if (activeDrag.lane < 0)
    {
        if (selectedLane != -1 || selectedClip != -1) { selectedLane = -1; selectedClip = -1; repaint(); }
        return;
    }
    if (selectedLane != activeDrag.lane || selectedClip != activeDrag.clip)
    {
        selectedLane = activeDrag.lane;
        selectedClip = activeDrag.clip;
        repaint();
    }

    if (activeDrag.clip < 0 || activeDrag.clip >= (int) clips[(size_t) activeDrag.lane].size())
    {
        pendingEditSnapshot = {};
        return;
    }
    const auto& clip = clips[(size_t) activeDrag.lane][(size_t) activeDrag.clip];
    switch (activeDrag.handle)
    {
        case Handle::TrimStart: dragStartValue = clip.trimStartSeconds;     break;
        case Handle::TrimEnd:   dragStartValue = clip.trimEndSeconds;       break;
        case Handle::FadeIn:    dragStartValue = clip.fadeInSeconds;        break;
        case Handle::FadeOut:   dragStartValue = clip.fadeOutSeconds;       break;
        case Handle::Body:      dragStartValue = clip.timelineStartSeconds; break;
        default: dragStartValue = 0.0;
    }
    dragStartPoint = e.getPosition();
    // Every clip-targeted handle (including Body-move) snapshots for undo.
    pendingEditSnapshot = captureLaneState (activeDrag.lane);
}

void MainPluginEditor::ArrangementRecorderView::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDrag.lane < 0 || activeDrag.handle == Handle::None)
        return;
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen <= 0.0)
        return;
    const int laneIdx = activeDrag.lane;
    if (activeDrag.clip < 0 || activeDrag.clip >= (int) clips[(size_t) laneIdx].size())
        return;
    const auto laneRect = laneRects[(size_t) laneIdx];
    const int labelWidth = 90;
    const int trackWidth = juce::jmax (1, laneRect.getWidth() - labelWidth - 8);
    const double secondsPerPixel = maxLen / (double) trackWidth;
    const double delta = (double) (e.getPosition().x - dragStartPoint.x) * secondsPerPixel;

    auto& clip = clips[(size_t) laneIdx][(size_t) activeDrag.clip];
    const double total = clip.durationSeconds;
    const double minClipWidth = 0.05;
    double readoutValue = 0.0;
    juce::String readoutLabel;
    switch (activeDrag.handle)
    {
        case Handle::TrimStart:
        {
            const double oldStart = clip.trimStartSeconds;
            clip.trimStartSeconds = juce::jlimit (0.0, juce::jmax (0.0, clip.trimEndSeconds - minClipWidth), snapTime (dragStartValue + delta));
            // Keep the clip's right edge fixed on the timeline as the in-point moves.
            clip.timelineStartSeconds = juce::jmax (0.0, clip.timelineStartSeconds + (clip.trimStartSeconds - oldStart));
            if (clip.fadeInSeconds > clip.trimEndSeconds - clip.trimStartSeconds)
                clip.fadeInSeconds = juce::jmax (0.0, clip.trimEndSeconds - clip.trimStartSeconds);
            readoutValue = clip.trimStartSeconds;
            readoutLabel = "Start";
            break;
        }
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
        case Handle::Body:
            clip.timelineStartSeconds = juce::jmax (0.0, snapTime (dragStartValue + delta));
            readoutValue = clip.timelineStartSeconds;
            readoutLabel = "Move";
            break;
        default: break;
    }
    const double beats = snapBpm > 0.0 ? readoutValue * snapBpm / 60.0 : 0.0;
    dragReadout = readoutLabel + ": " + juce::String (readoutValue, 3) + " s  (" + juce::String (beats, 2) + " beats)";
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::mouseUp (const juce::MouseEvent& e)
{
    if (pendingEditSnapshot.lane >= 0 && activeDrag.clip >= 0)
    {
        const auto& beforeVec = pendingEditSnapshot.state;
        const auto& afterVec  = clips[(size_t) pendingEditSnapshot.lane];
        bool changed = false;
        if (activeDrag.clip < (int) beforeVec.size() && activeDrag.clip < (int) afterVec.size())
        {
            const auto& before = beforeVec[(size_t) activeDrag.clip];
            const auto& after  = afterVec[(size_t) activeDrag.clip];
            changed = before.trimStartSeconds     != after.trimStartSeconds
                   || before.trimEndSeconds       != after.trimEndSeconds
                   || before.timelineStartSeconds != after.timelineStartSeconds
                   || before.fadeInSeconds        != after.fadeInSeconds
                   || before.fadeOutSeconds       != after.fadeOutSeconds;
        }
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
        if (len <= 0.0) return;
        // Only the freshly-loaded single clip (duration still unknown) gets sized;
        // once the user has split/edited, clip durations are already established.
        for (auto& clip : clips[(size_t) laneIndex])
        {
            if (clip.durationSeconds <= 0.0)
            {
                clip.durationSeconds = len;
                if (clip.trimEndSeconds <= 0.0)
                    clip.trimEndSeconds = len;
            }
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

void MainPluginEditor::ArrangementRecorderView::setPlaybackPosition (double seconds, bool active)
{
    const bool changed = active != playbackActive || std::abs (seconds - playbackPositionSeconds) > 0.02;
    playbackPositionSeconds = seconds;
    playbackActive = active;
    if (changed)
        repaint();
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
    selectedClip = -1;
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
    selectedClip = -1;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setRazorMode (bool shouldSplit)
{
    razorMode = shouldSplit;
    setMouseCursor (razorMode ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
}

void MainPluginEditor::ArrangementRecorderView::splitClipAt (int laneIndex, int clipIndex, double timeSeconds)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& laneClips = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) laneClips.size())) return;

    auto& clip = laneClips[(size_t) clipIndex];
    // timeSeconds is a timeline position; map it to a source in-point.
    const double tlStart = clip.timelineStartSeconds;
    const double tlEnd   = clip.timelineEndSeconds();
    const double cutTl   = snapTime (timeSeconds);
    const double minWidth = 0.05;
    if (cutTl <= tlStart + minWidth || cutTl >= tlEnd - minWidth)
        return;   // too close to an edge to split meaningfully

    const double sourceCut = clip.trimStartSeconds + (cutTl - tlStart);

    auto before = captureLaneState (laneIndex);

    // Left half keeps the fade-in; right half keeps the fade-out + its timeline slot.
    LaneClip right = clip;
    clip.trimEndSeconds      = sourceCut;
    clip.fadeOutSeconds      = 0.0;
    right.trimStartSeconds    = sourceCut;
    right.timelineStartSeconds = cutTl;
    right.fadeInSeconds       = 0.0;
    right.selected = false;
    clip.selected  = false;

    laneClips.insert (laneClips.begin() + clipIndex + 1, right);
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::deleteClip (int laneIndex, int clipIndex)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& laneClips = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) laneClips.size())) return;
    if (laneClips.size() <= 1) return;   // keep at least one clip per lane

    auto before = captureLaneState (laneIndex);
    laneClips.erase (laneClips.begin() + clipIndex);
    if (selectedLane == laneIndex)
        selectedClip = -1;
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::deleteSelectedClip()
{
    if (selectedLane >= 0 && selectedClip >= 0)
        deleteClip (selectedLane, selectedClip);
}

void MainPluginEditor::ArrangementRecorderView::resetClipEdits (int laneIndex)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& laneClips = clips[(size_t) laneIndex];
    if (laneClips.empty()) return;
    const double dur = laneClips.front().durationSeconds;
    const juce::File file = laneClips.front().file;
    if (dur <= 0.0) return;

    auto before = captureLaneState (laneIndex);
    // Collapse splits and edits back to a single full-length clip.
    laneClips.assign (1, LaneClip { file, dur, 0.0, dur, 0.0, 0.0, 0.0, false });
    selectedClip = -1;
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
    auto& src = clips[(size_t) sourceLane];
    if (src.empty() || src.front().file == juce::File() || src.front().durationSeconds <= 0.0) return false;

    auto before = captureLaneState (destLane);
    clips[(size_t) destLane] = src;                 // copy the whole lane (all clips)
    for (auto& c : clips[(size_t) destLane])
        c.selected = false;
    if (destLane != masterLane)
        deckFiles[(size_t) destLane] = src.front().file;
    if (auto* thumb = thumbForLane (destLane))
        loadThumbnail (*thumb, src.front().file);
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
    // Include moved clips so a clip dragged past the source length stays on-screen.
    for (const auto& laneClips : clips)
        for (const auto& clip : laneClips)
            maxLen = juce::jmax (maxLen, clip.timelineEndSeconds());
    return maxLen;
}

void MainPluginEditor::ArrangementRecorderView::paint (juce::Graphics& g)
{
    using namespace sattari::brand;
    const auto bounds = getLocalBounds().toFloat();

    // FL-style flat blue-grey panel — no gradient, brand palette.
    const auto panelBase = juce::Colour (kPanelRaised).interpolatedWith (juce::Colour (kAccentViolet), 0.04f);
    g.setColour (panelBase);
    g.fillRoundedRectangle (bounds, 12.0f);

    juce::Colour accent;
    juce::String modeLabel;
    switch (mode)
    {
        case Mode::Recording: accent = juce::Colour (kDanger);     modeLabel = "RECORDING";  break;
        case Mode::Finished:  accent = juce::Colour (kAccent);     modeLabel = "ARRANGEMENT"; break;
        case Mode::Idle:      accent = juce::Colour (kTextMuted);  modeLabel = "READY";       break;
    }
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 12.0f, 1.0f);

    auto area = getLocalBounds().reduced (16, 12);

    // Title row.
    auto header = area.removeFromTop (22);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (juce::Colour (kTextPrimary));
    g.drawText ("Arrangement", header.removeFromLeft (200), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (accent.withAlpha (0.95f));
    if (mode == Mode::Recording)
    {
        const int total = (int) std::floor (elapsedSeconds);
        const auto blink = ((juce::Time::currentTimeMillis() / 500) % 2) == 0;
        const juce::String dot = blink ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String ("  ");
        g.drawText (dot + modeLabel + "  " + juce::String::formatted ("%02d:%02d", total / 60, total % 60),
                    header, juce::Justification::centredRight);
    }
    else
    {
        g.drawText (modeLabel, header, juce::Justification::centredRight);
    }
    area.removeFromTop (8);

    const double maxLen = computeMaxLengthSeconds();
    const bool hasAudio = mode == Mode::Finished && maxLen > 0.0;

    const int labelWidth = 90;       // must match hitTestAt()
    const int rulerHeight = 18;
    const int statusHeight = 16;

    auto statusArea = area.removeFromBottom (statusHeight);
    area.removeFromBottom (4);

    // Bar/beat math driven by snap BPM (default 120). Empty arrangement shows 8 empty bars.
    const double bpm = juce::jlimit (40.0, 240.0, snapBpm > 0.0 ? snapBpm : 120.0);
    const double secsPerBeat = 60.0 / bpm;
    const double secsPerBar  = 4.0 * secsPerBeat;
    const double rulerLen    = hasAudio ? maxLen : (8.0 * secsPerBar);

    auto ruler = area.removeFromTop (rulerHeight);
    auto rulerLabel = ruler.removeFromLeft (labelWidth);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.setColour (juce::Colour (kTextMuted));
    g.drawText ("BARS", rulerLabel.reduced (8, 0), juce::Justification::centredLeft);

    auto rulerTimeToX = [&] (double t) -> int
    {
        return ruler.getX() + (int) std::round ((double) ruler.getWidth() * t / rulerLen);
    };

    const int totalBars = juce::jmax (1, (int) std::ceil (rulerLen / secsPerBar));
    int labelStep = 1;
    if      (totalBars > 96) labelStep = 16;
    else if (totalBars > 48) labelStep = 8;
    else if (totalBars > 24) labelStep = 4;
    else if (totalBars > 12) labelStep = 2;

    g.setColour (juce::Colour (kHairline));
    g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    for (int bar = 0; bar <= totalBars; ++bar)
    {
        const double t = (double) bar * secsPerBar;
        if (t > rulerLen) break;
        const int x = rulerTimeToX (t);
        const bool labeled = (bar % labelStep) == 0;
        g.setColour (juce::Colour (kTextFaint).withAlpha (labeled ? 0.85f : 0.45f));
        g.drawVerticalLine (x, (float) ruler.getY() + (labeled ? 3.0f : 8.0f), (float) ruler.getBottom());
        if (labeled)
        {
            g.setColour (juce::Colour (kTextMuted));
            g.drawText (juce::String (bar + 1), x + 3, ruler.getY() - 1, 30, rulerHeight, juce::Justification::centredLeft);
        }
    }

    // Lanes area.
    const int lanesTotal = numEditableLanes;
    const int laneGap = 2;
    const int laneAreaHeight = area.getHeight();
    const int laneHeight = juce::jmax (16, (laneAreaHeight - laneGap * (lanesTotal - 1)) / lanesTotal);

    // Vertical grid lines extending through the entire stack of lanes (drawn before clips so clips overlay).
    const int gridLeft  = area.getX() + labelWidth + 4;   // matches clip rect math below
    const int gridRight = area.getRight() - 4;
    const int gridTop   = area.getY();
    const int gridBottom = area.getY() + lanesTotal * laneHeight + (lanesTotal - 1) * laneGap;

    auto gridTimeToX = [&] (double t) -> int
    {
        return gridLeft + (int) std::round ((double) (gridRight - gridLeft) * t / rulerLen);
    };

    g.setColour (juce::Colour (kPanelDeep).withAlpha (0.65f));
    g.fillRect (gridLeft, gridTop, gridRight - gridLeft, gridBottom - gridTop);

    for (int bar = 0; bar <= totalBars; ++bar)
    {
        const double t = (double) bar * secsPerBar;
        if (t > rulerLen) break;
        const int x = gridTimeToX (t);
        g.setColour (juce::Colour (kHairline).withAlpha (0.9f));
        g.drawVerticalLine (x, (float) gridTop, (float) gridBottom);
        for (int beat = 1; beat < 4; ++beat)
        {
            const double bt = t + beat * secsPerBeat;
            if (bt > rulerLen) break;
            const int bx = gridTimeToX (bt);
            g.setColour (juce::Colour (kHairline).withAlpha (0.35f));
            g.drawVerticalLine (bx, (float) gridTop, (float) gridBottom);
        }
    }

    auto drawLane = [&] (juce::Rectangle<int> lane, const juce::String& title, juce::Colour colour,
                         juce::AudioThumbnail* thumb, const juce::File& file, int deckIndex)
    {
        if (deckIndex >= 0 && deckIndex < numEditableLanes)
            laneRects[(size_t) deckIndex] = lane;

        const bool isSelected = (deckIndex >= 0 && deckIndex == selectedLane);

        // Track header column (left).
        auto headerCell = lane.removeFromLeft (labelWidth);
        g.setColour (juce::Colour (kPanelRaised));
        g.fillRect (headerCell);
        g.setColour (colour);
        g.fillRect (headerCell.removeFromLeft (3));
        g.setColour (juce::Colour (kTextPrimary).withAlpha (0.92f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title, headerCell.reduced (8, 0), juce::Justification::centredLeft);

        // Track timeline area — must match hitTestAt: lane.withTrimmedLeft (labelWidth).reduced (4, 3)
        auto track = lane.reduced (4, 3);

        // Subtle row banding inside the timeline.
        g.setColour ((deckIndex % 2 == 0 ? juce::Colour (kPanel) : juce::Colour (kPanelDeep)).withAlpha (0.35f));
        g.fillRect (track);

        if (mode == Mode::Finished && thumb != nullptr && thumb->getTotalLength() > 0.0 && maxLen > 0.0
            && deckIndex >= 0 && deckIndex < numEditableLanes)
        {
            const double laneLen = thumb->getTotalLength();
            auto timeToX = [&] (double t) -> int
            {
                return track.getX() + (int) std::round ((double) track.getWidth() * t / maxLen);
            };

            const auto& laneClips = clips[(size_t) deckIndex];
            juce::Rectangle<int> firstBody;   // for automation overlay

            for (int ci = 0; ci < (int) laneClips.size(); ++ci)
            {
                const auto& clip = laneClips[(size_t) ci];
                if (clip.durationSeconds <= 0.0) continue;

                const double clipStart = juce::jlimit (0.0, laneLen, clip.trimStartSeconds);                          // source in-point
                const double clipEnd   = juce::jlimit (clipStart, laneLen, clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : laneLen);  // source out-point
                const double fadeIn    = juce::jlimit (0.0, clipEnd - clipStart, clip.fadeInSeconds);
                const double fadeOut   = juce::jlimit (0.0, clipEnd - clipStart - fadeIn, clip.fadeOutSeconds);
                const bool clipSelected = isSelected && ci == selectedClip;

                // Rect comes from the timeline position; the waveform is drawn from the source region.
                const double tlStart = clip.timelineStartSeconds;
                const double tlEnd   = tlStart + (clipEnd - clipStart);
                const int clipX0 = timeToX (tlStart);
                const int clipX1 = juce::jmax (clipX0 + 6, timeToX (tlEnd));
                auto clipRect = juce::Rectangle<int> (clipX0, track.getY(), clipX1 - clipX0, track.getHeight());

                const int headerBand = juce::jlimit (10, 14, clipRect.getHeight() / 3);
                auto patternHeader = clipRect.withHeight (headerBand);
                auto patternBody   = clipRect.withTrimmedTop (headerBand);
                if (ci == 0) firstBody = patternBody;

                g.setColour (colour.withAlpha (clipSelected ? 0.28f : 0.18f));
                g.fillRect (patternBody);
                g.setColour (colour.withAlpha (clipSelected ? 0.95f : 0.78f));
                g.fillRect (patternHeader);
                g.setColour (colour.withAlpha (clipSelected ? 1.0f : 0.65f));
                g.drawRect (clipRect, clipSelected ? 2 : 1);

                // Chevron + source filename (only label the first clip to avoid clutter).
                if (ci == 0)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.78f));
                    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                    const auto chevron = juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe "));
                    g.drawText (chevron + file.getFileName(), patternHeader.reduced (5, 0),
                                juce::Justification::centredLeft, true);
                }

                if (patternBody.getHeight() > 4)
                {
                    g.setColour (colour.withAlpha (0.92f));
                    thumb->drawChannels (g, patternBody.reduced (2, 1), clipStart, clipEnd, 0.95f);
                }

                if (fadeIn > 0.0)
                {
                    const int fadeInX = timeToX (tlStart + fadeIn);
                    juce::Path tri;
                    tri.startNewSubPath ((float) clipRect.getX(), (float) clipRect.getY());
                    tri.lineTo ((float) fadeInX, (float) clipRect.getY());
                    tri.lineTo ((float) clipRect.getX(), (float) clipRect.getBottom());
                    tri.closeSubPath();
                    g.setColour (juce::Colour (kPanelDeep).withAlpha (0.75f));
                    g.fillPath (tri);
                    g.setColour (colour.withAlpha (0.95f));
                    g.drawLine ((float) clipRect.getX(), (float) clipRect.getBottom(),
                                (float) fadeInX, (float) clipRect.getY(), 1.2f);
                }
                if (fadeOut > 0.0)
                {
                    const int fadeOutX = timeToX (tlEnd - fadeOut);
                    juce::Path tri;
                    tri.startNewSubPath ((float) fadeOutX, (float) clipRect.getY());
                    tri.lineTo ((float) clipRect.getRight(), (float) clipRect.getY());
                    tri.lineTo ((float) clipRect.getRight(), (float) clipRect.getBottom());
                    tri.closeSubPath();
                    g.setColour (juce::Colour (kPanelDeep).withAlpha (0.75f));
                    g.fillPath (tri);
                    g.setColour (colour.withAlpha (0.95f));
                    g.drawLine ((float) fadeOutX, (float) clipRect.getY(),
                                (float) clipRect.getRight(), (float) clipRect.getBottom(), 1.2f);
                }

                const float handleW = 3.0f;
                g.setColour (juce::Colours::white.withAlpha (clipSelected ? 0.85f : 0.45f));
                g.fillRect (juce::Rectangle<float> ((float) clipRect.getX(),
                                                    (float) clipRect.getY(), handleW, (float) clipRect.getHeight()));
                g.fillRect (juce::Rectangle<float> ((float) clipRect.getRight() - handleW,
                                                    (float) clipRect.getY(), handleW, (float) clipRect.getHeight()));

                const float grabSize = 6.0f;
                g.setColour (colour.brighter (0.3f).withAlpha (clipSelected ? 0.95f : 0.6f));
                if (fadeIn <= 0.0)
                {
                    juce::Path g0;
                    g0.addTriangle ((float) clipRect.getX(),            (float) clipRect.getY(),
                                    (float) clipRect.getX() + grabSize, (float) clipRect.getY(),
                                    (float) clipRect.getX(),            (float) clipRect.getY() + grabSize);
                    g.fillPath (g0);
                }
                if (fadeOut <= 0.0)
                {
                    juce::Path g1;
                    g1.addTriangle ((float) clipRect.getRight() - grabSize, (float) clipRect.getY(),
                                    (float) clipRect.getRight(),            (float) clipRect.getY(),
                                    (float) clipRect.getRight(),            (float) clipRect.getY() + grabSize);
                    g.fillPath (g1);
                }
            }

            // Automation curves span the whole lane (drawn once, over the track).
            if (deckIndex >= 0 && deckIndex < numLanes && ! firstBody.isEmpty())
                for (const auto& curve : deckAutomation[(size_t) deckIndex])
                    drawCurve (g, track.withTrimmedTop (firstBody.getY() - track.getY()).reduced (2), curve, maxLen);
        }
        else if (mode == Mode::Recording)
        {
            g.setColour (colour.withAlpha (0.22f));
            const auto rect = track.toFloat().withHeight ((float) track.getHeight() * 0.55f)
                                              .withY ((float) track.getCentreY() - (float) track.getHeight() * 0.275f);
            g.fillRect (rect);
            g.setColour (colour.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (u8 ("capturing\xe2\x80\xa6"), track.reduced (8, 0), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (juce::Colour (kTextFaint).withAlpha (0.65f));
            g.setFont (juce::FontOptions (9.5f));
            g.drawText ("empty", track.reduced (8, 0), juce::Justification::centredLeft);
        }
    };

    auto masterLaneRect = area.removeFromTop (laneHeight);
    drawLane (masterLaneRect, "MASTER", juce::Colour (kAccent), masterThumb.get(), masterFile, masterLane);
    area.removeFromTop (laneGap);

    for (int i = 0; i < numLanes; ++i)
    {
        auto laneRect = area.removeFromTop (laneHeight);
        drawLane (laneRect, "DECK " + juce::String::charToString ((juce_wchar) ('A' + i)),
                  deckAccent (i), deckThumbs[(size_t) i].get(), deckFiles[(size_t) i], i);
        if (i < numLanes - 1)
            area.removeFromTop (laneGap);
    }

    // Arrangement playback head — sweeps across all lanes during playback.
    if (playbackActive && playbackPositionSeconds >= 0.0 && rulerLen > 0.0)
    {
        const int px = gridTimeToX (playbackPositionSeconds);
        if (px >= gridLeft && px <= gridRight)
        {
            g.setColour (juce::Colour (kAccent).withAlpha (0.95f));
            g.drawVerticalLine (px, (float) gridTop, (float) gridBottom);
            juce::Path head;
            head.addTriangle ((float) px - 5.0f, (float) gridTop,
                              (float) px + 5.0f, (float) gridTop,
                              (float) px,        (float) gridTop + 6.0f);
            g.fillPath (head);
        }
    }

    g.setFont (juce::FontOptions (10.0f));
    const auto displayStatus = dragReadout.isNotEmpty()
                                   ? dragReadout
                                   : (mode == Mode::Recording && recordingPath.isNotEmpty()
                                       ? juce::String ("Writing: ") + recordingPath
                                       : status);
    g.setColour (dragReadout.isNotEmpty() ? juce::Colour (kAccent).withAlpha (0.92f)
                                          : juce::Colour (kTextMuted));
    g.drawText (displayStatus, statusArea, juce::Justification::centredLeft);

    if (snapEnabled)
    {
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
        g.drawText (u8 ("SNAP \xe2\x97\x86 ") + juce::String (snapBeatGrid, 2) + " beat @ " + juce::String (snapBpm, 1) + " BPM",
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

    setResizable (true, true);
    setResizeLimits (1180, 800, 4200, 2600);
    setSize (1500, 980);
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
    setupLibrary();

    title.setText ("StemDeck PRO", juce::dontSendNotification);
    title.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText (u8 ("FOUR DECKS  \xe2\x80\xa2  STEM MIXER  \xe2\x80\xa2  ARRANGEMENT"), juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    subtitle.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    standaloneWorkflowGuide.setText (
        u8 ("RECORD captures master + 4 deck WAVs + automation \xe2\x80\xa2 Reload bounces back into decks for post-set tweaks"),
        juce::dontSendNotification);
    standaloneWorkflowGuide.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    standaloneWorkflowGuide.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    standaloneWorkflowGuide.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (standaloneWorkflowGuide);

    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    tempoLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (tempoLabel);

    pluginHostStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pluginHostStatus.setFont (juce::FontOptions (10.5f));
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
    pluginHostMessage.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pluginHostMessage.setFont (juce::FontOptions (10.5f));
    pluginHostMessage.setJustificationType (juce::Justification::centredLeft);
    pluginHostMessage.setText ("FX host cache: " + processorRef.getPluginHostCacheFile().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (scanPluginsButton);
    addAndMakeVisible (reloadPluginCacheButton);
    addAndMakeVisible (loadPluginButton);
    addAndMakeVisible (clearChainButton);
    addAndMakeVisible (pluginHostMessage);

    masterKeyLabel.setText ("MASTER KEY", juce::dontSendNotification);
    masterKeyLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
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

        setupDeck (deckControls[(size_t) i], deckPrefix (i), "PLAY");
        {
            const auto accent = deckAccent (i);
            auto& dc = deckControls[(size_t) i];
            for (auto* knob : { &dc.low, &dc.mid, &dc.high, &dc.filter })
            {
                knob->setColour (juce::Slider::rotarySliderFillColourId, accent);
                knob->setColour (juce::Slider::trackColourId, accent);
            }
            for (auto& sg : dc.stemGains)
            {
                sg.setColour (juce::Slider::rotarySliderFillColourId, accent);
                sg.setColour (juce::Slider::trackColourId, accent);
            }
            for (auto* fader : { &dc.volume, &dc.fader, &dc.pitch })
                fader->setColour (juce::Slider::trackColourId, accent);
            dc.play   .setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
            dc.cue    .setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
            dc.liveKey.setColour (juce::ToggleButton::tickColourId, accent);
            dc.keyLock.setColour (juce::ToggleButton::tickColourId, accent);
        }
        deckControls[(size_t) i].load.onClick = [this, i] { openDeckChooser (i); };
        deckControls[(size_t) i].loadStem.onClick = [this, i]
        {
            const int selectedLane = deckControls[(size_t) i].stemLane.getSelectedId() - 1;
            openDeckStemChooser (i, selectedLane < 0 ? DeckPlayer::fullMixStem : selectedLane);
        };
        deckControls[(size_t) i].aiStems.onClick = [this, i] { processorRef.requestDeckAiStemsAsync (i); };
        deckControls[(size_t) i].cue.onClick = [this, i] { processorRef.rewindDeck (i); };

        // Seek on waveform click
        deckWaves[(size_t) i]->setOnSeekRequest ([this, i] (double seconds)
        {
            processorRef.seekDeckSeconds (i, seconds);
        });

        setupHotCueButtons (i);
        setupLoopControls (i);
        setupToolTabs (i);

        // Beat jump buttons — back: bars[0]=4 [1]=8 [2]=16 [3]=32
        {
            static constexpr double kBars[] = { 4.0, 8.0, 16.0, 32.0 };
            static constexpr const char* kBackLabels[] = { "◀4", "◀8", "◀16", "◀32" };
            static constexpr const char* kFwdLabels[]  = { "4▶", "8▶", "16▶", "32▶" };
            auto& dc = deckControls[(size_t) i];
            for (int b = 0; b < 4; ++b)
            {
                dc.beatJumpBack[b].setButtonText (u8 (kBackLabels[b]));
                setupButton (dc.beatJumpBack[b]);
                dc.beatJumpBack[b].onClick = [this, i, b] { processorRef.beatJumpDeck (i, -kBars[b]); };
                addAndMakeVisible (dc.beatJumpBack[b]);

                dc.beatJumpFwd[b].setButtonText (u8 (kFwdLabels[b]));
                setupButton (dc.beatJumpFwd[b]);
                dc.beatJumpFwd[b].onClick = [this, i, b] { processorRef.beatJumpDeck (i, kBars[b]); };
                addAndMakeVisible (dc.beatJumpFwd[b]);
            }
        }

        // Built-in FX sliders
        {
            auto& dc = deckControls[(size_t) i];
            setupSlider (dc.reverbMix, true);
            dc.reverbMix.setTooltip ("Reverb mix");
            dc.reverbMix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            dc.reverbMix.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (dc.reverbMix);
            dc.reverbMixAttachment = std::make_unique<SliderAttachment> (
                processorRef.parameters(), deckPrefix (i) + "RevMix", dc.reverbMix);

            setupSlider (dc.echoMix, true);
            dc.echoMix.setTooltip ("Echo mix");
            dc.echoMix.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            dc.echoMix.setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (dc.echoMix);
            dc.echoMixAttachment = std::make_unique<SliderAttachment> (
                processorRef.parameters(), deckPrefix (i) + "EchoMix", dc.echoMix);
        }
    }

    // Tap tempo
    setupButton (tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };
    addAndMakeVisible (tapTempoButton);
    tapTempoLabel.setFont (juce::FontOptions (9.0f));
    tapTempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    tapTempoLabel.setText ("TAP BPM", juce::dontSendNotification);
    addAndMakeVisible (tapTempoLabel);

    // Camelot wheel & MIDI learn buttons
    setupButton (camelotButton);
    camelotButton.onClick = [this] { showCamelotWheel (-1); };
    addAndMakeVisible (camelotButton);

    setupButton (midiLearnButton);
    midiLearnButton.onClick = [this] { updateMidiLearnUI(); };
    addAndMakeVisible (midiLearnButton);

    libraryMetaLabel.setFont (juce::FontOptions (10.0f));
    libraryMetaLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    libraryMetaLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (libraryMetaLabel);

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
    setupButton (razorButton);
    razorButton.setClickingTogglesState (true);
    razorButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kDanger).withAlpha (0.6f));
    razorButton.onClick = [this]
    {
        arrangementView.setRazorMode (razorButton.getToggleState());
    };
    addAndMakeVisible (razorButton);
    setupButton (playArrButton);
    playArrButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    playArrButton.onClick = [this] { toggleArrangementPlayback(); };
    addAndMakeVisible (playArrButton);
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
    recordStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    recordStatus.setFont (juce::FontOptions (10.5f, juce::Font::bold));
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
            // Shift+Delete resets the whole lane; plain Delete removes the
            // selected clip (a no-op on the last remaining clip in a lane).
            if (key.getModifiers().isShiftDown())
                arrangementView.resetSelectedClipEdits();
            else
                arrangementView.deleteSelectedClip();
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
    using namespace sattari::brand;
    button.setWantsKeyboardFocus (false);
    button.setColour (juce::TextButton::buttonColourId,    juce::Colour (kPanelRaised));
    button.setColour (juce::TextButton::buttonOnColourId,  juce::Colour (kAccent).withAlpha (0.35f).withBrightness (0.55f));
    button.setColour (juce::TextButton::textColourOffId,   juce::Colour (kTextPrimary).withAlpha (0.9f));
    button.setColour (juce::TextButton::textColourOnId,    juce::Colour (kTextPrimary));
    button.setColour (juce::ToggleButton::textColourId,    juce::Colour (kTextPrimary).withAlpha (0.9f));
    button.setColour (juce::ToggleButton::tickColourId,    juce::Colour (kAccent));
    button.setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (kHairline));
}

void MainPluginEditor::setupSlider (juce::Slider& slider, bool rotary, const juce::String& suffix)
{
    using namespace sattari::brand;
    slider.setWantsKeyboardFocus (false);
    slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 16);
    slider.setNumDecimalPlacesToDisplay (1);   // clean, untruncated readouts
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (kAccent));
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kHairline));
    slider.setColour (juce::Slider::trackColourId,               juce::Colour (kAccent));
    slider.setColour (juce::Slider::backgroundColourId,          juce::Colour (kPanelDeep));
    slider.setColour (juce::Slider::thumbColourId,               juce::Colour (kTextPrimary));
    slider.setColour (juce::Slider::textBoxTextColourId,         juce::Colour (kTextPrimary));
    slider.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
}

void MainPluginEditor::setupComboBox (juce::ComboBox& comboBox)
{
    using namespace sattari::brand;
    comboBox.setWantsKeyboardFocus (false);
    comboBox.setColour (juce::ComboBox::backgroundColourId,    juce::Colour (kPanelRaised));
    comboBox.setColour (juce::ComboBox::textColourId,          juce::Colour (kTextPrimary).withAlpha (0.92f));
    comboBox.setColour (juce::ComboBox::outlineColourId,       juce::Colour (kHairline));
    comboBox.setColour (juce::ComboBox::arrowColourId,         juce::Colour (kAccent));
    comboBox.setColour (juce::PopupMenu::backgroundColourId,   juce::Colour (kPanelDeep));
    comboBox.setColour (juce::PopupMenu::textColourId,         juce::Colour (kTextPrimary));
    comboBox.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (kAccent).withAlpha (0.20f));
    comboBox.setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colour (kTextPrimary));
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
    controls.keyInfo.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    controls.keyInfo.setJustificationType (juce::Justification::centredLeft);
    controls.keyInfo.setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (controls.keyInfo);
    controls.stemStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    controls.stemStatus.setJustificationType (juce::Justification::centredLeft);
    controls.stemStatus.setFont (juce::FontOptions (10.0f, juce::Font::bold));
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
    // djay-Pro feel: vertical faders read by position; values via tooltip on hover.
    controls.volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.fader .setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.pitch .setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    controls.volume.setPopupDisplayEnabled (true, true, this);
    controls.fader .setPopupDisplayEnabled (true, true, this);
    controls.pitch .setPopupDisplayEnabled (true, true, this);
    // Clean EQ knobs — no inline value text; value shows on hover (matches stems/faders).
    for (auto* eq : { &controls.low, &controls.mid, &controls.high, &controls.filter })
    {
        eq->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        eq->setPopupDisplayEnabled (true, true, this);
    }
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
        // Stem-knob fill tracks the deck's brand accent so each deck reads as a colour.
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
                if (auto* wave = deckWaves[(size_t) deckIndex].get())
                {
                    if (stemLaneIndex == DeckPlayer::fullMixStem)
                        wave->setFile (file);
                    else
                        wave->setStemFile (stemLaneIndex, file);
                }
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
        auto file = clips[(size_t) i].empty() ? juce::File() : clips[(size_t) i].front().file;
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
                              : u8 ("No deck captures to reload \xe2\x80\x94 record a set first"),
                          juce::dontSendNotification);
}

void MainPluginEditor::toggleArrangementPlayback()
{
    if (processorRef.isArrangementPlaying())
    {
        processorRef.stopArrangement();
        recordStatus.setText ("Arrangement stopped", juce::dontSendNotification);
        return;
    }

    // Build clip specs from the editable deck lanes (the master lane is their sum,
    // so we skip it to avoid doubling). Timeline position == the clip's recorded
    // region, so the edited multi-clip arrangement plays back in place.
    const auto& clips = arrangementView.getClips();
    std::vector<MainPluginProcessor::ArrangementClipSpec> specs;
    for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
    {
        for (const auto& clip : clips[(size_t) lane])
        {
            if (! clip.file.existsAsFile() || clip.durationSeconds <= 0.0) continue;
            const double start = juce::jlimit (0.0, clip.durationSeconds, clip.trimStartSeconds);
            const double end   = juce::jlimit (start, clip.durationSeconds,
                                               clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
            if (end - start < 0.02) continue;

            MainPluginProcessor::ArrangementClipSpec spec;
            spec.file                 = clip.file;
            spec.timelineStartSeconds = clip.timelineStartSeconds;   // free-positioned
            spec.sourceStartSeconds   = start;
            spec.lengthSeconds        = end - start;
            spec.gain                 = 1.0f;
            spec.fadeInSeconds        = clip.fadeInSeconds;
            spec.fadeOutSeconds       = clip.fadeOutSeconds;
            specs.push_back (spec);
        }
    }

    if (specs.empty())
    {
        recordStatus.setText (u8 ("Nothing to play \xe2\x80\x94 record or load an arrangement first"), juce::dontSendNotification);
        return;
    }

    processorRef.buildAndStartArrangement (std::move (specs));
    recordStatus.setText (u8 ("Playing arrangement\xe2\x80\xa6"), juce::dontSendNotification);
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
        const auto& laneClips = clips[(size_t) i];
        if (laneClips.empty() || ! laneClips.front().file.existsAsFile() || laneClips.front().durationSeconds <= 0.0)
            continue;

        const juce::File sourceFile = laneClips.front().file;

        // Decide whether this lane needs flattening: more than one clip (splits /
        // deletes), or a single clip that has been trimmed / faded.
        bool needsBounce = laneClips.size() > 1;
        double laneTimelineEnd = 0.0;
        for (const auto& clip : laneClips)
        {
            const double endSec = clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds;
            laneTimelineEnd = juce::jmax (laneTimelineEnd, clip.timelineEndSeconds());
            const double fadeIn  = juce::jlimit (0.0, endSec - clip.trimStartSeconds, clip.fadeInSeconds);
            const double fadeOut = juce::jlimit (0.0, endSec - clip.trimStartSeconds - fadeIn, clip.fadeOutSeconds);
            if (clip.trimStartSeconds > 0.001 || endSec < clip.durationSeconds - 0.001
                || std::abs (clip.timelineStartSeconds - clip.trimStartSeconds) > 0.001
                || fadeIn > 0.001 || fadeOut > 0.001)
                needsBounce = true;
        }
        if (! needsBounce || laneTimelineEnd < 0.05)
            continue;

        std::unique_ptr<juce::AudioFormatReader> reader (thumbnailFormatManager.createReaderFor (sourceFile));
        if (reader == nullptr)
        {
            errors << laneName (i) << ": read failed. ";
            continue;
        }

        const double sr = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        const int channels = (int) juce::jmax (1u, reader->numChannels);
        const int totalSamples = juce::jmax (1, (int) std::round (laneTimelineEnd * sr));

        // Flattened lane timeline: gaps are silence, each clip placed at its
        // source/timeline position with its fades applied.
        juce::AudioBuffer<float> out (channels, totalSamples);
        out.clear();

        bool ok = true;
        for (const auto& clip : laneClips)
        {
            const double startSec = juce::jlimit (0.0, clip.durationSeconds, clip.trimStartSeconds);
            const double endSec   = juce::jlimit (startSec, clip.durationSeconds,
                                                  clip.trimEndSeconds > 0.0 ? clip.trimEndSeconds : clip.durationSeconds);
            const int regionSamples = (int) std::round ((endSec - startSec) * sr);
            if (regionSamples < 1) continue;

            juce::AudioBuffer<float> region (channels, regionSamples);
            if (! reader->read (&region, 0, regionSamples, (juce::int64) std::round (startSec * sr), true, true))
            {
                ok = false;
                break;
            }

            const double regionLen = endSec - startSec;
            const double fadeIn  = juce::jlimit (0.0, regionLen, clip.fadeInSeconds);
            const double fadeOut = juce::jlimit (0.0, regionLen - fadeIn, clip.fadeOutSeconds);
            const int fadeInSamples  = (int) std::round (fadeIn  * sr);
            const int fadeOutSamples = (int) std::round (fadeOut * sr);
            if (fadeInSamples > 1)
                region.applyGainRamp (0, juce::jmin (fadeInSamples, regionSamples), 0.0f, 1.0f);
            if (fadeOutSamples > 1)
            {
                const int n = juce::jmin (fadeOutSamples, regionSamples);
                region.applyGainRamp (regionSamples - n, n, 1.0f, 0.0f);
            }

            const int destOffset = juce::jlimit (0, totalSamples, (int) std::round (clip.timelineStartSeconds * sr));
            const int copyLen = juce::jmin (regionSamples, totalSamples - destOffset);
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, destOffset, region, ch, 0, copyLen);
        }
        if (! ok)
        {
            errors << laneName (i) << ": region read failed. ";
            continue;
        }

        auto destFile = sourceFile.getParentDirectory()
                           .getChildFile (sourceFile.getFileNameWithoutExtension() + "-edited")
                           .withFileExtension ("wav");
        destFile.deleteFile();

        if (auto outStream = destFile.createOutputStream())
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (outStream.get(), sr, (unsigned int) channels, 24, {}, 0));
            if (writer != nullptr)
            {
                outStream.release();
                if (writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()))
                {
                    writer.reset();
                    arrangementView.updateClipAfterBounce (i, destFile, laneTimelineEnd);
                    ++rendered;
                    continue;
                }
            }
        }
        errors << laneName (i) << ": write failed. ";
    }

    if (rendered > 0)
        recordStatus.setText ("Bounced " + juce::String (rendered) + " edited lane" + (rendered == 1 ? "" : "s") + u8 (" \xe2\x80\x94 Reload to Decks uses the new versions"),
                              juce::dontSendNotification);
    else
        recordStatus.setText (errors.isNotEmpty() ? errors : u8 ("No edits to bounce \xe2\x80\x94 drag a clip edge or fade handle first"),
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
        arrangementView.setIdle (u8 ("Recording produced no files \xe2\x80\x94 check the destination folder."));
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
    pluginHostMessage.setText (u8 ("Scanning FX defaults\xe2\x80\xa6"), juce::dontSendNotification);
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
                                 + u8 (" \xe2\x80\xa2 known FX: ") + juce::String (result.plugins.size());
            if (! result.blacklistedFiles.isEmpty())
                message += u8 (" \xe2\x80\xa2 blacklisted: ") + juce::String (result.blacklistedFiles.size());
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
    pluginHostMessage.setText ("Loading " + pluginSelector.getText() + u8 (" \xe2\x86\x92 ") + pluginChainSelector.getText(), juce::dontSendNotification);
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
    fileBrowser->setColour (juce::TreeView::backgroundColourId, juce::Colour (sattari::brand::kPanelDeep));
    fileBrowser->setColour (juce::TreeView::linesColourId, juce::Colour (sattari::brand::kHairline));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.30f));
    fileBrowser->setColour (juce::DirectoryContentsDisplayComponent::textColourId, juce::Colour (sattari::brand::kTextPrimary).withAlpha (0.92f));
    addChildComponent (*fileBrowser);

    browserNavigateTo (juce::File::getSpecialLocation (juce::File::userMusicDirectory));
}

void MainPluginEditor::setupLibrary()
{
    setupButton (toggleLibraryButton);
    toggleLibraryButton.setClickingTogglesState (true);
    toggleLibraryButton.onClick = [this] { toggleLibrary(); };
    addAndMakeVisible (toggleLibraryButton);

    libraryView = std::make_unique<MusicLibraryView> (thumbnailFormatManager);
    libraryView->onLoadFile = [this] (const juce::File& f) { fileDoubleClicked (f); };
    addChildComponent (*libraryView);
    libraryView->restorePersistedCrates();
}

void MainPluginEditor::toggleLibrary()
{
    libraryVisible = toggleLibraryButton.getToggleState();
    if (libraryView != nullptr)
        libraryView->setVisible (libraryVisible);

    // Library and the basic file browser share the left column — keep one open.
    if (libraryVisible && browserVisible)
    {
        browserVisible = false;
        toggleBrowserButton.setToggleState (false, juce::dontSendNotification);
        if (fileBrowser != nullptr)        fileBrowser->setVisible (false);
        browserUpButton.setVisible (false);
        browserHomeButton.setVisible (false);
        browserPathLabel.setVisible (false);
    }
    resized();
}

void MainPluginEditor::toggleFileBrowser()
{
    browserVisible = toggleBrowserButton.getToggleState();
    if (fileBrowser != nullptr)
        fileBrowser->setVisible (browserVisible);
    browserUpButton.setVisible (browserVisible);
    browserHomeButton.setVisible (browserVisible);
    browserPathLabel.setVisible (browserVisible);

    // Mutually exclusive with the library panel — they share the left column.
    if (browserVisible && libraryVisible)
    {
        libraryVisible = false;
        toggleLibraryButton.setToggleState (false, juce::dontSendNotification);
        if (libraryView != nullptr)
            libraryView->setVisible (false);
    }
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
    menu.addSectionHeader (u8 ("All decks are loaded \xe2\x80\x94 pick one to replace"));
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
    lookAndFeel.paintModernBackground (g, getLocalBounds());

    // --- Header toolbar backdrop ---
    if (! headerBarBounds.isEmpty())
    {
        const auto r = headerBarBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanel));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (sattari::brand::kHairline).withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        // Accent stripe along the bottom of the toolbar = quiet product identity line.
        g.setColour (juce::Colour (sattari::brand::kAccent).withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 12.0f, r.getBottom() - 1.5f, r.getWidth() - 24.0f, 1.5f));
    }

    // --- Tempo chip in the right cluster ---
    if (! tempoChipBounds.isEmpty())
    {
        const auto r = tempoChipBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelDeep));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (sattari::brand::kAccent).withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("HOST TEMPO", juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 2.0f, r.getWidth() - 16.0f, 10.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (sattari::brand::kHairline).withAlpha (0.5f));
    g.drawHorizontalLine (title.getBounds().getBottom() + 9, 22.0f, (float) getWidth() - 22.0f);

    // --- Deck panels: flat brand-palette card per deck, accent stripe at the top ---
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto deckBounds = deckPanelBounds[(size_t) i];
        if (deckBounds.isEmpty())
            continue;
        const bool deckPlaying = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        drawPanel (g, deckBounds, deckAccent (i), 10.0f, deckPlaying);
        g.setColour (deckAccent (i).withAlpha (0.85f));
        g.fillRoundedRectangle (deckBounds.removeFromTop (3).toFloat(), 1.5f);

        // --- Deck header strip: letter badge + filename + key chip ---
        auto headerBar = deckHeaderBounds[(size_t) i];
        if (! headerBar.isEmpty())
        {
            const juce::String letter = juce::String::charToString ((juce_wchar) ('A' + i));
            auto badge = headerBar.removeFromLeft (22);
            g.setColour (deckAccent (i).withAlpha (0.95f));
            g.fillRoundedRectangle (badge.toFloat(), 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (letter, badge, juce::Justification::centred);
            headerBar.removeFromLeft (8);

            // Right-aligned BPM + key chips.
            const auto bpmValue  = deckControls[(size_t) i].bpm.getValue();
            const auto bpmText   = juce::String (bpmValue, 1);
            const auto keyText   = deckControls[(size_t) i].keyInfo.getText().upToFirstOccurrenceOf (" ", false, false);

            auto drawChip = [&] (juce::Rectangle<int> chip, const juce::String& text)
            {
                g.setColour (juce::Colour (sattari::brand::kPanelDeep));
                g.fillRoundedRectangle (chip.toFloat(), 3.0f);
                g.setColour (deckAccent (i).withAlpha (0.55f));
                g.drawRoundedRectangle (chip.toFloat(), 3.0f, 1.0f);
                g.setColour (deckAccent (i).withAlpha (0.95f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (text, chip.reduced (6, 0), juce::Justification::centredRight);
            };

            if (keyText.isNotEmpty())
                drawChip (headerBar.removeFromRight (juce::jmin (66, headerBar.getWidth() / 3)), keyText);
            headerBar.removeFromRight (4);
            drawChip (headerBar.removeFromRight (juce::jmin (80, headerBar.getWidth() / 3)), bpmText + " BPM");
            headerBar.removeFromRight (6);

            // Filename in the middle slot (truncate left-most path components).
            const auto path = processorRef.getDeckPath (i);
            const auto file = juce::File (path).getFileName();
            const auto display = file.isNotEmpty() ? file : u8 ("\xe2\x80\x94 empty deck \xe2\x80\x94");
            g.setColour (juce::Colour (sattari::brand::kTextPrimary).withAlpha (file.isNotEmpty() ? 0.92f : 0.45f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (display, headerBar.reduced (2, 0), juce::Justification::centredLeft, true);
        }

        // --- STEM SEPARATION centerpiece: accent-tinted module + per-stem labels ---
        auto stemBox = deckStemBounds[(size_t) i];
        if (! stemBox.isEmpty())
        {
            g.setColour (deckAccent (i).withAlpha (0.09f));
            g.fillRoundedRectangle (stemBox.toFloat(), 6.0f);
            g.setColour (deckAccent (i).withAlpha (0.30f));
            g.drawRoundedRectangle (stemBox.toFloat().reduced (0.5f), 6.0f, 1.0f);

            auto labelRow = stemBox.removeFromTop (13);

            // RTSS "LIVE" badge tucked at the far right of the stem label row.
            if (processorRef.isDeckRtssActive (i))
            {
                auto badge = labelRow.removeFromRight (30);
                g.setColour (deckAccent (i).withAlpha (0.85f));
                g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
                g.drawText ("LIVE", badge, juce::Justification::centredRight);
            }

            // Per-stem labels aligned to the 5 knob columns (FULL/DRUMS/BASS/MUSIC/VOX).
            static constexpr std::array<const char*, 5> stemNames { "FULL", "DRUMS", "BASS", "MUSIC", "VOX" };
            const int n = DeckPlayer::numStemLanes;
            const int colW = juce::jmax (1, stemBox.getWidth() / n);
            g.setColour (deckAccent (i).withAlpha (0.92f));
            g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
            for (int s = 0; s < juce::jmin (n, 5); ++s)
                g.drawText (stemNames[(size_t) s],
                            juce::Rectangle<int> (stemBox.getX() + s * colW, labelRow.getY(), colW, 12),
                            juce::Justification::centred);
        }
    }

    // --- Control labels above each control (matches djay-Pro nomenclature) ---
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const auto& controls = deckControls[(size_t) i];
        auto drawAbove = [&g] (const juce::String& text, const juce::Component& component)
        {
            if (! component.isVisible()) return;   // tab-hidden controls draw no stray label
            auto r = component.getBounds().withHeight (12).translated (0, -13);
            g.drawText (text, r, juce::Justification::centred);
        };

        g.setColour (deckAccent (i).withAlpha (0.92f));
        drawAbove ("VOL",   controls.volume);
        drawAbove ("XF",    controls.fader);
        drawAbove ("PITCH", controls.pitch);
        drawAbove ("LOW",   controls.low);
        drawAbove ("MID",   controls.mid);
        drawAbove ("HIGH",  controls.high);
        drawAbove ("FILT",  controls.filter);
        drawAbove ("BPM",   controls.bpm);
        drawAbove ("BEAT",  controls.beatOffset);
        drawAbove ("DOWN",  controls.downbeat);
    }

    using namespace sattari::brand;

    // Mixer panel box (left band).
    if (! mixerPanel.getBounds().isEmpty() && ! redoButton.getBounds().isEmpty())
    {
        const auto mixBox = mixerPanel.getBounds().getUnion (redoButton.getBounds()).expanded (10, 8);
        drawPanel (g, mixBox, juce::Colour (kAccent), 10.0f);
    }

    // Labels above the top-row sliders.
    g.setColour (juce::Colour (kTextMuted));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    auto sliderLabel = [&] (const juce::String& t, juce::Component& c)
    {
        if (c.isVisible() && ! c.getBounds().isEmpty())
            g.drawText (t, c.getBounds().withHeight (12).translated (0, -13), juce::Justification::centred);
    };
    sliderLabel (u8 ("CROSSFADER A/C \xe2\x86\x94 B/D"), crossfader);
    sliderLabel ("MASTER BPM", masterBpm);
    sliderLabel ("MASTER",     master);
    sliderLabel ("LIMIT",      limiter);
    sliderLabel ("MIC GAIN",   micGain);

    // --- Recording module: defined panel with a clear, prominent title ---
    if (! recordModuleBounds.isEmpty())
    {
        const bool recording = processorRef.isMasterRecording();
        const auto accent = recording ? juce::Colour (kDanger) : juce::Colour (kAccent);
        const auto r = recordModuleBounds.toFloat();
        g.setColour (juce::Colour (kPanel));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colour (kHairline).withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (accent.withAlpha (recording ? 0.95f : 0.6f));
        g.drawRoundedRectangle (r.reduced (1.5f), 9.0f, recording ? 2.0f : 1.5f);

        g.setColour (accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        const auto dot = recording ? juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) : juce::String();
        g.drawText (dot + "RECORDING", recordModuleBounds.reduced (12, 6).withHeight (14),
                    juce::Justification::centredLeft);
    }
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (50);
    headerBarBounds = header;

    // Left cluster: Library + Files toggles + subtitle (mode line).
    toggleLibraryButton.setBounds (header.removeFromLeft (72).withSizeKeepingCentre (72, 30));
    header.removeFromLeft (6);
    toggleBrowserButton.setBounds (header.removeFromLeft (58).withSizeKeepingCentre (58, 30));
    header.removeFromLeft (10);
    auto leftSlot = header.removeFromLeft (220);
    subtitle.setBounds (leftSlot.withSizeKeepingCentre (220, 18));

    // Right cluster: save / user presets / tempo chip — built right-to-left.
    savePresetButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, 30));
    header.removeFromRight (8);
    userPresets.setBounds (header.removeFromRight (160).withSizeKeepingCentre (160, 28));
    header.removeFromRight (10);
    tempoChipBounds = header.removeFromRight (150).withSizeKeepingCentre (150, 30);
    tempoLabel.setBounds (tempoChipBounds.reduced (10, 0));
    header.removeFromRight (12);

    // Centre: title fills whatever remains.
    title.setBounds (header);

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
    else if (libraryVisible && libraryView != nullptr)
    {
        const int libraryWidth = juce::jmin (460, area.getWidth() / 3);
        auto libraryCol = area.removeFromLeft (libraryWidth);
        area.removeFromLeft (10);
        libraryView->setBounds (libraryCol);
    }

    // Compact info strip: workflow guide + host status sharing one row, FX
    // management collapsed into a tight 22px strip so live use isn't crowded by
    // setup chrome that should already be configured before the gig.
    auto infoRow = area.removeFromTop (16);
    standaloneWorkflowGuide.setBounds (infoRow.removeFromLeft (infoRow.getWidth() * 2 / 3));
    pluginHostStatus.setBounds (infoRow);
    area.removeFromTop (4);

    auto fxRow = area.removeFromTop (22);
    pluginChainSelector.setBounds (fxRow.removeFromLeft (130));
    fxRow.removeFromLeft (4);
    pluginSelector.setBounds (fxRow.removeFromLeft (260));
    fxRow.removeFromLeft (4);
    loadPluginButton.setBounds (fxRow.removeFromLeft (66));
    fxRow.removeFromLeft (3);
    clearChainButton.setBounds (fxRow.removeFromLeft (66));
    fxRow.removeFromLeft (3);
    scanPluginsButton.setBounds (fxRow.removeFromLeft (60));
    fxRow.removeFromLeft (3);
    reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (80));
    fxRow.removeFromLeft (8);
    pluginHostMessage.setBounds (fxRow);
    area.removeFromTop (6);

    auto keyRow = area.removeFromTop (28);
    masterKeyLabel.setBounds (keyRow.removeFromLeft (108));
    keyRow.removeFromLeft (4);
    masterKey.setBounds (keyRow.removeFromLeft (110));
    keyRow.removeFromLeft (10);
    syncAllDecksButton.setBounds (keyRow.removeFromLeft (140));
    area.removeFromTop (6);

    liveMixerLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (2);

    // Deck row gets a fixed band; the arrangement below takes ALL remaining height
    // (so it is large and grows as the window resizes).
    auto decksArea = area.removeFromTop (juce::jmax (390, area.getHeight() * 44 / 100));
    const int deckGap = 10;
    const int deckWidth = (decksArea.getWidth() - deckGap * (MainPluginProcessor::numDecks - 1)) / MainPluginProcessor::numDecks;
    for (int deckIndex = 0; deckIndex < MainPluginProcessor::numDecks; ++deckIndex)
    {
        auto laneOuter = decksArea.removeFromLeft (deckWidth);
        decksArea.removeFromLeft (deckGap);
        deckPanelBounds[(size_t) deckIndex] = laneOuter;
        auto lane = laneOuter.reduced (10, 10);

        auto& controls = deckControls[(size_t) deckIndex];

        // Header strip (badge + filename + chips — painted in paint()).
        auto headerStrip = lane.removeFromTop (22);
        deckHeaderBounds[(size_t) deckIndex] = headerStrip;
        lane.removeFromTop (6);

        // ---- Bottom-up fixed sections; the waveform claims the flexible middle ----

        // Status row (key / stem provider status).
        auto statusRow = lane.removeFromBottom (13);
        controls.keyInfo   .setBounds (statusRow.removeFromLeft (statusRow.getWidth() / 2));
        controls.stemStatus.setBounds (statusRow);
        lane.removeFromBottom (4);

        // Toolbox content (the active tab's controls render here).
        auto tabContent = lane.removeFromBottom (58);
        lane.removeFromBottom (3);

        // Toolbox tab strip (CUE / LOOP / FX / SYNC / SRC).
        auto tabStrip = lane.removeFromBottom (20);
        {
            const int tw = tabStrip.getWidth() / DeckControls::NumToolTabs;
            for (int t = 0; t < DeckControls::NumToolTabs; ++t)
                controls.toolTabs[(size_t) t].setBounds (tabStrip.removeFromLeft (tw).reduced (1, 0));
        }
        lane.removeFromBottom (8);

        // Mix band: EQ knobs (left) + VOL / FADER / PITCH vertical faders (right).
        auto mixBand = lane.removeFromBottom (64);
        {
            const int faderColW = 32, fgap = 4;
            auto fadersCol = mixBand.removeFromRight (faderColW * 3 + fgap * 2);
            auto eqArea = mixBand.withTrimmedRight (8);
            const int eqW = juce::jmax (26, eqArea.getWidth() / 4);
            controls.low   .setBounds (eqArea.removeFromLeft (eqW).reduced (3, 2));
            controls.mid   .setBounds (eqArea.removeFromLeft (eqW).reduced (3, 2));
            controls.high  .setBounds (eqArea.removeFromLeft (eqW).reduced (3, 2));
            controls.filter.setBounds (eqArea.removeFromLeft (eqW).reduced (3, 2));
            controls.volume.setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.fader .setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.pitch .setBounds (fadersCol.reduced (3, 2));
        }
        lane.removeFromBottom (6);

        // Transport row — big, clean PLAY + CUE (key toggles moved to SYNC tab).
        auto transport = lane.removeFromBottom (28);
        {
            const int w = transport.getWidth();
            controls.play.setBounds (transport.removeFromLeft (w * 58 / 100).reduced (1, 0));
            controls.cue .setBounds (transport.reduced (1, 0));
        }
        lane.removeFromBottom (6);

        // STEM SEPARATION — the centerpiece: label band + 5 colour-coded knobs.
        auto stemKnobRow = lane.removeFromBottom (54);
        auto stemLabelRow = lane.removeFromBottom (14);
        deckStemBounds[(size_t) deckIndex] = stemLabelRow.getUnion (stemKnobRow);
        {
            const int stemW = juce::jmax (28, stemKnobRow.getWidth() / DeckPlayer::numStemLanes);
            for (int s = 0; s < DeckPlayer::numStemLanes; ++s)
                controls.stemGains[(size_t) s].setBounds (stemKnobRow.removeFromLeft (stemW).reduced (3, 3));
        }
        lane.removeFromBottom (6);

        // Waveform — flexible: everything that's left between header and stems.
        if (auto* wave = deckWaves[(size_t) deckIndex].get())
            wave->setBounds (lane);

        // ---- Active toolbox tab content ----
        switch (controls.activeToolTab)
        {
            case DeckControls::TabCue:
            {
                auto row = tabContent.removeFromTop (24);
                const int w = juce::jmax (12, row.getWidth() / 8);
                for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
                    controls.hotCues[(size_t) c].setBounds (row.removeFromLeft (w).reduced (1, 0));
                break;
            }
            case DeckControls::TabLoop:
            {
                auto r1 = tabContent.removeFromTop (24);
                const int bw = juce::jmax (28, r1.getWidth() / 5);
                controls.loopIn    .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopOut   .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopToggle.setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopHalf  .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopDouble.setBounds (r1.reduced (1, 0));
                tabContent.removeFromTop (2);
                auto r2 = tabContent.removeFromTop (24);
                const int jw = juce::jmax (12, r2.getWidth() / 8);
                for (int b = 3; b >= 0; --b)
                    controls.beatJumpBack[(size_t) b].setBounds (r2.removeFromLeft (jw).reduced (1, 0));
                for (int b = 0; b < 4; ++b)
                    controls.beatJumpFwd[(size_t) b].setBounds (r2.removeFromLeft (jw).reduced (1, 0));
                break;
            }
            case DeckControls::TabFx:
            {
                const int knobW = juce::jmin (60, tabContent.getWidth() / 2 - 6);
                controls.reverbMix.setBounds (tabContent.removeFromLeft (knobW).reduced (4, 2));
                tabContent.removeFromLeft (8);
                controls.echoMix  .setBounds (tabContent.removeFromLeft (knobW).reduced (4, 2));
                break;
            }
            case DeckControls::TabSync:
            {
                auto r1 = tabContent.removeFromTop (22);
                const int w = juce::jmax (44, r1.getWidth() / 3);
                controls.bpm       .setBounds (r1.removeFromLeft (w).reduced (2, 1));
                controls.beatOffset.setBounds (r1.removeFromLeft (w).reduced (2, 1));
                controls.downbeat  .setBounds (r1.reduced (2, 1));
                tabContent.removeFromTop (2);
                auto r2 = tabContent.removeFromTop (20);
                const int cw = juce::jmax (44, r2.getWidth() / 3);
                controls.tempoInterpretation.setBounds (r2.removeFromLeft (cw).reduced (1, 0));
                controls.syncMode           .setBounds (r2.removeFromLeft (cw).reduced (1, 0));
                controls.keyTarget          .setBounds (r2.reduced (1, 0));
                tabContent.removeFromTop (2);
                auto r3 = tabContent.removeFromTop (18);
                controls.liveKey.setBounds (r3.removeFromLeft (r3.getWidth() / 2).reduced (1, 0));
                controls.keyLock.setBounds (r3.reduced (1, 0));
                break;
            }
            case DeckControls::TabSrc:
            {
                auto row = tabContent.removeFromTop (24);
                controls.load    .setBounds (row.removeFromLeft (juce::jmax (44, row.getWidth() * 22 / 100)).reduced (1, 0));
                controls.aiStems .setBounds (row.removeFromLeft (juce::jmax (60, row.getWidth() * 36 / 100)).reduced (1, 0));
                controls.loadStem.setBounds (row.removeFromLeft (juce::jmax (44, row.getWidth() * 38 / 100)).reduced (1, 0));
                controls.stemLane.setBounds (row.reduced (1, 0));
                break;
            }
            default: break;
        }

        applyDeckToolTabVisibility (deckIndex);
    }

    area.removeFromTop (8);

    // ---- Mixer + defined recording module band ----
    auto mixerStrip = area.removeFromTop (100);
    recordModuleBounds = mixerStrip.removeFromRight (juce::jmax (320, mixerStrip.getWidth() * 30 / 100));
    mixerStrip.removeFromRight (10);

    auto mixer = mixerStrip.reduced (2, 5);
    mixerPanel.setBounds (mixer.removeFromLeft (140));
    auto mixControls = mixer.reduced (10, 10);
    auto mcTop = mixControls.removeFromTop (mixControls.getHeight() / 2);
    auto mcBot = mixControls;
    // Top row: crossfader + master bpm + master + limiter + mic gain.
    crossfader.setBounds (mcTop.removeFromLeft (juce::jmax (180, mcTop.getWidth() * 32 / 100)));
    mcTop.removeFromLeft (12);
    masterBpm.setBounds (mcTop.removeFromLeft (110));
    master.setBounds   (mcTop.removeFromLeft (80));
    limiter.setBounds  (mcTop.removeFromLeft (80));
    micGain.setBounds  (mcTop.removeFromLeft (84));
    // Bottom row: sync toggles + utility buttons + edit/transport buttons.
    auto chip = [&mcBot] (juce::Component& c, int w) { c.setBounds (mcBot.removeFromLeft (w).withHeight (30)); mcBot.removeFromLeft (3); };
    hostSync.setBounds (mcBot.removeFromLeft (96).withTrimmedTop (8)); mcBot.removeFromLeft (3);
    beatSync.setBounds (mcBot.removeFromLeft (96).withTrimmedTop (8)); mcBot.removeFromLeft (3);
    micMonitor.setBounds (mcBot.removeFromLeft (104).withTrimmedTop (8)); mcBot.removeFromLeft (6);
    chip (tapTempoButton, 48);
    chip (camelotButton, 44);
    chip (midiLearnButton, 48);
    chip (snapToggleButton, 48);
    chip (razorButton, 50);
    chip (playArrButton, 60);
    chip (bounceEditsButton, 64);
    chip (undoButton, 46);
    chip (redoButton, 46);

    // Recording module (painted bordered panel in paint()): big record button + status.
    {
        auto rec = recordModuleBounds.reduced (12, 10);
        rec.removeFromTop (16);   // room for the painted "RECORDING" title
        auto recRow = rec.removeFromTop (34);
        recordButton.setBounds (recRow.removeFromLeft (juce::jmax (96, recRow.getWidth() * 36 / 100)));
        recRow.removeFromLeft (6);
        revealRecordingButton.setBounds (recRow.removeFromLeft (64));
        recRow.removeFromLeft (6);
        reloadIntoDecksButton.setBounds (recRow);
        rec.removeFromTop (6);
        recordStatus.setBounds (rec.removeFromTop (16));
    }

    area.removeFromTop (8);
    {
        auto labelRow = area.removeFromTop (22);
        arrangementLabel.setBounds (labelRow.removeFromLeft (labelRow.getWidth() / 2));
        libraryMetaLabel.setBounds (labelRow);   // MIDI-learn / status feedback
    }
    auto bottom = area;   // everything left → the arrangement (large, grows on resize)
    auto samplerStrip = bottom.removeFromRight (260);
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
    // MIDI learn feedback — reflect bindings as they happen.
    {
        auto& midi = processorRef.getMidiMapping();
        if (auto res = midi.pollLearnResult(); res.valid)
        {
            const auto name = res.isCc ? midi.ccName (res.targetIdx) : midi.noteName (res.targetIdx);
            libraryMetaLabel.setText ((res.isCc ? "Mapped CC " : "Mapped Note ")
                                      + juce::String (res.controller) + u8 ("  \xe2\x86\x92  ") + name,
                                      juce::dontSendNotification);
        }
        midiLearnButton.setButtonText (midi.isLearning() ? u8 ("LEARN\xe2\x80\xa6") : juce::String ("MIDI"));
        midiLearnButton.setColour (juce::TextButton::buttonColourId,
                                   midi.isLearning() ? juce::Colour (sattari::brand::kAccent).withAlpha (0.55f)
                                                     : juce::Colour (sattari::brand::kPanelRaised));
    }

    processorRef.refreshLiveKeyAnalysis();
    auto snapshot = processorRef.getTransportSnapshot();
    tempoLabel.setText (juce::String (snapshot.bpm, 1) + (snapshot.hostPlaying ? u8 ("  \xe2\x96\xb6") : u8 ("  \xe2\x97\xbc")),
                        juce::dontSendNotification);
    pluginHostStatus.setText (processorRef.getPluginHostStatus(), juce::dontSendNotification);
    const bool recording = processorRef.isMasterRecording();
    recordButton.setButtonText (recording ? "Stop Rec" : "Record WAV");
    recordButton.setColour (juce::TextButton::buttonColourId,
                            recording ? juce::Colour (sattari::brand::kDanger)
                                      : juce::Colour (sattari::brand::kPanelRaised));
    recordButton.setColour (juce::TextButton::textColourOffId,
                            recording ? juce::Colours::white
                                      : juce::Colour (sattari::brand::kTextPrimary).withAlpha (0.9f));

    if (snapToggleButton.getToggleState())
    {
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        arrangementView.setSnap (true, 0.25, bpm);
    }

    // Push live playback state into each deck's waveform display so the playhead
    // line + play/pause glyph track the actual deck transport in real time.
    bool deckPlayChanged = false;
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        const bool playing = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        if (auto* wave = deckWaves[(size_t) i].get())
            wave->setPlaybackState (processorRef.getDeckPlayheadSeconds (i),
                                    processorRef.getDeckTotalSeconds (i),
                                    playing);
        if (playing != lastDeckPlayingUI[(size_t) i])
        {
            lastDeckPlayingUI[(size_t) i] = playing;
            deckPlayChanged = true;
        }
    }
    if (deckPlayChanged)
        repaint();   // refresh the deck-card active glow

    undoButton.setEnabled (arrangementView.canUndo());
    redoButton.setEnabled (arrangementView.canRedo());

    // Arrangement transport feedback: moving playhead + button state.
    const bool arrPlaying = processorRef.isArrangementPlaying();
    arrangementView.setPlaybackPosition (processorRef.getArrangementPositionSeconds(), arrPlaying);
    playArrButton.setToggleState (arrPlaying, juce::dontSendNotification);
    playArrButton.setButtonText (arrPlaying ? "Stop Arr" : "Play Arr");
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
        deckControls[(size_t) i].keyInfo.setText (processorRef.getDeckKeyInfo (i) + u8 (" \xe2\x80\xa2 ") + processorRef.getDeckTimePitchStatus (i) + u8 (" \xe2\x80\xa2 ") + processorRef.getDeckBeatSyncStatus (i),
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

        // Keep stem thumbnails in sync when AI stem separation completes or on reload.
        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto stemPath = processorRef.getDeckStemPath (i, lane);
            if (stemPath != deckStemPaths[(size_t) i][(size_t) lane])
            {
                deckStemPaths[(size_t) i][(size_t) lane] = stemPath;
                if (auto* wave = deckWaves[(size_t) i].get())
                    wave->setStemFile (lane, stemPath.isNotEmpty() ? juce::File (stemPath) : juce::File());
            }
        }

        // RTSS badge — illuminates when real-time separation is running
        if (auto* wave = deckWaves[(size_t) i].get())
            wave->setRtssActive (processorRef.isDeckRtssActive (i));
    }
}

// ── Toolbox tabs (declutter) ──────────────────────────────────────────────────

void MainPluginEditor::setupToolTabs (int deckIndex)
{
    static const char* const kLabels[] = { "CUE", "LOOP", "FX", "SYNC", "SRC" };
    auto& dc = deckControls[(size_t) deckIndex];
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
    {
        auto& btn = dc.toolTabs[(size_t) t];
        btn.setButtonText (kLabels[t]);
        setupButton (btn);
        btn.setColour (juce::TextButton::buttonOnColourId, deckAccent (deckIndex).withAlpha (0.55f));
        btn.onClick = [this, deckIndex, t] { setDeckToolTab (deckIndex, t); };
        addAndMakeVisible (btn);
    }
    setDeckToolTab (deckIndex, DeckControls::TabCue);
}

void MainPluginEditor::setDeckToolTab (int deckIndex, int tab)
{
    auto& dc = deckControls[(size_t) deckIndex];
    dc.activeToolTab = tab;
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
        dc.toolTabs[(size_t) t].setToggleState (t == tab, juce::dontSendNotification);
    applyDeckToolTabVisibility (deckIndex);
    resized();
}

void MainPluginEditor::applyDeckToolTabVisibility (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const int tab = dc.activeToolTab;

    const bool cue  = tab == DeckControls::TabCue;
    const bool loop = tab == DeckControls::TabLoop;
    const bool fx   = tab == DeckControls::TabFx;
    const bool sync = tab == DeckControls::TabSync;
    const bool src  = tab == DeckControls::TabSrc;

    for (auto& b : dc.hotCues)       b.setVisible (cue);
    dc.loopIn.setVisible (loop); dc.loopOut.setVisible (loop); dc.loopToggle.setVisible (loop);
    dc.loopHalf.setVisible (loop); dc.loopDouble.setVisible (loop);
    for (auto& b : dc.beatJumpBack)  b.setVisible (loop);
    for (auto& b : dc.beatJumpFwd)   b.setVisible (loop);
    dc.reverbMix.setVisible (fx); dc.echoMix.setVisible (fx);
    dc.bpm.setVisible (sync); dc.beatOffset.setVisible (sync); dc.downbeat.setVisible (sync);
    dc.tempoInterpretation.setVisible (sync); dc.syncMode.setVisible (sync); dc.keyTarget.setVisible (sync);
    dc.load.setVisible (src); dc.aiStems.setVisible (src); dc.loadStem.setVisible (src); dc.stemLane.setVisible (src);
    dc.liveKey.setVisible (sync); dc.keyLock.setVisible (sync);   // key controls live in the SYNC tab
}

// ── Hot cue helpers ───────────────────────────────────────────────────────────

static const std::array<juce::Colour, 8> kHotCueColours {{
    juce::Colour (0xFFE03030), juce::Colour (0xFF3090E0), juce::Colour (0xFF30C050),
    juce::Colour (0xFFE0B020), juce::Colour (0xFFB040E0), juce::Colour (0xFFE06020),
    juce::Colour (0xFF20C8C8), juce::Colour (0xFFE040A0),
}};

void MainPluginEditor::setupHotCueButtons (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
    {
        auto& btn = dc.hotCues[(size_t) c];
        btn.setButtonText (juce::String (c + 1));
        btn.setClickingTogglesState (false);
        btn.setColour (juce::TextButton::buttonColourId,    kHotCueColours[(size_t) c].withAlpha (0.25f));
        btn.setColour (juce::TextButton::buttonOnColourId,  kHotCueColours[(size_t) c].withAlpha (0.80f));
        btn.setColour (juce::TextButton::textColourOffId,   kHotCueColours[(size_t) c]);
        btn.setColour (juce::TextButton::textColourOnId,    juce::Colours::white);
        btn.onClick = [this, deckIndex, c]
        {
            handleHotCuePress (deckIndex, c, juce::ModifierKeys::getCurrentModifiers());
        };
        addAndMakeVisible (btn);
    }
}

void MainPluginEditor::handleHotCuePress (int deckIndex, int cueIndex, juce::ModifierKeys mods)
{
    if (mods.isAltDown())
    {
        // Alt+click = clear cue
        processorRef.clearHotCue (deckIndex, cueIndex);
    }
    else if (mods.isShiftDown() || processorRef.getHotCue (deckIndex, cueIndex) < 0.0)
    {
        // Shift+click or unset cue = set at current playhead
        processorRef.setHotCue (deckIndex, cueIndex, processorRef.getDeckPlayheadSeconds (deckIndex));
    }
    else
    {
        // Normal click = jump to cue
        processorRef.jumpToHotCue (deckIndex, cueIndex);
    }
    refreshHotCueButtonColours (deckIndex);
    updateWaveformHotCues (deckIndex);
}

void MainPluginEditor::refreshHotCueButtonColours (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
    {
        const bool isSet = processorRef.getHotCue (deckIndex, c) >= 0.0;
        dc.hotCues[(size_t) c].setColour (juce::TextButton::buttonColourId,
                                          kHotCueColours[(size_t) c].withAlpha (isSet ? 0.60f : 0.18f));
    }
}

void MainPluginEditor::updateWaveformHotCues (int deckIndex)
{
    auto* wave = deckWaves[(size_t) deckIndex].get();
    if (wave == nullptr) return;
    std::array<WaveformDisplay::HotCue, WaveformDisplay::numHotCues> cues {};
    for (int c = 0; c < WaveformDisplay::numHotCues; ++c)
    {
        cues[(size_t) c].seconds = processorRef.getHotCue (deckIndex, c);
        cues[(size_t) c].colour  = kHotCueColours[(size_t) c];
    }
    wave->setHotCues (cues);
}

// ── Loop helpers ─────────────────────────────────────────────────────────────

void MainPluginEditor::setupLoopControls (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const auto accent = deckAccent (deckIndex);

    setupButton (dc.loopIn);
    dc.loopIn.onClick = [this, deckIndex]
    {
        // Set loop-in at current playhead; if loop-out already set keep it, else set 2-bar ahead
        const double pos = processorRef.getDeckPlayheadSeconds (deckIndex);
        double loopOut   = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (loopOut < pos)
        {
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            loopOut = pos + (60.0 / juce::jmax (1.0f, bpm)) * 8.0;  // 2 bars default
        }
        processorRef.setDeckLoopSeconds (deckIndex, pos, loopOut);
        processorRef.setDeckLoopActive (deckIndex, true);
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopIn);

    setupButton (dc.loopOut);
    dc.loopOut.onClick = [this, deckIndex]
    {
        const double pos    = processorRef.getDeckPlayheadSeconds (deckIndex);
        const double loopIn = juce::jmax (0.0, processorRef.getDeckLoopInSeconds (deckIndex));
        processorRef.setDeckLoopSeconds (deckIndex, loopIn, pos);
        processorRef.setDeckLoopActive (deckIndex, true);
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopOut);

    setupButton (dc.loopToggle);
    dc.loopToggle.setClickingTogglesState (false);
    dc.loopToggle.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.6f));
    dc.loopToggle.onClick = [this, deckIndex]
    {
        processorRef.setDeckLoopActive (deckIndex, ! processorRef.isDeckLoopActive (deckIndex));
        updateLoopButtonStates (deckIndex);
        updateWaveformLoopRegion (deckIndex);
    };
    addAndMakeVisible (dc.loopToggle);

    setupButton (dc.loopHalf);
    dc.loopHalf.onClick = [this, deckIndex]
    {
        const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
        const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (in >= 0.0 && out > in)
        {
            const double mid = in + (out - in) * 0.5;
            processorRef.setDeckLoopSeconds (deckIndex, in, mid);
            updateWaveformLoopRegion (deckIndex);
        }
    };
    addAndMakeVisible (dc.loopHalf);

    setupButton (dc.loopDouble);
    dc.loopDouble.onClick = [this, deckIndex]
    {
        const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
        const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
        if (in >= 0.0 && out > in)
        {
            processorRef.setDeckLoopSeconds (deckIndex, in, out + (out - in));
            updateWaveformLoopRegion (deckIndex);
        }
    };
    addAndMakeVisible (dc.loopDouble);
}

void MainPluginEditor::updateLoopButtonStates (int deckIndex)
{
    const bool active = processorRef.isDeckLoopActive (deckIndex);
    deckControls[(size_t) deckIndex].loopToggle.setToggleState (active, juce::dontSendNotification);
}

void MainPluginEditor::updateWaveformLoopRegion (int deckIndex)
{
    auto* wave = deckWaves[(size_t) deckIndex].get();
    if (wave == nullptr) return;
    const double in  = processorRef.getDeckLoopInSeconds (deckIndex);
    const double out = processorRef.getDeckLoopOutSeconds (deckIndex);
    const bool active = processorRef.isDeckLoopActive (deckIndex);
    if (in >= 0.0 && out > in)
        wave->setLoopRegion (in, out, active);
    else
        wave->clearLoopRegion();
}

// ── Camelot wheel popup ───────────────────────────────────────────────────────

void MainPluginEditor::showCamelotWheel (int /*deckIndex*/)
{
    // 12 keys × 2 (major/minor) shown in a grid popup
    // Columns: C Db D Eb E F F# G Ab A Bb B
    static const char* const kKeys[] = {
        "C","Db","D","Eb","E","F","F#","G","Ab","A","Bb","B"
    };
    static const int kCamelotMaj[] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };   // camelot number
    static const int kCamelotMin[] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };

    juce::PopupMenu menu;
    menu.addSectionHeader ("Camelot Key Wheel");
    for (int k = 0; k < 12; ++k)
    {
        menu.addItem (k + 1,
                      juce::String (kCamelotMaj[k]) + "B  " + kKeys[k] + " maj   |   "
                      + juce::String (kCamelotMin[k]) + "A  " + kKeys[k] + " min");
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (camelotButton), nullptr);
}

// ── Library meta info ─────────────────────────────────────────────────────────

void MainPluginEditor::showLibraryMeta (const juce::File& file)
{
    if (! file.existsAsFile()) { libraryMetaLabel.setText ({}, juce::dontSendNotification); return; }
    // Show what we know without full analysis (duration via thumbnail if loaded)
    libraryMetaLabel.setText (file.getFileName() + "  |  " + file.getFileExtension().toUpperCase(),
                               juce::dontSendNotification);
}

// ── MIDI learn — real controller mapping ───────────────────────────────────────

void MainPluginEditor::updateMidiLearnUI()
{
    auto& midi = processorRef.getMidiMapping();

    // If a learn is already in progress, a second click cancels it.
    if (midi.isLearning())
    {
        midi.cancelLearn();
        libraryMetaLabel.setText ("MIDI learn cancelled", juce::dontSendNotification);
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (u8 ("MIDI Learn  (enable controller in \xe2\x9a\x99 Options first)"));

    constexpr int kCcBase   = 1;
    constexpr int kNoteBase = 1000;
    constexpr int kClearAll = 9999;

    juce::PopupMenu cont;
    for (int i = 0; i < midi.numCc(); ++i)
    {
        const int cc = midi.ccForTarget (i);
        cont.addItem (kCcBase + i,
                      midi.ccName (i) + (cc >= 0 ? "   [CC " + juce::String (cc) + "]" : u8 ("   [\xe2\x80\x94]")));
    }
    menu.addSubMenu ("Continuous  (move a knob / fader)", cont);

    juce::PopupMenu trig;
    for (int i = 0; i < midi.numNote(); ++i)
    {
        const int n = midi.noteForTarget (i);
        trig.addItem (kNoteBase + i,
                      midi.noteName (i) + (n >= 0 ? "   [Note " + juce::String (n) + "]" : u8 ("   [\xe2\x80\x94]")));
    }
    menu.addSubMenu ("Triggers  (press a pad / button)", trig);

    menu.addSeparator();
    menu.addItem (kClearAll, "Clear all MIDI mappings");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiLearnButton),
        [this, kCcBase, kNoteBase, kClearAll] (int result)
        {
            if (result <= 0) return;
            auto& m = processorRef.getMidiMapping();

            if (result == kClearAll)
            {
                m.clearAll();
                libraryMetaLabel.setText ("All MIDI mappings cleared", juce::dontSendNotification);
                return;
            }

            if (result >= kNoteBase)
            {
                const int idx = result - kNoteBase;
                m.beginLearnNote (idx);
                libraryMetaLabel.setText ("MIDI learn: press a pad for \"" + m.noteName (idx) + u8 ("\"\xe2\x80\xa6"),
                                          juce::dontSendNotification);
            }
            else
            {
                const int idx = result - kCcBase;
                m.beginLearnCc (idx);
                libraryMetaLabel.setText ("MIDI learn: move a control for \"" + m.ccName (idx) + u8 ("\"\xe2\x80\xa6"),
                                          juce::dontSendNotification);
            }
            midiLearnButton.setButtonText (u8 ("LEARN\xe2\x80\xa6"));
        });
}
