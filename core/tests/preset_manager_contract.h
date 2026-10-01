#pragma once

#include <sattari_audio_core/SattariPresetManager.h>
#include <limits>

// Called with the production rack's APVTS; all file IO stays in an owned temp dir.
inline int runPresetManagerContract (juce::AudioProcessorValueTreeState& state)
{
    int failures = 0, checks = 0;
    auto check = [&] (bool ok, const char* message)
    {
        ++checks;
        if (! ok) { ++failures; std::fprintf (stderr, "FAIL: %s\n", message); }
    };
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("sattari-preset-" + juce::Uuid().toString());
    check (directory.createDirectory().wasOk(), "create isolated preset directory");
    struct Cleanup { juce::File directory; ~Cleanup() { directory.deleteRecursively(); } } cleanup { directory };
    sattari::presets::SattariPresetManager manager (state, "preset-contract", "Sattari Audio", directory);
    const auto file = directory.getChildFile ("test.sattaripreset");
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (state.processor.getParameters()[0]);
    if (param == nullptr) return 1;
    const auto id = dynamic_cast<juce::AudioProcessorParameterWithID*> (param)->paramID;
    auto snapshot = manager.captureState();
    const float initial = param->getValue();
    auto reject = [&] (const juce::String& json, const char* message)
    {
        check (file.replaceWithText (json), "write preset fixture");
        check (! manager.loadFile (file), message);
        check (std::abs (param->getValue() - initial) < 1.0e-6f, "rejected preset must not change state");
        param->setValueNotifyingHost (initial);
    };
    check (! manager.loadFile (directory.getChildFile ("missing")), "missing file rejected");
    check (! manager.loadFile (directory), "directory rejected");
    reject ("{", "malformed JSON rejected");
    reject ("{}", "empty object rejected");
    reject ("[]", "array rejected");
    reject (juce::String::repeatedString ("{\"a\":", 10000)
            + "0" + juce::String::repeatedString ("}", 10000), "deep JSON rejected without recursion");
    reject (juce::JSON::toString (snapshot) + "garbage", "trailing garbage rejected");
    auto variant = [&] { return juce::JSON::parse (juce::JSON::toString (snapshot)); };
    auto wrongProduct = variant();
    wrongProduct.getDynamicObject()->setProperty ("product", "another-product");
    reject (juce::JSON::toString (wrongProduct), "wrong-product preset rejected");
    auto future = variant();
    future.getDynamicObject()->setProperty ("schemaVersion", 99);
    reject (juce::JSON::toString (future), "unsupported schema rejected");
    for (const auto& invalid : { juce::var ("bad"), juce::var (true), juce::var(), juce::var (1.0e100) })
    {
        auto bad = variant();
        bad["parameters"].getDynamicObject()->setProperty (id, invalid);
        reject (juce::JSON::toString (bad), "non-numeric or overflowing parameter rejected");
    }
    // Invalid later parameters must reject the entire transaction, including
    // an otherwise valid change to the first parameter.
    if (state.processor.getParameters().size() > 1)
    {
        auto bad = variant();
        auto* second = dynamic_cast<juce::AudioProcessorParameterWithID*> (state.processor.getParameters()[1]);
        bad["parameters"].getDynamicObject()->setProperty (id, (double) param->convertFrom0to1 (1.0f - initial));
        bad["parameters"].getDynamicObject()->setProperty (second->paramID, "bad");
        reject (juce::JSON::toString (bad), "invalid later value rejects all changes");
    }
    reject (juce::String::repeatedString (" ", 1024 * 1024 + 1), "oversized file rejected");
    auto compatible = variant();
    compatible["parameters"].getDynamicObject()->setProperty ("futureParameter", 42.0);
    check (file.replaceWithText (juce::JSON::toString (compatible)), "write compatible preset");
    param->setValueNotifyingHost (1.0f - initial);
    check (manager.loadFile (file), "same-product preset with unknown numeric ID loads");
    check (std::abs (param->getValue() - initial) < 1.0e-6f, "file round trip restores parameter");
    manager.captureSlotA();
    param->setValueNotifyingHost (1.0f - initial);
    manager.captureSlotB();
    check (manager.recallSlotA() && std::abs (param->getValue() - initial) < 1.0e-6f, "A/B recall preserves snapshot");
    manager.swapSlots();
    check (manager.recallSlotB() && std::abs (param->getValue() - initial) < 1.0e-6f, "A/B swap preserves snapshot");
    for (int i = 0; i < 100; ++i)
        check (manager.loadFile (file), "repeated preset loads remain valid");
    const auto firstName = manager.newUserPresetName();
    check (manager.saveUserPreset (firstName), "save user preset in isolated directory");
    const auto firstBytes = directory.getChildFile (firstName + manager.fileExtension).loadFileAsString();
    const auto secondName = manager.newUserPresetName();
    check (secondName != firstName, "rapid saves have distinct names");
    param->setValueNotifyingHost (1.0f - initial);
    check (manager.saveUserPreset (secondName), "second rapid save succeeds");
    check (directory.getChildFile (firstName + manager.fileExtension).loadFileAsString() == firstBytes,
           "rapid save does not overwrite previous preset");
    check (manager.loadUserPreset (firstName) && std::abs (param->getValue() - initial) < 1.0e-6f,
           "named user preset restores settings");
    const auto blocked = directory.getChildFile ("blocked" + juce::String (manager.fileExtension));
    check (blocked.createDirectory().wasOk(), "create blocked save fixture");
    check (! manager.saveUserPreset ("blocked"), "save failure reported without false success");
    std::printf ("Preset manager contract: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
