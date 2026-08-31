#include "MainPluginEditor.h"

// Primary-surface coordination lives outside the already-large editor translation
// unit. The individual views still own their content; this file owns only navigation
// state and the invariant that exactly one primary work surface is active.
void MainPluginEditor::selectPrimarySurface (PrimarySurface surface)
{
    if (toggleLibraryButton.getToggleState())
    {
        toggleLibraryButton.setToggleState (false, juce::dontSendNotification);
        toggleLibrary();
    }
    if (toggleBrowserButton.getToggleState())
    {
        toggleBrowserButton.setToggleState (false, juce::dontSendNotification);
        toggleFileBrowser();
    }

    // Do not toggle the requested destination off and immediately back on. Apart
    // from redundant layout passes, that created two opposing fade animations and
    // intermittently restored the Perform deck rectangles over Replay's toolbar.
    if (surface != PrimarySurface::arranger)
    {
        pianoRollButton.setToggleState (false, juce::dontSendNotification);
        if (pianoRollButton.onClick) pianoRollButton.onClick();
    }
    if (surface != PrimarySurface::mixer)
    {
        mixerButton.setToggleState (false, juce::dontSendNotification);
        if (mixerButton.onClick) mixerButton.onClick();
    }

    switch (surface)
    {
        case PrimarySurface::perform:
            if (navDecksButton.onClick) navDecksButton.onClick();
            break;
        case PrimarySurface::library:
            toggleLibraryButton.setToggleState (true, juce::dontSendNotification);
            toggleLibrary();
            break;
        case PrimarySurface::mixer:
            mixerButton.setToggleState (true, juce::dontSendNotification);
            if (mixerButton.onClick) mixerButton.onClick();
            break;
        case PrimarySurface::arranger:
            pianoRollButton.setToggleState (true, juce::dontSendNotification);
            if (pianoRollButton.onClick) pianoRollButton.onClick();
            break;
        case PrimarySurface::files:
            toggleBrowserButton.setToggleState (true, juce::dontSendNotification);
            toggleFileBrowser();
            break;
    }

    resized();
}

void MainPluginEditor::setMasterDetailVisible (bool visible, bool showAdvanced)
{
    selectPrimarySurface (PrimarySurface::perform);
    masterFocused = visible;
    focusMode = false;
    if (visible)
    {
        advancedVisible = showAdvanced;
        advancedToggle.setToggleState (showAdvanced, juce::dontSendNotification);
        advancedToggle.setButtonText (showAdvanced ? "Less" : "More");
    }
    resized();
    repaint();
}
