#include "ChopEditor.h"

SattariChopAudioProcessorEditor::SattariChopAudioProcessorEditor (SattariChopAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (940, 620);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Chop");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Slicing sampler");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    statusLabel.setFont (sattari::brand::fontOptions (11.5f, juce::Font::bold));
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    auto setup = [this] (juce::Slider& s, const char* name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setName (name);
        addAndMakeVisible (s);
    };
    setup (sensitivitySlider, "Sensitivity");
    setup (outputSlider, "Output");
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "sensitivity", sensitivitySlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "output",      outputSlider));

    startTimerHz (20);
}

SattariChopAudioProcessorEditor::~SattariChopAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariChopAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load audio to chop", juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile()) processor.loadFile (f);
            repaint();
        });
}

bool SattariChopAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".mp3"))
            return true;
    return false;
}

void SattariChopAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.existsAsFile() && processor.loadFile (file)) break;
    }
    repaint();
}

// --- §3 coordinate mapping + direct manipulation --------------------------------
std::int64_t SattariChopAudioProcessorEditor::sampleAtX (int x) const
{
    auto src = processor.snapshot();
    if (src == nullptr) return 0;
    const auto inner = waveformArea.reduced (8);
    const std::int64_t total = src->audio.getNumSamples();
    if (inner.getWidth() <= 0 || total <= 0) return 0;
    const double f = (double) (x - inner.getX()) / inner.getWidth();
    return (std::int64_t) juce::jlimit (0.0, (double) total, f * (double) total);
}

int SattariChopAudioProcessorEditor::xAtSample (std::int64_t sample) const
{
    auto src = processor.snapshot();
    const auto inner = waveformArea.reduced (8);
    if (src == nullptr) return inner.getX();
    const std::int64_t total = src->audio.getNumSamples();
    if (total <= 0) return inner.getX();
    return inner.getX() + (int) ((double) sample / (double) total * inner.getWidth());
}

int SattariChopAudioProcessorEditor::nearestMarkerX (int x) const
{
    auto src = processor.snapshot();
    if (src == nullptr) return -1;
    const int grab = 6;
    int best = -1, bestD = grab + 1;
    for (int i = 0; i < (int) src->slices.size(); ++i)
    {
        const int d = std::abs (xAtSample (src->slices[(size_t) i].first) - x);
        if (d <= grab && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void SattariChopAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! waveformArea.contains (e.getPosition())) return;
    const int mi = nearestMarkerX (e.x);
    if (mi >= 0)
    {
        auto src = processor.snapshot();                       // grab an existing marker to drag
        dragging = true;
        dragFromSample = src->slices[(size_t) mi].first;
        dragCurrentX = e.x;
        dragWillDelete = false;
    }
    else
    {
        processor.addSliceAtSample (sampleAtX (e.x), ! e.mods.isAltDown());   // click on empty adds
        repaint();
    }
}

void SattariChopAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragCurrentX = juce::jlimit (waveformArea.getX(), waveformArea.getRight(), e.x);
    dragWillDelete = (e.y < waveformArea.getY() - 10 || e.y > waveformArea.getBottom() + 10);  // drag off = delete
    repaint();
}

void SattariChopAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    if (dragWillDelete)
        processor.deleteSliceNear (dragFromSample);
    else
        processor.moveSliceNear (dragFromSample, sampleAtX (dragCurrentX), ! e.mods.isAltDown());
    dragWillDelete = false;
    repaint();
}

// Sensitivity is a live control: when it moves, re-threshold the stored curve. The
// analysis itself never re-runs (§2.2).
void SattariChopAudioProcessorEditor::timerCallback()
{
    const float s = processor.parameters().getRawParameterValue ("sensitivity")->load();
    if (processor.hasSource() && std::abs (s - lastSensitivity) > 1.0e-4f)
    {
        lastSensitivity = s;
        processor.reslice();
        repaint();
    }

    const int n = processor.numSlices();
    if (n != lastSliceCount)
    {
        lastSliceCount = n;
        statusLabel.setText (processor.hasSource()
                                 ? processor.sourceName() + "  -  " + juce::String (n) + " slices  -  play from C1"
                                 : "drop a file, or Load audio...",
                             juce::dontSendNotification);
        repaint();
    }
}

void SattariChopAudioProcessorEditor::rebuildPeaks()
{
    auto src = processor.snapshot();
    const int w = waveformArea.getWidth();
    if (src == nullptr || w <= 0) { peakMin.clear(); peakMax.clear(); peaksForSource = nullptr; return; }
    if (peaksForSource == src.get() && peaksForWidth == w) return;   // cached

    peakMin.assign ((std::size_t) w, 0.0f);
    peakMax.assign ((std::size_t) w, 0.0f);
    const auto& b = src->audio;
    const std::int64_t n = b.getNumSamples();
    const float* d = b.getReadPointer (0);
    for (int x = 0; x < w; ++x)
    {
        const std::int64_t a = (std::int64_t) ((double) x / w * (double) n);
        const std::int64_t e = (std::int64_t) ((double) (x + 1) / w * (double) n);
        float lo = 0.0f, hi = 0.0f;
        for (std::int64_t i = a; i < e && i < n; ++i) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
        peakMin[(std::size_t) x] = lo;
        peakMax[(std::size_t) x] = hi;
    }
    peaksForSource = src.get();
    peaksForWidth = w;
}

void SattariChopAudioProcessorEditor::drawWaveform (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (dragHighlight ? sattari::brand::kAccent : sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, dragHighlight ? 2.0f : 1.0f);

    auto src = processor.snapshot();
    if (src == nullptr)
    {
        g.setColour (juce::Colour (sattari::brand::kTextMuted));
        g.setFont (sattari::brand::fontOptions (13.0f, juce::Font::bold));
        g.drawText ("drop an audio file here", area, juce::Justification::centred);
        return;
    }

    rebuildPeaks();
    const auto inner = area.reduced (8);
    const float midY = (float) inner.getCentreY();
    const float half = inner.getHeight() * 0.46f;

    g.setColour (juce::Colour (sattari::brand::kTextMuted).withAlpha (0.75f));
    for (int x = 0; x < (int) peakMin.size(); ++x)
    {
        const float px = (float) (inner.getX() + x);
        g.drawLine (px, midY - peakMax[(std::size_t) x] * half,
                    px, midY - peakMin[(std::size_t) x] * half, 1.0f);
    }

    // Slice markers.
    const std::int64_t total = src->audio.getNumSamples();
    if (total <= 0) return;
    const auto accent = juce::Colour (sattari::brand::productAccent ("chop"));
    for (const auto& sl : src->slices)
    {
        const bool grabbed = dragging && sl.first == dragFromSample;
        const float px = inner.getX() + (float) ((double) sl.first / (double) total) * inner.getWidth();
        g.setColour (grabbed ? accent.withAlpha (0.25f) : accent.withAlpha (0.9f));   // dim the grabbed one
        g.drawLine (px, (float) inner.getY(), px, (float) inner.getBottom(), grabbed ? 1.0f : 1.0f);
    }

    // Drag preview: where the grabbed marker will land — red if it will be deleted.
    if (dragging)
    {
        g.setColour (dragWillDelete ? juce::Colour (sattari::brand::kDanger)
                                    : juce::Colours::white);
        g.drawLine ((float) dragCurrentX, (float) inner.getY(), (float) dragCurrentX, (float) inner.getBottom(),
                    dragWillDelete ? 2.0f : 1.5f);
    }
}

void SattariChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
    drawWaveform (g, waveformArea);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (sattari::brand::fontOptions (11.0f, juce::Font::bold));
    for (auto* s : { &sensitivitySlider, &outputSlider })
        g.drawText (s->getName(), s->getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
}

void SattariChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (56);
    auto titleCol = top.removeFromLeft (360);
    title.setBounds (titleCol.removeFromTop (34));
    subtitle.setBounds (titleCol.removeFromTop (20));
    statusLabel.setBounds (top.removeFromBottom (22));

    auto bar = area.removeFromTop (32);
    loadButton.setBounds (bar.removeFromLeft (130));
    area.removeFromTop (10);

    auto controls = area.removeFromBottom (96);
    area.removeFromBottom (10);
    waveformArea = area;

    auto knobRow = controls.withSizeKeepingCentre (juce::jmin (controls.getWidth(), 260), controls.getHeight());
    sensitivitySlider.setBounds (knobRow.removeFromLeft (knobRow.getWidth() / 2).reduced (6));
    outputSlider.setBounds (knobRow.reduced (6));
}
