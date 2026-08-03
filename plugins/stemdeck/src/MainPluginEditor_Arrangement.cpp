// MainPluginEditor, part 2 of 2: the ArrangementRecorderView nested class.
// Split out of MainPluginEditor.cpp (which was 6,620 lines) so the arranger and
// the main editor compile as separate translation units. Same class, no
// behaviour change — C++ allows a class's methods to be defined across TUs.
#include "MainPluginEditor.h"
#include "MainPluginEditorInternal.h"

using namespace sdedit;

MainPluginEditor::ArrangementRecorderView::ArrangementRecorderView()
{
    for (int i = 0; i < numLanes; ++i)
        laneDisplayNames[(size_t) i] =
            "CH " + juce::String::charToString ((juce_wchar) ('A' + i));
}

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
        laneDisplayNames[(size_t) i] =
            "CH " + juce::String::charToString ((juce_wchar) ('A' + i));
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

bool MainPluginEditor::ArrangementRecorderView::setArrangementFromCapture (
    const juce::File& arrangementFile, juce::String statusLine)
{
    if (! arrangementFile.existsAsFile() || formatManager == nullptr)
        return false;

    const auto json = juce::JSON::parse (arrangementFile);
    const auto* root = json.getDynamicObject();
    const auto* lanes = json.getProperty ("lanes", {}).getArray();
    if (root == nullptr || lanes == nullptr
        || ! json.getProperty ("schema", {}).toString().startsWith (
                "SattariStemDeck.arrangement."))
        return false;

    mode = Mode::Finished;
    status = std::move (statusLine);
    selectedLane = selectedClip = -1;
    activeDrag = {};
    undoStack.clear();
    redoStack.clear();
    dragReadout.clear();
    for (auto& lane : clips) lane.clear();
    for (auto& curves : deckAutomation) curves.clear();
    for (auto& a : editAuto) { a.points.clear(); a.visible = false; a.target = 0; }
    for (auto& dsp : capturedDsp) dsp = {};
    capturedMaster = {};
    for (int i = 0; i < numLanes; ++i)
        laneDisplayNames[(size_t) i] =
            "CH " + juce::String::charToString ((juce_wchar) ('A' + i));

    masterFile = juce::File (
        json.getProperty ("masterAudio", {}).toString());
    if (masterThumb != nullptr)
        loadThumbnail (*masterThumb, masterFile);

    auto durationFor = [this] (const juce::File& file) -> double
    {
        if (! file.existsAsFile()) return 0.0;
        if (std::unique_ptr<juce::AudioFormatReader> reader {
                formatManager->createReaderFor (file) })
            if (reader->sampleRate > 0.0)
                return (double) reader->lengthInSamples / reader->sampleRate;
        return 0.0;
    };

    auto laneNumber = [] (const juce::String& name) -> int
    {
        if (! name.startsWithIgnoreCase ("Deck ") || name.length() < 6)
            return -1;
        const int i = juce::CharacterFunctions::toUpperCase (name[5]) - 'A';
        return juce::isPositiveAndBelow (i, numLanes) ? i : -1;
    };

    int highestUsedLane = -1;
    int nextAuxLane = 0;
    for (const auto& laneVar : *lanes)
    {
        const auto type = laneVar.getProperty ("type", {}).toString();
        const auto name = laneVar.getProperty ("name", {}).toString();
        if (type == "master_reference_lane")
        {
            if (const auto* automation =
                    laneVar.getProperty ("automationLanes", {}).getArray())
                for (const auto& autoVar : *automation)
                    if (const auto* points =
                            autoVar.getProperty ("points", {}).getArray())
                        for (const auto& point : *points)
                        {
                            const double time = point.getProperty (
                                "timeSeconds", 0.0);
                            const auto parameter = point.getProperty (
                                "parameter", {}).toString();
                            const auto text = point.getProperty (
                                "value", {}).toString();
                            const float value = text.equalsIgnoreCase ("on")
                                                    ? 1.0f
                                                    : text.equalsIgnoreCase ("off")
                                                          ? 0.0f
                                                          : (float) text.getDoubleValue();
                            std::vector<std::pair<double, float>>* destination = nullptr;
                            if (parameter == "master_gain") destination = &capturedMaster.gainDb;
                            else if (parameter == "master_fx_x") destination = &capturedMaster.fxX;
                            else if (parameter == "master_fx_y") destination = &capturedMaster.fxY;
                            else if (parameter == "smart_master_amount") destination = &capturedMaster.smartAmount;
                            else if (parameter == "smart_master_mode") destination = &capturedMaster.smartMode;
                            else if (parameter == "smart_master_on") destination = &capturedMaster.smartOn;
                            else if (parameter == "limiter_ceiling") destination = &capturedMaster.limiterCeilingDb;
                            if (destination != nullptr)
                                destination->emplace_back (time, value);
                        }
            const double duration = durationFor (masterFile);
            if (duration > 0.0)
                clips[(size_t) masterLane].push_back (
                    LaneClip { masterFile, duration, 0.0, duration, 0.0,
                               0.0, 0.0, false, "Live master reference" });
            continue;
        }

        int lane = (int) laneVar.getProperty ("slotIndex", -1);
        if (! juce::isPositiveAndBelow (lane, numLanes))
            lane = laneNumber (name);
        if (type == "mic_lane")
        {
            while (nextAuxLane < numLanes
                   && (! clips[(size_t) nextAuxLane].empty()
                       || nextAuxLane <= highestUsedLane))
                ++nextAuxLane;
            lane = nextAuxLane;
        }
        if (! juce::isPositiveAndBelow (lane, numLanes))
            continue;

        const juce::File rendered (
            laneVar.getProperty ("renderedAudio", {}).toString());
        const double duration = durationFor (rendered);
        if (! rendered.existsAsFile() || duration <= 0.0
            || rendered.getSize() <= 1024)
            continue;

        deckFiles[(size_t) lane] = rendered;
        laneDisplayNames[(size_t) lane] =
            type == "mic_lane" ? "INPUT RAW" : name.toUpperCase();
        highestUsedLane = juce::jmax (highestUsedLane, lane);

        if (const auto* clipVars =
                laneVar.getProperty ("clips", {}).getArray())
        {
            for (const auto& clipVar : *clipVars)
            {
                const double start = juce::jlimit (
                    0.0, duration,
                    (double) clipVar.getProperty ("startSeconds", 0.0));
                const double end = juce::jlimit (
                    start, duration,
                    (double) clipVar.getProperty ("endSeconds", duration));
                if (end - start <= 0.001)
                    continue;
                LaneClip clip;
                clip.file = rendered;
                clip.durationSeconds = duration;
                clip.trimStartSeconds = start;
                clip.trimEndSeconds = end;
                clip.timelineStartSeconds = start;
                clip.label = type == "mic_lane"
                                 ? "Raw input"
                                 : juce::File (clipVar.getProperty (
                                       "source", {}).toString())
                                       .getFileNameWithoutExtension();
                clips[(size_t) lane].push_back (std::move (clip));
            }
        }

        // An Input/instrument lane may have no deck transport events. Its rendered
        // capture is still first-class editable audio.
        if (clips[(size_t) lane].empty())
            clips[(size_t) lane].push_back (
                LaneClip { rendered, duration, 0.0, duration, 0.0,
                           0.0, 0.0, false, name });

        if (deckThumbs[(size_t) lane] != nullptr)
            loadThumbnail (*deckThumbs[(size_t) lane], rendered);

        std::vector<std::pair<double, float>> performedFader;
        if (const auto* automation =
                laneVar.getProperty ("automationLanes", {}).getArray())
        {
            for (const auto& autoVar : *automation)
            {
                AutomationCurve curve;
                curve.label = autoVar.getProperty ("name", {}).toString();
                curve.colour = juce::Colour (0xff67f6d2)
                                   .withRotatedHue (
                                       (float) deckAutomation[(size_t) lane].size()
                                       * 0.13f);
                curve.minValue = 0.0f;
                curve.maxValue = 1.0f;
                const auto id = autoVar.getProperty ("id", {}).toString();
                if (id == "volume") { curve.minValue = -60.0f; curve.maxValue = 12.0f; }
                else if (id == "pitch") { curve.minValue = -12.0f; curve.maxValue = 12.0f; }
                else if (id == "tempo") { curve.minValue = 40.0f; curve.maxValue = 240.0f; }
                else if (id == "eq") { curve.minValue = -24.0f; curve.maxValue = 24.0f; }
                else if (id == "filter") { curve.minValue = -1.0f; curve.maxValue = 1.0f; }

                if (const auto* points =
                        autoVar.getProperty ("points", {}).getArray())
                    for (const auto& point : *points)
                    {
                        const double time = point.getProperty (
                            "timeSeconds", 0.0);
                        const float value = (float) point.getProperty (
                            "value", {}).toString().getDoubleValue();
                        curve.points.emplace_back (time, value);
                        if (id == "fader")
                            performedFader.emplace_back (time, value);
                        else if (id == "eq")
                        {
                            const auto parameter = point.getProperty (
                                "parameter", {}).toString();
                            auto& dsp = capturedDsp[(size_t) lane];
                            if (parameter == "eq_low")
                                dsp.lowDb.emplace_back (time, value);
                            else if (parameter == "eq_mid")
                                dsp.midDb.emplace_back (time, value);
                            else if (parameter == "eq_high")
                                dsp.highDb.emplace_back (time, value);
                        }
                        else if (id == "filter")
                            capturedDsp[(size_t) lane].filter.emplace_back (
                                time, value);
                        else if (id == "effects")
                        {
                            const auto parameter = point.getProperty (
                                "parameter", {}).toString();
                            if (parameter == "reverb_mix")
                                capturedDsp[(size_t) lane].reverbMix.emplace_back (
                                    time, value);
                            else if (parameter == "echo_mix")
                                capturedDsp[(size_t) lane].echoMix.emplace_back (
                                    time, value);
                        }
                    }
                if (! curve.points.empty())
                    deckAutomation[(size_t) lane].push_back (
                        std::move (curve));
            }
        }

        // v3 deck lanes point at a clean pre-fader/pre-insert source. Reconstruct the
        // performed channel gain as editable sample-accurate automation so initial
        // arrangement playback matches the take, while every fader move remains
        // changeable. v2 printed captures deliberately skip this to avoid applying
        // their already-baked fader a second time.
        if (laneVar.getProperty ("captureStage", {}).toString()
                == "pre_fader_pre_insert")
        {
            auto sortPoints = [] (auto& points)
            {
                std::sort (points.begin(), points.end(),
                           [] (const auto& a, const auto& b)
                           { return a.first < b.first; });
            };
            sortPoints (performedFader);
            sortPoints (capturedDsp[(size_t) lane].lowDb);
            sortPoints (capturedDsp[(size_t) lane].midDb);
            sortPoints (capturedDsp[(size_t) lane].highDb);
            sortPoints (capturedDsp[(size_t) lane].filter);
            sortPoints (capturedDsp[(size_t) lane].reverbMix);
            sortPoints (capturedDsp[(size_t) lane].echoMix);

            auto valueAt = [] (const auto& points, double time, float fallback)
            {
                if (points.empty()) return fallback;
                if (time <= points.front().first) return points.front().second;
                if (time >= points.back().first) return points.back().second;
                const auto next = std::lower_bound (
                    points.begin(), points.end(), time,
                    [] (const auto& point, double t)
                    { return point.first < t; });
                const auto& b = *next;
                const auto& a = *(next - 1);
                const double alpha = (time - a.first)
                                   / juce::jmax (1.0e-9, b.first - a.first);
                return (float) juce::jmap (
                    alpha, (double) a.second, (double) b.second);
            };

            std::vector<double> times { 0.0 };
            for (const auto& p : performedFader) times.push_back (p.first);
            std::sort (times.begin(), times.end());
            times.erase (std::unique (times.begin(), times.end(),
                                     [] (double a, double b)
                                     { return std::abs (a - b) < 1.0e-9; }),
                         times.end());

            auto& editable = editAuto[(size_t) lane];
            editable.points.clear();
            editable.points.reserve (times.size());
            for (const double time : times)
            {
                const float fader = juce::jlimit (
                    0.0f, 1.0f, valueAt (performedFader, time, 1.0f));
                editable.points.emplace_back (
                    time, fader);
            }
            editable.target = 0;
            editable.visible = ! editable.points.empty();
        }
    }

    auto sortMasterPoints = [] (auto& points)
    {
        std::sort (points.begin(), points.end(),
                   [] (const auto& a, const auto& b)
                   { return a.first < b.first; });
    };
    sortMasterPoints (capturedMaster.gainDb);
    sortMasterPoints (capturedMaster.fxX);
    sortMasterPoints (capturedMaster.fxY);
    sortMasterPoints (capturedMaster.smartAmount);
    sortMasterPoints (capturedMaster.smartMode);
    sortMasterPoints (capturedMaster.smartOn);
    sortMasterPoints (capturedMaster.limiterCeilingDb);

    if (clips[(size_t) masterLane].empty())
    {
        const double duration = durationFor (masterFile);
        if (duration > 0.0)
            clips[(size_t) masterLane].push_back (
                LaneClip { masterFile, duration, 0.0, duration, 0.0,
                           0.0, 0.0, false, "Live master reference" });
    }

    if (highestUsedLane >= 0)
        setVisibleLanes (highestUsedLane + 1);
    repaint();
    return highestUsedLane >= 0 || ! clips[(size_t) masterLane].empty();
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
        if (lane < numLanes)
        {
            const auto& dsp = capturedDsp[(size_t) lane];
            auto appendDsp = [&lt] (
                juce::StringRef parameter,
                const std::vector<std::pair<double, float>>& points)
            {
                if (points.empty()) return;
                juce::ValueTree automation ("DSPAUTO");
                automation.setProperty (
                    "parameter", juce::String (parameter), nullptr);
                for (const auto& [time, value] : points)
                {
                    juce::ValueTree point ("AP");
                    point.setProperty ("t", time, nullptr);
                    point.setProperty ("v", (double) value, nullptr);
                    automation.appendChild (point, nullptr);
                }
                lt.appendChild (automation, nullptr);
            };
            appendDsp ("eq_low", dsp.lowDb);
            appendDsp ("eq_mid", dsp.midDb);
            appendDsp ("eq_high", dsp.highDb);
            appendDsp ("filter", dsp.filter);
            appendDsp ("reverb_mix", dsp.reverbMix);
            appendDsp ("echo_mix", dsp.echoMix);
        }
        root.appendChild (lt, nullptr);
    }
    if (! capturedMaster.empty())
    {
        juce::ValueTree masterAutomation ("MASTERAUTO");
        auto append = [&masterAutomation] (
            juce::StringRef parameter,
            const std::vector<std::pair<double, float>>& points)
        {
            if (points.empty()) return;
            juce::ValueTree lane ("PARAM");
            lane.setProperty ("id", juce::String (parameter), nullptr);
            for (const auto& [time, value] : points)
            {
                juce::ValueTree point ("AP");
                point.setProperty ("t", time, nullptr);
                point.setProperty ("v", (double) value, nullptr);
                lane.appendChild (point, nullptr);
            }
            masterAutomation.appendChild (lane, nullptr);
        };
        append ("gain", capturedMaster.gainDb);
        append ("fx_x", capturedMaster.fxX);
        append ("fx_y", capturedMaster.fxY);
        append ("smart_amount", capturedMaster.smartAmount);
        append ("smart_mode", capturedMaster.smartMode);
        append ("smart_on", capturedMaster.smartOn);
        append ("limiter", capturedMaster.limiterCeilingDb);
        root.appendChild (masterAutomation, nullptr);
    }
    return root;
}

void MainPluginEditor::ArrangementRecorderView::fromState (const juce::ValueTree& root)
{
    if (! root.hasType ("ARRANGEMENT")) return;
    for (auto& v : clips) v.clear();
    for (auto& m : midiClips) m = {};
    for (auto& dsp : capturedDsp) dsp = {};
    capturedMaster = {};
    loopStartSeconds = root.getProperty ("loopStart", 0.0);
    loopEndSeconds   = root.getProperty ("loopEnd",   0.0);
    loopEnabled      = root.getProperty ("loopEnabled", false);

    if (const auto masterAutomation = root.getChildWithName ("MASTERAUTO");
        masterAutomation.isValid())
        for (int parameter = 0; parameter < masterAutomation.getNumChildren(); ++parameter)
        {
            const auto lane = masterAutomation.getChild (parameter);
            if (! lane.hasType ("PARAM")) continue;
            const auto id = lane.getProperty ("id").toString();
            std::vector<std::pair<double, float>>* destination = nullptr;
            if (id == "gain") destination = &capturedMaster.gainDb;
            else if (id == "fx_x") destination = &capturedMaster.fxX;
            else if (id == "fx_y") destination = &capturedMaster.fxY;
            else if (id == "smart_amount") destination = &capturedMaster.smartAmount;
            else if (id == "smart_mode") destination = &capturedMaster.smartMode;
            else if (id == "smart_on") destination = &capturedMaster.smartOn;
            else if (id == "limiter") destination = &capturedMaster.limiterCeilingDb;
            if (destination == nullptr) continue;
            for (int point = 0; point < lane.getNumChildren(); ++point)
            {
                const auto value = lane.getChild (point);
                if (value.hasType ("AP"))
                    destination->emplace_back (
                        (double) value.getProperty ("t", 0.0),
                        (float) (double) value.getProperty ("v", 0.0));
            }
        }

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

            auto& dsp = capturedDsp[(size_t) lane];
            for (int child = 0; child < lt.getNumChildren(); ++child)
            {
                const auto automation = lt.getChild (child);
                if (! automation.hasType ("DSPAUTO")) continue;
                const auto parameter =
                    automation.getProperty ("parameter").toString();
                std::vector<std::pair<double, float>>* destination = nullptr;
                if (parameter == "eq_low") destination = &dsp.lowDb;
                else if (parameter == "eq_mid") destination = &dsp.midDb;
                else if (parameter == "eq_high") destination = &dsp.highDb;
                else if (parameter == "filter") destination = &dsp.filter;
                else if (parameter == "reverb_mix") destination = &dsp.reverbMix;
                else if (parameter == "echo_mix") destination = &dsp.echoMix;
                if (destination == nullptr) continue;
                for (int point = 0;
                     point < automation.getNumChildren(); ++point)
                {
                    const auto value = automation.getChild (point);
                    if (value.hasType ("AP"))
                        destination->emplace_back (
                            (double) value.getProperty ("t", 0.0),
                            (float) (double) value.getProperty ("v", 0.0));
                }
                std::sort (
                    destination->begin(), destination->end(),
                    [] (const auto& left, const auto& right)
                    { return left.first < right.first; });
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
    const auto panelBase = juce::Colour (stemdeck::tokens::bgWell).darker (0.12f);
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

    // Lanes area. Height is divided by the lanes actually SHOWN (master + the
    // existing channels), not by the pool size — dividing by the pool left the
    // drawn lanes squeezed into the top with dead space beneath them.
    const int lanesTotal = visibleLanes + 1;
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
            static const char* targetNames[numAutoTargets] = { "VOL" };

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

    for (int i = 0; i < visibleLanes; ++i)
    {
        auto laneRect = area.removeFromTop (laneHeight);
        drawLane (laneRect, laneDisplayNames[(size_t) i],
                  deckAccent (i), deckThumbs[(size_t) i].get(), deckFiles[(size_t) i], i);
        if (i < visibleLanes - 1)
            area.removeFromTop (laneGap);
    }
    // Lanes past the channel count are not drawn, so their cached rectangles must
    // not linger — laneAtY()/hitTestAt() read these to route clicks.
    for (int i = visibleLanes; i < numLanes; ++i)
        laneRects[(size_t) i] = {};

    // Clip launcher slot-state overlay: WaitingToStart = pulsing amber, Running = solid green.
    {
        const bool blink = ((juce::Time::currentTimeMillis() / 220) % 2) == 0;
        for (int i = 0; i < visibleLanes; ++i)
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
