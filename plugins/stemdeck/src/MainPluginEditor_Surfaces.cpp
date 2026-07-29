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

    pianoRollButton.setToggleState (false, juce::dontSendNotification);
    if (pianoRollButton.onClick) pianoRollButton.onClick();
    mixerButton.setToggleState (false, juce::dontSendNotification);
    if (mixerButton.onClick) mixerButton.onClick();

    switch (surface)
    {
        case PrimarySurface::decks:
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
        case PrimarySurface::performance:
            toggleBrowserButton.setToggleState (true, juce::dontSendNotification);
            toggleFileBrowser();
            break;
    }

    resized();
}
