#include "MainPluginEditor.h"
#include "MainPluginEditorInternal.h"

using namespace sdedit;

namespace
{
juce::String deckDropName (int deck)
{
    return "DECK " + juce::String::charToString ((juce_wchar) ('A' + deck));
}
}

void MainPluginEditor::paintOverChildren (juce::Graphics& g)
{
    if (activeDropTarget.kind == DropTarget::Kind::none
        || activeDropTarget.bounds.isEmpty())
        return;

    auto target = activeDropTarget.bounds.toFloat().reduced (2.0f);
    const auto colour = activeDropTarget.colour.isTransparent()
                          ? stemdeck::tokens::c (stemdeck::tokens::selected)
                          : activeDropTarget.colour;
    g.setColour (colour.withAlpha (0.16f));
    g.fillRoundedRectangle (target, 9.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (target.reduced (1.0f), 9.0f, 2.0f);
    auto label = target.toNearestInt().withSizeKeepingCentre (
        juce::jmin (target.toNearestInt().getWidth() - 12, 280), 30);
    g.setColour (stemdeck::tokens::c (stemdeck::tokens::bgWell).withAlpha (0.92f));
    g.fillRoundedRectangle (label.toFloat(), 6.0f);
    g.setColour (colour.brighter (0.25f));
    g.setFont (stemdeck::tokens::technical (10.0f, true));
    g.drawFittedText (activeDropTarget.label, label.reduced (8, 0),
                      juce::Justification::centred, 1);
}

MainPluginEditor::DropTarget MainPluginEditor::findDropTarget (juce::Point<int> point,
                                                               bool pluginPayload) const
{
    DropTarget target;
    target.localPoint = point;

    if (pluginPayload)
    {
        // The per-stem FX grid is the most precise destination. Its visible order
        // is drums, bass, music, vocals, matching HostGraph::stemChain().
        for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
        {
            const auto stemFx = deckStemFxBounds[(size_t) deck];
            if (! stemFx.isEmpty() && stemFx.contains (point))
            {
                const int stem = juce::jlimit (0, 3,
                    (point.x - stemFx.getX()) * 4 / juce::jmax (1, stemFx.getWidth()));
                target.kind = DropTarget::Kind::pluginChain;
                target.chain = (int) HostGraph::stemChain (deck, stem);
                target.bounds = stemFx.withLeft (stemFx.getX() + stem * stemFx.getWidth() / 4)
                                      .withRight (stemFx.getX() + (stem + 1) * stemFx.getWidth() / 4);
                target.label = "ADD PLUGIN TO "
                             + HostGraph::chainName ((HostGraph::Chain) target.chain).toUpperCase();
                target.colour = stemdeck::tokens::c (stemdeck::tokens::effects);
                return target;
            }

            // The fallback separated-stem module is drums/bass/music/vocals + a
            // deck-wide FX cell. A drop on a named stem receives that stem chain.
            const auto stemModule = deckStemBounds[(size_t) deck];
            if (! stemModule.isEmpty() && stemModule.contains (point))
            {
                const int column = juce::jlimit (0, 4,
                    (point.x - stemModule.getX()) * 5 / juce::jmax (1, stemModule.getWidth()));
                target.kind = DropTarget::Kind::pluginChain;
                target.chain = column < 4 ? (int) HostGraph::stemChain (deck, column) : deck;
                target.bounds = stemModule.withLeft (stemModule.getX() + column * stemModule.getWidth() / 5)
                                          .withRight (stemModule.getX() + (column + 1) * stemModule.getWidth() / 5);
                target.label = "ADD PLUGIN TO "
                             + HostGraph::chainName ((HostGraph::Chain) target.chain).toUpperCase();
                target.colour = stemdeck::tokens::c (stemdeck::tokens::effects);
                return target;
            }
        }

        if (! fxRack.getBounds().isEmpty() && fxRack.getBounds().contains (point))
        {
            target.kind = DropTarget::Kind::pluginChain;
            target.chain = fxRack.currentChainIndex();
            target.bounds = fxRack.getBounds();
            target.label = "ADD PLUGIN TO " + fxRack.currentTargetName().toUpperCase();
            target.colour = stemdeck::tokens::c (stemdeck::tokens::effects);
            return target;
        }

        if (! masterOutBounds.isEmpty() && masterOutBounds.contains (point))
        {
            target.kind = DropTarget::Kind::pluginChain;
            target.chain = (int) HostGraph::Chain::master;
            target.bounds = masterOutBounds;
            target.label = "ADD PLUGIN TO MASTER BUS";
            target.colour = stemdeck::tokens::c (stemdeck::tokens::effects);
            return target;
        }

        for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
        {
            auto bounds = deckPanelBounds[(size_t) deck];
            if (bounds.isEmpty()) bounds = deckCollapsedBounds[(size_t) deck];
            if (! bounds.isEmpty() && bounds.contains (point))
            {
                target.kind = DropTarget::Kind::pluginChain;
                target.chain = deck;
                target.bounds = bounds;
                target.label = "ADD PLUGIN TO " + deckDropName (deck);
                target.colour = stemdeck::tokens::c (stemdeck::tokens::effects);
                return target;
            }
        }
        return target;
    }

    // Pads win over their enclosing performance footer.
    for (int pad = 0; pad < PadBank::numPads; ++pad)
    {
        auto bounds = pads[(size_t) pad].trigger.getBounds();
        if (bounds.isEmpty()) bounds = pads[(size_t) pad].load.getBounds();
        else if (! pads[(size_t) pad].load.getBounds().isEmpty())
            bounds = bounds.getUnion (pads[(size_t) pad].load.getBounds());
        if (! bounds.isEmpty() && bounds.contains (point))
        {
            target.kind = DropTarget::Kind::pad;
            target.index = pad;
            target.bounds = bounds.expanded (3);
            target.label = "LOAD SAMPLE INTO PAD " + juce::String (pad + 1);
            target.colour = juce::Colour (padSpectrum (pad));
            return target;
        }
    }

    if (arrangementView.isVisible() && arrangementView.getBounds().contains (point))
    {
        target.kind = DropTarget::Kind::arrangement;
        target.bounds = arrangementView.getBounds();
        target.localPoint = arrangementView.getLocalPoint (this, point);
        target.label = "PLACE AUDIO IN REPLAY";
        target.colour = stemdeck::tokens::c (stemdeck::tokens::selected);
        return target;
    }

    for (int deck = 0; deck < MainPluginProcessor::numDecks; ++deck)
    {
        auto bounds = deckPanelBounds[(size_t) deck];
        if (bounds.isEmpty()) bounds = deckCollapsedBounds[(size_t) deck];
        if (! bounds.isEmpty() && bounds.contains (point))
        {
            target.kind = DropTarget::Kind::deck;
            target.index = deck;
            target.bounds = bounds;
            target.label = "LOAD AUDIO INTO " + deckDropName (deck);
            target.colour = deckAccent (deck);
            return target;
        }
    }

    if (browserVisible && ! browserPanelBounds.isEmpty() && browserPanelBounds.contains (point))
    {
        target.kind = DropTarget::Kind::explorer;
        target.bounds = browserPanelBounds;
        target.label = "ADD TO EXPLORE";
        target.colour = stemdeck::tokens::c (stemdeck::tokens::deckA);
        return target;
    }
    if (libraryVisible && libraryView != nullptr && libraryView->getBounds().contains (point))
    {
        target.kind = DropTarget::Kind::explorer;
        target.bounds = libraryView->getBounds();
        target.label = "ADD TO MUSIC LIBRARY";
        target.colour = stemdeck::tokens::c (stemdeck::tokens::deckA);
    }
    return target;
}

void MainPluginEditor::updateDropTarget (juce::Point<int> point, bool pluginPayload)
{
    const auto previous = activeDropTarget.bounds;
    activeDropTarget = findDropTarget (point, pluginPayload);
    if (! previous.isEmpty()) repaint (previous.expanded (5));
    if (! activeDropTarget.bounds.isEmpty()) repaint (activeDropTarget.bounds.expanded (5));
}

void MainPluginEditor::clearDropTarget()
{
    const auto previous = activeDropTarget.bounds;
    activeDropTarget = {};
    if (! previous.isEmpty()) repaint (previous.expanded (5));
}

bool MainPluginEditor::isInterestedInDragSource (const DragDetails& details)
{
    const auto payloadKind = stemdeck::drag::kind (details.description);
    if (payloadKind == stemdeck::drag::audioKind
        || payloadKind == stemdeck::drag::pluginKind)
        return true;
    return payloadKind == stemdeck::drag::fileBrowserKind
        && fileBrowser != nullptr && fileBrowser->getNumSelectedFiles() > 0;
}

void MainPluginEditor::itemDragEnter (const DragDetails& details)
{
    updateDropTarget (details.localPosition,
                      stemdeck::drag::kind (details.description) == stemdeck::drag::pluginKind);
}

void MainPluginEditor::itemDragMove (const DragDetails& details)
{
    updateDropTarget (details.localPosition,
                      stemdeck::drag::kind (details.description) == stemdeck::drag::pluginKind);
}

void MainPluginEditor::itemDragExit (const DragDetails&)
{
    clearDropTarget();
}

void MainPluginEditor::itemDropped (const DragDetails& details)
{
    const auto payloadKind = stemdeck::drag::kind (details.description);
    const bool pluginPayload = payloadKind == stemdeck::drag::pluginKind;
    const auto target = findDropTarget (details.localPosition, pluginPayload);

    if (pluginPayload && target.kind == DropTarget::Kind::pluginChain)
    {
        const int plugin = stemdeck::drag::pluginIndex (details.description);
        loadHostPluginToChain (plugin, target.chain,
                               HostGraph::chainName ((HostGraph::Chain) target.chain));
    }
    else
    {
        auto files = stemdeck::drag::audioFiles (details.description);
        if (payloadKind == stemdeck::drag::fileBrowserKind && fileBrowser != nullptr
            && fileBrowser->getNumSelectedFiles() > 0)
        {
            files.clear();
            for (int i = 0; i < fileBrowser->getNumSelectedFiles(); ++i)
                files.add (fileBrowser->getSelectedFile (i));
        }
        handleAudioFilesDrop (files, target);
    }
    clearDropTarget();
}

bool MainPluginEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.isDirectory() || stemdeck::drag::isSupportedAudioFile (file))
            return true;
    }
    return false;
}

void MainPluginEditor::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void MainPluginEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    bool hasAudio = false;
    for (const auto& path : files)
        hasAudio = hasAudio || stemdeck::drag::isSupportedAudioFile (juce::File (path));
    auto target = findDropTarget ({ x, y }, false);
    if (! hasAudio && target.kind != DropTarget::Kind::explorer)
    {
        clearDropTarget();
        return;
    }
    updateDropTarget ({ x, y }, false);
}

void MainPluginEditor::fileDragExit (const juce::StringArray&)
{
    clearDropTarget();
}

void MainPluginEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto target = findDropTarget ({ x, y }, false);
    juce::Array<juce::File> dropped;
    for (const auto& path : files)
        dropped.add (juce::File (path));

    if (target.kind == DropTarget::Kind::explorer)
    {
        if (libraryView != nullptr) libraryView->addFiles (dropped);
        if (! dropped.isEmpty())
        {
            const auto first = dropped.getFirst();
            browserNavigateTo (first.isDirectory() ? first : first.getParentDirectory());
            if (fileBrowser != nullptr && first.existsAsFile())
                fileBrowser->setSelectedFile (first);
        }
    }
    else
        handleAudioFilesDrop (dropped, target);
    clearDropTarget();
}

void MainPluginEditor::handleAudioFilesDrop (const juce::Array<juce::File>& files,
                                              const DropTarget& target)
{
    juce::Array<juce::File> audio;
    for (const auto& file : files)
        if (stemdeck::drag::isSupportedAudioFile (file))
            audio.add (file);
    if (audio.isEmpty())
        return;

    // Batch drops behave like a hardware loading workflow: the pointed-at target
    // receives the first file and later files advance through consecutive slots.
    // A four-track selection dropped on Deck B therefore fills B/C/D, while a
    // sample folder selection dropped on Pad 3 fills Pads 3..8.
    if (target.kind == DropTarget::Kind::deck)
    {
        int loaded = 0;
        for (const auto& file : audio)
        {
            const int deck = target.index + loaded;
            if (! juce::isPositiveAndBelow (deck, MainPluginProcessor::numDecks))
                break;
            setFocusedDeck (deck);
            requestDeckLoad (deck, file);
            ++loaded;
        }
        if (loaded > 1)
            recordStatus.setText ("Loaded " + juce::String (loaded)
                                  + " tracks across Decks "
                                  + juce::String::charToString ((juce_wchar) ('A' + target.index))
                                  + "-"
                                  + juce::String::charToString ((juce_wchar) ('A' + target.index + loaded - 1)),
                                  juce::dontSendNotification);
        return;
    }

    if (target.kind == DropTarget::Kind::pad)
    {
        int loaded = 0;
        for (const auto& file : audio)
        {
            const int pad = target.index + loaded;
            if (! juce::isPositiveAndBelow (pad, PadBank::numPads))
                break;
            processorRef.loadPadFileAsync (pad, file);
            ++loaded;
        }
        recordStatus.setText ("Loaded " + juce::String (loaded)
                              + (loaded == 1 ? " sample into Pad " : " samples starting at Pad ")
                              + juce::String (target.index + 1),
                              juce::dontSendNotification);
        return;
    }

    if (target.kind == DropTarget::Kind::arrangement)
    {
        juce::StringArray paths;
        for (const auto& file : audio)
            paths.add (file.getFullPathName());
        arrangementView.filesDropped (paths, target.localPoint.x, target.localPoint.y);
        return;
    }

    for (const auto& file : audio)
        handleAudioDrop (file, target);
}

void MainPluginEditor::handleAudioDrop (const juce::File& file, const DropTarget& target)
{
    if (! stemdeck::drag::isSupportedAudioFile (file))
        return;

    switch (target.kind)
    {
        case DropTarget::Kind::deck:
            setFocusedDeck (target.index);
            requestDeckLoad (target.index, file);
            break;
        case DropTarget::Kind::pad:
            processorRef.loadPadFileAsync (target.index, file);
            recordStatus.setText ("Loaded " + file.getFileNameWithoutExtension()
                                    + " into Pad " + juce::String (target.index + 1),
                                  juce::dontSendNotification);
            break;
        case DropTarget::Kind::arrangement:
        {
            juce::StringArray paths { file.getFullPathName() };
            arrangementView.filesDropped (paths, target.localPoint.x, target.localPoint.y);
            break;
        }
        case DropTarget::Kind::explorer:
        {
            if (libraryView != nullptr) libraryView->addFiles ({ file });
            browserNavigateTo (file.getParentDirectory());
            if (fileBrowser != nullptr) fileBrowser->setSelectedFile (file);
            break;
        }
        case DropTarget::Kind::none:
        case DropTarget::Kind::pluginChain:
            break;
    }
}

bool MainPluginEditor::shouldDropFilesWhenDraggedExternally (const DragDetails& details,
                                                              juce::StringArray& files,
                                                              bool& canMoveFiles)
{
    canMoveFiles = false;
    const auto audio = stemdeck::drag::audioFiles (details.description);
    for (const auto& file : audio)
    {
        if (stemdeck::drag::isSupportedAudioFile (file))
            files.add (file.getFullPathName());
    }
    if (! files.isEmpty()) return true;
    if (stemdeck::drag::kind (details.description) == stemdeck::drag::fileBrowserKind
        && fileBrowser != nullptr)
    {
        for (int i = 0; i < fileBrowser->getNumSelectedFiles(); ++i)
            files.add (fileBrowser->getSelectedFile (i).getFullPathName());
        return ! files.isEmpty();
    }
    return false;
}
