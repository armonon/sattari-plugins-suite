#include "PluginEditor.h"

using namespace sattari::automix;

AutoMixPanel::AutoMixPanel (SattariMixAudioProcessor& p) : processor (p), controller (p.mixController())
{
    controller.startViewing();
    for (juce::Component* c : std::initializer_list<juce::Component*> {
        &header, &group, &name, &newGroup, &join, &leave, &analyze, &propose, &stop,
        &original, &proposed, &keep, &undo, &match, &protect, &enabled, &duration,
        &role, &gain, &low, &presence, &compression, &amount, &localGain, &status, &reason, &table,
        &busInsert, &showHistory, &routingWarning })
        addAndMakeVisible (c);
    const char* texts[] = { "Mix group", "This track", "Capture", "No track selected", "Role",
        "Gain / dB", "Low EQ / dB", "Presence EQ / dB", "Dynamics / %", "Local amount", "Local gain / dB" };
    for (size_t i = 0; i < labels.size(); ++i)
    {
        labels[i].setText (texts[i], juce::dontSendNotification);
        labels[i].setFont (sattari::brand::fontOptions (12.0f));
        labels[i].setColour (juce::Label::textColourId, sattari::releaseui::muted);
        addAndMakeVisible (labels[i]);
    }
    labels[3].setColour (juce::Label::textColourId, sattari::releaseui::text);
    labels[3].setFont (sattari::brand::fontOptions (19.0f, juce::Font::bold));
    status.setFont (sattari::brand::fontOptions (11.0f)); reason.setFont (sattari::brand::fontOptions (11.0f));
    status.setColour (juce::Label::textColourId, sattari::releaseui::muted);
    reason.setColour (juce::Label::textColourId, sattari::releaseui::muted);
    reason.setJustificationType (juce::Justification::topLeft);
    addChildComponent (historyView); historyView.setMultiLine (true); historyView.setReadOnly (true);
    historyView.setScrollbarsShown (true); historyView.setTitle ("Decision history");
    showHistory.setTitle ("Decision history");
    showHistory.onClick = [this] { historyView.setVisible (showHistory.getToggleState()); table.setVisible (! showHistory.getToggleState()); };
    routingWarning.setFont (sattari::brand::fontOptions (11)); routingWarning.setColour (juce::Label::textColourId, juce::Colour (0xffba763a));
    busInsert.setTitle ("Summing bus insert"); busInsert.setTooltip ("Declare this insert receives a summed signal. This does not detect or change DAW routing.");
    busAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters(), "auto.isBus", busInsert);
    sattari::releaseui::primary (analyze); sattari::releaseui::primary (keep);
    sattari::releaseui::icon (undo, sattari::releaseui::Icon::undo, "Undo mix");
    group.setText (p.groupName()); name.setText (p.trackName());
    displayedGroup = p.groupName(); displayedName = p.trackName();
    group.setInputRestrictions (80); name.setInputRestrictions (80);
    group.setTitle ("Mix group code"); name.setTitle ("Track name");
    group.setTooltip ("Use the same unique group code on participating track inserts. Use a different code for each DAW project.");
    newGroup.setTooltip ("Create a unique group and copy its code to the clipboard.");
    join.setTooltip ("Join this group and save this track name.");
    leave.setTooltip ("Disconnect this track. Kept processing remains active.");
    newGroup.onClick = [this]
    {
        group.setText (juce::Uuid().toString().substring (0, 13));
        juce::SystemClipboard::copyTextToClipboard (group.getText());
        processor.setIdentity (group.getText(), name.getText()); controller.clear();
    };
    join.onClick = [this] { processor.setIdentity (group.getText(), name.getText()); controller.clear(); };
    leave.onClick = [this] { processor.setIdentity ({}, name.getText()); group.clear(); controller.clear(); };
    name.onReturnKey = [this] { if (! controller.busy() && ! controller.auditioning()) processor.setIdentity (processor.groupName(), name.getText()); };
    group.onReturnKey = [this] { if (join.isEnabled()) join.triggerClick(); };
    duration.addItem ("8 seconds", 8); duration.addItem ("16 seconds", 16); duration.addItem ("30 seconds", 30);
    duration.setSelectedId (16);
    analyze.setTooltip ("Capture a playing passage on every linked track, then propose a balance.");
    analyze.onClick = [this] { controller.analyze (duration.getSelectedId()); };
    propose.onClick = [this] { controller.buildProposal(); };
    stop.onClick = [this] { controller.cancelAnalysis(); };
    proposed.onClick = [this] { controller.audition (true); };
    original.onClick = [this] { controller.audition (false); };
    keep.onClick = [this] { controller.keep(); };
    undo.onClick = [this] { controller.undo(); };
    match.setToggleState (controller.levelMatch, juce::dontSendNotification);
    match.setTooltip ("Estimated active-level comparison using attenuation only, not a rendered whole-mix loudness measurement.");
    match.onClick = [this]
    {
        controller.levelMatch = match.getToggleState();
        if (controller.auditioning()) controller.audition (controller.mode == MixController::Mode::proposed);
    };
    original.setTooltip ("Audition the settings saved before this proposal.");
    proposed.setTooltip ("Audition the editable proposal across the group.");
    keep.setTooltip ("Keep proposed settings and remove comparison attenuation.");
    undo.setTooltip ("Restore the settings saved before this proposal.");
    role.addItemList (roleNames(), 1);
    role.onChange = [this]
    { if (! refreshing) if (const auto* t = selectedTrack()) controller.configure (*t, role.getSelectedItemIndex(), protect.getToggleState()); };
    protect.onClick = [this]
    { if (const auto* t = selectedTrack()) controller.configure (*t, role.getSelectedItemIndex(), protect.getToggleState()); };
    protect.setTooltip ("Preserve this track's processing in group proposals.");
    const auto setup = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& title)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 24);
        s.setRange (lo, hi, step); s.setTitle (title); s.setTooltip (title); s.setScrollWheelEnabled (false);
        s.onValueChange = [this] { if (! refreshing) editProposal(); };
    };
    setup (gain, -36, 12, 0.1, "Proposed gain"); setup (low, -6, 0, 0.1, "Proposed low EQ at 120 Hz");
    setup (presence, -6, 0, 0.1, "Proposed presence EQ at 2.2 kHz"); setup (compression, 0, 100, 1, "Proposed compression strength");
    setup (amount, 0, 1, 0.01, "This track processing amount"); setup (localGain, -36, 12, 0.1, "This track saved gain");
    amount.onValueChange = nullptr; localGain.onValueChange = nullptr;
    enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.parameters(), "auto.enabled", enabled);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), "auto.amount", amount);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), "auto.gain", localGain);
    amount.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    amount.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    amount.updateText();
    localGain.textFromValueFunction = [] (double v) { return juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1); };
    localGain.updateText();
    const char* headings[] = { "Track", "Role", "Active", "Level*", "Peak", "Gain", "Low EQ", "Presence", "Decision" };
    const int widths[] = { 160, 110, 60, 75, 70, 70, 70, 80, 225 };
    for (int i = 0; i < 9; ++i)
        table.getHeader().addColumn (headings[i], i + 1, widths[i], 40, 600,
            juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable);
    table.getHeader().setColumnVisible (3, false);
    table.getHeader().setColumnVisible (7, false);
    table.getHeader().setColumnVisible (8, false);
    table.setRowHeight (44); table.setHeaderHeight (34); table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, sattari::releaseui::canvas);
    table.setColour (juce::ListBox::outlineColourId, sattari::releaseui::line); table.setOutlineThickness (0);
    table.setTooltip ("Level is active K-weighted dB, not integrated LUFS. Peak is sample peak. Gain and EQ show the proposal when one exists.");
    startTimerHz (5); timerCallback();
}

AutoMixPanel::~AutoMixPanel() { stopTimer(); controller.stopViewing(); table.setModel (nullptr); }
int AutoMixPanel::getNumRows() { return (int) controller.tracks.size(); }
void AutoMixPanel::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    g.fillAll (selected ? accent.withAlpha (0.10f) : row % 2 == 0 ? sattari::releaseui::canvas : sattari::releaseui::surface);
    g.setColour (sattari::releaseui::line.withAlpha (0.5f)); g.drawHorizontalLine (h - 1, 0, (float) w);
    if (selected) { g.setColour (accent); g.fillRect (0, 0, 2, h); }
}
void AutoMixPanel::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) controller.tracks.size())) return;
    const auto& t = controller.tracks[(size_t) row];
    const Suggestion* suggestion = nullptr;
    for (const auto& s : controller.plan.tracks) if (s.source.id == t.id) suggestion = &s;
    const auto settings = suggestion != nullptr && controller.mode != MixController::Mode::kept ? suggestion->proposed : t.settings;
    const auto db = [] (float v) { return juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1); };
    juce::String text;
    switch (col)
    {
        case 1: text = t.name + (t.id == processor.instanceId() ? " (this)" : ""); break;
        case 2: text = roleNames()[(int) t.role]; break;
        case 3: text = juce::String (t.measured.activeSeconds, 1) + " s"; break;
        case 4: text = t.measured.activeSeconds > 0 ? db (t.measured.loudness) : "--"; break;
        case 5: text = t.measured.progress > 0 ? db (t.measured.peakDb) : "--"; break;
        case 6: text = db (settings.gain); break;
        case 7: text = db (settings.low); break;
        case 8: text = db (settings.presence); break;
        case 9: text = t.error.isNotEmpty() ? t.error : suggestion != nullptr ? suggestion->reason
            : t.measured.invalid ? "Analyze again" : t.locked ? "Locked" : t.measured.complete ? "Analyzed" : t.measured.progress > 0 ? "Listening" : "Awaiting analysis"; break;
        default: break;
    }
    g.setFont (sattari::brand::fontOptions (12.0f));
    g.setColour (col >= 6 && col <= 8 && suggestion != nullptr ? accent : sattari::releaseui::text);
    g.drawText (text, 10, 0, w - 20, h, col >= 3 && col <= 8 ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
}
juce::String AutoMixPanel::getCellTooltip (int row, int)
{
    if (! juce::isPositiveAndBelow (row, (int) controller.tracks.size())) return {};
    const auto& t = controller.tracks[(size_t) row];
    return t.name + " / " + roleNames()[(int) t.role] + " / " + juce::String (t.measured.activeSeconds, 1)
        + " s active" + (t.locked ? " / Locked" : "") + (t.error.isNotEmpty() ? " / " + t.error : "");
}
const Track* AutoMixPanel::selectedTrack() const
{ for (const auto& t : controller.tracks) if (t.id == selectedId) return &t; return nullptr; }
Suggestion* AutoMixPanel::selectedSuggestion()
{ for (auto& s : controller.plan.tracks) if (s.source.id == selectedId) return &s; return nullptr; }
void AutoMixPanel::selectedRowsChanged (int row)
{
    if (juce::isPositiveAndBelow (row, (int) controller.tracks.size())) selectedId = controller.tracks[(size_t) row].id;
    refreshSelection();
}
void AutoMixPanel::refreshSelection()
{
    refreshing = true;
    const auto* t = selectedTrack(); const auto* s = selectedSuggestion();
    labels[3].setText (t != nullptr ? t->name : "No track selected", juce::dontSendNotification);
    if (t != nullptr) { role.setSelectedItemIndex ((int) t->role, juce::dontSendNotification); protect.setToggleState (t->locked, juce::dontSendNotification); }
    const auto values = s != nullptr && controller.mode != MixController::Mode::kept ? s->proposed : t != nullptr ? t->settings : Settings();
    for (auto e : std::initializer_list<std::pair<juce::Slider*, double>> {
        { &gain, values.gain }, { &low, values.low }, { &presence, values.presence }, { &compression, values.compression * 100 } })
        if (! e.first->isMouseButtonDown()) e.first->setValue (e.second, juce::dontSendNotification);
    reason.setText (t != nullptr ? (s != nullptr ? s->reason + " / " : juce::String())
        + juce::String (t->measured.activeSeconds, 1) + " s active" : juce::String(), juce::dontSendNotification);
    reason.setTooltip (reason.getText());
    const bool editing = s != nullptr && controller.plan.valid() && ! s->source.locked && controller.mode == MixController::Mode::draft && ! controller.busy();
    gain.setEnabled (editing); low.setEnabled (editing); presence.setEnabled (editing); compression.setEnabled (editing);
    role.setEnabled (t != nullptr && ! controller.busy() && ! controller.auditioning()); protect.setEnabled (role.isEnabled());
    refreshing = false;
}
void AutoMixPanel::editProposal()
{
    if (auto* s = selectedSuggestion())
    {
        s->proposed.gain = (float) gain.getValue(); s->proposed.low = (float) low.getValue();
        s->proposed.presence = (float) presence.getValue(); s->proposed.compression = (float) compression.getValue() / 100;
        s->reason = "User adjusted";
        double peak = 0;
        for (const auto& e : controller.plan.tracks) peak += amplitude (e.source.measured.peakDb + effectiveGain (e.proposed));
        controller.plan.estimatedPeakDb = decibels (peak); table.repaint();
    }
}
void AutoMixPanel::timerCallback()
{
    if (displayedGroup != processor.groupName()) { displayedGroup = processor.groupName(); group.setText (displayedGroup, false); }
    if (displayedName != processor.trackName()) { displayedName = processor.trackName(); name.setText (displayedName, false); }
    const bool busy = controller.busy(), audition = controller.auditioning(), analyzing = controller.analyzing();
    newGroup.setEnabled (! busy && ! audition && ! analyzing); join.setEnabled (newGroup.isEnabled()); leave.setEnabled (newGroup.isEnabled());
    group.setEnabled (join.isEnabled()); name.setEnabled (join.isEnabled());
    analyze.setEnabled (! busy && ! audition && ! analyzing && controller.tracks.size() >= 2);
    propose.setEnabled (! busy && ! audition && ! analyzing && controller.tracks.size() >= 2);
    stop.setEnabled (analyzing); duration.setEnabled (analyze.isEnabled());
    const bool ready = controller.plan.valid() && ! busy && ! analyzing;
    original.setEnabled (ready); proposed.setEnabled (ready); keep.setEnabled (ready); undo.setEnabled (ready); match.setEnabled (ready);
    amount.setEnabled (! busy && ! audition); localGain.setEnabled (amount.isEnabled()); enabled.setEnabled (amount.isEnabled());
    busInsert.setEnabled (! busy && ! audition && ! analyzing);
    const auto history = processor.decisionHistory();
    const auto historyText = history.isEmpty() ? juce::String ("No recorded decisions") : history;
    if (historyView.getText() != historyText) historyView.setText (historyText, false);
    juce::String warning = controller.plan.warning;
    if (warning.isEmpty())
        for (const auto& track : controller.tracks) if (track.isBus && controller.tracks.size() > 1)
        { warning = "Summing bus linked: verify one contribution per signal. DAW routing is not visible."; break; }
    routingWarning.setText (warning, juce::dontSendNotification); routingWarning.setTooltip (warning);
    original.setToggleState (controller.mode == MixController::Mode::original, juce::dontSendNotification);
    proposed.setToggleState (controller.mode == MixController::Mode::proposed, juce::dontSendNotification);
    auto text = controller.status;
    if (controller.plan.valid()) text += "  |  Estimated sum peak " + juce::String (controller.plan.estimatedPeakDb, 1) + " dBFS";
    status.setText (text, juce::dontSendNotification); status.setTooltip (text);
    header.setStatus (juce::String ((int) controller.tracks.size()) + " tracks", ! processor.groupName().isEmpty());
    table.updateContent();
    int selected = -1;
    for (int i = 0; i < (int) controller.tracks.size(); ++i) if (controller.tracks[(size_t) i].id == selectedId) selected = i;
    if (selected < 0 && ! controller.tracks.empty()) selected = 0;
    if (selected >= 0 && table.getSelectedRow() != selected) table.selectRow (selected);
    refreshSelection(); table.repaint();
}
void AutoMixPanel::paint (juce::Graphics& g)
{
    g.fillAll (sattari::releaseui::canvas);
    g.setColour (sattari::releaseui::surface); g.fillRect (detailArea); g.fillRect (footerArea);
    g.setColour (sattari::releaseui::line);
    g.drawHorizontalLine (footerArea.getY(), 0, (float) getWidth());
    g.drawHorizontalLine (132, 0, (float) getWidth());
    g.drawVerticalLine (detailArea.getX(), (float) detailArea.getY(), (float) detailArea.getBottom());
}
void AutoMixPanel::paintOverChildren (juce::Graphics& g)
{
    if (controller.tracks.empty() && ! showHistory.getToggleState())
    {
        g.setColour (sattari::releaseui::muted); g.setFont (sattari::brand::fontOptions (16.0f));
        g.drawText ("No linked tracks", table.getBounds().withTrimmedTop (40), juce::Justification::centred);
    }
}
void AutoMixPanel::resized()
{
    auto a = getLocalBounds(); header.setBounds (a.removeFromTop (72));
    auto ids = a.removeFromTop (60).reduced (22, 5);
    auto n = ids.removeFromLeft (juce::jlimit (200, 290, getWidth() / 4));
    labels[1].setBounds (n.removeFromTop (18)); name.setBounds (n); ids.removeFromLeft (16);
    auto commands = ids.removeFromRight (222).withTrimmedTop (18);
    newGroup.setBounds (commands.removeFromLeft (86)); commands.removeFromLeft (6);
    join.setBounds (commands.removeFromLeft (62)); commands.removeFromLeft (6); leave.setBounds (commands);
    ids.removeFromRight (14); labels[0].setBounds (ids.removeFromTop (18)); group.setBounds (ids);
    commands = a.removeFromTop (50).reduced (22, 9);
    labels[2].setBounds (commands.removeFromLeft (60)); duration.setBounds (commands.removeFromLeft (116)); commands.removeFromLeft (12);
    analyze.setBounds (commands.removeFromLeft (128)); commands.removeFromLeft (6);
    stop.setBounds (commands.removeFromLeft (58)); commands.removeFromLeft (6); propose.setBounds (commands.removeFromLeft (130));
    commands.removeFromLeft (12); showHistory.setBounds (commands.removeFromLeft (100));
    routingWarning.setBounds (a.removeFromTop (24).reduced (22, 0));
    footerArea = a.removeFromBottom (116);
    auto footer = footerArea.reduced (22, 8); auto audition = footer.removeFromTop (32);
    original.setBounds (audition.removeFromLeft (94)); audition.removeFromLeft (4);
    proposed.setBounds (audition.removeFromLeft (94)); audition.removeFromLeft (18);
    match.setBounds (audition.removeFromLeft (130));
    undo.setBounds (audition.removeFromRight (34)); audition.removeFromRight (8); keep.setBounds (audition.removeFromRight (110));
    status.setBounds (footer.removeFromBottom (20));
    auto local = footer.reduced (0, 2);
    enabled.setBounds (local.removeFromLeft (120).withTrimmedTop (12));
    auto r = local.removeFromLeft (260); labels[9].setBounds (r.removeFromTop (17)); amount.setBounds (r);
    local.removeFromLeft (24); r = local.removeFromLeft (270); labels[10].setBounds (r.removeFromTop (17)); localGain.setBounds (r);
    local.removeFromLeft (12); busInsert.setBounds (local.withTrimmedTop (12));

    detailArea = a.removeFromRight (juce::jlimit (260, 320, getWidth() / 4));
    auto detail = detailArea.reduced (18, 12);
    labels[3].setBounds (detail.removeFromTop (27)); reason.setBounds (detail.removeFromTop (38));
    labels[4].setBounds (detail.removeFromTop (18)); role.setBounds (detail.removeFromTop (32));
    detail.removeFromTop (5); protect.setBounds (detail.removeFromTop (26)); detail.removeFromTop (10);
    juce::Slider* sliders[] = { &gain, &low, &presence, &compression };
    for (int i = 0; i < 4; ++i)
    { r = detail.removeFromTop (54); labels[(size_t) i + 5].setBounds (r.removeFromTop (18)); sliders[i]->setBounds (r.removeFromTop (30)); }
    table.setBounds (a.reduced (22, 8));
    historyView.setBounds (table.getBounds());
    const int trackWidth = juce::jlimit (130, 280, table.getWidth() / 4);
    table.getHeader().setColumnWidth (1, trackWidth); table.getHeader().setColumnWidth (2, 102);
    for (int id : { 4, 5, 6 }) table.getHeader().setColumnWidth (id, 64);
    table.getHeader().setColumnWidth (9, juce::jmax (150, table.getWidth() - trackWidth - 102 - 192 - table.getVerticalScrollBar().getWidth()));
    table.resized();
}
SattariMixAudioProcessorEditor::SattariMixAudioProcessorEditor (SattariMixAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), autoMix (p), rack (p, "Sattari Mix", "MIXING WORKFLOW", "mix")
{
    setLookAndFeel (&look); addAndMakeVisible (tabs);
    tabs.setColour (juce::TabbedComponent::backgroundColourId, sattari::releaseui::surface);
    tabs.addTab ("Session balance", sattari::releaseui::canvas, &autoMix, false);
    tabs.addTab ("Signal chain", sattari::releaseui::canvas, &rack, false); tabs.setTabBarDepth (36); tabs.setOutline (0);
    setResizable (true, true); setResizeLimits (1000, 760, 1600, 1100); setSize (1180, 820);
}
SattariMixAudioProcessorEditor::~SattariMixAudioProcessorEditor() { tabs.clearTabs(); setLookAndFeel (nullptr); }
void SattariMixAudioProcessorEditor::resized() { tabs.setBounds (getLocalBounds()); }
