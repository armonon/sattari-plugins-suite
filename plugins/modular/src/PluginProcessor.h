#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>

class SattariModularProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int effectSlots = 8, mainSlots = 6, sourceSlots = 4;
    static constexpr int nodes = effectSlots + sourceSlots, controls = 8, macros = 8, mappings = 2;
    static constexpr int allControls = 64, legacyParameterCount = 160, previousParameterCount = 423;
    static constexpr int previousNodes = 10, previousControls = 32;
    enum Type { empty, clean, eq, comp, heat, echo, space, pulse, deess, image,
                granulator, rhythm, doubleFx, riser, shimmer, warble, warp, vocoder, bass808,
                externalEffect, externalInstrument, gate, face, glassChoir, chop,
                transient, multiband, peakFx, typeCount };
    struct ControlInfo
    {
        juce::String name, unit, parameterId;
        juce::NormalisableRange<float> range { 0.0f, 1.0f };
        juce::StringArray choices;
        float initial = 0.0f;
        float displayScale = 1.0f;
        bool valid = false, toggle = false;
        juce::String text (float normalised) const;
    };

    SattariModularProcessor();
    ~SattariModularProcessor() override;
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    int getNumPrograms() override { return 14; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::String nodeId (int, const juce::String&);
    static juce::String controlId (int, int);
    static juce::String macroId (int, const juce::String& suffix = "value");
    static juce::String typeName (int);
    static juce::StringArray availableTypes (int node);
    static juce::Array<int> collectionTypes (int collection);
    static bool allowedType (int node, int type);
    static bool isInstrument (int type) { return type == granulator || type == rhythm || type == chop || type == externalInstrument; }
    static bool isHosted (int type) { return type == externalEffect || type == externalInstrument; }
    juce::Array<juce::PluginDescription> scanHostedPlugin (const juce::File&, bool instrument, juce::String& error);
    juce::Result loadHostedPlugin (int node, const juce::PluginDescription&);
    juce::String hostedStatus (int node) const;
    std::shared_ptr<juce::AudioPluginInstance> hostedInstance (int node) const;
    void serviceHostedChanges();
    int nodeType (int) const;
    ControlInfo info (int type, int control) const;
    float value (const juce::String&) const;
    void set (const juce::String&, float actual, bool gesture = true);
    void selectModule (int node, int type);
    bool duplicateModule (int node);
    void moveModule (int node, int direction);
    void clearRack();
    void loadFactory (int program);
    void captureUndo();
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void selectScene (int);
    int selectedScene() const { const juce::ScopedLock lock (storageLock); return sceneIndex; }
    void selectAB (int);
    int selectedAB() const { const juce::ScopedLock lock (storageLock); return abIndex; }
    void copyAB();
    bool isMapped (int node, int control) const;
    int mappingTarget (int macro, int mapping) const;
    void setMappingTarget (int macro, int mapping, int target);
    float effectiveValue (int node, int control) const;
    juce::Result loadSample (int layer, const juce::File&);
    juce::String sampleName (int layer) const;
    juce::Result savePreset (const juce::File&);
    juce::Result loadPreset (const juce::File&);
    juce::Result importLegacyPreset (const juce::File&);
    void queueNote (int note, float velocity, bool on);
    int currentWorkspace() const { return juce::roundToInt (value ("workspace")); }
    juce::String presetName() const;
    juce::String lastError() const;
    void allNotesOff();
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>, 2> rms { 0.0f, 0.0f }, peak { 0.0f, 0.0f };
    std::atomic<float> cpu { 0.0f };
    std::atomic<bool> clipped { false };
    std::atomic<unsigned> uiRevision { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    struct Impl;
    struct History;
    std::unique_ptr<Impl> impl;
    mutable juce::CriticalSection storageLock;
    std::vector<History> undoStates, redoStates;
    std::array<juce::ValueTree, 3> scenes;
    std::array<juce::ValueTree, 2> comparisons;
    juce::String name { "Studio Vocal" }, error;
    int sceneIndex = 0, abIndex = 0;
    std::atomic<int> currentProgram { 0 };
    std::atomic<unsigned> editRevision { 0 };
    juce::ValueTree snapshot();
    History historySnapshot();
    void restoreHistory (const History&);
    juce::Result restoreState (const void*, int);
    void restore (const juce::ValueTree&);
    void selectModuleInternal (int, int);
    void resetEngine();
    void resetLayerRouting();
    void handleAsyncUpdate() override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariModularProcessor)
};
