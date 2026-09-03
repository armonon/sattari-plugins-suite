#include "SongKeyEditor.h"

#include <cmath>

SattariSongKeyAudioProcessorEditor::SattariSongKeyAudioProcessorEditor (
    SattariSongKeyAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setTitle ("Sattari Key");
    setDescription (
        "Whole-song and live key detection with suite publication");
    setResizable (true, true);
    setResizeLimits (720, 600, 1120, 840);
    setSize (840, 650);

    addAndMakeVisible (header);
    addAndMakeVisible (sessionHud);
    addAndMakeVisible (confidenceMetric);
    addAndMakeVisible (tuningMetric);
    addAndMakeVisible (tempoMetric);
    addAndMakeVisible (alternativeMetric);

    lockToggle.setTitle ("Lock the reviewed session key");
    lockToggle.setDescription (
        "Freeze this reviewed key and tempo, then publish them to other Sattari products");
    lockToggle.onClick = [this]
    {
        audioProcessor.setSessionLocked (lockToggle.getToggleState());
    };
    addAndMakeVisible (lockToggle);
    dynamicFollowToggle.setTitle ("Follow live key changes automatically");
    dynamicFollowToggle.setDescription (
        "Analyse every second and publish only changes confirmed by the fast or stable lane");
    dynamicFollowToggle.onClick = [this]
    {
        audioProcessor.setDynamicFollowEnabled (
            dynamicFollowToggle.getToggleState());
        updateManualControls();
        updateSnapshot();
    };
    addAndMakeVisible (dynamicFollowToggle);

    dynamicInterval.setSliderStyle (juce::Slider::LinearHorizontal);
    dynamicInterval.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 22);
    dynamicInterval.setTextValueSuffix (" s");
    dynamicInterval.setTitle ("Dynamic stability horizon");
    dynamicInterval.setDescription (
        "Seconds of agreement required for marginal key changes; analysis always runs every second");
    addAndMakeVisible (dynamicInterval);
    manualToggle.setTitle ("Manual key override");
    manualToggle.setDescription (
        "Use an explicit key instead of the detected result");
    manualToggle.onStateChange = [this]
    {
        audioProcessor.unlockSession();
        updateManualControls();
        updateSnapshot();
    };
    addAndMakeVisible (manualToggle);

    setupCombo (
        manualRootBox, SattariSongKeyAudioProcessor::rootChoices(),
        "Manual root");
    setupCombo (
        manualScaleBox, SattariSongKeyAudioProcessor::scaleChoices(),
        "Manual scale");

    lockAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "lockSession", lockToggle);
    dynamicFollowAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "dynamicFollow", dynamicFollowToggle);
    dynamicIntervalAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.parameters(), "dynamicInterval", dynamicInterval);
    manualAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.parameters(), "manual", manualToggle);
    rootAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "manualRoot", manualRootBox);
    scaleAttachment = std::make_unique<ComboAttachment> (
        audioProcessor.parameters(), "manualScale", manualScaleBox);
    manualRootBox.onChange = [this] { audioProcessor.unlockSession(); };
    manualScaleBox.onChange = [this] { audioProcessor.unlockSession(); };

    chooseSongButton.setTitle ("Choose a song file");
    chooseSongButton.setDescription (
        "Analyse a complete WAV, AIFF, FLAC, MP3, M4A, AAC, or OGG song");
    chooseSongButton.onClick = [this] { chooseSong(); };
    addAndMakeVisible (chooseSongButton);

    liveButton.setTitle ("Listen to live input");
    liveButton.setDescription (
        "Continuously detect the key of audio passing through this plugin");
    liveButton.onClick = [this] { audioProcessor.listenLive(); };
    addAndMakeVisible (liveButton);

    rescanButton.setTitle ("Analyse the current source again");
    rescanButton.setDescription (
        "Clear the current decision and analyse the live input or selected song again");
    rescanButton.onClick = [this] { audioProcessor.reanalyseCurrentSource(); };
    addAndMakeVisible (rescanButton);

    updateManualControls();
    updateSnapshot();
    startTimerHz (12);
}

SattariSongKeyAudioProcessorEditor::~SattariSongKeyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::String SattariSongKeyAudioProcessorEditor::rootName (
    int pitchClass)
{
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    return names[((pitchClass % 12) + 12) % 12];
}

juce::String SattariSongKeyAudioProcessorEditor::camelotName (
    int pitchClass, bool minor)
{
    static constexpr int majorNumbers[12] = {
        8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1
    };
    static constexpr int minorNumbers[12] = {
        5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10
    };
    const int pc = ((pitchClass % 12) + 12) % 12;
    return juce::String (minor ? minorNumbers[pc] : majorNumbers[pc])
        + (minor ? "A" : "B");
}

void SattariSongKeyAudioProcessorEditor::chooseSong()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a song to analyse", juce::File {},
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.aac;*.ogg");
    auto safeThis = juce::Component::SafePointer<
        SattariSongKeyAudioProcessorEditor> (this);
    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto result = chooser.getResult();
            if (result.existsAsFile())
                safeThis->beginFileAnalysis (result);
            safeThis->fileChooser.reset();
        });
}

void SattariSongKeyAudioProcessorEditor::beginFileAnalysis (
    const juce::File& file)
{
    audioProcessor.analyseFileAsync (file);
    updateSnapshot();
}

bool SattariSongKeyAudioProcessorEditor::isInterestedInFileDrag (
    const juce::StringArray& files)
{
    return files.size() == 1
        && SattariSongKeyAudioProcessor::isSupportedAudioFile (
            juce::File (files[0]));
}

void SattariSongKeyAudioProcessorEditor::fileDragEnter (
    const juce::StringArray&, int, int)
{
    dragActive = true;
    repaint (sourceArea);
}

void SattariSongKeyAudioProcessorEditor::fileDragExit (
    const juce::StringArray&)
{
    dragActive = false;
    repaint (sourceArea);
}

void SattariSongKeyAudioProcessorEditor::filesDropped (
    const juce::StringArray& files, int, int)
{
    dragActive = false;
    if (files.size() == 1)
        beginFileAnalysis (juce::File (files[0]));
}

void SattariSongKeyAudioProcessorEditor::setupCombo (
    juce::ComboBox& combo, const juce::StringArray& items,
    const juce::String& title)
{
    combo.addItemList (items, 1);
    combo.setTitle (title);
    combo.setDescription ("Choose " + title.toLowerCase());
    addAndMakeVisible (combo);
}

void SattariSongKeyAudioProcessorEditor::timerCallback()
{
    updateSnapshot();
}

void SattariSongKeyAudioProcessorEditor::updateManualControls()
{
    const bool dynamic = dynamicFollowToggle.getToggleState();
    const bool manual = manualToggle.getToggleState() && ! dynamic;
    manualToggle.setEnabled (! dynamic);
    manualRootBox.setEnabled (manual && ! dynamic);
    manualScaleBox.setEnabled (manual && ! dynamic);
    dynamicInterval.setEnabled (dynamic);
}

juce::String SattariSongKeyAudioProcessorEditor::currentRoot() const
{
    if (snapshot.dynamicActive)
        return rootName (snapshot.dynamicRoot);
    if (snapshot.dynamicEnabled)
        return "--";
    if (snapshot.sessionLocked)
        return rootName (snapshot.lockedRoot);
    if (manualToggle.getToggleState())
        return rootName ((int) std::round (
            audioProcessor.parameters()
                .getRawParameterValue ("manualRoot")->load()));
    const bool trusted = snapshot.valid
        && snapshot.confidence >= sattari::dsp::kReleaseKeyConfidence;
    return trusted ? rootName (snapshot.root) : juce::String ("--");
}

juce::String SattariSongKeyAudioProcessorEditor::currentMode() const
{
    if (snapshot.dynamicActive)
        return juce::String (snapshot.dynamicMinor ? "MINOR" : "MAJOR")
            + " · " + camelotName (snapshot.dynamicRoot, snapshot.dynamicMinor)
            + " · DYNAMIC";
    if (snapshot.dynamicEnabled)
        return "DYNAMIC · BUILDING EVIDENCE";
    if (snapshot.sessionLocked)
        return juce::String (snapshot.lockedMinor ? "MINOR" : "MAJOR")
            + " · " + camelotName (snapshot.lockedRoot, snapshot.lockedMinor)
            + " · LOCKED";
    if (manualToggle.getToggleState())
    {
        const bool minor = audioProcessor.parameters()
                               .getRawParameterValue ("manualScale")->load() > 0.5f;
        const int root = (int) std::round (audioProcessor.parameters()
                                               .getRawParameterValue ("manualRoot")->load());
        return juce::String (minor ? "MINOR" : "MAJOR")
            + " · " + camelotName (root, minor) + " · MANUAL";
    }
    if (snapshot.analysing)
        return "ANALYSING FULL SONG";
    if (! snapshot.valid
        || snapshot.confidence < sattari::dsp::kReleaseKeyConfidence)
        return snapshot.valid ? "NO RESULT" : "LISTENING";
    return juce::String (snapshot.minor ? "MINOR" : "MAJOR")
        + " · " + camelotName (snapshot.root, snapshot.minor);
}

void SattariSongKeyAudioProcessorEditor::updateSnapshot()
{
    snapshot = audioProcessor.detected();
    dynamicFollowToggle.setToggleState (
        snapshot.dynamicEnabled, juce::dontSendNotification);
    updateManualControls();
    const bool manual = manualToggle.getToggleState() && ! snapshot.dynamicEnabled;
    const bool trustedDetector = snapshot.valid
        && snapshot.confidence >= sattari::dsp::kReleaseKeyConfidence;
    const bool trusted = snapshot.dynamicActive || trustedDetector;
    // The detector returns a winner-vs-runner-up margin, not a calibrated
    // probability. Display it without the old arbitrary 3x inflation.
    const float confidence = manual ? 1.0f
        : juce::jlimit (0.0f, 1.0f, (float) (snapshot.dynamicActive
                                                ? snapshot.dynamicConfidence
                                                : snapshot.confidence));
    confidenceMetric.setValue (
        juce::String ((int) std::round (confidence * 100.0f)),
        "%", manual ? "explicit key"
            : snapshot.dynamicActive ? "accepted rolling margin"
                                     : "uncalibrated margin");
    tuningMetric.setValue (
        trusted ? juce::String (snapshot.detuneCents, 1)
                       : juce::String ("--"),
        "cents", "relative to A440");
    tempoMetric.setValue (
        snapshot.tempoValid ? juce::String (snapshot.bpm, 1) : juce::String ("--"),
        snapshot.tempoValid ? "BPM" : juce::String {},
        snapshot.tempoFromHost
            ? "host tempo"
            : snapshot.tempoValid
                ? juce::String ((int) std::round (snapshot.tempoConfidence * 100.0))
                    + "% rhythm agreement"
                : "keep listening",
        snapshot.tempoOctaveAmbiguous);

    if (manual)
    {
        const int root = (int) std::round (audioProcessor.parameters()
                                               .getRawParameterValue ("manualRoot")->load());
        const bool minor = audioProcessor.parameters()
                               .getRawParameterValue ("manualScale")->load() > 0.5f;
        const int relativeRoot = minor ? (root + 3) % 12 : (root + 9) % 12;
        alternativeMetric.setValue (
            rootName (relativeRoot) + (minor ? " MAJOR" : " MINOR"), {},
            "relative · " + camelotName (relativeRoot, ! minor));
    }
    else if (snapshot.alternativeValid)
    {
        const int relativeRoot = snapshot.minor
            ? (snapshot.root + 3) % 12 : (snapshot.root + 9) % 12;
        const bool isRelative = snapshot.alternativeRoot == relativeRoot
            && snapshot.alternativeMinor != snapshot.minor;
        alternativeMetric.setValue (
            rootName (snapshot.alternativeRoot)
                + (snapshot.alternativeMinor ? " MINOR" : " MAJOR"), {},
            isRelative
                ? "relative · " + camelotName (
                    snapshot.alternativeRoot, snapshot.alternativeMinor)
                : "runner-up · relative " + rootName (relativeRoot)
                    + (snapshot.minor ? " major" : " minor"));
    }
    else
        alternativeMetric.setValue ("--", {}, "waiting for a ranked result");

    liveButton.setToggleState (
        snapshot.source == SattariSongKeyAudioProcessor::AnalysisSource::Live,
        juce::dontSendNotification);
    lockToggle.setButtonText (
        snapshot.sessionLocked ? "Session locked"
            : snapshot.lockRequested ? "Waiting to lock" : "Lock session");
    lockToggle.setEnabled (! snapshot.dynamicEnabled
        && (snapshot.lockRequested || manual || trustedDetector));

    juce::String status;
    bool active = false;
    if (snapshot.analysing)
    {
        status = "ANALYSING FILE · "
            + juce::String ((int) std::round (snapshot.progress * 100.0)) + "%";
        active = true;
    }
    else if (snapshot.error.isNotEmpty())
    {
        status = "FILE COULD NOT BE ANALYSED";
    }
    else if (snapshot.dynamicEnabled && snapshot.dynamicActive
             && snapshot.publishing)
    {
        status = "ENGINE V2 / "
            + juce::String (snapshot.dynamicFastLane ? "FAST" : "STABLE")
            + " / REV "
            + juce::String (snapshot.dynamicRevision)
            + " / NEXT EVIDENCE "
            + juce::String (snapshot.dynamicSecondsUntilScan, 1) + " S";
        active = true;
    }
    else if (snapshot.dynamicEnabled && snapshot.dynamicPending)
    {
        status = "DYNAMIC / CONFIRMING "
            + rootName (snapshot.dynamicPendingRoot)
            + (snapshot.dynamicPendingMinor ? " MINOR " : " MAJOR ")
            + juce::String (snapshot.dynamicPendingConfirmations)
            + "/" + juce::String (snapshot.dynamicRequiredConfirmations);
        active = true;
    }
    else if (snapshot.dynamicEnabled && snapshot.dynamicActive)
    {
        status = "DYNAMIC READY / SUITE BUS OWNED ELSEWHERE";
        active = true;
    }
    else if (snapshot.dynamicEnabled)
    {
        status = "ENGINE V2 / FAST 1-2 S / STABLE "
            + juce::String (snapshot.dynamicIntervalSeconds, 1) + " S";
        active = true;
    }
    else if (snapshot.sessionLocked && snapshot.publishing)
    {
        status = "SESSION LOCKED / PUBLISHING";
        active = true;
    }
    else if (snapshot.lockRequested)
    {
        status = "LOCK WAITING / NEED CONFIDENT KEY";
    }
    else if (manual)
    {
        status = "MANUAL / LOCAL";
        active = true;
    }
    else if (trusted)
    {
        status = "KEY DETECTED";
        active = true;
    }
    else if (snapshot.valid)
    {
        status = "NO RESULT / KEEP LISTENING";
    }
    else
    {
        status = "LISTENING";
    }
    header.setStatus (status, active);
    repaint();
}

void SattariSongKeyAudioProcessorEditor::paint (juce::Graphics& g)
{
    sattari::studio::paintCanvas (g, getLocalBounds(), accent);
    sattari::studio::paintSurface (g, sourceArea.toFloat(), true);
    sattari::studio::paintSurface (g, keyArea.toFloat(), true);
    sattari::studio::paintSurface (g, manualArea.toFloat(), true);

    auto key = keyArea.reduced (16, 12);
    sattari::studio::paintSectionTitle (
        g, key.removeFromTop (18), "Detected key",
        snapshot.dynamicEnabled ? "rolling trusted decision"
            : snapshot.sessionLocked ? "locked session truth"
                                     : "review before locking");
    key.removeFromTop (4);

    auto rootArea = key.removeFromTop (
        juce::jmax (72, (int) std::round (key.getHeight() * 0.58f)));
    g.setColour (
        currentRoot() == "--"
            ? sattari::studio::colour::tertiary.withAlpha (0.6f)
            : sattari::studio::colour::text);
    g.setFont (sattari::brand::fontOptions (
        (float) juce::jlimit (
            66, 104, (int) std::round (rootArea.getHeight() * 0.92f)),
        juce::Font::bold));
    g.drawText (
        currentRoot(), rootArea,
        juce::Justification::centred, false);

    g.setColour (accent);
    g.setFont (sattari::brand::fontOptions (14.0f, juce::Font::bold));
    g.drawText (
        currentMode(), key.removeFromTop (24),
        juce::Justification::centred, false);

    key.removeFromTop (6);
    auto confidenceBar = key.removeFromTop (8).toFloat();
    g.setColour (sattari::studio::colour::separator);
    g.fillRoundedRectangle (confidenceBar, 4.0f);
    const bool manual = manualToggle.getToggleState() && ! snapshot.dynamicEnabled;
    const float confidence = manual ? 1.0f : juce::jlimit (
        0.0f, 1.0f, (float) (snapshot.dynamicActive
                                 ? snapshot.dynamicConfidence
                                 : snapshot.confidence));
    g.setColour (accent);
    g.fillRoundedRectangle (
        confidenceBar.withWidth (
            confidenceBar.getWidth() * confidence),
        4.0f);

    sattari::studio::paintSectionTitle (
        g, manualArea.reduced (14).removeFromTop (18),
        "Detection", "automatic or explicit key");

    auto source = sourceArea.reduced (16, 11);
    sattari::studio::paintSectionTitle (
        g, source.removeFromTop (18), "Analysis source",
        snapshot.source == SattariSongKeyAudioProcessor::AnalysisSource::File
            ? "full-song scan" : "plugin input");
    source.removeFromTop (5);

    juce::String primary;
    juce::String secondary;
    if (snapshot.source == SattariSongKeyAudioProcessor::AnalysisSource::File)
    {
        primary = snapshot.sourceName.isNotEmpty()
            ? snapshot.sourceName : "Choose or drop a song";
        if (snapshot.analysing)
            secondary = "Reading the complete track · "
                + juce::String ((int) std::round (snapshot.progress * 100.0)) + "%";
        else if (snapshot.error.isNotEmpty())
            secondary = snapshot.error;
        else if (snapshot.possibleModulation)
            secondary = "Section results differ · possible key change";
        else
            secondary = "Drop another audio file here to replace it";
    }
    else
    {
        primary = "Live input";
        secondary = snapshot.dynamicEnabled
            ? "Dynamic Follow holds the last trusted key through silence and ambiguity"
            : "Play the song through this track, master bus, or standalone input";
    }

    auto text = source.removeFromLeft (juce::jmax (240, source.getWidth() - 190));
    g.setColour (sattari::studio::colour::text);
    g.setFont (sattari::brand::fontOptions (15.0f, juce::Font::bold));
    g.drawFittedText (primary, text.removeFromTop (21),
                      juce::Justification::centredLeft, 1);
    g.setColour (snapshot.error.isNotEmpty()
                     ? juce::Colours::orangered.withAlpha (0.9f)
                     : sattari::studio::colour::secondary);
    g.setFont (sattari::brand::fontOptions (11.5f));
    g.drawFittedText (secondary, text.removeFromTop (18),
                      juce::Justification::centredLeft, 1);

    auto drop = source.reduced (4, 0).toFloat();
    g.setColour ((dragActive ? accent : sattari::studio::colour::separator)
                     .withAlpha (dragActive ? 0.95f : 0.65f));
    g.drawRoundedRectangle (drop, 8.0f, dragActive ? 2.0f : 1.0f);
    g.setColour (dragActive ? accent : sattari::studio::colour::secondary);
    g.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    g.drawText (dragActive ? "DROP TO ANALYSE" : "DROP SONG HERE",
                drop.toNearestInt(), juce::Justification::centred, false);

    if (snapshot.analysing)
    {
        auto bar = sourceArea.reduced (16, 7).removeFromBottom (4).toFloat();
        g.setColour (sattari::studio::colour::separator);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (
            bar.withWidth (bar.getWidth() * juce::jlimit (
                0.0f, 1.0f, (float) snapshot.progress)), 2.0f);
    }
}

void SattariSongKeyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (
        (int) sattari::studio::metric::outerMargin);
    header.setBounds (
        area.removeFromTop (sattari::studio::metric::headerHeight));

    auto command = area.removeFromTop (
        sattari::studio::metric::commandHeight);
    lockToggle.setBounds (command.removeFromLeft (152));
    command.removeFromLeft (6);
    chooseSongButton.setBounds (command.removeFromLeft (104));
    command.removeFromLeft (6);
    liveButton.setBounds (command.removeFromLeft (94));
    command.removeFromLeft (6);
    rescanButton.setBounds (command.removeFromLeft (76));
    sessionHud.setBounds (
        command.removeFromRight (190).withSizeKeepingCentre (190, 26));

    area.removeFromTop ((int) sattari::studio::metric::gap);
    sourceArea = area.removeFromTop (76);
    area.removeFromTop ((int) sattari::studio::metric::gap);
    manualArea = area.removeFromBottom (72);
    area.removeFromBottom ((int) sattari::studio::metric::gap);

    const int gap = (int) sattari::studio::metric::gap;
    keyArea = area.removeFromLeft (
        juce::jlimit (360, 500, (int) std::round (
            (float) area.getWidth() * 0.66f)));
    area.removeFromLeft (gap);

    auto metrics = area;
    const int metricHeight = (metrics.getHeight() - gap * 3) / 4;
    confidenceMetric.setBounds (metrics.removeFromTop (metricHeight));
    metrics.removeFromTop (gap);
    tempoMetric.setBounds (metrics.removeFromTop (metricHeight));
    metrics.removeFromTop (gap);
    alternativeMetric.setBounds (metrics.removeFromTop (metricHeight));
    metrics.removeFromTop (gap);
    tuningMetric.setBounds (metrics.removeFromTop (metricHeight));

    auto manual = manualArea.reduced (14);
    manual.removeFromTop (24);
    manualToggle.setBounds (manual.removeFromLeft (144));
    manual.removeFromLeft (8);
    manualRootBox.setBounds (manual.removeFromLeft (92));
    manual.removeFromLeft (8);
    manualScaleBox.setBounds (manual.removeFromLeft (112));
    manual.removeFromLeft (8);
    dynamicFollowToggle.setBounds (manual.removeFromLeft (140));
    manual.removeFromLeft (8);
    dynamicInterval.setBounds (manual);
}
