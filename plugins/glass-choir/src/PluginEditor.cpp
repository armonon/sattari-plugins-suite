#include "PluginEditor.h"

namespace
{
using namespace sattari::brand;

void styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (kTextMuted));
}

// Draw a titled section panel and return the interior body rectangle.
juce::Rectangle<int> drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name)
{
    auto rf = r.toFloat();
    g.setColour (juce::Colour (kPanel));
    g.fillRoundedRectangle (rf, 10.0f);
    g.setColour (juce::Colour (kHairline));
    g.drawRoundedRectangle (rf.reduced (0.5f), 10.0f, 1.0f);

    g.setColour (juce::Colour (kTextFaint));
    g.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    g.drawText (name, r.reduced (12, 8).removeFromTop (14), juce::Justification::topLeft);
    return r;
}
} // namespace

// =============================================================================
//  Construction
// =============================================================================
SattariGlassChoirAudioProcessorEditor::SattariGlassChoirAudioProcessorEditor (SattariGlassChoirAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    // --- Header text ----------------------------------------------------------
    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Glass Choir");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle,
        "Vocal-ensemble effect - one voice into a lush, key-aware choir");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    // --- Header controls ------------------------------------------------------
    addKnob   ("input",  "IN",  " dB", inputK);
    addKnob   ("output", "OUT", " dB", outputK);
    addToggle ("bypass", "Bypass", bypassT);

    // --- Hero macro + anchors + mode -----------------------------------------
    addKnob ("character", "CHARACTER", {}, characterK);
    characterK.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    characterAnchors.setText ("HUMAN      MASSIVE      DESTROYED", juce::dontSendNotification);
    characterAnchors.setJustificationType (juce::Justification::centred);
    characterAnchors.setFont (sattari::brand::fontOptions (9.5f, juce::Font::bold));
    characterAnchors.setColour (juce::Label::textColourId, juce::Colour (kTextFaint));
    addAndMakeVisible (characterAnchors);
    addChoice ("mode", "MODE", SattariGlassChoirAudioProcessor::modeChoices(), modeC);

    // --- ENSEMBLE -------------------------------------------------------------
    addChoice ("voiceCount",    "VOICES", SattariGlassChoirAudioProcessor::voiceCountChoices(), voiceCountC);
    addChoice ("engineQuality", "ENGINE", SattariGlassChoirAudioProcessor::engineChoices(),     engineC);
    addKnob ("detuneSpread",  "DETUNE",  " c",  detuneK);
    addKnob ("timing",        "TIMING",  " ms", timingK);
    addKnob ("humanize",      "HUMAN",   {},    humanizeK);
    addKnob ("formantSpread", "FORMANT", " st", formantK);
    addKnob ("ensembleLevel", "LEVEL",   " dB", ensembleLevelK);

    // --- HARMONY --------------------------------------------------------------
    addToggle ("harmonyOn", "Harmony", harmonyT);
    addToggle ("alignOn",   "Align",   alignT);

    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId    = "glass-choir";
        cfg.showFollow  = true;
        cfg.showAutoKey = false;
        cfg.rootNames   = SattariGlassChoirAudioProcessor::rootChoices();
        cfg.scaleNames  = SattariGlassChoirAudioProcessor::scaleChoices();
        cfg.hint        = "click: follow song key / set by hand";
        keyButton.setConfig (cfg);
    }
    keyButton.onLinkFollow = [this] { setKeyParam ("followKey", 1.0f); };
    keyButton.onManualKey  = [this] (int root, int scaleIdx)
    {
        setKeyParam ("followKey", 0.0f);
        setKeyParam ("root",  (float) root);
        setKeyParam ("scale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    addKnob ("harmonyMix",   "MIX",     {},    harmonyMixK);
    addKnob ("alignAmount",  "ALIGN",   {},    alignAmountK);
    addKnob ("alignSearchMs","SEARCH",  " ms", alignSearchK);
    addKnob ("pitchTighten", "TIGHTEN", {},    pitchTightenK);

    for (int v = 0; v < kNumHarmony; ++v)
    {
        const auto s = juce::String (v + 1);
        auto& hv = hvoices[(size_t) v];
        addToggle (("h" + s + "on").toRawUTF8(), "V" + s, hv.on);
        addChoice (("h" + s + "int").toRawUTF8(), {},
                   SattariGlassChoirAudioProcessor::intervalChoices(), hv.interval);
        addKnob   (("h" + s + "level").toRawUTF8(), "LEVEL", " dB", hv.level);
    }

    // --- DESTROY --------------------------------------------------------------
    addToggle ("destroyOn", "Destroy", destroyT);
    addKnob ("destroyMix", "MIX",   {}, destroyMixK);
    addKnob ("drive",      "DRIVE", {}, driveK);
    addKnob ("crush",      "CRUSH", {}, crushK);
    addKnob ("vowelAmt",   "VOWEL", {}, vowelK);
    addKnob ("ringAmt",    "RING",  {}, ringK);

    // --- REVERB + DUCK --------------------------------------------------------
    addKnob ("reverbMix",     "MIX",     {},    reverbMixK);
    addKnob ("reverbSize",    "SIZE",    {},    reverbSizeK);
    addKnob ("reverbDamp",    "DAMP",    {},    reverbDampK);
    addKnob ("duckAmount",    "DUCK",    {},    duckAmountK);
    addKnob ("duckAttackMs",  "ATTACK",  " ms", duckAttackK);
    addKnob ("duckReleaseMs", "RELEASE", " ms", duckReleaseK);

    // --- WIDTH ----------------------------------------------------------------
    addToggle ("lowMono",      "Low Mono",      lowMonoT);
    addToggle ("monoAudition", "Mono Audition", monoAuditionT);
    addKnob ("width",       "WIDTH",   {},    widthK);
    addKnob ("lowMonoFreq", "LM FREQ", " Hz", lowMonoFreqK);

    // --- TRAILS ---------------------------------------------------------------
    addToggle ("trailsOn", "Trails", trailsT);
    addKnob ("trailsMix",    "MIX",     {},    trailsMixK);
    addKnob ("trailReverse", "REVERSE", {},    trailReverseK);
    addKnob ("trailSizeMs",  "SIZE",    " ms", trailSizeK);

    // --- OUTPUT trims ---------------------------------------------------------
    addKnob ("leadLevel",  "LEAD",  " dB", leadLevelK);
    addKnob ("choirLevel", "CHOIR", " dB", choirLevelK);
    addKnob ("atmosLevel", "ATMOS", " dB", atmosLevelK);

    // --- Live meters ----------------------------------------------------------
    for (auto* m : { &leadMeter, &choirMeter, &atmosMeter })
    {
        m->setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
        m->setColour (juce::Label::textColourId, juce::Colour (kTextMuted));
        m->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*m);
    }

    setSize (960, 720);
    setResizable (false, false);
    startTimerHz (24);
}

SattariGlassChoirAudioProcessorEditor::~SattariGlassChoirAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// =============================================================================
//  Control builders  (overloads that fill an owned member so the ctor is flat)
// =============================================================================
void SattariGlassChoirAudioProcessorEditor::addKnob (const char* paramId, const juce::String& caption,
                                                     const juce::String& suffix, Knob& k)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
    k.slider.setTextValueSuffix (suffix);
    addAndMakeVisible (k.slider);
    if (caption.isNotEmpty())
    {
        styleCaption (k.caption, caption);
        addAndMakeVisible (k.caption);
    }
    k.att = std::make_unique<SliderAttachment> (processor.parameters(), paramId, k.slider);
}

void SattariGlassChoirAudioProcessorEditor::addToggle (const char* paramId, const juce::String& text, Toggle& t)
{
    t.button.setButtonText (text);
    addAndMakeVisible (t.button);
    t.att = std::make_unique<ButtonAttachment> (processor.parameters(), paramId, t.button);
}

void SattariGlassChoirAudioProcessorEditor::addChoice (const char* paramId, const juce::String& caption,
                                                       const juce::StringArray& items, Choice& c)
{
    c.box.addItemList (items, 1);           // populate BEFORE attaching (no blank dropdown)
    addAndMakeVisible (c.box);
    if (caption.isNotEmpty())
    {
        styleCaption (c.caption, caption);
        addAndMakeVisible (c.caption);
    }
    c.att = std::make_unique<ComboAttachment> (processor.parameters(), paramId, c.box);
}

void SattariGlassChoirAudioProcessorEditor::layoutKnobRow (juce::Rectangle<int> row, std::initializer_list<Knob*> ks)
{
    const int n = (int) ks.size();
    if (n == 0) return;
    const int w = row.getWidth() / n;
    int i = 0;
    for (auto* k : ks)
    {
        auto cell = row.removeFromLeft (i == n - 1 ? row.getWidth() : w);
        ++i;
        k->caption.setBounds (cell.removeFromTop (13));
        k->slider.setBounds (cell.reduced (3, 0));
    }
}

void SattariGlassChoirAudioProcessorEditor::setKeyParam (const char* id, float value)
{
    if (auto* prm = processor.parameters().getParameter (id))
    {
        const float norm = prm->convertTo0to1 (value);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (norm);
        prm->endChangeGesture();
    }
}

// =============================================================================
//  Timer:  UI-only reads of the processor viz atomics
// =============================================================================
void SattariGlassChoirAudioProcessorEditor::timerCallback()
{
    keyButton.setState (processor.isFollowingBusKey() ? sattari::ui::KeyButton::Mode::Follow
                                                      : sattari::ui::KeyButton::Mode::Manual,
                        processor.effectiveRoot(), processor.effectiveScale());

    auto fmt = [] (const char* tag, float db)
    {
        return juce::String (tag) + "  "
             + (db <= -99.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92inf"))
                             : juce::String (db, 1))
             + " dB";
    };
    leadMeter.setText  (fmt ("LEAD",  processor.leadLevelDb()),  juce::dontSendNotification);
    choirMeter.setText (fmt ("CHOIR", processor.choirLevelDb()), juce::dontSendNotification);
    atmosMeter.setText (fmt ("ATMOS", processor.atmosLevelDb()), juce::dontSendNotification);

    repaint();   // refresh the character-driven hero halo + meters
}

// =============================================================================
//  Paint
// =============================================================================
void SattariGlassChoirAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    for (const auto& s : sections)
        drawSection (g, s.bounds, s.name);

    // A soft accent halo behind the hero CHARACTER knob, brightening with the
    // live macro amount (UI-only read; no DSP coupling).
    if (characterK.slider.getWidth() > 0)
    {
        const float amt   = juce::jlimit (0.0f, 1.0f, processor.characterNow());
        const auto  ring  = characterK.slider.getBounds().toFloat().reduced (6.0f);
        const auto  accent = lookAndFeel.getAccent();
        g.setColour (accent.withAlpha (0.05f + 0.14f * amt));
        g.fillEllipse (ring.expanded (10.0f + 14.0f * amt));
    }
}

// =============================================================================
//  Layout
// =============================================================================
void SattariGlassChoirAudioProcessorEditor::resized()
{
    sections.clear();
    auto area = getLocalBounds().reduced (18);

    // --- Header row: title/subtitle | bypass | IN/OUT ------------------------
    auto header = area.removeFromTop (52);
    {
        auto titleCol = header.removeFromLeft (460);
        title.setBounds    (titleCol.removeFromTop (30));
        subtitle.setBounds (titleCol.removeFromTop (20));

        auto io = header.removeFromRight (150);
        const int kw = io.getWidth() / 2;
        auto packIO = [] (Knob& k, juce::Rectangle<int> cell)
        {
            k.caption.setBounds (cell.removeFromTop (12));
            k.slider.setBounds (cell);
        };
        packIO (inputK,  io.removeFromLeft (kw));
        packIO (outputK, io);

        bypassT.button.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, 24));
    }
    area.removeFromTop (12);

    // --- Hero band: big CHARACTER knob (left) + MODE (right) -----------------
    auto hero = area.removeFromTop (150);
    {
        auto left = hero.removeFromLeft (300);
        characterK.caption.setBounds (left.removeFromTop (16));
        characterAnchors.setBounds (left.removeFromBottom (14));
        characterK.slider.setBounds (left.reduced (40, 2));

        hero.removeFromLeft (18);
        auto modeCol = hero.removeFromLeft (200).withSizeKeepingCentre (200, 60).withY (hero.getY() + 30);
        modeC.caption.setBounds (modeCol.removeFromTop (14));
        modeC.box.setBounds (modeCol.removeFromTop (30));

        // The three live bus meters sit to the right of MODE.
        hero.removeFromLeft (18);
        auto meters = hero.withSizeKeepingCentre (hero.getWidth(), 70).withY (hero.getY() + 40);
        leadMeter.setBounds  (meters.removeFromTop (22));
        choirMeter.setBounds (meters.removeFromTop (22));
        atmosMeter.setBounds (meters.removeFromTop (22));
    }
    area.removeFromTop (14);

    // --- Section grid --------------------------------------------------------
    // Two rows of panels. Top row: ENSEMBLE (wide) | DESTROY.
    // Middle row: HARMONY (wide, incl. KeyButton + 4 voice strips).
    // Bottom row: REVERB+DUCK | WIDTH | TRAILS | OUTPUT.
    const int gap = 12;

    auto addSection = [this] (juce::Rectangle<int> r, const juce::String& name) -> juce::Rectangle<int>
    {
        Section s; s.name = name; s.bounds = r;
        s.body = r.reduced (12).withTrimmedTop (18);
        sections.push_back (s);
        return s.body;
    };

    // Row 1: ENSEMBLE (2/3) + DESTROY (1/3).
    auto row1 = area.removeFromTop (168);
    {
        const int destroyW = 300;
        auto destroyR  = row1.removeFromRight (destroyW);
        row1.removeFromRight (gap);
        auto ensembleR = row1;

        // ENSEMBLE
        auto eb = addSection (ensembleR, "ENSEMBLE");
        auto topStrip = eb.removeFromTop (46);
        {
            auto vc = topStrip.removeFromLeft (topStrip.getWidth() / 2).reduced (4, 4);
            voiceCountC.caption.setBounds (vc.removeFromTop (12));
            voiceCountC.box.setBounds (vc.removeFromTop (26));
            auto en = topStrip.reduced (4, 4);
            engineC.caption.setBounds (en.removeFromTop (12));
            engineC.box.setBounds (en.removeFromTop (26));
        }
        eb.removeFromTop (6);
        layoutKnobRow (eb.removeFromTop (94),
                       { &detuneK, &timingK, &humanizeK, &formantK, &ensembleLevelK });

        // DESTROY
        auto db = addSection (destroyR, "DESTROY");
        destroyT.button.setBounds (db.removeFromTop (22).removeFromLeft (150));
        db.removeFromTop (6);
        // 5 knobs, 3 + 2.
        layoutKnobRow (db.removeFromTop (54), { &destroyMixK, &driveK, &crushK });
        db.removeFromTop (6);
        layoutKnobRow (db.removeFromTop (54), { &vowelK, &ringK });
    }
    area.removeFromTop (gap);

    // Row 2: HARMONY (full width).
    auto row2 = area.removeFromTop (196);
    {
        auto hb = addSection (row2, "HARMONY  -  KEY-AWARE DIATONIC VOICES");

        // Top: toggles + KeyButton + the 4 macro knobs.
        auto htop = hb.removeFromTop (58);
        {
            auto toggles = htop.removeFromLeft (120);
            harmonyT.button.setBounds (toggles.removeFromTop (24));
            toggles.removeFromTop (4);
            alignT.button.setBounds (toggles.removeFromTop (24));

            htop.removeFromLeft (10);
            keyButton.setBounds (htop.removeFromLeft (280).withSizeKeepingCentre (280, 54));

            htop.removeFromLeft (12);
            layoutKnobRow (htop, { &harmonyMixK, &alignAmountK, &alignSearchK, &pitchTightenK });
        }

        hb.removeFromTop (8);

        // Bottom: 4 voice strips (on | interval | level).
        auto strips = hb;
        const int colW = strips.getWidth() / kNumHarmony;
        for (int v = 0; v < kNumHarmony; ++v)
        {
            auto col = strips.removeFromLeft (v == kNumHarmony - 1 ? strips.getWidth() : colW)
                             .reduced (6, 0);
            auto onRow = col.removeFromTop (22);
            hvoices[(size_t) v].on.button.setBounds (onRow.removeFromLeft (60));
            col.removeFromTop (4);
            hvoices[(size_t) v].interval.box.setBounds (col.removeFromTop (26));
            col.removeFromTop (6);
            hvoices[(size_t) v].level.caption.setBounds (col.removeFromTop (12));
            hvoices[(size_t) v].level.slider.setBounds (col.removeFromTop (56).reduced (14, 0));
        }
    }
    area.removeFromTop (gap);

    // Row 3: REVERB+DUCK | WIDTH | TRAILS | OUTPUT.
    auto row3 = area;
    {
        const int outW    = 210;
        const int trailsW = 210;
        const int widthW  = 190;

        auto outputR  = row3.removeFromRight (outW);   row3.removeFromRight (gap);
        auto trailsR  = row3.removeFromRight (trailsW); row3.removeFromRight (gap);
        auto widthR   = row3.removeFromRight (widthW);  row3.removeFromRight (gap);
        auto reverbR  = row3;

        // REVERB + DUCK (6 knobs, 3 + 3)
        auto rb = addSection (reverbR, "REVERB  +  DUCK");
        layoutKnobRow (rb.removeFromTop (rb.getHeight() / 2 - 4),
                       { &reverbMixK, &reverbSizeK, &reverbDampK });
        rb.removeFromTop (6);
        layoutKnobRow (rb, { &duckAmountK, &duckAttackK, &duckReleaseK });

        // WIDTH
        auto wb = addSection (widthR, "WIDTH");
        auto wtog = wb.removeFromBottom (48);
        lowMonoT.button.setBounds      (wtog.removeFromTop (22));
        monoAuditionT.button.setBounds (wtog.removeFromTop (22));
        layoutKnobRow (wb, { &widthK, &lowMonoFreqK });

        // TRAILS
        auto tb = addSection (trailsR, "TRAILS");
        trailsT.button.setBounds (tb.removeFromTop (22).removeFromLeft (100));
        tb.removeFromTop (6);
        layoutKnobRow (tb, { &trailsMixK, &trailReverseK, &trailSizeK });

        // OUTPUT trims
        auto ob = addSection (outputR, "OUTPUT");
        layoutKnobRow (ob, { &leadLevelK, &choirLevelK, &atmosLevelK });
    }
}
