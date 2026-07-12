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

// Draw uppercase micro-labels with even letter-spacing — the single biggest lever
// for a precise, "engineered" type feel. Centres the tracked string in `area`.
void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                  float tracking, juce::Justification just = juce::Justification::centred)
{
    const auto& f = g.getCurrentFont();
    float total = -tracking;
    for (int i = 0; i < text.length(); ++i)
        total += f.getStringWidthFloat (text.substring (i, i + 1)) + tracking;

    const auto a = area.toFloat();
    float x = just.testFlags (juce::Justification::left)  ? a.getX()
            : just.testFlags (juce::Justification::right) ? a.getRight() - total
            : a.getCentreX() - total * 0.5f;
    for (int i = 0; i < text.length(); ++i)
    {
        const juce::String ch = text.substring (i, i + 1);
        const float w = f.getStringWidthFloat (ch);
        g.drawText (ch, juce::Rectangle<float> (x, a.getY(), w + 2.0f, a.getHeight()),
                    juce::Justification::centredLeft, false);
        x += w + tracking;
    }
}

// Fade an overlay view in/out (160/130 ms) instead of snapping — used by the
// MIXER and ARRANGER overlays so view changes feel composed, not abrupt.
void fadeOverlay (juce::Component& c, bool show)
{
    auto& anim = juce::Desktop::getInstance().getAnimator();
    if (show)
    {
        c.setAlpha (0.0f);
        anim.fadeIn (&c, 160);
    }
    else if (c.isVisible())
    {
        anim.fadeOut (&c, 130);
    }
}

// Performance-pad spectrum (cyan → green → blue → violet → gold → orange → red → pink).
juce::uint32 padSpectrum (int i)
{
    static const std::array<juce::uint32, 8> cols {
        0xff26d9ff, 0xff62f5c8, 0xff4a90e2, 0xff9b7bff,
        0xffffb020, 0xffe0742b, 0xffe03030, 0xffd44fc8 };
    return cols[(size_t) juce::jlimit (0, 7, i % 8)];
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour accent,
                float corner = 10.0f, bool active = false)
{
    namespace sdp = stemdeck::palette;
    const auto r = bounds.toFloat();

    // Flat matte card.
    g.setColour (juce::Colour (sdp::kPanel));
    g.fillRoundedRectangle (r, corner);

    // Single border: neutral hairline at rest, accent only when active. No sheen.
    if (active)
    {
        g.setColour (accent.withAlpha (0.80f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);
    }
    else
    {
        g.setColour (juce::Colour (sdp::kHairline));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
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

        const int labelWidth = 118;
        auto track = laneRect.withTrimmedLeft (labelWidth).reduced (4, 3);
        auto timeToX = [&] (double t) -> int { return timeToXIn (track, t); };
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
    const int labelWidth = 118;
    const auto track = laneRects[(size_t) laneIndex].withTrimmedLeft (labelWidth).reduced (4, 3);
    if (track.getWidth() <= 0) return 0.0;
    return juce::jlimit (0.0, maxLen, xToTimeIn (track, x));
}

float MainPluginEditor::ArrangementRecorderView::automationValueAt (int lane, double seconds, int& targetOut) const
{
    targetOut = -1;
    if (! juce::isPositiveAndBelow (lane, numLanes))
        return -1.0f;
    const auto& a = editAuto[(size_t) lane];
    if (! a.visible || a.points.empty())
        return -1.0f;

    targetOut = a.target;
    if (seconds <= a.points.front().first)  return a.points.front().second;
    if (seconds >= a.points.back().first)   return a.points.back().second;
    for (size_t i = 1; i < a.points.size(); ++i)
    {
        if (seconds <= a.points[i].first)
        {
            const auto& [t0, v0] = a.points[i - 1];
            const auto& [t1, v1] = a.points[i];
            const double span = juce::jmax (1.0e-9, t1 - t0);
            return (float) juce::jmap ((seconds - t0) / span, (double) v0, (double) v1);
        }
    }
    return a.points.back().second;
}

void MainPluginEditor::ArrangementRecorderView::mouseDown (const juce::MouseEvent& e)
{
    if (scrollThumbBounds.contains (e.getPosition()) || scrollTrackBounds.contains (e.getPosition()))
    {
        draggingScrollBar = true;
        if (! scrollThumbBounds.contains (e.getPosition()))   // clicked the track: center the window on the click
        {
            const double content = timelineContentSeconds();
            const double frac = juce::jlimit (0.0, 1.0,
                (double) (e.getPosition().x - scrollTrackBounds.getX()) / (double) juce::jmax (1, scrollTrackBounds.getWidth()));
            viewStartSeconds = frac * content - visibleSeconds() * 0.5;
            clampView();
        }
        scrollDragAnchorX = e.getPosition().x;
        scrollDragAnchorView = viewStartSeconds;
        repaint();
        return;
    }

    if (rulerBounds.contains (e.getPosition()))   // ruler: click to seek, drag to set a loop region
    {
        const double t = juce::jlimit (0.0, timelineContentSeconds(), xToTimeIn (timelineMapRect, e.getPosition().x));
        cursorSeconds = t;
        if (onSeek) onSeek (t);
        rulerDragActive = true;
        loopAnchorSeconds = t;
        rulerDownX = e.getPosition().x;
        repaint();
        return;
    }

    for (int i = 0; i < numLanes; ++i)   // M / S / A toggles in the deck headers
    {
        if (muteBtnRects[(size_t) i].contains (e.getPosition())) { laneMuted[(size_t) i]  = ! laneMuted[(size_t) i];  if (onLaneMixChanged) onLaneMixChanged(); repaint(); return; }
        if (soloBtnRects[(size_t) i].contains (e.getPosition())) { laneSoloed[(size_t) i] = ! laneSoloed[(size_t) i]; if (onLaneMixChanged) onLaneMixChanged(); repaint(); return; }
        if (autoBtnRects[(size_t) i].contains (e.getPosition())) { editAuto[(size_t) i].visible = ! editAuto[(size_t) i].visible; repaint(); return; }
        if (editAuto[(size_t) i].visible && autoTargetRects[(size_t) i].contains (e.getPosition()))
        {
            editAuto[(size_t) i].target = (editAuto[(size_t) i].target + 1) % numAutoTargets;
            repaint();
            return;
        }
    }

    // Automation editing takes over a lane while its curve is visible: click adds
    // a point, dragging an existing point moves it, right-click deletes it.
    for (int i = 0; i < numLanes; ++i)
    {
        auto& a = editAuto[(size_t) i];
        if (! a.visible) continue;
        const int labelWidth = 118;
        const auto track = laneRects[(size_t) i].withTrimmedLeft (labelWidth).reduced (4, 3);
        if (! track.contains (e.getPosition())) continue;

        auto pointPos = [&] (const std::pair<double, float>& p) -> juce::Point<int>
        {
            return { timeToXIn (track, p.first),
                     track.getBottom() - (int) (juce::jlimit (0.0f, 1.0f, p.second) * (float) track.getHeight()) };
        };

        // Hit an existing point?
        for (size_t pi = 0; pi < a.points.size(); ++pi)
        {
            if (pointPos (a.points[pi]).getDistanceFrom (e.getPosition()) <= 7)
            {
                if (e.mods.isPopupMenu())
                {
                    commitEdit (captureFullState());   // point delete is undoable
                    a.points.erase (a.points.begin() + (long) pi);
                    repaint();
                    return;
                }
                pendingEditSnapshot = captureFullState();   // point move is undoable
                dragAutoLane = i;
                dragAutoPointIdx = (int) pi;
                return;
            }
        }
        if (e.mods.isPopupMenu())
            return;

        // Add a new point where clicked, then drag it.
        pendingEditSnapshot = captureFullState();   // point add is undoable
        const double t = timeAtX (i, e.getPosition().x);
        const float  v = juce::jlimit (0.0f, 1.0f,
                          (float) (track.getBottom() - e.getPosition().y) / (float) juce::jmax (1, track.getHeight()));
        auto insertAt = std::upper_bound (a.points.begin(), a.points.end(), std::pair<double, float> (t, v),
                                          [] (const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        dragAutoPointIdx = (int) std::distance (a.points.begin(), insertAt);
        a.points.insert (insertAt, { t, v });
        dragAutoLane = i;
        repaint();
        return;
    }

    // MIDI clip blocks: right-click deletes, drag moves (and can change lane).
    if (const auto mref = midiClipAt (e.getPosition()); mref.valid())
    {
        if (e.mods.isPopupMenu())
        {
            commitEdit (captureFullState());   // snapshot before delete (undoable)
            midiClips[(size_t) mref.lane].erase (midiClips[(size_t) mref.lane].begin() + mref.clip);
            repaint();
            return;
        }
        activeMidiDrag      = mref;
        dragOrigLane        = mref.lane;
        dragStartPoint      = e.getPosition();
        dragStartMidiStart  = midiClips[(size_t) mref.lane][(size_t) mref.clip].timelineStartSeconds;
        pendingEditSnapshot = captureFullState();   // snapshot before drag (undoable)
        return;
    }

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

            const std::array<juce::uint32, 8> clipPalette {
                0xffe2564f, 0xffe08a3a, 0xffd9c84a, 0xff5fae6e,
                0xff4fb6c9, 0xff5b8def, 0xff9a6bff, 0xffd86bb0 };
            const juce::String currentClipName = overClip
                ? (clips[(size_t) lane][(size_t) clipIdx].label.isNotEmpty()
                       ? clips[(size_t) lane][(size_t) clipIdx].label
                       : sourceFile.getFileNameWithoutExtension())
                : juce::String();

            juce::PopupMenu menu;
            menu.addSectionHeader (lane == masterLane
                                      ? juce::String ("Master lane")
                                      : "Deck " + juce::String::charToString ((juce_wchar) ('A' + lane)));
            menu.addItem (6, "Split clip here", hasClip);
            menu.addItem (8, "Duplicate clip", overClip);
            menu.addItem (9, u8 ("Rename clip\xe2\x80\xa6"), overClip);
            menu.addItem (7, "Delete this clip", overClip && clips[(size_t) lane].size() > 1);
            {
                juce::PopupMenu colourMenu;
                colourMenu.addItem (128, "Default (lane colour)");
                static const char* const names[] = { "Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink" };
                for (int i = 0; i < 8; ++i)
                    colourMenu.addItem (120 + i, names[i]);
                menu.addSubMenu ("Clip colour", colourMenu, overClip);
            }
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
            if (overClip)
            {
                const bool isLocked = clips[(size_t) lane][(size_t) clipIdx].locked;
                menu.addSeparator();
                menu.addItem (12, isLocked ? u8 ("\xf0\x9f\x94\x93 Unlock clip") : u8 ("\xf0\x9f\x94\x92 Lock clip"));
                menu.addItem (13, "Follow action...");
            }
            if (lane != masterLane)
            {
                menu.addSeparator();
                menu.addItem (10, u8 ("Launch at next beat  \xe2\x96\xb6"), hasClip);
                menu.addItem (11, "Stop deck && launcher", slotStates[(size_t) lane] != SlotState::Idle);
            }
            menu.addSeparator();
            menu.addItem (4, "Undo", canUndo());
            menu.addItem (5, "Redo", canRedo());

            const juce::Component::SafePointer<ArrangementRecorderView> safeThis (this);
            const double splitTime = timeAtX (lane, e.getPosition().x);
            menu.showMenuAsync (juce::PopupMenu::Options(),
                [safeThis, lane, clipIdx, splitTime, sourceFile, clipPalette, currentClipName] (int choice)
                {
                    if (safeThis == nullptr) return;
                    auto& self = *safeThis;
                    switch (choice)
                    {
                        case 6: self.splitClipAt (lane, clipIdx, splitTime); break;
                        case 7: self.deleteClip (lane, clipIdx); break;
                        case 8: self.duplicateClip (lane, clipIdx); break;
                        case 9:
                        {
                            auto* w = new juce::AlertWindow ("Rename clip", "Display name:",
                                                             juce::MessageBoxIconType::NoIcon);
                            w->addTextEditor ("name", currentClipName);
                            w->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
                            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                            w->enterModalState (true, juce::ModalCallbackFunction::create (
                                [safeThis, lane, clipIdx, w] (int r)
                                {
                                    if (r == 1 && safeThis != nullptr)
                                        safeThis->setClipLabel (lane, clipIdx, w->getTextEditorContents ("name").trim());
                                    delete w;
                                }), false);
                            break;
                        }
                        case 1: self.resetClipEdits (lane); break;
                        case 2: if (sourceFile.existsAsFile()) sourceFile.revealToUser(); break;
                        case 3: if (self.onClearDeckRequest) self.onClearDeckRequest (lane); break;
                        case 4: self.undo(); break;
                        case 5: self.redo(); break;
                        case 10: self.triggerSlot (lane, clipIdx); break;
                        case 11:
                            // Stop = reset the launcher AND actually stop the deck.
                            self.setSlotState (lane, SlotState::Idle);
                            if (self.onStopDeckRequest) self.onStopDeckRequest (lane);
                            break;
                        case 12:
                            if (juce::isPositiveAndBelow (clipIdx, (int) self.clips[(size_t) lane].size()))
                                self.setClipLocked (lane, clipIdx, ! self.clips[(size_t) lane][(size_t) clipIdx].locked);
                            break;
                        case 13:
                            if (self.onFollowActionRequest)
                                self.onFollowActionRequest (lane, clipIdx);
                            break;
                        default:
                            if      (choice == 128)                       self.setClipColour (lane, clipIdx, 0);
                            else if (choice >= 120 && choice < 128)       self.setClipColour (lane, clipIdx, clipPalette[(size_t) (choice - 120)]);
                            else if (choice >= 100 && choice < 100 + numLanes) self.copyClip (lane, choice - 100);
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

    // Locked clip: allow selection but block all drag operations. Clicking the
    // amber "L" badge itself (top-right of the clip) unlocks — the badge is a
    // real control, not just an indicator.
    if (hit.clip >= 0
        && juce::isPositiveAndBelow (hit.clip, (int) clips[(size_t) hit.lane].size())
        && clips[(size_t) hit.lane][(size_t) hit.clip].locked)
    {
        const auto& clip = clips[(size_t) hit.lane][(size_t) hit.clip];
        const auto track = laneRects[(size_t) hit.lane].withTrimmedLeft (118).reduced (4, 3);
        const int clipX1 = timeToXIn (track, clip.timelineEndSeconds());
        const juce::Rectangle<int> badgeZone (clipX1 - 20, track.getY(), 20, 16);
        if (badgeZone.contains (e.getPosition()))
        {
            setClipLocked (hit.lane, hit.clip, false);
            return;
        }
        if (selectedLane != hit.lane || selectedClip != hit.clip)
        {
            selectedLane = hit.lane;
            selectedClip = hit.clip;
            repaint();
        }
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
    // Every clip-targeted handle snapshots for undo. Body drags can cross lanes, so
    // they snapshot the whole arrangement; other handles snapshot just their lane.
    dragOrigLane = activeDrag.lane;
    pendingEditSnapshot = (activeDrag.handle == Handle::Body) ? captureFullState()
                                                             : captureLaneState (activeDrag.lane);
}

void MainPluginEditor::ArrangementRecorderView::mouseDrag (const juce::MouseEvent& e)
{
    // Automation point drag: clamp between neighbouring points so order holds.
    if (dragAutoLane >= 0 && juce::isPositiveAndBelow (dragAutoLane, numLanes))
    {
        auto& a = editAuto[(size_t) dragAutoLane];
        if (juce::isPositiveAndBelow (dragAutoPointIdx, (int) a.points.size()))
        {
            const int labelWidth = 118;
            const auto track = laneRects[(size_t) dragAutoLane].withTrimmedLeft (labelWidth).reduced (4, 3);
            double t = timeAtX (dragAutoLane, e.getPosition().x);
            const double lo = dragAutoPointIdx > 0 ? a.points[(size_t) (dragAutoPointIdx - 1)].first + 0.01 : 0.0;
            const double hi = dragAutoPointIdx + 1 < (int) a.points.size()
                            ? a.points[(size_t) (dragAutoPointIdx + 1)].first - 0.01
                            : computeMaxLengthSeconds() + 60.0;
            t = juce::jlimit (lo, juce::jmax (lo, hi), t);
            const float v = juce::jlimit (0.0f, 1.0f,
                             (float) (track.getBottom() - e.getPosition().y) / (float) juce::jmax (1, track.getHeight()));
            a.points[(size_t) dragAutoPointIdx] = { t, v };
            repaint();
        }
        return;
    }

    if (activeMidiDrag.valid())
    {
        // Same label-column width as hit-test/paint (118) — a mismatched 90 here
        // made dragged MIDI clips drift ~15% against the cursor.
        const int trackW = juce::jmax (1, laneRects[(size_t) activeMidiDrag.lane].getWidth() - 118 - 8);
        const double secondsPerPixel = visibleSeconds() / (double) trackW;
        const double newStart = juce::jmax (0.0, snapTime (dragStartMidiStart
                                              + (double) (e.getPosition().x - dragStartPoint.x) * secondsPerPixel));
        int targetLane = laneAtY (e.getPosition().y);
        if (! juce::isPositiveAndBelow (targetLane, numEditableLanes)) targetLane = activeMidiDrag.lane;
        if (targetLane != activeMidiDrag.lane)   // move the clip to another lane
        {
            auto mc = midiClips[(size_t) activeMidiDrag.lane][(size_t) activeMidiDrag.clip];
            midiClips[(size_t) activeMidiDrag.lane].erase (midiClips[(size_t) activeMidiDrag.lane].begin() + activeMidiDrag.clip);
            midiClips[(size_t) targetLane].push_back (mc);
            activeMidiDrag.lane = targetLane;
            activeMidiDrag.clip = (int) midiClips[(size_t) targetLane].size() - 1;
        }
        midiClips[(size_t) activeMidiDrag.lane][(size_t) activeMidiDrag.clip].timelineStartSeconds = newStart;
        dragReadout = "MIDI clip: " + juce::String (newStart, 2) + " s";
        repaint();
        return;
    }

    if (rulerDragActive)
    {
        if (std::abs (e.getPosition().x - rulerDownX) > 4)   // a real drag → define a loop region
        {
            const double t = juce::jlimit (0.0, timelineContentSeconds(), xToTimeIn (timelineMapRect, e.getPosition().x));
            loopStartSeconds = juce::jmin (loopAnchorSeconds, t);
            loopEndSeconds   = juce::jmax (loopAnchorSeconds, t);
            loopEnabled = loopEndSeconds - loopStartSeconds > 1.0e-4;
            if (onLoopChanged) onLoopChanged (loopStartSeconds, loopEndSeconds, loopEnabled);
            repaint();
        }
        return;
    }

    if (draggingScrollBar)
    {
        const double content = timelineContentSeconds();
        const double perPixel = scrollTrackBounds.getWidth() > 0 ? content / (double) scrollTrackBounds.getWidth() : 0.0;
        viewStartSeconds = scrollDragAnchorView + (double) (e.getPosition().x - scrollDragAnchorX) * perPixel;
        clampView();
        repaint();
        return;
    }

    if (activeDrag.lane < 0 || activeDrag.handle == Handle::None)
        return;
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen <= 0.0)
        return;
    int laneIdx = activeDrag.lane;
    if (activeDrag.clip < 0 || activeDrag.clip >= (int) clips[(size_t) laneIdx].size())
        return;

    // Body drag can move the clip to another lane (follow the cursor's y).
    if (activeDrag.handle == Handle::Body)
    {
        const int targetLane = laneAtY (e.getPosition().y);
        if (targetLane >= 0 && targetLane < numEditableLanes && targetLane != laneIdx)
        {
            LaneClip moved = clips[(size_t) laneIdx][(size_t) activeDrag.clip];
            clips[(size_t) laneIdx].erase (clips[(size_t) laneIdx].begin() + activeDrag.clip);
            clips[(size_t) targetLane].push_back (moved);

            // Give the moved clip its own thumbnail so its waveform follows it across lanes.
            const auto key = moved.file.getFullPathName();
            if (moved.file.existsAsFile() && formatManager != nullptr && thumbnailCache != nullptr
                && clipThumbs.find (key) == clipThumbs.end())
            {
                auto t = std::make_unique<juce::AudioThumbnail> (512, *formatManager, *thumbnailCache);
                t->addChangeListener (this);
                t->setSource (new juce::FileInputSource (moved.file));
                clipThumbs[key] = std::move (t);
            }

            laneIdx          = targetLane;
            activeDrag.lane  = targetLane;
            activeDrag.clip  = (int) clips[(size_t) targetLane].size() - 1;
            selectedLane     = targetLane;
            selectedClip     = activeDrag.clip;
        }
    }

    const auto laneRect = laneRects[(size_t) laneIdx];
    const int labelWidth = 118;
    const int trackWidth = juce::jmax (1, laneRect.getWidth() - labelWidth - 8);
    const double secondsPerPixel = visibleSeconds() / (double) trackWidth;
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
    if (dragAutoLane >= 0)
    {
        // Automation edit finished — push the pre-edit snapshot so it is undoable
        // like every other timeline edit.
        if (pendingEditSnapshot.full)
            commitEdit (std::move (pendingEditSnapshot));
        pendingEditSnapshot = {};
        dragAutoLane = -1;
        dragAutoPointIdx = -1;
        repaint();
        return;
    }

    if (activeMidiDrag.valid())
    {
        bool changed = (activeMidiDrag.lane != dragOrigLane);
        if (! changed && juce::isPositiveAndBelow (activeMidiDrag.clip, (int) midiClips[(size_t) activeMidiDrag.lane].size()))
            changed = std::abs (midiClips[(size_t) activeMidiDrag.lane][(size_t) activeMidiDrag.clip].timelineStartSeconds
                                - dragStartMidiStart) > 1.0e-9;
        if (changed && pendingEditSnapshot.full)
            commitEdit (std::move (pendingEditSnapshot));
        pendingEditSnapshot = {};
        activeMidiDrag = {};
        dragOrigLane = -1;
        dragReadout.clear();
        repaint();
        return;
    }
    if (rulerDragActive) { rulerDragActive = false; return; }
    if (draggingScrollBar) { draggingScrollBar = false; repaint(); return; }

    if (pendingEditSnapshot.full)   // Body drag (possibly cross-lane)
    {
        bool changed = (activeDrag.lane != dragOrigLane);
        if (! changed && activeDrag.lane >= 0 && activeDrag.clip >= 0
            && activeDrag.clip < (int) clips[(size_t) activeDrag.lane].size())
            changed = std::abs (clips[(size_t) activeDrag.lane][(size_t) activeDrag.clip].timelineStartSeconds
                                - dragStartValue) > 1.0e-9;
        if (changed)
            commitEdit (std::move (pendingEditSnapshot));
    }
    else if (pendingEditSnapshot.lane >= 0 && activeDrag.clip >= 0)
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
    dragOrigLane = -1;
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

juce::AudioThumbnail* MainPluginEditor::ArrangementRecorderView::thumbForClip (int laneIndex, const LaneClip& clip) const
{
    const auto it = clipThumbs.find (clip.file.getFullPathName());
    if (it != clipThumbs.end())
        return it->second.get();
    return thumbForLane (laneIndex);   // recorded / split clips share the lane thumbnail
}

void MainPluginEditor::ArrangementRecorderView::ensureClipThumb (const juce::File& file)
{
    if (! file.existsAsFile() || formatManager == nullptr || thumbnailCache == nullptr) return;
    const auto key = file.getFullPathName();
    if (clipThumbs.find (key) != clipThumbs.end()) return;
    auto t = std::make_unique<juce::AudioThumbnail> (512, *formatManager, *thumbnailCache);
    t->addChangeListener (this);
    t->setSource (new juce::FileInputSource (file));
    clipThumbs[key] = std::move (t);
}

juce::ValueTree MainPluginEditor::ArrangementRecorderView::toState() const
{
    juce::ValueTree root ("ARRANGEMENT");
    root.setProperty ("loopStart",   loopStartSeconds, nullptr);
    root.setProperty ("loopEnd",     loopEndSeconds,   nullptr);
    root.setProperty ("loopEnabled", loopEnabled,      nullptr);
    for (int lane = 0; lane < numEditableLanes; ++lane)
    {
        juce::ValueTree lt ("LANE");
        lt.setProperty ("index",  lane,                   nullptr);
        lt.setProperty ("muted",  laneMuted[(size_t) lane],  nullptr);
        lt.setProperty ("soloed", laneSoloed[(size_t) lane], nullptr);
        lt.setProperty ("gainDb", (double) laneGainDb[(size_t) lane], nullptr);
        for (const auto& c : clips[(size_t) lane])
        {
            juce::ValueTree ct ("CLIP");
            ct.setProperty ("file",      c.file.getFullPathName(), nullptr);
            ct.setProperty ("dur",       c.durationSeconds,        nullptr);
            ct.setProperty ("trimStart", c.trimStartSeconds,       nullptr);
            ct.setProperty ("trimEnd",   c.trimEndSeconds,         nullptr);
            ct.setProperty ("tlStart",   c.timelineStartSeconds,   nullptr);
            ct.setProperty ("fadeIn",    c.fadeInSeconds,          nullptr);
            ct.setProperty ("fadeOut",   c.fadeOutSeconds,         nullptr);
            ct.setProperty ("label",     c.label,                  nullptr);
            ct.setProperty ("colour",    (int) c.colourArgb,       nullptr);
            if (c.locked)      ct.setProperty ("locked",    true,               nullptr);
            if (c.followAction != FollowAction::None)
                               ct.setProperty ("followAct", (int) c.followAction, nullptr);
            if (c.followProbability < 0.999f)
                               ct.setProperty ("followProb", c.followProbability, nullptr);
            lt.appendChild (ct, nullptr);
        }
        for (const auto& mc : midiClips[(size_t) lane])
        {
            juce::ValueTree mt ("MIDICLIP");
            mt.setProperty ("tlStart", mc.timelineStartSeconds, nullptr);
            for (const auto& nn : mc.notes)
            {
                juce::ValueTree nt ("MNOTE");
                nt.setProperty ("pitch", nn.pitch,       nullptr);
                nt.setProperty ("start", nn.startBeats,  nullptr);
                nt.setProperty ("len",   nn.lengthBeats, nullptr);
                nt.setProperty ("vel",   nn.velocity,    nullptr);
                mt.appendChild (nt, nullptr);
            }
            lt.appendChild (mt, nullptr);
        }
        if (lane < numLanes && ! editAuto[(size_t) lane].points.empty())
        {
            juce::ValueTree at ("EDITAUTO");
            at.setProperty ("target",  editAuto[(size_t) lane].target,  nullptr);
            at.setProperty ("visible", editAuto[(size_t) lane].visible, nullptr);
            for (const auto& [t, v] : editAuto[(size_t) lane].points)
            {
                juce::ValueTree pt ("AP");
                pt.setProperty ("t", t, nullptr);
                pt.setProperty ("v", (double) v, nullptr);
                at.appendChild (pt, nullptr);
            }
            lt.appendChild (at, nullptr);
        }
        root.appendChild (lt, nullptr);
    }
    return root;
}

void MainPluginEditor::ArrangementRecorderView::fromState (const juce::ValueTree& root)
{
    if (! root.hasType ("ARRANGEMENT")) return;
    for (auto& v : clips) v.clear();
    for (auto& m : midiClips) m = {};
    loopStartSeconds = root.getProperty ("loopStart", 0.0);
    loopEndSeconds   = root.getProperty ("loopEnd",   0.0);
    loopEnabled      = root.getProperty ("loopEnabled", false);

    bool anyClip = false;
    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        const auto lt = root.getChild (i);
        if (! lt.hasType ("LANE")) continue;
        const int lane = (int) lt.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (lane, numEditableLanes)) continue;
        laneMuted[(size_t) lane]  = lt.getProperty ("muted",  false);
        laneSoloed[(size_t) lane] = lt.getProperty ("soloed", false);
        laneGainDb[(size_t) lane] = (float) (double) lt.getProperty ("gainDb", 0.0);
        for (int j = 0; j < lt.getNumChildren(); ++j)
        {
            const auto ct = lt.getChild (j);
            if (! ct.hasType ("CLIP")) continue;
            LaneClip c;
            c.file                 = juce::File (ct.getProperty ("file").toString());
            c.durationSeconds      = ct.getProperty ("dur",       0.0);
            c.trimStartSeconds     = ct.getProperty ("trimStart", 0.0);
            c.trimEndSeconds       = ct.getProperty ("trimEnd",   0.0);
            c.timelineStartSeconds = ct.getProperty ("tlStart",   0.0);
            c.fadeInSeconds        = ct.getProperty ("fadeIn",    0.0);
            c.fadeOutSeconds       = ct.getProperty ("fadeOut",   0.0);
            c.label                = ct.getProperty ("label").toString();
            c.colourArgb           = (juce::uint32) (int) ct.getProperty ("colour", 0);
            c.locked               = (bool) ct.getProperty ("locked", false);
            c.followAction         = (FollowAction) (int) ct.getProperty ("followAct",  0);
            c.followProbability    = (float) ct.getProperty ("followProb", 1.0f);
            ensureClipThumb (c.file);
            clips[(size_t) lane].push_back (c);
            anyClip = true;
        }
        for (int kc = 0; kc < lt.getNumChildren(); ++kc)
        {
            const auto mt = lt.getChild (kc);
            if (! mt.hasType ("MIDICLIP")) continue;
            MidiClipData mc;
            mc.active = true;
            mc.timelineStartSeconds = mt.getProperty ("tlStart", 0.0);
            for (int k = 0; k < mt.getNumChildren(); ++k)
            {
                const auto nt = mt.getChild (k);
                if (! nt.hasType ("MNOTE")) continue;
                MidiNote nn;
                nn.pitch       = nt.getProperty ("pitch", 60);
                nn.startBeats  = nt.getProperty ("start", 0.0);
                nn.lengthBeats = nt.getProperty ("len",   1.0);
                nn.velocity    = nt.getProperty ("vel",   100);
                mc.notes.push_back (nn);
            }
            midiClips[(size_t) lane].push_back (std::move (mc));
            anyClip = true;
        }
        if (lane < numLanes)
        {
            const auto at = lt.getChildWithName ("EDITAUTO");
            auto& a = editAuto[(size_t) lane];
            a.points.clear();
            if (at.isValid())
            {
                a.target  = juce::jlimit (0, numAutoTargets - 1, (int) at.getProperty ("target", 0));
                a.visible = (bool) at.getProperty ("visible", false);
                for (int kp = 0; kp < at.getNumChildren(); ++kp)
                {
                    const auto pt = at.getChild (kp);
                    if (pt.hasType ("AP"))
                        a.points.emplace_back ((double) pt.getProperty ("t", 0.0),
                                               (float) (double) pt.getProperty ("v", 0.0));
                }
            }
            else
            {
                a.visible = false;
            }
        }
    }
    mode = anyClip ? Mode::Finished : Mode::Idle;
    undoStack.clear();
    redoStack.clear();
    selectedLane = selectedClip = -1;
    repaint();
}

int MainPluginEditor::ArrangementRecorderView::laneAtY (int y) const
{
    for (int i = 0; i < numEditableLanes; ++i)
        if (y >= laneRects[(size_t) i].getY() && y < laneRects[(size_t) i].getBottom())
            return i;
    return -1;
}

// ── Per-lane MIDI clips (multiple per lane, draggable) ─────────────────────────
double MainPluginEditor::ArrangementRecorderView::midiClipStart (int lane, int idx) const noexcept
{
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return 0.0;
    if (! juce::isPositiveAndBelow (idx, (int) midiClips[(size_t) lane].size())) return 0.0;
    return midiClips[(size_t) lane][(size_t) idx].timelineStartSeconds;
}

const std::vector<MainPluginEditor::ArrangementRecorderView::MidiNote>&
MainPluginEditor::ArrangementRecorderView::midiClipNotes (int lane, int idx) const
{
    static const std::vector<MidiNote> empty;
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return empty;
    if (! juce::isPositiveAndBelow (idx, (int) midiClips[(size_t) lane].size())) return empty;
    return midiClips[(size_t) lane][(size_t) idx].notes;
}

int MainPluginEditor::ArrangementRecorderView::addMidiClip (int lane, double timeSec)
{
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return -1;
    MidiClipData mc;
    mc.active = true;
    mc.timelineStartSeconds = juce::jmax (0.0, snapTime (timeSec));
    midiClips[(size_t) lane].push_back (std::move (mc));
    mode = Mode::Finished;
    repaint();
    return (int) midiClips[(size_t) lane].size() - 1;
}

void MainPluginEditor::ArrangementRecorderView::setMidiClipNotes (int lane, int idx, std::vector<MidiNote> notes)
{
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return;
    if (! juce::isPositiveAndBelow (idx, (int) midiClips[(size_t) lane].size())) return;
    midiClips[(size_t) lane][(size_t) idx].notes = std::move (notes);
    repaint();
}

MainPluginEditor::ArrangementRecorderView::MidiRef
MainPluginEditor::ArrangementRecorderView::midiClipAt (juce::Point<int> p) const
{
    const int lane = laneAtY (p.y);
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return {};
    const double bpm = juce::jlimit (40.0, 240.0, snapBpm > 0.0 ? snapBpm : 120.0);
    const double spb = 60.0 / bpm;
    const int labelWidth = 118;
    const auto track = laneRects[(size_t) lane].withTrimmedLeft (labelWidth).reduced (4, 3);
    const auto& vec = midiClips[(size_t) lane];
    for (int ci = (int) vec.size() - 1; ci >= 0; --ci)   // topmost (last drawn) first
    {
        const auto& mc = vec[(size_t) ci];
        double lenBeats = 4.0;
        for (const auto& nn : mc.notes) lenBeats = juce::jmax (lenBeats, nn.startBeats + nn.lengthBeats);
        const int x0 = timeToXIn (track, mc.timelineStartSeconds);
        const int x1 = juce::jmax (x0 + 8, timeToXIn (track, mc.timelineStartSeconds + lenBeats * spb));
        if (p.x >= x0 && p.x <= x1) return { lane, ci };
    }
    return {};
}

void MainPluginEditor::ArrangementRecorderView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto ref = midiClipAt (e.getPosition());
    if (ref.valid()) { if (onEditMidiClip) onEditMidiClip (ref.lane, ref.clip); return; }

    const int lane = laneAtY (e.getPosition().y);   // double-click empty lane area → new MIDI clip there
    if (lane < 0) return;
    const int labelWidth = 118;
    const auto hit = hitTestAt (e.getPosition());
    if (hit.clip < 0 && e.getPosition().x > laneRects[(size_t) lane].getX() + labelWidth)
    {
        const double t = juce::jlimit (0.0, timelineContentSeconds(), xToTimeIn (timelineMapRect, e.getPosition().x));
        commitEdit (captureFullState());   // snapshot before adding the clip (undoable)
        const int idx = addMidiClip (lane, t);
        if (idx >= 0 && onEditMidiClip) onEditMidiClip (lane, idx);
    }
}

// ── Free audio import (drag a file onto a lane) ────────────────────────────────
bool MainPluginEditor::ArrangementRecorderView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg"))
            return true;
    return false;
}

void MainPluginEditor::ArrangementRecorderView::fileDragEnter (const juce::StringArray&, int, int y)
{
    fileDragLane = laneAtY (y);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::fileDragMove (const juce::StringArray&, int, int y)
{
    const int lane = laneAtY (y);
    if (lane != fileDragLane) { fileDragLane = lane; repaint(); }
}

void MainPluginEditor::ArrangementRecorderView::fileDragExit (const juce::StringArray&)
{
    fileDragLane = -1;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragLane = -1;
    if (formatManager == nullptr || thumbnailCache == nullptr) { repaint(); return; }

    int lane = laneAtY (y);
    if (lane < 0) lane = 0;

    double dropTime = juce::jmax (0.0, snapTime (xToTimeIn (timelineMapRect, x)));
    auto before = captureLaneState (lane);
    bool added = false;

    for (const auto& fs : files)
    {
        juce::File file (fs);
        if (! file.existsAsFile() || ! file.hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg"))
            continue;

        double dur = 0.0;
        if (std::unique_ptr<juce::AudioFormatReader> reader { formatManager->createReaderFor (file) })
            if (reader->sampleRate > 0.0)
                dur = (double) reader->lengthInSamples / reader->sampleRate;
        if (dur <= 0.0) continue;

        auto& slot = clipThumbs[file.getFullPathName()];
        if (slot == nullptr)
        {
            slot = std::make_unique<juce::AudioThumbnail> (512, *formatManager, *thumbnailCache);
            slot->addChangeListener (this);
            slot->setSource (new juce::FileInputSource (file));
        }

        LaneClip clip;
        clip.file                 = file;
        clip.durationSeconds      = dur;
        clip.trimStartSeconds     = 0.0;
        clip.trimEndSeconds       = dur;
        clip.timelineStartSeconds = dropTime;
        applyOverlapResolution (lane, clip);
        clips[(size_t) lane].push_back (clip);

        dropTime += dur;   // stack multiple dropped files end-to-end
        added = true;
    }

    if (added)
    {
        mode = Mode::Finished;
        commitEdit (std::move (before));
    }
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::applyOverlapResolution (int laneIndex, const LaneClip& incoming)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& lane = clips[(size_t) laneIndex];
    const double iStart = incoming.timelineStartSeconds;
    const double iEnd   = incoming.timelineEndSeconds();

    if (overlapMode == OverlapMode::Push)
    {
        // Shift any clip whose start falls within or after the incoming clip rightward
        for (auto& c : lane)
            if (c.timelineStartSeconds >= iStart)
                c.timelineStartSeconds += incoming.lengthSeconds();
    }
    else if (overlapMode == OverlapMode::Replace)
    {
        // Remove clips fully covered; trim clips partially overlapping
        lane.erase (std::remove_if (lane.begin(), lane.end(), [&] (const LaneClip& c)
        {
            return c.timelineStartSeconds >= iStart && c.timelineEndSeconds() <= iEnd;
        }), lane.end());

        for (auto& c : lane)
        {
            if (c.timelineStartSeconds < iStart && c.timelineEndSeconds() > iStart)
            {
                // Tail overlaps → trim out-point
                const double excess = c.timelineEndSeconds() - iStart;
                c.trimEndSeconds = juce::jmax (c.trimStartSeconds, c.trimEndSeconds - excess);
            }
            else if (c.timelineStartSeconds >= iStart && c.timelineStartSeconds < iEnd)
            {
                // Head overlaps → shift clip start forward
                const double excess = iEnd - c.timelineStartSeconds;
                c.timelineStartSeconds = iEnd;
                c.trimStartSeconds     = juce::jmin (c.trimEndSeconds, c.trimStartSeconds + excess);
            }
        }
    }
    // OverlapMode::Keep: do nothing — clips stack on top of each other
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
    if (active)   // follow: page the view so the playhead stays visible when zoomed in
    {
        const double vis = visibleSeconds();
        if (seconds < viewStartSeconds || seconds > viewStartSeconds + vis * 0.98)
        {
            viewStartSeconds = seconds - vis * 0.1;
            clampView();
        }
    }
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

MainPluginEditor::ArrangementRecorderView::LaneSnapshot
MainPluginEditor::ArrangementRecorderView::captureFullState() const
{
    LaneSnapshot s;
    s.full = true;
    s.allState = clips;
    s.allMidi  = midiClips;
    for (int i = 0; i < numLanes; ++i)
        s.allAutoPoints[(size_t) i] = editAuto[(size_t) i].points;
    return s;
}

void MainPluginEditor::ArrangementRecorderView::commitEdit (LaneSnapshot before)
{
    if (! before.full && before.lane < 0) return;
    undoStack.push_back (std::move (before));
    redoStack.clear();
}

void MainPluginEditor::ArrangementRecorderView::undo()
{
    if (undoStack.empty()) return;
    auto entry = std::move (undoStack.back());
    undoStack.pop_back();
    LaneSnapshot redoEntry;
    if (entry.full)
    {
        redoEntry = captureFullState();
        clips     = entry.allState;
        midiClips = entry.allMidi;
        for (int i = 0; i < numLanes; ++i)
            editAuto[(size_t) i].points = entry.allAutoPoints[(size_t) i];
    }
    else
    {
        redoEntry.lane = entry.lane;
        redoEntry.state = clips[(size_t) entry.lane];
        clips[(size_t) entry.lane] = entry.state;
    }
    redoStack.push_back (std::move (redoEntry));
    selectedClip = -1;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::redo()
{
    if (redoStack.empty()) return;
    auto entry = std::move (redoStack.back());
    redoStack.pop_back();
    LaneSnapshot undoEntry;
    if (entry.full)
    {
        undoEntry = captureFullState();
        clips     = entry.allState;
        midiClips = entry.allMidi;
        for (int i = 0; i < numLanes; ++i)
            editAuto[(size_t) i].points = entry.allAutoPoints[(size_t) i];
    }
    else
    {
        undoEntry.lane = entry.lane;
        undoEntry.state = clips[(size_t) entry.lane];
        clips[(size_t) entry.lane] = entry.state;
    }
    undoStack.push_back (std::move (undoEntry));
    selectedClip = -1;
    repaint();
}

// ── Clip launcher (beat-quantized slot triggers) ──────────────────────────────

void MainPluginEditor::ArrangementRecorderView::triggerSlot (int lane, int clip) noexcept
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return;
    slotStates[(size_t) lane]         = SlotState::WaitingToStart;
    launcherClipPerLane[(size_t) lane] = clip;
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::triggerAllSlots() noexcept
{
    for (int lane = 0; lane < numLanes; ++lane)
    {
        if (juce::isPositiveAndBelow (lane, (int) clips.size()) && ! clips[(size_t) lane].empty())
            triggerSlot (lane, 0);
    }
}

void MainPluginEditor::ArrangementRecorderView::setSlotState (int lane, SlotState state) noexcept
{
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return;
    slotStates[(size_t) lane] = state;
    repaint();
}

MainPluginEditor::ArrangementRecorderView::SlotState
MainPluginEditor::ArrangementRecorderView::getSlotState (int lane) const noexcept
{
    if (! juce::isPositiveAndBelow (lane, numEditableLanes)) return SlotState::Idle;
    return slotStates[(size_t) lane];
}

int MainPluginEditor::ArrangementRecorderView::getLauncherClip (int lane) const noexcept
{
    if (! juce::isPositiveAndBelow (lane, numLanes)) return -1;
    return launcherClipPerLane[(size_t) lane];
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

void MainPluginEditor::ArrangementRecorderView::duplicateClip (int laneIndex, int clipIndex)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& vec = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) vec.size())) return;
    auto before = captureLaneState (laneIndex);
    LaneClip copy = vec[(size_t) clipIndex];
    copy.timelineStartSeconds = vec[(size_t) clipIndex].timelineEndSeconds();   // place right after the original
    copy.selected = false;
    vec.push_back (copy);
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setClipLabel (int laneIndex, int clipIndex, const juce::String& label)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& vec = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) vec.size())) return;
    auto before = captureLaneState (laneIndex);
    vec[(size_t) clipIndex].label = label;
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setClipColour (int laneIndex, int clipIndex, juce::uint32 argb)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& vec = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) vec.size())) return;
    auto before = captureLaneState (laneIndex);
    vec[(size_t) clipIndex].colourArgb = argb;
    commitEdit (std::move (before));
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::setClipLocked (int laneIndex, int clipIndex, bool shouldLock)
{
    if (! juce::isPositiveAndBelow (laneIndex, numEditableLanes)) return;
    auto& vec = clips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) vec.size())) return;
    auto before = captureLaneState (laneIndex);
    vec[(size_t) clipIndex].locked = shouldLock;
    commitEdit (std::move (before));
    repaint();
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

// ── Timeline viewport (zoom + horizontal scroll) ───────────────────────────────
double MainPluginEditor::ArrangementRecorderView::timelineContentSeconds() const
{
    const double maxLen = computeMaxLengthSeconds();
    if (maxLen > 0.0 && mode == Mode::Finished)
        return maxLen;
    const double bpm = juce::jlimit (40.0, 240.0, snapBpm > 0.0 ? snapBpm : 120.0);
    return 8.0 * (4.0 * 60.0 / bpm);   // empty arrangement spans 8 bars
}

double MainPluginEditor::ArrangementRecorderView::visibleSeconds() const
{
    return timelineContentSeconds() / juce::jmax (1.0, zoom);
}

void MainPluginEditor::ArrangementRecorderView::clampView()
{
    zoom = juce::jlimit (1.0, 64.0, zoom);
    const double maxStart = juce::jmax (0.0, timelineContentSeconds() - visibleSeconds());
    viewStartSeconds = juce::jlimit (0.0, maxStart, viewStartSeconds);
}

int MainPluginEditor::ArrangementRecorderView::timeToXIn (juce::Rectangle<int> track, double t) const
{
    const double vis = juce::jmax (1.0e-6, visibleSeconds());
    const double pps = (double) track.getWidth() / vis;
    return track.getX() + (int) std::round ((t - viewStartSeconds) * pps);
}

double MainPluginEditor::ArrangementRecorderView::xToTimeIn (juce::Rectangle<int> track, int x) const
{
    const double vis = juce::jmax (1.0e-6, visibleSeconds());
    const double pps = (double) track.getWidth() / vis;
    if (pps <= 0.0) return 0.0;
    return viewStartSeconds + (double) (x - track.getX()) / pps;
}

void MainPluginEditor::ArrangementRecorderView::setZoomAnchored (double newZoom, double anchorTime)
{
    newZoom = juce::jlimit (1.0, 64.0, newZoom);
    const double oldVis = visibleSeconds();
    const double frac = oldVis > 0.0 ? (anchorTime - viewStartSeconds) / oldVis : 0.0;
    zoom = newZoom;
    viewStartSeconds = anchorTime - frac * visibleSeconds();
    clampView();
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::mouseWheelMove (const juce::MouseEvent& e,
                                                                const juce::MouseWheelDetails& wheel)
{
    const int labelWidth = 118;
    auto track = laneRects[0].withTrimmedLeft (labelWidth).reduced (4, 3);
    if (track.getWidth() <= 0)
        track = getLocalBounds().withTrimmedLeft (labelWidth + 20).reduced (16, 12);

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())   // zoom, anchored under the cursor
    {
        const double anchorTime = xToTimeIn (track, e.getPosition().x);
        setZoomAnchored (zoom * (wheel.deltaY > 0.0f ? 1.2 : 1.0 / 1.2), anchorTime);
    }
    else                                                 // horizontal scroll
    {
        viewStartSeconds -= (double) wheel.deltaY * visibleSeconds() * 0.35;
        clampView();
        repaint();
    }
}

void MainPluginEditor::ArrangementRecorderView::zoomIn()    { setZoomAnchored (zoom * 1.5, viewStartSeconds + visibleSeconds() * 0.5); }
void MainPluginEditor::ArrangementRecorderView::zoomOut()   { setZoomAnchored (zoom / 1.5, viewStartSeconds + visibleSeconds() * 0.5); }
void MainPluginEditor::ArrangementRecorderView::zoomToFit() { zoom = 1.0; viewStartSeconds = 0.0; clampView(); repaint(); }

void MainPluginEditor::ArrangementRecorderView::setLoopEnabled (bool shouldLoop)
{
    loopEnabled = shouldLoop && (loopEndSeconds - loopStartSeconds > 1.0e-4);
    if (onLoopChanged) onLoopChanged (loopStartSeconds, loopEndSeconds, loopEnabled);
    repaint();
}

void MainPluginEditor::ArrangementRecorderView::paint (juce::Graphics& g)
{
    using namespace sattari::brand;
    const auto bounds = getLocalBounds().toFloat();

    // djay-Pro-style arrangement panel: OLED-cold deep navy.
    const auto panelBase = juce::Colour (stemdeck::palette::kPanelDeep).darker (0.12f);
    g.setColour (panelBase);
    g.fillRoundedRectangle (bounds, 10.0f);

    juce::Colour accent;
    juce::String modeLabel;
    switch (mode)
    {
        case Mode::Recording: accent = juce::Colour (kDanger);     modeLabel = "RECORDING";  break;
        case Mode::Finished:  accent = juce::Colour (kAccent);     modeLabel = "ARRANGEMENT"; break;
        case Mode::Idle:      accent = juce::Colour (kTextMuted);  modeLabel = "READY";       break;
    }
    g.setColour (accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 10.0f, 1.0f);

    auto area = getLocalBounds().reduced (16, 12);

    // Title row.
    auto header = area.removeFromTop (22);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (juce::Colour (kTextPrimary));
    g.drawText ("Arrangement", header.removeFromLeft (200), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
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

    const int labelWidth = 118;       // must match hitTestAt()
    const int rulerHeight = 18;
    const int statusHeight = 16;

    auto statusArea = area.removeFromBottom (statusHeight);
    area.removeFromBottom (4);

    const int scrollBarH = 9;                       // horizontal scrollbar strip
    auto scrollStrip = area.removeFromBottom (scrollBarH);
    area.removeFromBottom (4);

    // Bar/beat math driven by snap BPM (default 120). Empty arrangement shows 8 empty bars.
    const double bpm = juce::jlimit (40.0, 240.0, snapBpm > 0.0 ? snapBpm : 120.0);
    const double secsPerBeat = 60.0 / bpm;
    const double secsPerBar  = 4.0 * secsPerBeat;
    const double rulerLen    = timelineContentSeconds();   // honors zoom via timeToXIn below

    // X-range shared by the ruler, grid, clips, playhead and scrollbar (zoom/scroll aware).
    const int gridLeftX  = area.getX() + labelWidth + 4;
    const int gridRightX = area.getRight() - 4;
    auto mapRect = juce::Rectangle<int> (gridLeftX, area.getY(), juce::jmax (1, gridRightX - gridLeftX), 1);
    timelineMapRect = mapRect;   // remembered for click-to-seek hit-testing

    auto ruler = area.removeFromTop (rulerHeight);
    g.setColour (juce::Colour (kPanelDeep).darker (0.4f));   // FL timebar strip
    g.fillRect (ruler);
    auto rulerLabel = ruler.removeFromLeft (labelWidth);
    rulerBounds = ruler;   // timeline portion of the ruler — click here to seek
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.setColour (juce::Colour (kTextMuted));
    g.drawText ("BARS", rulerLabel.reduced (8, 0), juce::Justification::centredLeft);

    auto rulerTimeToX = [&] (double t) -> int { return timeToXIn (mapRect, t); };

    const int totalBars = juce::jmax (1, (int) std::ceil (rulerLen / secsPerBar));
    int labelStep = 1;
    if      (totalBars > 96) labelStep = 16;
    else if (totalBars > 48) labelStep = 8;
    else if (totalBars > 24) labelStep = 4;
    else if (totalBars > 12) labelStep = 2;

    g.setColour (juce::Colour (kHairline));
    g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.saveState();
    g.reduceClipRegion (ruler);
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
            const bool major = (bar % 4) == 0;   // emphasise 4-bar groups like FL
            g.setColour (major ? juce::Colour (kTextPrimary).withAlpha (0.9f) : juce::Colour (kTextMuted));
            g.drawText (juce::String (bar + 1), x + 3, ruler.getY() - 1, 30, rulerHeight, juce::Justification::centredLeft);
        }
    }
    g.restoreState();

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

    auto gridTimeToX = [&] (double t) -> int { return timeToXIn (mapRect, t); };

    g.setColour (juce::Colour (kPanelDeep).withAlpha (0.65f));
    g.fillRect (gridLeft, gridTop, gridRight - gridLeft, gridBottom - gridTop);

    // Bar/beat grid is drawn per-lane (inside drawLane, over each lane's banding) so
    // it stays crisp instead of being washed out by the translucent lane fills.

    auto drawLane = [&] (juce::Rectangle<int> lane, const juce::String& title, juce::Colour colour,
                         juce::AudioThumbnail* thumb, const juce::File& file, int deckIndex)
    {
        juce::ignoreUnused (thumb, file);   // clips now resolve their own per-clip thumbnail
        if (deckIndex >= 0 && deckIndex < numEditableLanes)
            laneRects[(size_t) deckIndex] = lane;

        const bool isSelected = (deckIndex >= 0 && deckIndex == selectedLane);

        // Track header column (left).
        auto headerCell = lane.removeFromLeft (labelWidth);
        g.setColour (juce::Colour (kPanelRaised));
        g.fillRect (headerCell);
        g.setColour (colour);
        g.fillRect (headerCell.removeFromLeft (3));

        auto hc = headerCell.reduced (5, 1);
        if (deckIndex >= 0 && deckIndex != masterLane && deckIndex < numEditableLanes)   // M/S on deck lanes
        {
            auto sRect = hc.removeFromRight (16).withSizeKeepingCentre (16, juce::jmin (hc.getHeight(), 14));
            hc.removeFromRight (3);
            auto mRect = hc.removeFromRight (16).withSizeKeepingCentre (16, juce::jmin (hc.getHeight(), 14));
            hc.removeFromRight (4);
            muteBtnRects[(size_t) deckIndex] = mRect;
            soloBtnRects[(size_t) deckIndex] = sRect;
            auto drawTag = [&] (juce::Rectangle<int> r, const char* tag, bool on, juce::Colour onCol)
            {
                g.setColour (on ? onCol.withAlpha (0.9f) : juce::Colour (kPanelDeep).withAlpha (0.85f));
                g.fillRoundedRectangle (r.toFloat(), 2.5f);
                g.setColour (on ? juce::Colours::black.withAlpha (0.85f) : juce::Colour (kTextMuted));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (tag, r, juce::Justification::centred);
            };
            drawTag (mRect, "M", laneMuted[(size_t) deckIndex], juce::Colour (kDanger));
            drawTag (sRect, "S", laneSoloed[(size_t) deckIndex], juce::Colour (kAccent));

            // "A" toggles the lane's editable automation curve.
            auto aRect = hc.removeFromRight (16).withSizeKeepingCentre (16, juce::jmin (hc.getHeight(), 14));
            hc.removeFromRight (3);
            autoBtnRects[(size_t) deckIndex] = aRect;
            drawTag (aRect, "A", editAuto[(size_t) deckIndex].visible, juce::Colour (kAccentViolet));
        }
        g.setColour (juce::Colour (kTextPrimary).withAlpha (laneAudible (deckIndex) ? 0.92f : 0.4f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, hc, juce::Justification::centredLeft);

        // Track timeline area — must match hitTestAt: lane.withTrimmedLeft (labelWidth).reduced (4, 3)
        auto track = lane.reduced (4, 3);

        // Clip everything in this lane to the track so zoomed/scrolled content never
        // bleeds into the header column or past the right edge.
        g.saveState();
        g.reduceClipRegion (track);

        // Sunken "well": the track lane is distinctly darker than the panel around
        // it, with a top inner-shadow so it reads as recessed below the mixer.
        g.setColour (juce::Colour (0xff08090b));
        g.fillRect (track);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine ((float) track.getX(), (float) track.getY() + 0.5f, (float) track.getRight(), (float) track.getY() + 0.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.03f));   // faint bottom lip
        g.drawLine ((float) track.getX(), (float) track.getBottom() - 0.5f, (float) track.getRight(), (float) track.getBottom() - 0.5f, 1.0f);

        // FL-style bar/beat grid over this lane (track spans the same x-range as the ruler).
        {
            auto gx = [&] (double t) { return timeToXIn (track, t); };
            const float top = (float) track.getY(), bot = (float) track.getBottom();
            for (int bar = 0; bar < totalBars; ++bar)
            {
                const double t = (double) bar * secsPerBar;
                if (t > rulerLen) break;
                const int x  = gx (t);
                const int xN = gx (juce::jmin (rulerLen, (double) (bar + 1) * secsPerBar));
                if ((bar % 2) == 1)   // alternating bar shading
                {
                    g.setColour (juce::Colours::white.withAlpha (0.025f));
                    g.fillRect (x, track.getY(), xN - x, track.getHeight());
                }
                const bool wideBar = (xN - x) > 120;
                for (int beat = 1; beat < 4; ++beat)
                {
                    const double bt = t + beat * secsPerBeat;
                    if (bt > rulerLen) break;
                    g.setColour (juce::Colour (kHairline).withAlpha (0.28f));
                    g.drawVerticalLine (gx (bt), top, bot);
                    if (wideBar)   // half-beat ticks only when there's room
                    {
                        g.setColour (juce::Colour (kHairline).withAlpha (0.12f));
                        g.drawVerticalLine (gx (bt - 0.5 * secsPerBeat), top, bot);
                    }
                }
            }
            for (int bar = 0; bar <= totalBars; ++bar)   // bar lines on top; every 4th brighter
            {
                const double t = (double) bar * secsPerBar;
                if (t > rulerLen) break;
                const bool major = (bar % 4) == 0;
                g.setColour (juce::Colour (major ? kTextFaint : kHairline).withAlpha (major ? 0.55f : 0.4f));
                g.drawVerticalLine (gx (t), top, bot);
            }
        }

        // Showcase demo clip block on empty deck lanes (replaced by real clips once
        // anything is recorded/imported). Matches the mockup's colored blocks.
        if (showDemoBlocks && deckIndex >= 0 && deckIndex < numLanes
            && clips[(size_t) deckIndex].empty() && midiClips[(size_t) deckIndex].empty())
        {
            static const std::array<float, 4> demoEnd { 0.56f, 0.38f, 0.47f, 0.60f };
            auto blk = track.toFloat().reduced (0.0f, 3.0f).translated (track.getWidth() * 0.006f, 0.0f);
            blk = blk.withWidth (track.getWidth() * demoEnd[(size_t) deckIndex]);
            g.setColour (colour.withAlpha (0.30f));
            g.fillRoundedRectangle (blk, 3.0f);
            g.setColour (colour.withAlpha (0.9f));
            g.drawRoundedRectangle (blk.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (colour.withAlpha (0.45f));
            for (float bx = blk.getX() + 6.0f; bx < blk.getRight() - 4.0f; bx += 5.0f)
                g.drawVerticalLine ((int) bx, blk.getY() + 4.0f, blk.getBottom() - 4.0f);
        }

        if (mode == Mode::Finished && maxLen > 0.0
            && deckIndex >= 0 && deckIndex < numEditableLanes && ! clips[(size_t) deckIndex].empty())
        {
            auto timeToX = [&] (double t) -> int { return timeToXIn (track, t); };

            const auto& laneClips = clips[(size_t) deckIndex];
            juce::Rectangle<int> firstBody;   // for automation overlay

            for (int ci = 0; ci < (int) laneClips.size(); ++ci)
            {
                const auto& clip = laneClips[(size_t) ci];
                if (clip.durationSeconds <= 0.0) continue;

                // Each clip draws from its own source thumbnail (free-imported clips) or
                // the lane's recording thumbnail (recorded / razor-split clips).
                auto* clipThumb = thumbForClip (deckIndex, clip);
                const double laneLen = (clipThumb != nullptr && clipThumb->getTotalLength() > 0.0)
                                           ? clipThumb->getTotalLength() : clip.durationSeconds;
                const juce::Colour clipColour = clip.colourArgb != 0 ? juce::Colour (clip.colourArgb) : colour;

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

                // FL clip block: rounded tinted body (soft gradient) + solid rounded-top title strip.
                auto clipF = clipRect.toFloat().reduced (0.5f);
                const float radius = 3.0f;
                g.setGradientFill (juce::ColourGradient (
                    clipColour.withAlpha (clipSelected ? 0.38f : 0.26f), clipF.getX(), clipF.getY(),
                    clipColour.withAlpha (clipSelected ? 0.20f : 0.12f), clipF.getX(), clipF.getBottom(), false));
                g.fillRoundedRectangle (clipF, radius);

                juce::Path titlePath;
                titlePath.addRoundedRectangle (clipF.getX(), clipF.getY(), clipF.getWidth(), (float) headerBand,
                                               radius, radius, true, true, false, false);
                g.setColour (clipColour.withAlpha (clipSelected ? 0.98f : 0.82f));
                g.fillPath (titlePath);

                g.setColour (juce::Colours::white.withAlpha (clipSelected ? 0.22f : 0.12f));   // top highlight
                g.drawLine (clipF.getX() + radius, clipF.getY() + 0.75f, clipF.getRight() - radius, clipF.getY() + 0.75f, 1.0f);

                g.setColour (clipSelected ? juce::Colours::white.withAlpha (0.9f) : clipColour.withAlpha (0.7f));
                g.drawRoundedRectangle (clipF, radius, clipSelected ? 1.8f : 1.0f);

                // Chevron + source filename (only label the first clip to avoid clutter).
                if (ci == 0)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.78f));
                    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                    const auto chevron = juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe "));
                    const auto labelText = clip.label.isNotEmpty() ? clip.label : clip.file.getFileName();
                    g.drawText (chevron + labelText, patternHeader.reduced (5, 0),
                                juce::Justification::centredLeft, true);
                }

                // Lock badge — amber "L" chip on the top-right corner
                if (clip.locked)
                {
                    auto lr = patternHeader.removeFromRight (15).withSizeKeepingCentre (13, 11);
                    g.setColour (juce::Colour (0xffffb020).withAlpha (0.92f));
                    g.fillRoundedRectangle (lr.toFloat(), 2.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.9f));
                    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                    g.drawText ("L", lr, juce::Justification::centred);
                }

                if (patternBody.getHeight() > 4 && clipThumb != nullptr && clipThumb->getTotalLength() > 0.0)
                {
                    g.setColour (clipColour.withAlpha (0.92f));
                    clipThumb->drawChannels (g, patternBody.reduced (2, 1), clipStart, clipEnd, 0.95f);
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

                const float handleW = 2.5f;
                g.setColour (juce::Colours::white.withAlpha (clipSelected ? 0.7f : 0.18f));
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
        else if (! (deckIndex >= 0 && deckIndex < numEditableLanes && ! midiClips[(size_t) deckIndex].empty()))
        {
            g.setColour (juce::Colour (kTextFaint).withAlpha (0.65f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("empty", track.reduced (8, 0), juce::Justification::centredLeft);
        }

        // ---- Per-lane MIDI clip blocks (each a mini piano-roll preview) ----
        if (deckIndex >= 0 && deckIndex < numEditableLanes)
        {
            for (const auto& mc : midiClips[(size_t) deckIndex])
            {
                double lenBeats = 4.0;
                for (const auto& nn : mc.notes) lenBeats = juce::jmax (lenBeats, nn.startBeats + nn.lengthBeats);
                const double endSec = mc.timelineStartSeconds + lenBeats * secsPerBeat;
                const int mx0 = timeToXIn (track, mc.timelineStartSeconds);
                const int mx1 = juce::jmax (mx0 + 8, timeToXIn (track, endSec));
                auto mr = juce::Rectangle<int> (mx0, track.getY(), mx1 - mx0, track.getHeight());

                auto mf = mr.toFloat().reduced (0.5f);
                g.setColour (colour.withAlpha (0.20f));
                g.fillRoundedRectangle (mf, 3.0f);
                g.setColour (colour.withAlpha (0.85f));
                g.drawRoundedRectangle (mf, 3.0f, 1.0f);

                const double pps = (double) (mx1 - mx0) / juce::jmax (1.0e-6, lenBeats);
                const int lo = 36, rng = 84 - 36;
                for (const auto& nn : mc.notes)
                {
                    const int nx0 = mx0 + (int) std::round (nn.startBeats * pps);
                    const int nx1 = mx0 + (int) std::round ((nn.startBeats + nn.lengthBeats) * pps);
                    const float fy = 1.0f - juce::jlimit (0.0f, 1.0f, (float) (nn.pitch - lo) / (float) rng);
                    const int ny  = mr.getY() + 2 + (int) (fy * (float) juce::jmax (1, mr.getHeight() - 4));
                    g.setColour (juce::Colours::white.withAlpha (0.7f));
                    g.fillRect (nx0, ny, juce::jmax (2, nx1 - nx0), 2);
                }
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText ("MIDI", mr.withHeight (10).reduced (4, 0), juce::Justification::centredLeft);
            }
        }

        // ---- Editable automation overlay (draggable breakpoints) ----
        if (deckIndex >= 0 && deckIndex < numLanes && editAuto[(size_t) deckIndex].visible)
        {
            const auto& a = editAuto[(size_t) deckIndex];
            static const char* targetNames[numAutoTargets] = { "VOL", "FILT", "PITCH", "VOC FX" };

            // Dim the lane so the curve reads as the active editing layer.
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (track);

            const auto curveCol = juce::Colour (kAccentViolet);
            auto pointPos = [&] (const std::pair<double, float>& p) -> juce::Point<float>
            {
                return { (float) timeToXIn (track, p.first),
                         (float) track.getBottom() - juce::jlimit (0.0f, 1.0f, p.second) * (float) track.getHeight() };
            };

            if (! a.points.empty())
            {
                juce::Path path;
                auto first = pointPos (a.points.front());
                path.startNewSubPath ((float) track.getX(), first.y);
                path.lineTo (first);
                for (size_t pi = 1; pi < a.points.size(); ++pi)
                    path.lineTo (pointPos (a.points[pi]));
                path.lineTo ((float) track.getRight(), pointPos (a.points.back()).y);
                g.setColour (curveCol.withAlpha (0.9f));
                g.strokePath (path, juce::PathStrokeType (1.6f));

                for (const auto& p : a.points)
                {
                    const auto pp = pointPos (p);
                    g.setColour (curveCol);
                    g.fillEllipse (pp.x - 4.5f, pp.y - 4.5f, 9.0f, 9.0f);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (pp.x - 2.0f, pp.y - 2.0f, 4.0f, 4.0f);
                }
            }
            else
            {
                g.setColour (curveCol.withAlpha (0.6f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText ("click to add automation points",
                            track, juce::Justification::centred);
            }

            // Target chip (click to cycle VOL → FILT → PITCH → VOC FX).
            auto chipR = juce::Rectangle<int> (track.getRight() - 52, track.getY() + 2, 50, 13);
            autoTargetRects[(size_t) deckIndex] = chipR;
            g.setColour (curveCol.withAlpha (0.85f));
            g.fillRoundedRectangle (chipR.toFloat(), 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (targetNames[juce::jlimit (0, numAutoTargets - 1, a.target)], chipR, juce::Justification::centred);
        }
        else if (deckIndex >= 0 && deckIndex < numLanes)
        {
            autoTargetRects[(size_t) deckIndex] = {};
        }

        g.restoreState();   // end track clip region
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

    // Clip launcher slot-state overlay: WaitingToStart = pulsing amber, Running = solid green.
    {
        const bool blink = ((juce::Time::currentTimeMillis() / 220) % 2) == 0;
        for (int i = 0; i < numLanes; ++i)
        {
            const auto st = slotStates[(size_t) i];
            if (st == SlotState::Idle) continue;
            const auto lr = laneRects[(size_t) i];
            const auto trackR = lr.withTrimmedLeft (labelWidth).reduced (2, 1);
            const juce::Colour col = (st == SlotState::WaitingToStart)
                ? juce::Colour (0xffffb020).withAlpha (blink ? 0.95f : 0.35f)   // amber pulse
                : juce::Colour (0xff37d98a).withAlpha (0.9f);                    // green solid
            g.setColour (col);
            g.drawRect (trackR, 2);
            // Launch-arrow glyph on the left edge of the track area
            const float cy = (float) lr.getCentreY();
            const float ax = (float) (lr.getX() + labelWidth + 5);
            juce::Path arrow;
            arrow.addTriangle (ax, cy - 5.0f, ax + 8.0f, cy, ax, cy + 5.0f);
            g.fillPath (arrow);
            // Small status label
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            const auto stLabel = (st == SlotState::WaitingToStart) ? juce::String ("QUEUED") : juce::String ("LIVE");
            g.drawText (stLabel, trackR.withTrimmedLeft (14).withHeight (12).withY (lr.getY() + 2),
                        juce::Justification::centredLeft);
        }
    }

    // Highlight the lane under an in-progress file drag.
    if (fileDragLane >= 0 && fileDragLane < numEditableLanes)
    {
        auto hl = laneRects[(size_t) fileDragLane].withTrimmedLeft (labelWidth).reduced (4, 3);
        g.setColour (juce::Colour (kAccent).withAlpha (0.12f));
        g.fillRect (hl);
        g.setColour (juce::Colour (kAccent).withAlpha (0.8f));
        g.drawRect (hl, 2);
    }

    // Loop region overlay (shaded band across the lanes + a bracket on the ruler).
    if (loopEnabled && loopEndSeconds > loopStartSeconds && rulerLen > 0.0)
    {
        const int lx0 = juce::jlimit (gridLeft, gridRight, gridTimeToX (loopStartSeconds));
        const int lx1 = juce::jlimit (gridLeft, gridRight, gridTimeToX (loopEndSeconds));
        if (lx1 > lx0)
        {
            g.setColour (juce::Colour (kAccent).withAlpha (0.10f));
            g.fillRect (lx0, gridTop, lx1 - lx0, gridBottom - gridTop);
            g.setColour (juce::Colour (kAccent).withAlpha (0.85f));
            g.drawVerticalLine (lx0, (float) gridTop, (float) gridBottom);
            g.drawVerticalLine (lx1, (float) gridTop, (float) gridBottom);
            g.fillRect (lx0, rulerBounds.getY(), lx1 - lx0, 3);
        }
    }

    // Play cursor / playhead — bright while playing, dim play-start cursor when stopped.
    {
        const double markT = playbackActive ? playbackPositionSeconds : cursorSeconds;
        if (rulerLen > 0.0 && markT >= 0.0)
        {
            const int px = gridTimeToX (markT);
            if (px >= gridLeft && px <= gridRight)
            {
                g.setColour (juce::Colours::white.withAlpha (playbackActive ? 0.92f : 0.5f));
                g.fillRect (px - 1, gridTop, playbackActive ? 2 : 1, gridBottom - gridTop);
                juce::Path head;
                head.addTriangle ((float) px - 5.0f, (float) gridTop,
                                  (float) px + 5.0f, (float) gridTop,
                                  (float) px,        (float) gridTop + 6.0f);
                g.setColour (juce::Colour (kAccent).withAlpha (playbackActive ? 1.0f : 0.7f));
                g.fillPath (head);
            }
        }
    }

    // Horizontal scrollbar: the visible window over the whole arrangement (drag to scroll).
    {
        const double content = timelineContentSeconds();
        scrollTrackBounds = juce::Rectangle<int> (gridLeftX, scrollStrip.getY(),
                                                  juce::jmax (1, gridRightX - gridLeftX), scrollStrip.getHeight());
        g.setColour (juce::Colour (kPanelDeep).withAlpha (0.85f));
        g.fillRoundedRectangle (scrollTrackBounds.toFloat(), 3.0f);
        if (content > 0.0)
        {
            const double f0 = juce::jlimit (0.0, 1.0, viewStartSeconds / content);
            const double f1 = juce::jlimit (0.0, 1.0, (viewStartSeconds + visibleSeconds()) / content);
            const int tx = scrollTrackBounds.getX() + (int) std::round (f0 * scrollTrackBounds.getWidth());
            const int tw = juce::jmax (18, (int) std::round ((f1 - f0) * scrollTrackBounds.getWidth()));
            scrollThumbBounds = juce::Rectangle<int> (tx, scrollTrackBounds.getY(), tw, scrollTrackBounds.getHeight());
            g.setColour (juce::Colour (kAccent).withAlpha (zoom > 1.0001 ? 0.85f : 0.4f));
            g.fillRoundedRectangle (scrollThumbBounds.toFloat(), 3.0f);
        }
        else
            scrollThumbBounds = {};
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
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
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
    pendingSlotSeekSec.fill (-1.0);

    setLookAndFeel (&lookAndFeel);
    thumbnailFormatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    thumbnailFormatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif

    setResizable (true, true);
    setResizeLimits (1320, 820, 4200, 2600);
    setSize (1680, 1000);
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

    // TOOLS toggle reveals the advanced toolbar (plugin host / AI master /
    // rekordbox / project I/O / utility chips) hidden for the clean mockup view.
    // Advanced tools: a real, visible header toggle (title double-click still works
    // as a shortcut). Reveals rekordbox I/O, project save/open, metronome, talkover,
    // undo/redo, MIDI learn, mic + sync utilities.
    advancedToggle.setClickingTogglesState (true);
    advancedToggle.setToggleState (advancedVisible, juce::dontSendNotification);
    advancedToggle.setTooltip ("Show advanced tools: Rekordbox import/export, project save/open, "
                               "metronome, talkover, undo/redo, MIDI learn, mic & sync utilities");
    advancedToggle.onClick = [this]
    {
        advancedVisible = advancedToggle.getToggleState();
        resized();
        repaint();
    };
    addAndMakeVisible (advancedToggle);
    title.setInterceptsMouseClicks (true, false);
    title.addMouseListener (this, false);

    // Discoverable Settings gear (advanced tools + showcase toggle live here).
    setupButton (settingsButton);
    settingsButton.setButtonText (u8 ("\xe2\x9a\x99"));   // gear glyph (UTF-8)
    settingsButton.setTooltip ("Settings");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    setupFileBrowser();
    setupLibrary();

    title.setText ("StemDeck PRO", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextPrimary));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText (u8 ("FOUR DECKS  \xe2\x80\xa2  STEM MIXER  \xe2\x80\xa2  ARRANGEMENT"), juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    subtitle.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    standaloneWorkflowGuide.setText (
        u8 ("RECORD captures master + 4 deck WAVs + automation \xe2\x80\xa2 Reload bounces back into decks for post-set tweaks"),
        juce::dontSendNotification);
    standaloneWorkflowGuide.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    standaloneWorkflowGuide.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    standaloneWorkflowGuide.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (standaloneWorkflowGuide);

    tempoLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    tempoLabel.setJustificationType (juce::Justification::centredRight);
    tempoLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    addAndMakeVisible (tempoLabel);

    pluginHostStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    pluginHostStatus.setFont (juce::FontOptions (11.5f));
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
    pluginHostMessage.setFont (juce::FontOptions (11.5f));
    pluginHostMessage.setJustificationType (juce::Justification::centredLeft);
    pluginHostMessage.setText ("FX host cache: " + processorRef.getPluginHostCacheFile().getFileName(), juce::dontSendNotification);
    addAndMakeVisible (scanPluginsButton);
    addAndMakeVisible (reloadPluginCacheButton);
    addAndMakeVisible (loadPluginButton);
    addAndMakeVisible (clearChainButton);
    addAndMakeVisible (pluginHostMessage);

    masterKeyLabel.setText ("MASTER KEY", juce::dontSendNotification);
    masterKeyLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.85f));
    masterKeyLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
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

    // All-deck BPM / Key sync (also on hotkeys B / K).
    setupButton (syncBpmButton);
    syncBpmButton.setTooltip ("Warp + downbeat phase-lock all decks to Deck A's tempo (hotkey: B)");
    syncBpmButton.onClick = [this] { syncBpmAll(); };
    addAndMakeVisible (syncBpmButton);
    setupButton (syncKeyButton);
    syncKeyButton.setTooltip ("Harmonically match all decks to Deck A's key, Camelot-style (hotkey: K)");
    syncKeyButton.onClick = [this] { syncKeyAll(); };
    addAndMakeVisible (syncKeyButton);
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
        // Empty decks show a real call to action; clicking it opens the file chooser.
        deckWaves[(size_t) i]->setShowDropHint (true);
        deckWaves[(size_t) i]->setOnBrowseRequest ([this, i] { openDeckChooser (i); });
        addAndMakeVisible (*deckWaves[(size_t) i]);

        setupDeck (deckControls[(size_t) i], deckPrefix (i), u8 ("\xe2\x96\xb6 PLAY"));   // stopped default; timer swaps to STOP
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
        // CUE = return to the cue point (hot cue 1 when set, else track start) —
        // real DJ cue semantics rather than a mislabelled rewind.
        deckControls[(size_t) i].cue.setTooltip ("Jump to the cue point (hot cue 1 if set, else track start)");
        deckControls[(size_t) i].cue.onClick = [this, i]
        {
            const double cuePoint = processorRef.getHotCue (i, 0);
            if (cuePoint >= 0.0)
                processorRef.seekDeckSeconds (i, cuePoint);
            else
                processorRef.rewindDeck (i);
        };

        // Audio→MIDI: extract the deck's melody (YIN) into an arrangement MIDI clip.
        {
            auto& btn = deckControls[(size_t) i].extractMidi;
            setupButton (btn);
            btn.setButtonText (u8 ("\xe2\x86\x92 MIDI"));
            btn.setTooltip ("Extract melody from this deck's audio into a MIDI clip (monophonic)");
            btn.onClick = [this, i]
            {
                const juce::File file (processorRef.getDeckPath (i));
                auto& dcc = deckControls[(size_t) i];
                if (! file.existsAsFile())
                {
                    dcc.stemStatus.setText ("Load audio first", juce::dontSendNotification);
                    return;
                }
                const bool neural = NeuralTranscriber::isAvailable();
                dcc.stemStatus.setText (neural ? u8 ("Neural transcription (basic-pitch)\xe2\x80\xa6")
                                               : u8 ("Extracting melody (YIN)\xe2\x80\xa6"),
                                        juce::dontSendNotification);
                juce::Component::SafePointer<MainPluginEditor> safe (this);
                juce::Thread::launch ([safe, file, i, neural]
                {
                    // Self-contained: no editor/processor state touched off-thread.
                    std::vector<MelodyExtractor::Note> notes;
                    juce::String msg;
                    bool usedNeural = false;
                    if (neural)
                        usedNeural = NeuralTranscriber::transcribe (file, notes, msg);
                    if (! usedNeural)   // not installed, failed, or produced nothing → YIN
                    {
                        juce::AudioFormatManager fm;
                        fm.registerBasicFormats();
                        if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
                        {
                            const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                            (juce::int64) (reader->sampleRate * 90.0));
                            juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
                            reader->read (&buf, 0, len, 0, true, true);
                            notes = MelodyExtractor::extract (buf, reader->sampleRate);
                        }
                    }
                    juce::MessageManager::callAsync ([safe, notes = std::move (notes), i, usedNeural]() mutable
                    {
                        if (safe != nullptr)
                            safe->applyExtractedMelody (i, notes, usedNeural);
                    });
                });
            };
            addAndMakeVisible (btn);
        }

        // Beatbox→drums: classify transients into kick/snare/hat, print a drum clip.
        {
            auto& btn = deckControls[(size_t) i].extractDrums;
            setupButton (btn);
            btn.setButtonText (u8 ("\xe2\x86\x92 DRUMS"));
            btn.setTooltip ("Turn beatboxing / percussion into a drum pattern (kick, snare, hat)");
            btn.onClick = [this, i]
            {
                const juce::File file (processorRef.getDeckPath (i));
                auto& dcc = deckControls[(size_t) i];
                if (! file.existsAsFile())
                {
                    dcc.stemStatus.setText ("Load audio first", juce::dontSendNotification);
                    return;
                }
                dcc.stemStatus.setText (u8 ("Detecting drum hits\xe2\x80\xa6"), juce::dontSendNotification);
                juce::Component::SafePointer<MainPluginEditor> safe (this);
                juce::Thread::launch ([safe, file, i]
                {
                    juce::AudioFormatManager fm;
                    fm.registerBasicFormats();
                    std::vector<MelodyExtractor::Note> drumHits;
                    if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
                    {
                        const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                        (juce::int64) (reader->sampleRate * 60.0));
                        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
                        reader->read (&buf, 0, len, 0, true, true);
                        drumHits = DrumExtractor::extract (buf, reader->sampleRate);
                    }
                    juce::MessageManager::callAsync ([safe, drumHits = std::move (drumHits), i]() mutable
                    {
                        if (safe != nullptr)
                            safe->applyExtractedDrums (i, drumHits);
                    });
                });
            };
            addAndMakeVisible (btn);
        }

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

            setupMacroKnob (i);

            // Per-stem FX: filter + space/echo send for drums/bass/music/vocals.
            static const char* stemShort[4] = { "Drums", "Bass", "Music", "Vocals" };
            for (int s = 0; s < 4; ++s)
            {
                const auto id = DeckPlayer::stemLaneId (DeckPlayer::drumsStem + s);
                setupSlider (dc.stemFilt[(size_t) s], true);
                dc.stemFilt[(size_t) s].setTooltip (juce::String (stemShort[s]) + " filter (LP ↔ HP)");
                dc.stemFilt[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemFilt[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemFilt[(size_t) s]);
                dc.stemFiltAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Filt", dc.stemFilt[(size_t) s]);

                setupSlider (dc.stemSend[(size_t) s], true);
                dc.stemSend[(size_t) s].setTooltip (juce::String (stemShort[s]) + " space/echo send");
                dc.stemSend[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemSend[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemSend[(size_t) s]);
                dc.stemSendAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Send", dc.stemSend[(size_t) s]);

                // Independent per-stem pitch (semitones) — e.g. pitch vocals only.
                setupSlider (dc.stemPitch[(size_t) s], true);
                dc.stemPitch[(size_t) s].setTooltip (juce::String (stemShort[s]) + " pitch (semitones, independent)");
                dc.stemPitch[(size_t) s].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                dc.stemPitch[(size_t) s].setPopupDisplayEnabled (true, true, this);
                addAndMakeVisible (dc.stemPitch[(size_t) s]);
                dc.stemPitchAttachments[(size_t) s] = std::make_unique<SliderAttachment> (
                    processorRef.parameters(), deckPrefix (i) + "Stem" + id + "Pitch", dc.stemPitch[(size_t) s]);
            }
        }

        setupSlipAndRoll (i);

        // ── Crossfader assign buttons: A / Ctr / B ───────────────────────────
        static const char* const kCfLabels[] = { "A", "Ctr", "B" };
        auto& dc2 = deckControls[(size_t) i];
        for (int s = 0; s < 3; ++s)
        {
            auto& btn = dc2.cfSideBtns[(size_t) s];
            btn.setButtonText (kCfLabels[s]);
            setupButton (btn);
            btn.setClickingTogglesState (false);
            const int side = s;
            const int deckIdx = i;
            btn.onClick = [this, deckIdx, side]
            {
                processorRef.setDeckCfSide (deckIdx, side);
                // Refresh toggle visuals
                for (int ss = 0; ss < 3; ++ss)
                    deckControls[(size_t) deckIdx].cfSideBtns[(size_t) ss]
                        .setToggleState (ss == side, juce::dontSendNotification);
            };
            addAndMakeVisible (btn);
        }
        // Set initial toggle state from state
        const int curSide = processorRef.getDeckCfSide (i);
        for (int s = 0; s < 3; ++s)
            dc2.cfSideBtns[(size_t) s].setToggleState (s == curSide, juce::dontSendNotification);
    }

    // Tap tempo
    setupButton (tapTempoButton);
    tapTempoButton.onClick = [this] { processorRef.tapTempo(); };
    addAndMakeVisible (tapTempoButton);

    // Universal transport: one button to play / stop every deck at once.
    setupButton (masterPlayButton);
    masterPlayButton.setButtonText (u8 ("\xe2\x96\xb6 PLAY ALL"));
    masterPlayButton.setColour (juce::TextButton::buttonColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.22f));
    masterPlayButton.setTooltip ("Play or stop every deck at once");
    masterPlayButton.onClick = [this]
    {
        bool anyPlaying = false;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            anyPlaying = anyPlaying || processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        const float target = anyPlaying ? 0.0f : 1.0f;   // any playing -> stop all, else start all
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (i) + "Play"))
                p->setValueNotifyingHost (target);
    };
    addAndMakeVisible (masterPlayButton);
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
    masterFxPad.setAccent (deckAccent (0));   // product cyan
    masterFxPad.attach (processorRef.parameters(), "masterFxX", "masterFxY");
    addAndMakeVisible (masterFxPad);
    setupSlider (crossfader, false);
    // A crossfader's raw 0..1 value is meaningless; show the actual A/C <-> B/D
    // balance instead, and double-click snaps back to CENTER.
    crossfader.textFromValueFunction = [] (double v) -> juce::String
    {
        if (std::abs (v - 0.5) < 0.02) return "CENTER";
        const int pct = (int) std::round (std::abs (v - 0.5) * 200.0);
        return (v < 0.5 ? "A/C " : "B/D ") + juce::String (pct) + "%";
    };
    crossfader.setDoubleClickReturnValue (true, 0.5);
    crossfader.updateText();
    crossfader.setTooltip ("Crossfader: blends the A/C bus into the B/D bus. Double-click to centre.");
    setupSlider (masterBpm, false, " BPM");
    setupSlider (master, true, " dB");
    setupSlider (limiter, true, " dB");
    // Widen these readouts so "120.0 BPM" / "-60.0 dB" fit without truncation.
    masterBpm.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    master   .setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    limiter  .setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
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
    micGain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
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
    setupButton (arrRecordButton);
    arrRecordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5f6a));
    arrRecordButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    arrRecordButton.onClick = [this] { toggleRecording(); };
    addAndMakeVisible (arrRecordButton);
    setupButton (undoButton);
    undoButton.onClick = [this] { arrangementView.undo(); };
    addAndMakeVisible (undoButton);
    setupButton (redoButton);
    redoButton.onClick = [this] { arrangementView.redo(); };
    addAndMakeVisible (redoButton);
    setupButton (arrZoomOutButton);
    arrZoomOutButton.setTooltip ("Zoom out");
    arrZoomOutButton.onClick = [this] { arrangementView.zoomOut(); };
    addAndMakeVisible (arrZoomOutButton);
    setupButton (arrZoomInButton);
    arrZoomInButton.setTooltip ("Zoom in");
    arrZoomInButton.onClick = [this] { arrangementView.zoomIn(); };
    addAndMakeVisible (arrZoomInButton);
    setupButton (arrZoomFitButton);
    arrZoomFitButton.setTooltip ("Zoom to fit");
    arrZoomFitButton.onClick = [this] { arrangementView.zoomToFit(); };
    addAndMakeVisible (arrZoomFitButton);
    setupButton (arrLoopButton);
    arrLoopButton.setClickingTogglesState (true);
    arrLoopButton.setTooltip ("Loop the region (drag the ruler to set it)");
    arrLoopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    arrLoopButton.onClick = [this] { arrangementView.setLoopEnabled (arrLoopButton.getToggleState()); };
    addAndMakeVisible (arrLoopButton);

    setupButton (pianoRollButton);
    pianoRollButton.setClickingTogglesState (true);
    pianoRollButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    pianoRollButton.onClick = [this]
    {
        if (pianoRollButton.getToggleState())
            openPianoRollForClip (pianoRollLane < 0 ? 0 : pianoRollLane, pianoRollClip);
        else
            pianoRoll.setVisible (false);
    };
    addAndMakeVisible (pianoRollButton);

    pianoRoll.onClose = [this]
    {
        pianoRoll.setVisible (false);
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
    };
    pianoRoll.onNotesChanged = [this]
    {
        std::vector<ArrangementRecorderView::MidiNote> notes;
        for (const auto& n : pianoRoll.getNotes())
            notes.push_back ({ n.pitch, n.startBeats, n.lengthBeats, n.velocity });
        arrangementView.setMidiClipNotes (pianoRollLane, pianoRollClip, std::move (notes));
    };
    pianoRoll.setBars (8);
    addChildComponent (pianoRoll);   // hidden until toggled

    arrangementView.onEditMidiClip = [this] (int lane, int clip) { openPianoRollForClip (lane, clip); };

    // ── Mixer overlay (deck channel strips: volume + mute/solo) ──
    setupButton (mixerButton);
    mixerButton.setClickingTogglesState (true);
    mixerButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    mixerButton.onClick = [this]
    {
        const bool on = mixerButton.getToggleState();
        if (on) { pianoRollButton.setToggleState (false, juce::dontSendNotification); fadeOverlay (pianoRoll, false); }
        fadeOverlay (mixerView, on);
        if (on) mixerView.toFront (true);
    };
    addAndMakeVisible (mixerButton);

    // ── djay-Pro nav bar: LIBRARY / DECKS / MIXER / ARRANGER / PERFORMANCE ──
    // Icon + label are painted in paint(); buttons are naked click targets that
    // only show a subtle highlight when active.
    for (juce::Button* b : { (juce::Button*) &toggleLibraryButton, (juce::Button*) &navDecksButton,
                             (juce::Button*) &mixerButton, (juce::Button*) &pianoRollButton,
                             (juce::Button*) &toggleBrowserButton })
    {
        b->setButtonText ({});
        b->getProperties().set ("navNaked", true);
    }
    setupButton (navDecksButton);
    navDecksButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    navDecksButton.onClick = [this]
    {
        fadeOverlay (pianoRoll, false);
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
        fadeOverlay (mixerView, false);
        mixerButton.setToggleState (false, juce::dontSendNotification);
        if (toggleLibraryButton.getToggleState()) { toggleLibraryButton.setToggleState (false, juce::dontSendNotification); toggleLibrary(); }
        if (toggleBrowserButton.getToggleState()) { toggleBrowserButton.setToggleState (false, juce::dontSendNotification); toggleFileBrowser(); }
    };
    addAndMakeVisible (navDecksButton);

    // Global Key Lock: engages pitch-preserve on all four decks at once.
    setupButton (keyLockAllButton);
    keyLockAllButton.setClickingTogglesState (true);
    keyLockAllButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    keyLockAllButton.onClick = [this]
    {
        const float v = keyLockAllButton.getToggleState() ? 1.0f : 0.0f;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (i) + "KeyLock"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (v);
                p->endChangeGesture();
            }
    };
    addAndMakeVisible (keyLockAllButton);

    // Smart mastering assistant controls (toolbar): toggle + target mode.
    setupButton (aiMasterButton);
    aiMasterButton.setClickingTogglesState (true);
    aiMasterButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    aiMasterButton.setTooltip ("AI mastering: auto EQ balance + bus dynamics + loudness targeting");
    addAndMakeVisible (aiMasterButton);
    aiMasterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.parameters(), "smartMasterOn", aiMasterButton);

    aiMasterMode.addItem ("Streaming -14", 1);
    aiMasterMode.addItem ("Club -9", 2);
    aiMasterMode.addItem ("Warm -16", 3);
    addAndMakeVisible (aiMasterMode);
    aiMasterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.parameters(), "smartMasterMode", aiMasterMode);

    // ── Rekordbox XML interop: export library + cues / import cue points ────
    importedCues = RekordboxIO::loadCueStore();
    setupButton (rbxExportButton);
    rbxExportButton.setTooltip ("Export the analyzed library + hot cues as rekordbox.xml");
    rbxExportButton.onClick = [this]
    {
        rbxChooser = std::make_unique<juce::FileChooser> ("Export rekordbox XML",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("rekordbox.xml"), "*.xml");
        rbxChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto dest = fc.getResult();
                if (dest == juce::File() || libraryView == nullptr)
                    return;
                const auto entries = libraryView->getAnalyzedEntries();
                const bool ok = RekordboxIO::exportXml (dest, entries,
                    [this] (const juce::String& path, std::vector<RekordboxIO::Cue>& cues)
                    {
                        for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // loaded decks: live cues
                            if (processorRef.getDeckPath (d) == path)
                                for (int c = 0; c < MainPluginProcessor::numHotCues; ++c)
                                    if (const double s = processorRef.getHotCue (d, c); s >= 0.0)
                                        cues.push_back ({ c, s });
                        if (cues.empty())                                          // else: previously imported
                            if (const auto found = importedCues.find (path); found != importedCues.end())
                                cues = found->second;
                    });
                recordStatus.setText (ok ? "Exported " + juce::String ((int) entries.size()) + " tracks to " + dest.getFileName()
                                         : "Rekordbox export failed", juce::dontSendNotification);
            });
    };
    addAndMakeVisible (rbxExportButton);

    setupButton (rbxImportButton);
    rbxImportButton.setTooltip ("Import cue points / metadata from a rekordbox XML export");
    rbxImportButton.onClick = [this]
    {
        rbxChooser = std::make_unique<juce::FileChooser> ("Import rekordbox XML",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.xml");
        rbxChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto src = fc.getResult();
                if (! src.existsAsFile())
                    return;
                int seen = 0;
                auto imported = RekordboxIO::importXml (src, &seen);
                for (auto& [path, cues] : imported)
                    importedCues[path] = std::move (cues);
                RekordboxIO::saveCueStore (importedCues);
                for (int d = 0; d < MainPluginProcessor::numDecks; ++d)   // apply to loaded decks now
                    applyImportedCues (d);
                recordStatus.setText ("Imported cues for " + juce::String ((int) imported.size())
                                        + " of " + juce::String (seen) + " tracks",
                                      juce::dontSendNotification);
            });
    };
    addAndMakeVisible (rbxImportButton);

    mixerView.numChannels = MainPluginProcessor::numDecks;
    mixerView.onClose    = [this] { fadeOverlay (mixerView, false); mixerButton.setToggleState (false, juce::dontSendNotification); };
    // These strips control the ARRANGEMENT lanes (gain/mute/solo of the timeline),
    // not the live deck channels — name them honestly.
    mixerView.getName    = [] (int i) { return "LANE " + juce::String::charToString ((juce_wchar) ('A' + i)); };
    mixerView.getColour  = [] (int i) { return deckAccent (i); };
    // Live lane mix: mute/solo/gain changes are applied to the running engine
    // immediately (TE audio lanes + MIDI synth lanes), not just at the next
    // schedule rebuild.
    arrangementView.onLaneMixChanged = [this]
    {
        for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
        {
            const float gain = arrangementView.laneAudible (lane) ? arrangementView.laneGainLinear (lane) : 0.0f;
            processorRef.setTeArrangementLaneGain (lane, gain);
            processorRef.setMidiLaneGain (lane, gain);
        }
    };
    mixerView.getGainDb  = [this] (int i) { return arrangementView.getLaneGainDb (i); };
    mixerView.setGainDb  = [this] (int i, float db) { arrangementView.setLaneGainDb (i, db);
                                                      if (arrangementView.onLaneMixChanged) arrangementView.onLaneMixChanged(); };
    mixerView.isMuted    = [this] (int i) { return arrangementView.isLaneMuted (i); };
    mixerView.isSoloed   = [this] (int i) { return arrangementView.isLaneSoloed (i); };
    mixerView.toggleMute = [this] (int i) { arrangementView.toggleLaneMute (i); };
    mixerView.toggleSolo = [this] (int i) { arrangementView.toggleLaneSolo (i); };
    addChildComponent (mixerView);   // hidden until toggled

    setupButton (saveProjButton);
    saveProjButton.setTooltip ("Save the arrangement + piano-roll to a .stemdeck project");
    saveProjButton.onClick = [this] { saveProject(); };
    addAndMakeVisible (saveProjButton);
    setupButton (openProjButton);
    openProjButton.setTooltip ("Open a .stemdeck project");
    openProjButton.onClick = [this] { loadProject(); };
    addAndMakeVisible (openProjButton);

    setupButton (metronomeButton);
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setTooltip ("Metronome click during arrangement playback");
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (sattari::brand::kAccent).withAlpha (0.55f));
    metronomeButton.onClick = [this] { processorRef.setMetronomeEnabled (metronomeButton.getToggleState()); };
    addAndMakeVisible (metronomeButton);

    setupButton (talkoverButton);
    talkoverButton.setClickingTogglesState (true);
    talkoverButton.setTooltip ("Talkover ducking: auto-reduces deck volume when mic is active");
    talkoverButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff5f6a).withAlpha (0.65f));
    talkoverButton.onClick = [this] { processorRef.setTalkoverEnabled (talkoverButton.getToggleState()); };
    addAndMakeVisible (talkoverButton);

    setupButton (overlapModeButton);
    overlapModeButton.setTooltip ("Clip overlap mode: Keep / Push / Replace");
    overlapModeButton.onClick = [this]
    {
        // Cycle Keep → Push → Replace → Keep
        const auto mode = arrangementView.getOverlapMode();
        if (mode == ArrangementRecorderView::OverlapMode::Keep)
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Push);
            overlapModeButton.setButtonText ("Push");
        }
        else if (mode == ArrangementRecorderView::OverlapMode::Push)
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Replace);
            overlapModeButton.setButtonText ("Repl");
        }
        else
        {
            arrangementView.setOverlapMode (ArrangementRecorderView::OverlapMode::Keep);
            overlapModeButton.setButtonText ("Keep");
        }
    };
    addAndMakeVisible (overlapModeButton);

    // Launch-all: queue clip[0] of every occupied deck lane at next beat
    setupButton (launchAllButton);
    launchAllButton.setTooltip ("Queue all deck lanes to launch at the next beat");
    launchAllButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1d5c40));
    launchAllButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.92f));
    launchAllButton.onClick = [this] { arrangementView.triggerAllSlots(); };
    addAndMakeVisible (launchAllButton);

    // Wire follow-action popup request from ArrangementRecorderView
    arrangementView.onFollowActionRequest = [this] (int lane, int clip) { showFollowActionMenu (lane, clip); };

    arrangementView.onSeek = [this] (double s)
    {
        arrangementCursorSeconds = s;
        if (processorRef.isArrangementPlaying())
            processorRef.setArrangementPositionSeconds (s);
    };
    arrangementView.onLoopChanged = [this] (double s, double e, bool on)
    {
        arrangementLoopStart = s; arrangementLoopEnd = e; arrangementLoopEnabled = on;
        arrLoopButton.setToggleState (on, juce::dontSendNotification);
        processorRef.setArrangementLoop (s, e, on);
    };
    arrangementView.onStopDeckRequest = [this] (int lane)
    {
        if (lane >= 0 && lane < MainPluginProcessor::numDecks)
            if (auto* p = processorRef.parameters().getParameter (deckPrefix (lane) + "Play"))
                p->setValueNotifyingHost (0.0f);
    };
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
    recordStatus.setFont (juce::FontOptions (11.5f, juce::Font::bold));
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
        pad.trigger.setButtonText ({});   // blank interior (mockup pads are unlabeled)
        setupButton (pad.trigger);
        // djay-Pro pads: spectrum colour per pad, drawn as a dark cell with a
        // glowing coloured border (LookAndFeel reads the "padColour" property;
        // the editor paints a soft halo behind each pad).
        {
            const auto pc = juce::Colour (padSpectrum ((int) i));
            pad.trigger.getProperties().set ("padColour", (int) pc.getARGB());
            pad.trigger.setColour (juce::TextButton::textColourOffId, pc.brighter (0.5f));
        }
        setupButton (pad.load);
        setupSlider (pad.gain, false, " dB");
        pad.trigger.onClick = [this, i]
        {
            processorRef.triggerPad (i);
            padFlashMs[(size_t) i] = juce::Time::getMillisecondCounterHiRes();
        };
        pad.load.onClick = [this, i] { openPadChooser (i); };
        pad.gainAttachment = std::make_unique<SliderAttachment> (processorRef.parameters(), "pad" + juce::String (i + 1) + "Gain", pad.gain);
        addAndMakeVisible (pad.trigger);
        addAndMakeVisible (pad.load);
        addAndMakeVisible (pad.gain);
    }

    startTimerHz (30);   // smooth meters, nav-underline slide, and pad flashes

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

    // All-deck sync hotkeys: B = BPM sync, K = key sync (no modifiers; pads use 1-8).
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (key.getKeyCode() == 'B') { syncBpmAll(); return true; }
        if (key.getKeyCode() == 'K') { syncKeyAll(); return true; }
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
        // djay-Pro cells: the four separated stems are vertical mini-faders;
        // the full-mix trim stays a knob (relocated to the SRC tab).
        if (lane != DeckPlayer::fullMixStem)
            slider.setSliderStyle (juce::Slider::LinearVertical);
        addAndMakeVisible (slider);
        controls.stemGainAttachments[(size_t) lane] = std::make_unique<SliderAttachment> (
            processorRef.parameters(),
            prefix + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
            slider);
    }
}

void MainPluginEditor::applyExtractedMelody (int deckIndex, const std::vector<MelodyExtractor::Note>& notes, bool neural)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    auto& dc = deckControls[(size_t) deckIndex];
    if (notes.empty())
    {
        dc.stemStatus.setText ("No melody found (needs a clear monophonic line)", juce::dontSendNotification);
        return;
    }

    // Seconds → beats at the current project tempo, then into a MIDI clip on
    // this deck's arrangement lane (editable in the piano roll).
    const double bpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
    const double beatsPerSec = bpm / 60.0;

    std::vector<ArrangementRecorderView::MidiNote> midiNotes;
    midiNotes.reserve (notes.size());
    for (const auto& n : notes)
        midiNotes.push_back ({ n.pitch,
                               n.startSeconds * beatsPerSec,
                               juce::jmax (0.125, n.lengthSeconds * beatsPerSec),
                               n.velocity });

    const int idx = arrangementView.addMidiClip (deckIndex, 0.0);
    arrangementView.setMidiClipNotes (deckIndex, idx, std::move (midiNotes));
    dc.stemStatus.setText (juce::String (neural ? "Neural: " : "YIN: ")
                             + juce::String ((int) notes.size()) + u8 (" notes \xe2\x86\x92 MIDI clip"),
                           juce::dontSendNotification);
}

void MainPluginEditor::applyImportedCues (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    const auto path = processorRef.getDeckPath (deckIndex);
    if (path.isEmpty())
        return;
    const auto found = importedCues.find (path);
    if (found == importedCues.end())
        return;
    for (const auto& cue : found->second)
        processorRef.setHotCue (deckIndex, cue.num, cue.seconds);
    refreshHotCueButtonColours (deckIndex);
    updateWaveformHotCues (deckIndex);
}

void MainPluginEditor::applyExtractedDrums (int deckIndex, const std::vector<MelodyExtractor::Note>& hits)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks))
        return;
    auto& dc = deckControls[(size_t) deckIndex];
    if (hits.empty())
    {
        dc.stemStatus.setText ("No drum hits detected", juce::dontSendNotification);
        return;
    }

    const double bpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
    const double beatsPerSec = bpm / 60.0;

    int kicks = 0, snares = 0, hats = 0;
    std::vector<ArrangementRecorderView::MidiNote> midiNotes;
    midiNotes.reserve (hits.size());
    for (const auto& h : hits)
    {
        midiNotes.push_back ({ h.pitch, h.startSeconds * beatsPerSec, 0.25, h.velocity });
        if (h.pitch == 36) ++kicks; else if (h.pitch == 42) ++hats; else ++snares;
    }

    const int idx = arrangementView.addMidiClip (deckIndex, 0.0);
    arrangementView.setMidiClipNotes (deckIndex, idx, std::move (midiNotes));
    dc.stemStatus.setText ("Drums: " + juce::String (kicks) + " kick / " + juce::String (snares)
                             + " snare / " + juce::String (hats) + u8 (" hat \xe2\x86\x92 MIDI clip"),
                           juce::dontSendNotification);
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

    // NOTE: do NOT clear the arrangement or disable Reveal/Reload/Bounce yet —
    // cancelling the save dialog must leave the current work untouched. Only wipe
    // once a destination is chosen and recording actually starts.
    recordChooser.launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
                return;   // cancelled — arrangement + buttons preserved

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            const bool started = processorRef.beginMasterRecording (file);
            recordStatus.setText (started ? "Recording: " + file.getFileName() : "Could not start recording", juce::dontSendNotification);
            if (started)
            {
                arrangementView.clearArrangement();
                revealRecordingButton.setEnabled (false);
                reloadIntoDecksButton.setEnabled (false);
                bounceEditsButton.setEnabled (false);
                arrangementView.setRecordingProgress (0.0, file.getFullPathName());
            }
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
        if (! arrangementView.laneAudible (lane)) continue;   // honour per-lane mute/solo
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
            spec.gain                 = arrangementView.laneGainLinear (lane);
            spec.fadeInSeconds        = clip.fadeInSeconds;
            spec.fadeOutSeconds       = clip.fadeOutSeconds;
            spec.lane                 = lane;
            specs.push_back (spec);
        }
    }

    // Piano-roll MIDI notes play through the built-in synth on the same transport.
    const double midiBpm = juce::jlimit (40.0, 240.0,
        (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());   // project tempo
    const double secsPerBeat = 60.0 / midiBpm;
    std::vector<MidiClipPlayer::SourceNote> midiNotes;
    double midiLenSec = 0.0;
    for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
    {
        if (lane < MainPluginProcessor::numDecks && ! arrangementView.laneAudible (lane)) continue;   // mute/solo
        for (int ci = 0; ci < arrangementView.numMidiClips (lane); ++ci)
        {
            const double clipStartBeats = arrangementView.midiClipStart (lane, ci) / secsPerBeat;
            for (const auto& n : arrangementView.midiClipNotes (lane, ci))
            {
                const double absStartBeats = clipStartBeats + n.startBeats;
                midiNotes.push_back ({ lane, n.pitch, absStartBeats, n.lengthBeats, n.velocity });
                midiLenSec = juce::jmax (midiLenSec, (absStartBeats + n.lengthBeats) * secsPerBeat);
            }
        }
    }
    // Per-lane MIDI gain (true gain stage on each lane's synth, independent of velocity).
    for (int lane = 0; lane < ArrangementRecorderView::numEditableLanes; ++lane)
        processorRef.setMidiLaneGain (lane, lane < MainPluginProcessor::numDecks
                                                ? arrangementView.laneGainLinear (lane) : 1.0f);

    double audioLenSec = 0.0;
    for (const auto& s : specs)
        audioLenSec = juce::jmax (audioLenSec, s.timelineStartSeconds + s.lengthSeconds);

    if (specs.empty() && midiNotes.empty())
    {
        recordStatus.setText (u8 ("Nothing to play \xe2\x80\x94 record, drop a file, or draw notes first"), juce::dontSendNotification);
        return;
    }

    processorRef.setMidiNotes (midiNotes, midiBpm);
    processorRef.setMetronomeBpm (midiBpm);
    processorRef.buildAndStartArrangement (std::move (specs),
                                           juce::jmax (audioLenSec, midiLenSec),
                                           arrangementCursorSeconds);
    processorRef.setArrangementLoop (arrangementLoopStart, arrangementLoopEnd, arrangementLoopEnabled);
    recordStatus.setText (u8 ("Playing arrangement\xe2\x80\xa6"), juce::dontSendNotification);
}

void MainPluginEditor::openPianoRollForClip (int lane, int clip)
{
    if (! juce::isPositiveAndBelow (lane, ArrangementRecorderView::numEditableLanes)) lane = 0;
    if (! juce::isPositiveAndBelow (clip, arrangementView.numMidiClips (lane)))
        clip = arrangementView.numMidiClips (lane) > 0 ? 0 : arrangementView.addMidiClip (lane, arrangementCursorSeconds);
    pianoRollLane = lane;
    pianoRollClip = clip;

    std::vector<PianoRollView::Note> notes;
    for (const auto& n : arrangementView.midiClipNotes (lane, clip))
        notes.push_back ({ n.pitch, n.startBeats, n.lengthBeats, n.velocity });
    pianoRoll.setNotes (std::move (notes));

    mixerButton.setToggleState (false, juce::dontSendNotification);
    fadeOverlay (mixerView, false);
    pianoRollButton.setToggleState (true, juce::dontSendNotification);
    fadeOverlay (pianoRoll, true);
    pianoRoll.toFront (true);
}

void MainPluginEditor::saveProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Save StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            f = f.withFileExtension ("stemdeck");
            juce::ValueTree root ("STEMDECK_PROJECT");
            root.appendChild (arrangementView.toState(), nullptr);
            root.appendChild (pianoRoll.toState(), nullptr);
            if (auto xml = root.createXml())
                xml->writeTo (f);
            recordStatus.setText ("Saved project: " + f.getFileName(), juce::dontSendNotification);
        });
}

void MainPluginEditor::loadProject()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Open StemDeck project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.stemdeck");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            if (auto xml = juce::XmlDocument::parse (f))
            {
                const auto root = juce::ValueTree::fromXml (*xml);
                arrangementView.fromState (root.getChildWithName ("ARRANGEMENT"));
                pianoRoll.fromState (root.getChildWithName ("PIANOROLL"));
                recordStatus.setText ("Loaded project: " + f.getFileName(), juce::dontSendNotification);
            }
        });
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
    browserPathLabel.setFont (juce::FontOptions (11.5f));
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
    // AI set builder: top four harmonic picks land straight on decks A-D.
    libraryView->onLoadSet = [this] (const std::array<juce::File, 4>& set)
    {
        for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        {
            if (! set[(size_t) d].existsAsFile())
                continue;
            processorRef.loadDeckFileAsync (d, set[(size_t) d]);
            if (auto* wave = deckWaves[(size_t) d].get())
                wave->setFile (set[(size_t) d]);
        }
    };
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

void MainPluginEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking the "StemDeck PRO" title reveals/hides the advanced toolbar.
    if (e.eventComponent == &title)
    {
        advancedVisible = ! advancedVisible;
        advancedToggle.setToggleState (advancedVisible, juce::dontSendNotification);
        resized();
        repaint();
    }
}

void MainPluginEditor::setDeckBpm (int deckIndex, double bpm)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)) return;
    if (auto* p = processorRef.parameters().getParameter (deckPrefix (deckIndex) + "Bpm"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (40.0, 240.0, bpm)));
    repaint();
}

void MainPluginEditor::showKeyMenuForDeck (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks)) return;
    const auto choices = processorRef.getKeyTargetChoices();
    const int current = (int) std::round (processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "KeyTarget")->load());
    juce::PopupMenu menu;
    menu.addSectionHeader ("Set Deck " + juce::String::charToString ((juce_wchar) ('A' + deckIndex)) + " key");
    for (int c = 0; c < choices.size(); ++c)
        menu.addItem (c + 1, choices[c], true, c == current);
    juce::Component::SafePointer<MainPluginEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(),
        [safe, deckIndex] (int choice)
        {
            if (safe == nullptr || choice <= 0) return;
            if (auto* p = safe->processorRef.parameters().getParameter (safe->deckPrefix (deckIndex) + "KeyTarget"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (choice - 1)));
            safe->repaint();
        });
}

void MainPluginEditor::syncBpmAll()
{
    auto& p = processorRef.parameters();
    const auto masterLetter = juce::String::charToString ((juce_wchar) ('A' + masterDeckIndex));
    const float refBpm = (float) p.getRawParameterValue (deckPrefix (masterDeckIndex) + "Bpm")->load();
    if (auto* mb = p.getParameter ("masterBpm"))
        mb->setValueNotifyingHost (mb->convertTo0to1 (refBpm));
    for (const char* id : { "hostSync", "beatSync" })
        if (auto* b = p.getParameter (id))
            b->setValueNotifyingHost (1.0f);
    // Downbeat phase-lock: force strict sync mode (choice 0) on every deck so the
    // WarpEngine grid-aligns each deck's downbeat to the master beat position.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (auto* sm = p.getParameter (deckPrefix (d) + "SyncMode"))
            sm->setValueNotifyingHost (sm->convertTo0to1 (0.0f));
    recordStatus.setText ("BPM SYNC: all decks warped + downbeat phase-locked to Deck " + masterLetter
                          + " (" + juce::String (refBpm, 1) + " BPM)", juce::dontSendNotification);
    repaint();
}

void MainPluginEditor::syncKeyAll()
{
    auto& p = processorRef.parameters();
    const auto masterLetter = juce::String::charToString ((juce_wchar) ('A' + masterDeckIndex));
    const auto refA = processorRef.getDeckAnalysis (masterDeckIndex);
    if (! refA.hasKey())
    {
        recordStatus.setText ("KEY SYNC: Deck " + masterLetter + " has no analyzed key yet — load/analyze audio first",
                              juce::dontSendNotification);
        return;
    }
    const int keyA = refA.keyPitchClass;
    // Camelot-compatible pitch classes relative to the master deck: tonic, +7 (fifth
    // up), +5 (fourth up / fifth down) — the harmonically adjacent moves.
    const std::array<int, 3> compat { keyA, (keyA + 7) % 12, (keyA + 5) % 12 };

    // masterKey Off so per-deck KeyTargets take effect.
    if (auto* mk = p.getParameter ("masterKey"))
        mk->setValueNotifyingHost (mk->convertTo0to1 (0.0f));

    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        int target = keyA;
        const auto a = processorRef.getDeckAnalysis (d);
        if (d != masterDeckIndex && a.hasKey())
        {
            // Choose the compatible key needing the SMALLEST pitch shift from this
            // deck's own key (harmonic mixing without over-shifting the audio).
            int best = compat[0], bestShift = 99;
            for (int c : compat)
            {
                const int raw = ((c - a.keyPitchClass) % 12 + 12) % 12;
                const int shift = juce::jmin (raw, 12 - raw);
                if (shift < bestShift) { bestShift = shift; best = c; }
            }
            target = best;
        }
        if (auto* kt = p.getParameter (deckPrefix (d) + "KeyTarget"))
            kt->setValueNotifyingHost (kt->convertTo0to1 ((float) (target + 1)));
    }
    recordStatus.setText ("KEY SYNC (harmonic): decks matched to Deck " + masterLetter + " "
                          + processorRef.getKeyTargetChoices()[keyA + 1] + " via Camelot-compatible keys",
                          juce::dontSendNotification);
    repaint();
}

void MainPluginEditor::setShowcaseMode (bool on)
{
    showcaseMode = on;
    // Decks A/B/C: demo waveform + PLAY-on only in showcase; otherwise reflect real
    // loaded audio / real transport state.
    for (int i = 0; i < juce::jmin (3, MainPluginProcessor::numDecks); ++i)
    {
        if (auto* w = deckWaves[(size_t) i].get())
        {
            w->setDemoWaveform (on);
            w->setShowDropHint (! on);
        }
        const bool realPlaying = processorRef.parameters().getRawParameterValue (deckPrefix (i) + "Play")->load() > 0.5f;
        deckControls[(size_t) i].play.setToggleState (on ? true : realPlaying, juce::dontSendNotification);
    }
    arrangementView.setShowDemoBlocks (on);
    recordStatus.setText (on ? "Showcase demo content ON" : "Showcase off — showing live loaded audio",
                          juce::dontSendNotification);
    resized();
    repaint();
}

void MainPluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Settings");
    menu.addItem (1, "Advanced tools (plugin host, AI master, rekordbox, project I/O)", true, advancedVisible);
    menu.addItem (2, "Showcase demo content (fake titles / waveforms / clips)", true, showcaseMode);
    menu.addSeparator();
    menu.addItem (3, "Re-sync motorized controllers", true, false);
    juce::Component::SafePointer<MainPluginEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton),
        [safe] (int choice)
        {
            if (safe == nullptr) return;
            if (choice == 1) { safe->advancedVisible = ! safe->advancedVisible; safe->advancedToggle.setToggleState (safe->advancedVisible, juce::dontSendNotification); safe->resized(); safe->repaint(); }
            else if (choice == 2) safe->setShowcaseMode (! safe->showcaseMode);
            else if (choice == 3) safe->processorRef.getMidiMapping().resyncFeedback();
        });
}

void MainPluginEditor::setMasterDeck (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, MainPluginProcessor::numDecks) && deckIndex != masterDeckIndex)
    {
        masterDeckIndex = deckIndex;
        recordStatus.setText ("Master deck = Deck " + juce::String::charToString ((juce_wchar) ('A' + deckIndex))
                              + " (BPM/Key sync reference)", juce::dontSendNotification);
        repaint();
    }
}

void MainPluginEditor::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckBadgeBounds[(size_t) i].contains (e.getPosition()))
        {
            setMasterDeck (i);
            return;
        }

    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        if (deckBpmChipBounds[(size_t) i].contains (e.getPosition()))
        {
            bpmDragDeck    = i;
            bpmDragStartY  = e.getPosition().y;
            bpmDragStartVal = (double) deckControls[(size_t) i].bpm.getValue();
            return;
        }
        if (deckKeyChipBounds[(size_t) i].contains (e.getPosition()))
        {
            showKeyMenuForDeck (i);
            return;
        }
    }

    // Console deck meters: click to cycle this deck's crossfader assignment
    // (A/C side -> Center -> B/D side), mirrored to the deck's Ctr/A/B buttons.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
        if (deckMeterBounds[(size_t) d].contains (e.getPosition()))
        {
            const int next = (processorRef.getDeckCfSide (d) + 1) % 3;
            processorRef.setDeckCfSide (d, next);
            for (int ss = 0; ss < 3; ++ss)
                deckControls[(size_t) d].cfSideBtns[(size_t) ss].setToggleState (ss == next, juce::dontSendNotification);
            repaint();
            return;
        }

    // GLOBAL TEMPO box: vertical drag adjusts the master tempo (same gesture as
    // the deck BPM chips) — the painted box is a real control, not a readout.
    if (masterTempoBounds.contains (e.getPosition()))
    {
        masterBpmDragging  = true;
        bpmDragStartY      = e.getPosition().y;
        bpmDragStartVal    = masterBpm.getValue();
        return;
    }
}

void MainPluginEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (bpmDragDeck >= 0)   // vertical drag on a BPM chip = nudge tempo
        setDeckBpm (bpmDragDeck, bpmDragStartVal + (double) (bpmDragStartY - e.getPosition().y) * 0.25);
    else if (masterBpmDragging)
        masterBpm.setValue (bpmDragStartVal + (double) (bpmDragStartY - e.getPosition().y) * 0.25,
                            juce::sendNotificationSync);
}

void MainPluginEditor::mouseUp (const juce::MouseEvent&)
{
    bpmDragDeck = -1;   // disarm so later, unrelated drags don't keep nudging tempo
    masterBpmDragging = false;
}

void MainPluginEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
        if (deckBpmChipBounds[(size_t) i].contains (e.getPosition()))
        {
            setDeckBpm (i, (double) deckControls[(size_t) i].bpm.getValue() + wheel.deltaY * 20.0);
            return;
        }

    if (masterTempoBounds.contains (e.getPosition()))
        masterBpm.setValue (masterBpm.getValue() + wheel.deltaY * 20.0, juce::sendNotificationSync);
}

void MainPluginEditor::drawNavIcon (juce::Graphics& g, int which, juce::Rectangle<float> b, juce::Colour c) const
{
    g.setColour (c);
    const float s = juce::jmin (b.getWidth(), b.getHeight());
    b = juce::Rectangle<float> (s, s).withCentre (b.getCentre());
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    const float t = juce::jmax (1.4f, s * 0.09f);   // stroke

    switch (which)
    {
        case 0:   // LIBRARY — folder
        {
            juce::Path p;
            p.startNewSubPath (x, y + h * 0.28f);
            p.lineTo (x, y + h * 0.85f); p.lineTo (x + w, y + h * 0.85f); p.lineTo (x + w, y + h * 0.15f);
            p.lineTo (x + w * 0.45f, y + h * 0.15f); p.lineTo (x + w * 0.34f, y + h * 0.28f); p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (t));
            break;
        }
        case 1:   // DECKS — 2×2 grid
        {
            const float g2 = w * 0.12f, cw = (w - g2) * 0.5f, ch = (h - g2) * 0.5f;
            for (int r = 0; r < 2; ++r)
                for (int col = 0; col < 2; ++col)
                    g.fillRoundedRectangle (x + (float) col * (cw + g2), y + (float) r * (ch + g2), cw, ch, 1.5f);
            break;
        }
        case 2:   // MIXER — three vertical sliders
        {
            for (int i = 0; i < 3; ++i)
            {
                const float cx = x + w * (0.2f + 0.3f * (float) i);
                g.drawLine (cx, y, cx, y + h, t);
                const float capY = y + h * (i == 1 ? 0.55f : 0.28f);
                g.fillRoundedRectangle (cx - w * 0.10f, capY, w * 0.20f, h * 0.14f, 1.5f);
            }
            break;
        }
        case 3:   // ARRANGER — timeline rows
        {
            for (int i = 0; i < 3; ++i)
            {
                const float ry = y + h * (0.15f + 0.32f * (float) i);
                g.fillRoundedRectangle (x, ry, w * (i == 1 ? 0.6f : 0.9f), h * 0.13f, 1.5f);
            }
            break;
        }
        default:  // PERFORMANCE — 3×3 dots
        {
            const float d = w * 0.2f, gap = (w - d * 3.0f) * 0.5f;
            for (int r = 0; r < 3; ++r)
                for (int col = 0; col < 3; ++col)
                    g.fillEllipse (x + (float) col * (d + gap), y + (float) r * (d + gap), d, d);
            break;
        }
    }
}

void MainPluginEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintModernBackground (g, getLocalBounds());

    // --- Header bar: flat, no backdrop. Thin hairline underline only. ---
    if (! headerBarBounds.isEmpty())
    {
        namespace sdp = stemdeck::palette;
        const auto r = headerBarBounds.toFloat();
        g.setColour (juce::Colour (sdp::kHairline).withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 1.0f, r.getWidth() - 8.0f, 1.0f));
    }

    // --- Nav bar: icon above label per cell (LIBRARY/DECKS/MIXER/ARRANGER/PERFORMANCE) ---
    {
        static const char* navLabels[5] = { "LIBRARY", "DECKS", "MIXER", "ARRANGER", "FILES" };
        const juce::Button* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                           &pianoRollButton, &toggleBrowserButton };
        for (int i = 0; i < 5; ++i)
        {
            const auto cell = navRects[(size_t) i];
            if (cell.isEmpty()) continue;
            const bool active = navBtns[i]->getToggleState();
            const auto col = active ? deckAccent (0) : juce::Colour (stemdeck::palette::kTextMuted);
            auto icon = cell.withHeight (22).translated (0, 3).toFloat().reduced ((cell.getWidth() - 22) * 0.5f, 0.0f);
            drawNavIcon (g, i, icon, col.withAlpha (active ? 0.95f : 0.7f));
            g.setColour (col.withAlpha (active ? 0.98f : 0.72f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            drawTracked (g, navLabels[i], cell.withTop (cell.getY() + 27).withHeight (12), 1.6f);
        }

        // Animated active-view underline: one accent bar that slides between
        // nav cells (position eased in timerCallback).
        if (navUnderlineW > 1.0f && ! navRects[0].isEmpty())
        {
            const float y = (float) navRects[0].getBottom() - 2.0f;
            g.setColour (deckAccent (0));
            g.fillRoundedRectangle (navUnderlineX, y, navUnderlineW, 2.0f, 1.0f);
        }
    }

    // --- Pad-trigger flash: colour halo that decays over ~350 ms ---
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < PadBank::numPads; ++i)
        {
            const double age = nowMs - padFlashMs[(size_t) i];
            if (age < 0.0 || age > 350.0) continue;
            const float k = 1.0f - (float) (age / 350.0);
            const auto pb = pads[(size_t) i].trigger.getBounds().toFloat();
            if (pb.isEmpty()) continue;
            g.setColour (juce::Colour (padSpectrum (i)).withAlpha (0.35f * k * k));
            g.drawRoundedRectangle (pb.expanded (2.0f + 3.0f * (1.0f - k)), 8.0f, 2.0f);
        }
    }

    // --- Tempo chip in the right cluster ---
    if (! tempoChipBounds.isEmpty())
    {
        namespace sdp = stemdeck::palette;
        const auto r = tempoChipBounds.toFloat();
        g.setColour (juce::Colour (sdp::kPanelDeep));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (deckAccent (0).withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (sdp::kTextMuted));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText ("HOST TEMPO", juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 2.0f, r.getWidth() - 16.0f, 10.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (juce::Colour (stemdeck::palette::kHairline).withAlpha (0.5f));
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
            deckBadgeBounds[(size_t) i] = badge;   // click to set master deck
            g.setColour (deckAccent (i).withAlpha (0.95f));
            g.fillRoundedRectangle (badge.toFloat(), 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (letter, badge, juce::Justification::centred);
            // Master-deck indicator: bright white ring around the letter badge
            // (this deck is the BPM/Key sync reference). Click any badge to change.
            if (i == masterDeckIndex)
            {
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawRoundedRectangle (badge.toFloat().expanded (2.5f), 6.0f, 1.2f);
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.drawRoundedRectangle (badge.toFloat().expanded (1.5f), 5.0f, 1.6f);
            }
            headerBar.removeFromLeft (8);

            // Right-aligned, interactive BPM + Key chips (drag BPM, click Key).
            const auto bpmValue   = deckControls[(size_t) i].bpm.getValue();
            const auto bpmText    = juce::String (bpmValue, 1);
            const auto detectKey  = deckControls[(size_t) i].keyInfo.getText().upToFirstOccurrenceOf (" ", false, false);
            const int  keyChoice  = (int) std::round (processorRef.parameters().getRawParameterValue (deckPrefix (i) + "KeyTarget")->load());
            const bool detectValid = detectKey.isNotEmpty() && detectKey != "Key:" && detectKey != "unknown";
            const auto keyText     = keyChoice > 0 ? processorRef.getKeyTargetChoices()[keyChoice]
                                                   : (detectValid ? detectKey : u8 ("\xe2\x80\x94"));

            auto drawChip = [&] (juce::Rectangle<int> chip, const juce::String& text)
            {
                g.setColour (juce::Colour (stemdeck::palette::kPanelDeep));
                g.fillRoundedRectangle (chip.toFloat(), 3.0f);
                g.setColour (deckAccent (i).withAlpha (0.7f));   // brighter = interactive
                g.drawRoundedRectangle (chip.toFloat(), 3.0f, 1.0f);
                g.setColour (deckAccent (i).withAlpha (0.95f));
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (text, chip.reduced (6, 0), juce::Justification::centredRight);
            };

            auto keyChip = headerBar.removeFromRight (juce::jmin (58, headerBar.getWidth() / 3));
            deckKeyChipBounds[(size_t) i] = keyChip;
            drawChip (keyChip, u8 ("\xe2\x99\xa9 ") + keyText);
            headerBar.removeFromRight (4);
            auto bpmChip = headerBar.removeFromRight (juce::jmin (80, headerBar.getWidth() / 3));
            deckBpmChipBounds[(size_t) i] = bpmChip;
            drawChip (bpmChip, bpmText + " BPM");
            headerBar.removeFromRight (6);

            // Filename in the middle slot. A/B/C carry fixed showcase titles to
            // match the mockup; D shows the real file or an empty placeholder.
            static const char* kDemoTitle[4] = {
                "Moneyplagg & MealThePred\xe2\x80\xa6",
                "Life is real controlle way",
                "Moneyplagg & MealThePred\xe2\x80\xa6",
                "" };
            const auto path = processorRef.getDeckPath (i);
            const auto file = juce::File (path).getFileName();
            const bool useDemo = showcaseMode && i < 4 && kDemoTitle[i][0] != '\0';
            const auto display = useDemo ? u8 (kDemoTitle[i])
                                         : (file.isNotEmpty() ? file : u8 ("\xe2\x80\x94 empty deck \xe2\x80\x94"));
            g.setColour (juce::Colour (stemdeck::palette::kTextPrimary).withAlpha ((useDemo || file.isNotEmpty()) ? 0.92f : 0.45f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (display, headerBar.reduced (2, 0), juce::Justification::centredLeft, true);
        }

        // --- STEM SEPARATION centerpiece: accent-tinted module + per-stem labels ---
        auto stemBox = deckStemBounds[(size_t) i];
        if (! stemBox.isEmpty())
        {
            // djay-Pro stem cells: DRUMS | BASS | MUSIC | VOCALS | FX — each a
            // rounded box with a header; matches the fader layout in resized().
            static constexpr std::array<const char*, 5> cellNames { "DRUMS", "BASS", "MUSIC", "VOCALS", "FX" };
            const int cellW = juce::jmax (28, stemBox.getWidth() / 5);
            auto cells = stemBox;
            for (int s = 0; s < 5; ++s)
            {
                const auto cell = (s < 4 ? cells.removeFromLeft (cellW) : cells).reduced (3, 2);
                g.setColour (deckAccent (i).withAlpha (0.08f));
                g.fillRoundedRectangle (cell.toFloat(), 5.0f);
                g.setColour (deckAccent (i).withAlpha (0.30f));
                g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 5.0f, 1.0f);

                g.setColour (deckAccent (i).withAlpha (0.92f));
                g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                g.drawText (cellNames[(size_t) s], cell.withHeight (12).translated (0, 2),
                            juce::Justification::centred);

                if (s == 4)   // FX cell caption under the reverb knob (C = Delay, per mockup)
                {
                    g.setColour (juce::Colour (stemdeck::palette::kTextMuted));
                    g.setFont (juce::FontOptions (8.5f));
                    g.drawText (i == 2 ? "Delay" : "Reverb", cell.withTop (cell.getBottom() - 11).translated (0, -1),
                                juce::Justification::centred);
                }
            }

            // Honest separation badge: NEURAL (demucs cache) vs DSP (real-time
            // heuristic), on the VOCALS cell corner.
            {
                const bool neural = processorRef.deckHasNeuralStems (i);
                const bool dsp    = processorRef.isDeckRtssActive (i);
                if (neural || dsp)
                {
                    const auto badgeCol = neural ? juce::Colour (0xff62f5c8) : deckAccent (i);
                    g.setColour (badgeCol.withAlpha (0.9f));
                    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
                    g.drawText (neural ? "NEURAL" : "DSP",
                                juce::Rectangle<int> (stemBox.getX() + cellW * 4 - 40,
                                                      stemBox.getY() + 3, 36, 9),
                                juce::Justification::centredRight);
                }
            }
        }
    }

    // --- Control labels above each control (matches djay-Pro nomenclature) ---
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
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

    // Per-stem FX grid headers (only when a deck's FX tab is active): column
    // labels DR/BA/MU/VO over the filter row, and F/S markers on the far left.
    for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
    {
        auto& dc = deckControls[(size_t) i];
        if (dc.activeToolTab != DeckControls::TabFx) continue;
        auto box = deckStemFxBounds[(size_t) i];
        if (box.isEmpty()) continue;
        static const char* cols[4] = { "DR", "BA", "MU", "VO" };
        auto header = box.removeFromTop (9);
        g.setColour (deckAccent (i).withAlpha (0.8f));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        const int colW = juce::jmax (18, header.getWidth() / 4);
        for (int s = 0; s < 4; ++s)
            g.drawText (cols[s], header.removeFromLeft (colW), juce::Justification::centred);
    }

    // Mixer panel box (left band).
    if (! mixerPanel.getBounds().isEmpty() && ! redoButton.getBounds().isEmpty())
    {
        const auto mixBox = mixerPanel.getBounds().getUnion (redoButton.getBounds()).expanded (10, 8);
        drawPanel (g, mixBox, juce::Colour (kAccent), 10.0f);
    }

    // Labels above the top-row sliders.
    g.setColour (juce::Colour (kTextMuted));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto sliderLabel = [&] (const juce::String& t, juce::Component& c)
    {
        if (c.isVisible() && ! c.getBounds().isEmpty())
            g.drawText (t, c.getBounds().withHeight (12).translated (0, -13), juce::Justification::centred);
    };
    sliderLabel (u8 ("CROSSFADER A/C \xe2\x86\x94 B/D"), crossfader);
    sliderLabel ("GLOBAL TEMPO", masterBpm);
    sliderLabel ("MASTER GAIN",  master);
    sliderLabel ("LIMIT",        limiter);
    sliderLabel ("MIC GAIN",     micGain);

    // Per-deck channel meters flanking the crossfader — see which deck is hot and
    // how the mix balances left (A/C) vs right (B/D) at a glance.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto mb = deckMeterBounds[(size_t) d];
        if (mb.isEmpty()) continue;
        const auto r = mb.toFloat();
        g.setColour (juce::Colour (stemdeck::palette::kPanelDeep));
        g.fillRoundedRectangle (r, 3.0f);

        auto barZone = r.reduced (2.0f).withTrimmedBottom (11.0f);   // reserve bottom for the letter
        const float peak = juce::jmax (processorRef.getDeckPeak (d, 0), processorRef.getDeckPeak (d, 1));
        const float db   = juce::Decibels::gainToDecibels (peak, -40.0f);
        const float norm = juce::jmap (juce::jlimit (-40.0f, 0.0f, db), -40.0f, 0.0f, 0.0f, 1.0f);
        const int   nSeg = 9;
        const int   lit  = (int) std::round (norm * (float) nSeg);
        const float segGap = 1.5f;
        const float segH = (barZone.getHeight() - segGap * (nSeg - 1)) / (float) nSeg;
        const auto accent = deckAccent (d);
        for (int seg = 0; seg < nSeg; ++seg)
        {
            const float frac = (float) (seg + 1) / (float) nSeg;
            juce::Colour c = frac > 0.9f ? juce::Colour (kDanger)
                           : frac > 0.7f ? juce::Colour (0xffffb020) : accent;
            g.setColour (seg < lit ? c : c.withAlpha (0.12f));
            const float y = barZone.getBottom() - (float) (seg + 1) * segH - (float) seg * segGap;
            g.fillRoundedRectangle (barZone.getX(), y, barZone.getWidth(), segH, 1.0f);
        }
        // Deck letter + crossfader-side indicator ("<" A/C, "=" centre, ">" B/D).
        // Clicking the meter cycles the side.
        const int   side = processorRef.getDeckCfSide (d);
        const char* mark = side == 0 ? "<" : side == 2 ? ">" : "=";
        g.setColour (accent.withAlpha (lit > 0 ? 0.95f : 0.5f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + d)) + " " + mark,
                    r.withTop (r.getBottom() - 11.0f), juce::Justification::centred);
    }

    // --- MASTER OUTPUT panel: title, stereo segment meters + dB scale ---
    if (! masterOutBounds.isEmpty())
    {
        namespace sdp = stemdeck::palette;
        const auto r = masterOutBounds.toFloat();
        g.setColour (juce::Colour (sdp::kPanel));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.35f).reduced (3.0f, 2.0f), 8.0f);
        g.setColour (juce::Colour (sdp::kHairline).withAlpha (0.9f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (juce::Colour (kAccent));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText ("MASTER OUTPUT", masterOutBounds.reduced (12, 6).withHeight (14),
                    juce::Justification::centredLeft);

        // GLOBAL TEMPO box: label, value (120.0), and a small green "synced" square.
        if (! masterTempoBounds.isEmpty())
        {
            const auto tb = masterTempoBounds.toFloat();
            g.setColour (juce::Colour (sdp::kPanelDeep));
            g.fillRoundedRectangle (tb, 5.0f);
            g.setColour (juce::Colour (sdp::kHairline));
            g.drawRoundedRectangle (tb.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (juce::Colour (sdp::kTextMuted));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText ("GLOBAL TEMPO", tb.withHeight (11.0f).translated (0.0f, 3.0f), juce::Justification::centred);
            const float bpm = (float) processorRef.parameters().getRawParameterValue ("masterBpm")->load();
            auto valRow = tb.withTrimmedTop (13.0f);
            g.setColour (juce::Colour (sdp::kTextPrimary));
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (juce::String (bpm, 1), valRow.translated (6.0f, -2.0f), juce::Justification::centred);
            g.setColour (juce::Colour (0xff37d98a));
            g.fillRoundedRectangle (valRow.getRight() - 15.0f, valRow.getCentreY() - 6.0f, 8.0f, 8.0f, 1.5f);
        }

        if (! masterMeterBounds.isEmpty())
        {
            auto meterArea = masterMeterBounds;
            auto scaleCol = meterArea.removeFromRight (22);
            const int nSeg = 14;
            const float segGap = 2.0f;
            const float segH = ((float) meterArea.getHeight() - segGap * (nSeg - 1)) / (float) nSeg;
            const int barW = juce::jmax (6, (meterArea.getWidth() - 6) / 2);

            for (int chIdx = 0; chIdx < 2; ++chIdx)
            {
                const float db   = juce::Decibels::gainToDecibels (processorRef.getMasterPeak (chIdx), -40.0f);
                const float norm = juce::jmap (juce::jlimit (-40.0f, 0.0f, db), -40.0f, 0.0f, 0.0f, 1.0f);
                const int   lit  = (int) std::round (norm * (float) nSeg);
                const float x    = (float) meterArea.getX() + (float) chIdx * (float) (barW + 6);

                for (int seg = 0; seg < nSeg; ++seg)
                {
                    const float frac = (float) (seg + 1) / (float) nSeg;   // 0..1 bottom→top
                    juce::Colour c = frac > 0.92f ? juce::Colour (kDanger)
                                   : frac > 0.72f ? juce::Colour (0xffffb020)
                                                  : juce::Colour (0xff37d98a);
                    const bool on = seg < lit;
                    g.setColour (on ? c : c.withAlpha (0.13f));
                    const float y = (float) meterArea.getBottom() - (float) (seg + 1) * segH - (float) seg * segGap;
                    g.fillRoundedRectangle (x, y, (float) barW, segH, 1.0f);
                }
            }

            // dB scale ticks (matches the mockup's 0 / -6 / -16 / -20 readout).
            g.setColour (juce::Colour (sdp::kTextMuted));
            g.setFont (juce::FontOptions (8.5f));
            for (const float tickDb : { 0.0f, -6.0f, -16.0f, -20.0f })
            {
                const float tn = juce::jmap (tickDb, -40.0f, 0.0f, 0.0f, 1.0f);
                const int y = meterArea.getBottom() - (int) (tn * (float) meterArea.getHeight()) - 4;
                g.drawText (juce::String ((int) tickDb), scaleCol.getX(), y, scaleCol.getWidth(), 8,
                            juce::Justification::centredLeft);
            }
        }

        // Smart-master readout: short-term loudness + gain reduction when engaged.
        if (aiMasterButton.getToggleState())
        {
            const float lufs = processorRef.getSmartMasterLufs();
            const float gr   = processorRef.getSmartMasterGr();
            juce::String txt = lufs > -69.0f ? juce::String (lufs, 1) + " LUFS" : juce::String ("-- LUFS");
            if (gr > 0.1f) txt += "  GR " + juce::String (gr, 1);
            g.setColour (juce::Colour (kAccent).withAlpha (0.9f));
            g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
            g.drawText (txt, masterOutBounds.reduced (12, 4).withTop (masterOutBounds.getBottom() - 16),
                        juce::Justification::bottomLeft);
        }
    }
}

void MainPluginEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto header = area.removeFromTop (50);
    headerBarBounds = header;

    // Left cluster: djay-Pro nav — icon-above-label cells (icon + text painted
    // in paint(); the button itself is a naked click target).
    {
        juce::Component* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                        &pianoRollButton, &toggleBrowserButton };
        const int widths[5] = { 66, 58, 56, 66, 92 };
        for (int i = 0; i < 5; ++i)
        {
            auto cell = header.removeFromLeft (widths[i]).withSizeKeepingCentre (widths[i], 46);
            navRects[(size_t) i] = cell;
            navBtns[i]->setBounds (cell);
            header.removeFromLeft (4);
        }
    }
    subtitle.setBounds ({});   // mode line retired from the header (mockup layout)
    header.removeFromLeft (8);

    // Universal transport: play/stop every deck at once (prominent, left of title).
    masterPlayButton.setBounds (header.removeFromLeft (108).withSizeKeepingCentre (108, 34));
    header.removeFromLeft (10);

    // Right cluster: settings gear / TOOLS / save / user presets / tempo chip.
    settingsButton.setBounds (header.removeFromRight (32).withSizeKeepingCentre (32, 30));
    header.removeFromRight (6);
    advancedToggle.setVisible (true);
    advancedToggle.setBounds (header.removeFromRight (64).withSizeKeepingCentre (64, 30));
    header.removeFromRight (6);
    savePresetButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, 30));
    header.removeFromRight (8);
    userPresets.setBounds (header.removeFromRight (160).withSizeKeepingCentre (160, 28));
    header.removeFromRight (10);
    tempoChipBounds = header.removeFromRight (150).withSizeKeepingCentre (150, 30);
    tempoLabel.setBounds (tempoChipBounds.reduced (10, 0));
    header.removeFromRight (12);

    // Centre: title fills whatever remains.
    title.setBounds (header);

    // View-switcher strip removed (the mockup has no transport row under the title).

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

    // Advanced tools toolbar (plugin host / master key / AI master / rekordbox /
    // project I/O) — hidden by default for the clean 1:1 view, revealed via TOOLS.
    juce::Component* advancedComps[] = {
        &standaloneWorkflowGuide, &pluginHostStatus,
        &pluginChainSelector, &pluginSelector, &loadPluginButton, &clearChainButton,
        &scanPluginsButton, &reloadPluginCacheButton, &pluginHostMessage,
        &masterKeyLabel, &masterKey, &syncAllDecksButton,
        &rbxExportButton, &rbxImportButton, &saveProjButton, &openProjButton,
        &metronomeButton, &talkoverButton, &overlapModeButton };

    if (advancedVisible)
    {
        for (auto* c : advancedComps) c->setVisible (true);

        auto infoRow = area.removeFromTop (16);
        standaloneWorkflowGuide.setBounds (infoRow.removeFromLeft (infoRow.getWidth() * 2 / 3));
        pluginHostStatus.setBounds (infoRow);
        area.removeFromTop (4);

        auto fxRow = area.removeFromTop (22);
        pluginChainSelector.setBounds (fxRow.removeFromLeft (130));   fxRow.removeFromLeft (4);
        pluginSelector.setBounds (fxRow.removeFromLeft (260));        fxRow.removeFromLeft (4);
        loadPluginButton.setBounds (fxRow.removeFromLeft (66));       fxRow.removeFromLeft (3);
        clearChainButton.setBounds (fxRow.removeFromLeft (66));       fxRow.removeFromLeft (3);
        scanPluginsButton.setBounds (fxRow.removeFromLeft (60));      fxRow.removeFromLeft (3);
        reloadPluginCacheButton.setBounds (fxRow.removeFromLeft (80)); fxRow.removeFromLeft (8);
        pluginHostMessage.setBounds (fxRow);
        area.removeFromTop (6);

        auto keyRow = area.removeFromTop (28);
        masterKeyLabel.setBounds (keyRow.removeFromLeft (108));       keyRow.removeFromLeft (4);
        masterKey.setBounds (keyRow.removeFromLeft (110));            keyRow.removeFromLeft (10);
        syncAllDecksButton.setBounds (keyRow.removeFromLeft (128));   keyRow.removeFromLeft (10);
        rbxExportButton.setBounds (keyRow.removeFromLeft (72));       keyRow.removeFromLeft (4);
        rbxImportButton.setBounds (keyRow.removeFromLeft (72));       keyRow.removeFromLeft (16);
        saveProjButton.setBounds (keyRow.removeFromLeft (90));        keyRow.removeFromLeft (6);
        openProjButton.setBounds (keyRow.removeFromLeft (90));        keyRow.removeFromLeft (12);
        metronomeButton.setBounds (keyRow.removeFromLeft (54));       keyRow.removeFromLeft (6);
        talkoverButton.setBounds (keyRow.removeFromLeft (54));        keyRow.removeFromLeft (6);
        overlapModeButton.setBounds (keyRow.removeFromLeft (44));
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : advancedComps) { c->setVisible (false); c->setBounds ({}); }
    }

    // "LIVE MIXER / STEM DECKS" section title removed (mockup has no such label).
    liveMixerLabel.setBounds ({});
    liveMixerLabel.setVisible (false);
    area.removeFromTop (2);

    // Deck row gets a fixed band; the arrangement below takes ALL remaining height
    // (so it is large and grows as the window resizes).
    auto decksArea = area.removeFromTop (juce::jmax (378, area.getHeight() * 40 / 100));
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
        // Deck footer status text removed for the clean 1:1 view (advanced only).
        auto statusRow = lane.removeFromBottom (advancedVisible ? 13 : 0);
        if (advancedVisible)
        {
            controls.keyInfo.setVisible (true); controls.stemStatus.setVisible (true);
            controls.keyInfo   .setBounds (statusRow.removeFromLeft (statusRow.getWidth() / 2));
            controls.stemStatus.setBounds (statusRow);
        }
        else
        {
            controls.keyInfo.setVisible (false);   controls.keyInfo.setBounds ({});
            controls.stemStatus.setVisible (false); controls.stemStatus.setBounds ({});
        }
        lane.removeFromBottom (4);

        // Toolbox content (the active tab's controls render here).
        auto tabContent = lane.removeFromBottom (66);   // fits LOOP (62) + SYNC (64) rows uncropped
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
        auto mixBand = lane.removeFromBottom (78).withTrimmedTop (14);   // 14px headroom for the painted VOL/XF/PITCH labels
        {
            const int faderColW = 32, fgap = 4;
            auto fadersCol = mixBand.removeFromRight (faderColW * 3 + fgap * 2);
            auto eqArea = mixBand.withTrimmedRight (8);
            const int eqW = juce::jmax (26, eqArea.getWidth() / 4);
            // Smaller knobs (was reduced(3,2)) so the rings read light and the
            // LOW/MID/HIGH/FILT labels above have room.
            controls.low   .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.mid   .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.high  .setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.filter.setBounds (eqArea.removeFromLeft (eqW).reduced (5, 9));
            controls.volume.setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.fader .setBounds (fadersCol.removeFromLeft (faderColW).reduced (3, 2));
            fadersCol.removeFromLeft (fgap);
            controls.pitch .setBounds (fadersCol.reduced (3, 2));
        }
        lane.removeFromBottom (6);

        // Transport row — PLAY + CUE + 3 tiny CF-assign buttons on the right
        auto transport = lane.removeFromBottom (28);
        {
            auto cfCol = transport.removeFromRight (54);
            transport.removeFromRight (4);
            const int cfW = cfCol.getWidth() / 3;
            for (int s = 0; s < 3; ++s)
                controls.cfSideBtns[(size_t) s].setBounds (cfCol.removeFromLeft (cfW).reduced (1, 3));
            const int w = transport.getWidth();
            controls.play.setBounds (transport.removeFromLeft (w * 58 / 100).reduced (1, 0));
            controls.cue .setBounds (transport.reduced (1, 0));
        }
        lane.removeFromBottom (6);

        // STEM SEPARATION — djay-Pro cells: DRUMS/BASS/MUSIC/VOCALS vertical
        // mini-faders + an FX cell (reverb knob). Painted cell boxes in paint().
        auto stemStrip = lane.removeFromBottom (72);
        deckStemBounds[(size_t) deckIndex] = stemStrip;
        {
            const int cellW = juce::jmax (28, stemStrip.getWidth() / 5);
            auto cells = stemStrip;
            for (int s = 0; s < 4; ++s)   // drums..vocals faders
            {
                auto cell = cells.removeFromLeft (cellW).reduced (3, 2);
                cell.removeFromTop (13);   // painted header
                controls.stemGains[(size_t) (DeckPlayer::drumsStem + s)].setBounds (cell.reduced (2, 0));
            }
            auto fxCell = cells.reduced (3, 2);
            fxCell.removeFromTop (13);
            fxCell.removeFromBottom (10);  // painted "Reverb" caption
            controls.reverbMix.setBounds (fxCell.reduced (2, 0));
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
                tabContent.removeFromTop (2);
                auto row2 = tabContent.removeFromTop (18);
                controls.introEndBtn  .setBounds (row2.removeFromLeft (row2.getWidth() / 2).reduced (1, 0));
                controls.outroStartBtn.setBounds (row2.reduced (1, 0));
                break;
            }
            case DeckControls::TabLoop:
            {
                // Row 1: IN / OUT / LOOP / 1/2 / x2 / Slip
                auto r1 = tabContent.removeFromTop (22);
                const int bw = juce::jmax (24, r1.getWidth() / 6);
                controls.loopIn    .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopOut   .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopToggle.setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopHalf  .setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.loopDouble.setBounds (r1.removeFromLeft (bw).reduced (1, 0));
                controls.slipButton.setBounds (r1.reduced (1, 0));
                tabContent.removeFromTop (2);
                // Row 2: beat-roll buttons (1/4 1/2 1 2 4 8)
                auto r2 = tabContent.removeFromTop (20);
                const int rw = juce::jmax (12, r2.getWidth() / 6);
                for (int b = 0; b < 6; ++b)
                    controls.beatRollBtns[(size_t) b].setBounds (r2.removeFromLeft (rw).reduced (1, 0));
                tabContent.removeFromTop (2);
                // Row 3: beat-jump back/fwd
                auto r3 = tabContent.removeFromTop (16);
                const int jw = juce::jmax (10, r3.getWidth() / 8);
                for (int b = 3; b >= 0; --b)
                    controls.beatJumpBack[(size_t) b].setBounds (r3.removeFromLeft (jw).reduced (1, 0));
                for (int b = 0; b < 4; ++b)
                    controls.beatJumpFwd[(size_t) b].setBounds (r3.removeFromLeft (jw).reduced (1, 0));
                break;
            }
            case DeckControls::TabFx:
            {
                // Deck-wide FX knobs on the left (echo / macro); reverb lives in
                // the stem strip's FX cell now.
                const int knobW = juce::jmin (46, tabContent.getWidth() / 6);
                controls.echoMix  .setBounds (tabContent.removeFromLeft (knobW).reduced (3, 2));
                tabContent.removeFromLeft (3);
                controls.macroKnob.setBounds (tabContent.removeFromLeft (knobW).reduced (3, 2));
                tabContent.removeFromLeft (6);

                // Per-stem FX grid: 4 columns (drums/bass/music/vocals) × 3 rows
                // (Filt / Send / Pitch). Column headers + row labels painted in paint().
                deckStemFxBounds[(size_t) deckIndex] = tabContent;
                auto grid = tabContent.reduced (0, 1);
                grid.removeFromTop (9);   // column-header strip (painted)
                const int colW = juce::jmax (18, grid.getWidth() / 4);
                auto filtRow  = grid.removeFromTop (grid.getHeight() / 3);
                auto sendRow  = grid.removeFromTop (grid.getHeight() / 2);
                auto pitchRow = grid;
                for (int s = 0; s < 4; ++s)
                {
                    controls.stemFilt[(size_t) s] .setBounds (filtRow.removeFromLeft (colW).reduced (2, 0));
                    controls.stemSend[(size_t) s] .setBounds (sendRow.removeFromLeft (colW).reduced (2, 0));
                    controls.stemPitch[(size_t) s].setBounds (pitchRow.removeFromLeft (colW).reduced (2, 0));
                }
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
                tabContent.removeFromTop (2);
                // Full-mix trim knob (relocated from the old stem knob row) + audio→MIDI.
                controls.stemGains[(size_t) DeckPlayer::fullMixStem]
                    .setBounds (tabContent.removeFromLeft (46).reduced (2, 1));
                tabContent.removeFromLeft (4);
                controls.extractMidi.setBounds (tabContent.removeFromLeft (72).withHeight (22).translated (0, 4));
                tabContent.removeFromLeft (4);
                controls.extractDrums.setBounds (tabContent.removeFromLeft (80).withHeight (22).translated (0, 4));
                break;
            }
            default: break;
        }

        applyDeckToolTabVisibility (deckIndex);
    }

    area.removeFromTop (8);

    // ---- Mixer + GLOBAL MASTER EFFECTS + MASTER OUTPUT band ----
    auto mixerStrip = area.removeFromTop (150);
    masterOutBounds = mixerStrip.removeFromRight (juce::jmax (300, mixerStrip.getWidth() * 22 / 100));
    mixerStrip.removeFromRight (10);
    // Frosted-glass XY hero panel (full band height) between mix controls and recorder.
    masterFxPad.setBounds (mixerStrip.removeFromRight (juce::jmax (240, mixerStrip.getWidth() * 24 / 100)).reduced (2, 6));
    mixerStrip.removeFromRight (12);

    // MIXER block: master scope + surfaced power controls + crossfader flanked by
    // per-deck channel meters. Rarely-used utilities stay advanced-only.
    juce::Component* mixExtras[] = { &micGain, &hostSync, &beatSync, &micMonitor,
                                     &camelotButton, &midiLearnButton, &snapToggleButton,
                                     &razorButton, &bounceEditsButton, &undoButton, &redoButton };
    auto mixer = mixerStrip.reduced (2, 5);
    mixerPanel.setBounds (mixer.removeFromLeft (140));
    auto mixControls = mixer.reduced (10, 8);

    // Always-visible power row: BPM SYNC · KEY SYNC · AI MASTER (+mode) · LIMIT.
    {
        for (juce::Component* c : { (juce::Component*) &syncBpmButton, (juce::Component*) &syncKeyButton,
                                    (juce::Component*) &aiMasterButton, (juce::Component*) &aiMasterMode,
                                    (juce::Component*) &limiter })
            c->setVisible (true);
        auto powerRow = mixControls.removeFromTop (26);
        syncBpmButton.setBounds (powerRow.removeFromLeft (78));   powerRow.removeFromLeft (4);
        syncKeyButton.setBounds (powerRow.removeFromLeft (78));   powerRow.removeFromLeft (10);
        aiMasterButton.setBounds (powerRow.removeFromLeft (80));  powerRow.removeFromLeft (4);
        aiMasterMode.setBounds (powerRow.removeFromLeft (110));   powerRow.removeFromLeft (10);
        limiter.setBounds (powerRow);   // LIMIT ceiling — label painted alongside
    }
    mixControls.removeFromTop (6);

    // Crossfader flanked by per-deck meters: A/C feed the left, B/D the right.
    {
        auto xfRow = mixControls;
        auto leftMeters  = xfRow.removeFromLeft (40);
        auto rightMeters = xfRow.removeFromRight (40);
        deckMeterBounds[0] = leftMeters.removeFromTop (leftMeters.getHeight() / 2).reduced (3, 2);   // A
        deckMeterBounds[2] = leftMeters.reduced (3, 2);                                               // C
        deckMeterBounds[1] = rightMeters.removeFromTop (rightMeters.getHeight() / 2).reduced (3, 2);  // B
        deckMeterBounds[3] = rightMeters.reduced (3, 2);                                              // D
        crossfader.setBounds (xfRow.reduced (8, 0).withSizeKeepingCentre (xfRow.getWidth() - 16, 40));
    }

    if (advancedVisible)
    {
        for (auto* c : mixExtras) c->setVisible (true);
        // Advanced utility chips live on the transport/keyRow area, not the mixer.
        auto util = area.removeFromTop (28);
        auto chip = [&util] (juce::Component& c, int w) { c.setBounds (util.removeFromLeft (w).withHeight (26)); util.removeFromLeft (3); };
        hostSync.setBounds (util.removeFromLeft (96)); util.removeFromLeft (3);
        beatSync.setBounds (util.removeFromLeft (96)); util.removeFromLeft (3);
        micMonitor.setBounds (util.removeFromLeft (104)); util.removeFromLeft (6);
        micGain.setBounds (util.removeFromLeft (84)); util.removeFromLeft (6);
        chip (camelotButton, 44); chip (midiLearnButton, 48); chip (snapToggleButton, 48);
        chip (razorButton, 50); chip (bounceEditsButton, 64); chip (undoButton, 46); chip (redoButton, 46);
        area.removeFromTop (6);
    }
    else
    {
        for (auto* c : mixExtras) { c->setVisible (false); c->setBounds ({}); }
    }

    // MASTER OUTPUT panel internals: meters | MASTER GAIN | GLOBAL TEMPO/TAP/KeyLock.
    {
        auto mo = masterOutBounds.reduced (12, 8);
        mo.removeFromTop (16);   // painted "MASTER OUTPUT" title
        masterMeterBounds = mo.removeFromLeft (76).reduced (0, 4);
        mo.removeFromLeft (8);
        master.setBounds (mo.removeFromLeft (66).withTrimmedTop (10));
        mo.removeFromLeft (8);
        auto tempoCol = mo;
        // GLOBAL TEMPO is a painted box (label + 120.0 + green square); the slider
        // itself is hidden but kept for automation/state.
        masterTempoBounds = tempoCol.removeFromTop (42);
        masterBpm.setVisible (false);
        masterBpm.setBounds ({});
        tempoCol.removeFromTop (6);
        auto tapRow = tempoCol.removeFromTop (26);
        tapTempoButton.setBounds (tapRow.removeFromLeft (juce::jmax (44, tapRow.getWidth() / 2 - 3)));
        tapRow.removeFromLeft (6);
        keyLockAllButton.setBounds (tapRow);
        // Reveal / Reload are advanced-only (not in the mockup master panel).
        if (advancedVisible)
        {
            revealRecordingButton.setVisible (true); reloadIntoDecksButton.setVisible (true);
            auto recRow = tempoCol.withTrimmedTop (4).withHeight (20);
            revealRecordingButton.setBounds (recRow.removeFromLeft (juce::jmax (44, recRow.getWidth() / 2 - 3)));
            recRow.removeFromLeft (6);
            reloadIntoDecksButton.setBounds (recRow);
        }
        else
        {
            revealRecordingButton.setVisible (false); revealRecordingButton.setBounds ({});
            reloadIntoDecksButton.setVisible (false); reloadIntoDecksButton.setBounds ({});
        }
        // recordStatus is placed in the pads strip (Recording chip) further down.
    }

    area.removeFromTop (8);
    {
        // djay-Pro transport: label left, centred >>> ▶ ● Loop cluster, zoom right.
        auto labelRow = area.removeFromTop (28);
        arrangementLabel.setBounds (labelRow.removeFromLeft (labelRow.getWidth() * 28 / 100));
        auto zoomBox = labelRow.removeFromRight (128);
        arrZoomFitButton.setBounds (zoomBox.removeFromRight (40).reduced (1, 2));
        zoomBox.removeFromRight (4);
        arrZoomInButton .setBounds (zoomBox.removeFromRight (32).reduced (1, 2));
        arrZoomOutButton.setBounds (zoomBox.removeFromRight (32).reduced (1, 2));

        auto cluster = labelRow.withSizeKeepingCentre (44 * 4 + 6 * 3, 26);
        auto tbtn = [&cluster] (juce::Component& c) { c.setBounds (cluster.removeFromLeft (44)); cluster.removeFromLeft (6); };
        tbtn (launchAllButton);
        tbtn (playArrButton);
        tbtn (arrRecordButton);
        tbtn (arrLoopButton);

        labelRow.removeFromRight (8);
        libraryMetaLabel.setBounds (labelRow);   // MIDI-learn / status feedback
    }
    // Performance pads relocate to a compact full-width strip along the bottom,
    // so the arrangement timeline spans the ENTIRE width above them.
    auto bottom = area;
    auto samplerStrip = bottom.removeFromBottom (64);
    bottom.removeFromBottom (8);
    arrangementView.setBounds (bottom);   // full-width arrangement timeline

    samplerLabel.setBounds (samplerStrip.removeFromLeft (150));
    samplerStrip.removeFromLeft (6);
    // Recording status chip anchors the right end of the pads strip.
    recordStatus.setBounds (samplerStrip.removeFromRight (170).reduced (2, 14));
    samplerStrip.removeFromRight (6);
    const int padGap = 6;
    const int padW = juce::jmax (96,
        (samplerStrip.getWidth() - padGap * (PadBank::numPads - 1)) / PadBank::numPads);
    for (int i = 0; i < PadBank::numPads; ++i)
    {
        auto cell = samplerStrip.removeFromLeft (padW);
        if (i < PadBank::numPads - 1)
            samplerStrip.removeFromLeft (padGap);

        auto top = cell.removeFromTop (cell.getHeight() * 58 / 100);
        pads[(size_t) i].trigger.setBounds (top.reduced (1, 1));
        cell.removeFromTop (2);
        pads[(size_t) i].load.setBounds (cell.removeFromLeft (cell.getWidth() * 42 / 100).reduced (1, 0));
        cell.removeFromLeft (3);
        pads[(size_t) i].gain.setBounds (cell.reduced (1, 0));
    }

    // Piano-roll / mixer overlays cover the decks + arrangement when toggled on.
    // Anchor to the actual deck-strip top (not a hardcoded offset) so header/nav
    // stay visible even if the upper sections change height.
    const int overlayTop = deckPanelBounds[0].isEmpty() ? 252 : deckPanelBounds[0].getY();
    const auto overlay = getLocalBounds().withTrimmedTop (overlayTop).reduced (8, 6);
    pianoRoll.setBounds (overlay);
    mixerView.setBounds (overlay);
}

void MainPluginEditor::timerCallback()
{
    // Service deferred audio-thread requests (e.g. TE transport auto-stop).
    processorRef.serviceDeferredActions();

    // ── Motion: nav underline slides toward the active view's cell ──────────
    {
        const juce::Button* navBtns[5] = { &toggleLibraryButton, &navDecksButton, &mixerButton,
                                           &pianoRollButton, &toggleBrowserButton };
        int active = -1;
        for (int i = 0; i < 5; ++i)
            if (navBtns[i]->getToggleState()) { active = i; break; }

        float tx = navUnderlineX, tw = 0.0f;
        if (active >= 0 && ! navRects[(size_t) active].isEmpty())
        {
            tx = (float) navRects[(size_t) active].getX() + 8.0f;
            tw = (float) navRects[(size_t) active].getWidth() - 16.0f;
        }
        const float k  = 0.35f;   // ease ~120ms at 30Hz
        const float nx = navUnderlineX < 0.0f ? tx : navUnderlineX + (tx - navUnderlineX) * k;
        const float nw = navUnderlineW + (tw - navUnderlineW) * k;
        if (std::abs (nx - navUnderlineX) > 0.2f || std::abs (nw - navUnderlineW) > 0.2f)
        {
            navUnderlineX = nx;
            navUnderlineW = nw;
            if (! navRects[0].isEmpty())
                repaint (navRects[0].getX() - 4, navRects[0].getBottom() - 5,
                         navRects[4].getRight() - navRects[0].getX() + 8, 8);
        }
    }
    // ── Motion: repaint decaying pad flashes ────────────────────────────────
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < PadBank::numPads; ++i)
            if (padFlashMs[(size_t) i] > 0.0 && nowMs - padFlashMs[(size_t) i] < 360.0)
                repaint (pads[(size_t) i].trigger.getBounds().expanded (8));
    }

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
    arrRecordButton.setToggleState (recording, juce::dontSendNotification);

    // Meters live in the MASTER OUTPUT panel — refresh continuously.
    if (! masterOutBounds.isEmpty())
        repaint (masterOutBounds);

    // Feed the live master-output scope + refresh the console channel meters.
    mixerPanel.pushLevel (processorRef.getMasterPeak (0), processorRef.getMasterPeak (1));
    for (const auto& mb : deckMeterBounds)
        if (! mb.isEmpty())
            repaint (mb);

    // DECKS nav chip reads active when no overlay view is open.
    navDecksButton.setToggleState (! pianoRoll.isVisible() && ! mixerView.isVisible()
                                     && ! libraryVisible && ! browserVisible,
                                   juce::dontSendNotification);

    // Newly-loaded deck files pick up imported Rekordbox cues automatically.
    for (int d = 0; d < MainPluginProcessor::numDecks; ++d)
    {
        const auto path = processorRef.getDeckPath (d);
        if (path != lastCuePaths[(size_t) d])
        {
            lastCuePaths[(size_t) d] = path;
            applyImportedCues (d);
        }
    }

    {
        // Project tempo (MASTER BPM) drives the arrangement grid + the piano roll.
        const auto bpm = juce::jlimit (40.0, 240.0, (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        arrangementView.setSnap (snapToggleButton.getToggleState(), 0.25, bpm);
        pianoRoll.setBpm (bpm);
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
            // Transport button reflects the action: STOP while playing, PLAY while stopped.
            deckControls[(size_t) i].play.setButtonText (playing ? u8 ("\xe2\x96\xa0 STOP")
                                                                  : u8 ("\xe2\x96\xb6 PLAY"));
            // Auto-follow: the last deck to START playing becomes the sync master.
            if (playing)
                setMasterDeck (i);
            lastDeckPlayingUI[(size_t) i] = playing;
            deckPlayChanged = true;
        }
    }
    if (deckPlayChanged)
    {
        // Universal transport label follows the fleet: STOP ALL if anything plays.
        bool anyPlaying = false;
        for (int i = 0; i < MainPluginProcessor::numDecks; ++i)
            anyPlaying = anyPlaying || lastDeckPlayingUI[(size_t) i];
        masterPlayButton.setButtonText (anyPlaying ? u8 ("\xe2\x96\xa0 STOP ALL") : u8 ("\xe2\x96\xb6 PLAY ALL"));
        masterPlayButton.setColour (juce::TextButton::buttonColourId,
            juce::Colour (sattari::brand::kAccent).withAlpha (anyPlaying ? 0.45f : 0.22f));
        repaint();   // refresh the deck-card active glow
    }

    undoButton.setEnabled (arrangementView.canUndo());
    redoButton.setEnabled (arrangementView.canRedo());

    // Arrangement transport feedback: moving playhead + button state.
    const bool arrPlaying = processorRef.isArrangementPlaying();
    arrangementView.setPlaybackPosition (processorRef.getArrangementPositionSeconds(), arrPlaying);

    // Apply editable automation curves to their target parameters while playing.
    if (arrPlaying)
    {
        const double arrPos = processorRef.getArrangementPositionSeconds();
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            int target = -1;
            const float v = arrangementView.automationValueAt (lane, arrPos, target);
            if (v < 0.0f || target < 0)
                continue;
            juce::String pid;
            switch (target)
            {
                case 0: pid = deckPrefix (lane) + "Fader";  break;
                case 1: pid = deckPrefix (lane) + "Filter"; break;
                case 2: pid = deckPrefix (lane) + "Pitch";  break;
                case 3: pid = deckPrefix (lane) + "Stem" + DeckPlayer::stemLaneId (DeckPlayer::vocalsStem) + "Send"; break;
                default: break;
            }
            if (auto* p = processorRef.parameters().getParameter (pid))
                p->setValueNotifyingHost (v);   // curve stores the normalized 0..1 value
        }
    }
    playArrButton.setToggleState (arrPlaying, juce::dontSendNotification);
    playArrButton.setButtonText (arrPlaying ? u8 ("\xe2\x96\xa0") : u8 ("\xe2\x96\xb6"));   // ■ / ▶
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
        // Update slip button visual state
        updateSlipButtonState (i);

        // Beat roll: auto-release after one loop cycle if loopRollActive
        auto& dc = deckControls[(size_t) i];
        if (dc.loopRollActive && ! processorRef.isDeckLoopActive (i))
        {
            dc.loopRollActive = false;
            for (auto& b : dc.beatRollBtns)
                b.setToggleState (false, juce::dontSendNotification);
        }

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
        {
            wave->setRtssActive (processorRef.isDeckRtssActive (i));
            wave->setIntroEnd   (processorRef.getIntroEnd   (i));
            wave->setOutroStart (processorRef.getOutroStart (i));

            // Beat-match grid: recompute the grid only when a grid-defining
            // parameter (file / BPM / offset / downbeat) actually changes.
            const auto pfx = deckPrefix (i);
            auto pval = [&] (const char* id) { return processorRef.parameters().getRawParameterValue (pfx + id)->load(); };
            const juce::String gridKey = deckPaths[(size_t) i]
                + "|" + juce::String (pval ("Bpm"), 2)
                + "|" + juce::String (pval ("BeatOffset"), 3)
                + "|" + juce::String (pval ("Downbeat"), 3);
            if (gridKey != lastBeatGridKey[(size_t) i])
            {
                lastBeatGridKey[(size_t) i] = gridKey;
                wave->setBeatGridSeconds (processorRef.getDeckBeatGridSeconds (i));
            }

            // Locked (green grid) when this deck's tempo matches the sync master's
            // — i.e. it is beat-matched. The master deck is always its own reference.
            const double myBpm      = (double) pval ("Bpm");
            const double masterDeckBpm = juce::isPositiveAndBelow (masterDeckIndex, MainPluginProcessor::numDecks)
                ? (double) processorRef.parameters().getRawParameterValue (deckPrefix (masterDeckIndex) + "Bpm")->load()
                : 0.0;
            const bool matched = myBpm > 1.0 && masterDeckBpm > 1.0
                && (i == masterDeckIndex || std::abs (myBpm - masterDeckBpm) < 0.15);
            wave->setBeatSyncState (matched, 0);
        }

        // Intro/Outro button toggle states
        deckControls[(size_t) i].introEndBtn  .setToggleState (processorRef.getIntroEnd   (i) >= 0.0, juce::dontSendNotification);
        deckControls[(size_t) i].outroStartBtn.setToggleState (processorRef.getOutroStart (i) >= 0.0, juce::dontSendNotification);
    }

    // ── Beat-quantized clip launcher ──────────────────────────────────────────
    {
        const double bpm = juce::jlimit (40.0, 240.0,
            (double) processorRef.parameters().getRawParameterValue ("masterBpm")->load());
        const double beatLenMs = (60.0 / bpm) * 1000.0;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();

        if (launcherBeatOriginMs < 0.0) launcherBeatOriginMs = nowMs;
        const double elapsedMs  = nowMs - launcherBeatOriginMs;
        const double beatPhase  = std::fmod (elapsedMs, beatLenMs) / beatLenMs;   // 0..1
        const bool newBeat = (beatPhase < 0.15 && launcherBeatPhase > 0.75);
        launcherBeatPhase = beatPhase;

        if (newBeat)
        {
            // Fire all lanes waiting for the next beat
            for (int lane = 0; lane < ArrangementRecorderView::numLanes; ++lane)
            {
                if (arrangementView.getSlotState (lane) != ArrangementRecorderView::SlotState::WaitingToStart)
                    continue;

                const int clipIdx = arrangementView.getLauncherClip (lane);
                const auto& allClips = arrangementView.getClips();

                if (juce::isPositiveAndBelow (lane, (int) allClips.size())
                    && juce::isPositiveAndBelow (clipIdx, (int) allClips[(size_t) lane].size()))
                {
                    const auto& clip = allClips[(size_t) lane][(size_t) clipIdx];
                    if (clip.file.existsAsFile())
                    {
                        processorRef.loadDeckFileAsync (lane, clip.file);
                        pendingSlotSeekSec[(size_t) lane] = clip.trimStartSeconds;
                    }
                }
                arrangementView.setSlotState (lane, ArrangementRecorderView::SlotState::Running);
            }
        }

        // Complete pending seek+play once the deck has finished loading
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            if (pendingSlotSeekSec[(size_t) lane] >= 0.0
                && processorRef.getDeckTotalSeconds (lane) > 0.0)
            {
                processorRef.seekDeckSeconds (lane, pendingSlotSeekSec[(size_t) lane]);
                if (auto* param = dynamic_cast<juce::AudioParameterBool*> (
                        processorRef.parameters().getParameter (deckPrefix (lane) + "Play")))
                    param->setValueNotifyingHost (1.0f);
                pendingSlotSeekSec[(size_t) lane] = -1.0;
            }
        }

        // Clear Running state when the deck stops; fire follow action if set
        for (int lane = 0; lane < MainPluginProcessor::numDecks; ++lane)
        {
            if (arrangementView.getSlotState (lane) == ArrangementRecorderView::SlotState::Running)
            {
                const bool playing = processorRef.parameters()
                    .getRawParameterValue (deckPrefix (lane) + "Play")->load() > 0.5f;
                if (! playing)
                {
                    const int clipIdx = arrangementView.getLauncherClip (lane);
                    applyFollowAction (lane, clipIdx);
                }
            }
        }
    }
}

// ── Toolbox tabs (declutter) ──────────────────────────────────────────────────

void MainPluginEditor::setupToolTabs (int deckIndex)
{
    static const char* const kLabels[] = { "CUE", "LOOP", "FX", "SYNC", "SRC" };
    static const char* const kTips[] = {
        "CUE - hot cues (1-8) + Intro End / Outro Start markers",
        "LOOP - loop in/out, halve/double, slip, beat-roll and beat-jump",
        "FX - this deck's echo/macro + per-stem filter, send and pitch",
        "SYNC - source BPM, beat-grid offset, tempo/sync mode, key match",
        "SRC - load a track, AI stem separation, and MIDI/drum extraction" };
    auto& dc = deckControls[(size_t) deckIndex];
    for (int t = 0; t < DeckControls::NumToolTabs; ++t)
    {
        auto& btn = dc.toolTabs[(size_t) t];
        btn.setButtonText (kLabels[t]);
        setupButton (btn);
        btn.setTooltip (kTips[t]);
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

    for (auto& b : dc.hotCues)        b.setVisible (cue);
    dc.introEndBtn.setVisible (cue);
    dc.outroStartBtn.setVisible (cue);
    dc.loopIn.setVisible (loop); dc.loopOut.setVisible (loop); dc.loopToggle.setVisible (loop);
    dc.loopHalf.setVisible (loop); dc.loopDouble.setVisible (loop);
    dc.slipButton.setVisible (loop);
    for (auto& b : dc.beatRollBtns)  b.setVisible (loop);
    for (auto& b : dc.beatJumpBack)  b.setVisible (loop);
    for (auto& b : dc.beatJumpFwd)   b.setVisible (loop);
    dc.reverbMix.setVisible (true);   // lives in the stem strip's FX cell now
    dc.echoMix.setVisible (fx); dc.macroKnob.setVisible (fx);
    for (auto& k : dc.stemFilt)  k.setVisible (fx);
    for (auto& k : dc.stemSend)  k.setVisible (fx);
    for (auto& k : dc.stemPitch) k.setVisible (fx);
    dc.stemGains[(size_t) DeckPlayer::fullMixStem].setVisible (src);   // FULL trim in SRC tab
    dc.extractMidi.setVisible (src);
    dc.extractDrums.setVisible (src);
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
        // Empty by default: calm neutral pad. refreshHotCueButtonColours lights it
        // up in its cue colour only once a cue is actually stored there.
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (sattari::brand::kPanelRaised));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colour (sattari::brand::kTextMuted));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btn.setTooltip ("Hot cue " + juce::String (c + 1)
                        + " - click to drop a cue at the playhead, or jump to it. "
                          "Alt-click clears; Cmd-click jumps + sets a 1-bar loop.");
        btn.onClick = [this, deckIndex, c]
        {
            handleHotCuePress (deckIndex, c, juce::ModifierKeys::getCurrentModifiers());
        };
        addAndMakeVisible (btn);
    }

    // Intro end / Outro start buttons (CUE tab, second row)
    auto setupDJMarker = [&] (juce::TextButton& btn, juce::Colour col, bool isIntro)
    {
        setupButton (btn);
        btn.setClickingTogglesState (true);
        btn.setColour (juce::TextButton::buttonOnColourId, col.withAlpha (0.65f));
        btn.setTooltip (isIntro
            ? "Set Intro End: marks where the intro finishes (click again to clear)"
            : "Set Outro Start: marks where the outro begins (click again to clear)");
        btn.onClick = [this, &btn, deckIndex, isIntro]
        {
            if (btn.getToggleState())
            {
                const double pos = processorRef.getDeckPlayheadSeconds (deckIndex);
                if (isIntro) processorRef.setIntroEnd   (deckIndex, pos);
                else         processorRef.setOutroStart (deckIndex, pos);
            }
            else
            {
                if (isIntro) processorRef.clearIntroEnd   (deckIndex);
                else         processorRef.clearOutroStart (deckIndex);
            }
        };
        addAndMakeVisible (btn);
    };
    setupDJMarker (dc.introEndBtn,   juce::Colour (0xff26d9ff), true);
    setupDJMarker (dc.outroStartBtn, juce::Colour (0xffffb020), false);
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
    else if (mods.isCtrlDown() || mods.isCommandDown())
    {
        // Ctrl/Cmd+click = Goto+Loop: jump to cue and set a 1-bar loop from there
        const double cuePos = processorRef.getHotCue (deckIndex, cueIndex);
        if (cuePos >= 0.0)
        {
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
            processorRef.jumpToHotCue (deckIndex, cueIndex);
            processorRef.setDeckLoopSeconds (deckIndex, cuePos, cuePos + barSeconds);
            processorRef.setDeckLoopActive (deckIndex, true);
            updateLoopButtonStates (deckIndex);
            updateWaveformLoopRegion (deckIndex);
        }
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
        // Set cue = its colour + white number; empty = neutral pad + muted number.
        dc.hotCues[(size_t) c].setColour (juce::TextButton::buttonColourId,
            isSet ? kHotCueColours[(size_t) c].withAlpha (0.55f) : juce::Colour (sattari::brand::kPanelRaised));
        dc.hotCues[(size_t) c].setColour (juce::TextButton::textColourOffId,
            isSet ? juce::Colours::white : juce::Colour (sattari::brand::kTextMuted));
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

// ── Slip mode + loop roll ─────────────────────────────────────────────────────

void MainPluginEditor::setupSlipAndRoll (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    const auto accent = deckAccent (deckIndex);

    // Slip button: toggles slip mode on/off
    setupButton (dc.slipButton);
    dc.slipButton.setClickingTogglesState (true);
    dc.slipButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff26d9ff).withAlpha (0.5f));
    dc.slipButton.setTooltip ("Slip mode: a shadow playhead advances at 1x while you loop/scratch. Disable to snap back.");
    dc.slipButton.onClick = [this, deckIndex]
    {
        processorRef.setDeckSlipEnabled (deckIndex, deckControls[(size_t) deckIndex].slipButton.getToggleState());
    };
    addAndMakeVisible (dc.slipButton);

    // Beat roll buttons: hold = loop+slip for that beat-length, release = snap back
    static constexpr std::array<const char*, 6> kLabels { "1/4","1/2","1","2","4","8" };
    static constexpr std::array<double, 6>      kLen    { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
    for (int b = 0; b < 6; ++b)
    {
        auto& btn = dc.beatRollBtns[(size_t) b];
        btn.setButtonText (kLabels[(size_t) b]);
        setupButton (btn);
        btn.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.55f));
        btn.onClick = [this, deckIndex, b]
        {
            // Single click (no hold tracking in TextButton): just arm a 1-shot loop
            const float bpm = *processorRef.parameters().getRawParameterValue (deckPrefix (deckIndex) + "Bpm");
            const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
            const double rollLen = kLen[(size_t) b] * barSeconds;
            processorRef.beginDeckLoopRoll (deckIndex, rollLen);
            // Schedule end after rollLen seconds via the timer; set flag
            deckControls[(size_t) deckIndex].loopRollActive = true;
            deckControls[(size_t) deckIndex].beatRollBtns[(size_t) b].setToggleState (true, juce::dontSendNotification);
        };
        addAndMakeVisible (btn);
    }
}

void MainPluginEditor::updateSlipButtonState (int deckIndex)
{
    const bool slip = processorRef.isDeckSlipEnabled (deckIndex);
    deckControls[(size_t) deckIndex].slipButton.setToggleState (slip, juce::dontSendNotification);
}

// ── Superknob / FX macro ──────────────────────────────────────────────────────

void MainPluginEditor::setupMacroKnob (int deckIndex)
{
    auto& dc = deckControls[(size_t) deckIndex];
    setupSlider (dc.macroKnob, true);
    dc.macroKnob.setRange (0.0, 1.0);
    dc.macroKnob.setValue (0.0, juce::dontSendNotification);
    dc.macroKnob.setTooltip ("FX Macro: drives Reverb + Echo simultaneously");
    dc.macroKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dc.macroKnob.setPopupDisplayEnabled (true, true, this);
    dc.macroKnob.onValueChange = [this, deckIndex]
    {
        const float v = (float) deckControls[(size_t) deckIndex].macroKnob.getValue();
        // Soft-link: macro drives both reverb and echo params
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                processorRef.parameters().getParameter (deckPrefix (deckIndex) + "RevMix")))
            p->setValueNotifyingHost (v);
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (
                processorRef.parameters().getParameter (deckPrefix (deckIndex) + "EchoMix")))
            p->setValueNotifyingHost (v);
    };
    addAndMakeVisible (dc.macroKnob);
}

// ── Follow actions ────────────────────────────────────────────────────────────

void MainPluginEditor::showFollowActionMenu (int laneIndex, int clipIndex)
{
    auto& lane = arrangementView.getClips()[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) lane.size())) return;

    juce::PopupMenu m;
    m.addSectionHeader ("Follow Action");
    m.addItem (1, "None",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::None);
    m.addItem (2, "Next clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Next);
    m.addItem (3, "Prev clip",   true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Prev);
    m.addItem (4, "Random other",true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::RandomOther);
    m.addItem (5, "Stop",        true, lane[(size_t) clipIndex].followAction == ArrangementRecorderView::FollowAction::Stop);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, laneIndex, clipIndex] (int r)
    {
        if (r < 1) return;
        auto snap = arrangementView.captureLaneState (laneIndex);
        auto& lane2 = const_cast<std::vector<ArrangementRecorderView::LaneClip>&> (arrangementView.getClips()[(size_t) laneIndex]);
        if (juce::isPositiveAndBelow (clipIndex, (int) lane2.size()))
        {
            switch (r)
            {
                case 1: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::None; break;
                case 2: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Next; break;
                case 3: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Prev; break;
                case 4: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::RandomOther; break;
                case 5: lane2[(size_t) clipIndex].followAction = ArrangementRecorderView::FollowAction::Stop; break;
                default: break;
            }
            arrangementView.commitEdit (snap);
        }
    });
}

void MainPluginEditor::applyFollowAction (int laneIndex, int clipIndex)
{
    using FA = ArrangementRecorderView::FollowAction;
    const auto& allClips = arrangementView.getClips();

    // Always clear running state; follow action may re-queue
    arrangementView.setSlotState (laneIndex, ArrangementRecorderView::SlotState::Idle);

    if (! juce::isPositiveAndBelow (laneIndex, (int) allClips.size())) return;
    const auto& lane = allClips[(size_t) laneIndex];
    if (! juce::isPositiveAndBelow (clipIndex, (int) lane.size())) return;

    const auto fa   = lane[(size_t) clipIndex].followAction;
    const float prob = lane[(size_t) clipIndex].followProbability;
    const int numClips = (int) lane.size();

    if (fa == FA::None || fa == FA::Stop) return;

    // Probability gate
    if (juce::Random::getSystemRandom().nextFloat() > prob) return;

    switch (fa)
    {
        case FA::Next:
            arrangementView.triggerSlot (laneIndex, (clipIndex + 1) % numClips);
            break;
        case FA::Prev:
            arrangementView.triggerSlot (laneIndex, (clipIndex - 1 + numClips) % numClips);
            break;
        case FA::RandomOther:
            if (numClips > 1)
            {
                int next;
                do { next = juce::Random::getSystemRandom().nextInt (numClips); } while (next == clipIndex);
                arrangementView.triggerSlot (laneIndex, next);
            }
            break;
        default: break;
    }
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

    // Selecting a key sets the suite-wide Master Key target (harmonic mixing):
    // every deck with key-match enabled transposes toward it.
    const int currentChoice = (int) std::round (
        processorRef.parameters().getRawParameterValue ("masterKey")->load());

    juce::PopupMenu menu;
    menu.addSectionHeader ("Master Key Target (Camelot)");
    menu.addItem (99, "Off - no key matching", true, currentChoice == 0);
    menu.addSeparator();
    for (int k = 0; k < 12; ++k)
    {
        menu.addItem (k + 1,
                      juce::String (kCamelotMaj[k]) + "B  " + kKeys[k] + " maj   |   "
                      + juce::String (kCamelotMin[k]) + "A  " + kKeys[k] + " min",
                      true, currentChoice == k + 1);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (camelotButton),
        [safe = juce::Component::SafePointer<MainPluginEditor> (this)] (int result)
        {
            if (safe == nullptr || result == 0) return;
            if (auto* p = safe->processorRef.parameters().getParameter ("masterKey"))
            {
                const int choice = result == 99 ? 0 : result;   // 0 = Off, else pitch class + 1
                p->setValueNotifyingHost (p->convertTo0to1 ((float) choice));
                safe->recordStatus.setText (choice == 0 ? "Master key matching off"
                                                        : "Master key target set",
                                            juce::dontSendNotification);
            }
        });
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
