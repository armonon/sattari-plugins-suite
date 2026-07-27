#pragma once

#include <juce_core/juce_core.h>

// Locates ONNX model files that ship INSIDE the app / plugin bundle, so a packaged
// StemDeck is self-contained and needs no manual ~/Library install. The neural
// engines search: env-var override -> bundled Resources (here) -> ~/Library install.
namespace stemdeck
{
// Returns the model shipped in the bundle's Resources if present, else an empty File.
//   macOS  : Foo.app/Contents/Resources/<fileName>  and  Foo.vst3/Contents/Resources/<fileName>
//   win/lin: <exe dir>/Resources/<fileName>  or  <exe dir>/<fileName>
inline juce::File bundledModelFile (const juce::String& fileName)
{
    using juce::File;

    juce::Array<File> bases;
    // macOS .app / .vst3 / .component all keep resources under Contents/Resources.
    const auto app = File::getSpecialLocation (File::currentApplicationFile);
    bases.add (app.getChildFile ("Contents").getChildFile ("Resources"));

    const auto exeDir = File::getSpecialLocation (File::currentExecutableFile).getParentDirectory();
    bases.add (exeDir.getChildFile ("Resources"));   // typical win/linux install layout
    bases.add (exeDir);                              // model sitting next to the binary

    for (auto& base : bases)
    {
        const auto candidate = base.getChildFile (fileName);
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}
}
